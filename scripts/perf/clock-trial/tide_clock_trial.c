// SPDX-License-Identifier: GPL-2.0-only
/* Temporary SG2002 A53 experiment. Never included in the default firmware. */
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_clk.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/mutex.h>
#include <linux/thermal.h>
#include <linux/workqueue.h>
#include <dt-bindings/clock/cv181x-clock.h>

static struct clk *mpll, *cpu;
static struct thermal_zone_device *zone;
static void __iomem *pll_regs;
static unsigned long original_rate;
static u32 original_csr;
static DEFINE_MUTEX(trial_lock);
static struct delayed_work guardian;
static unsigned long expires;
static unsigned int mhz = 1000;
static unsigned int timeout_s = 180;
module_param(timeout_s, uint, 0444);
MODULE_PARM_DESC(timeout_s, "Automatically restore after this many seconds (10..300)");

static int restore(void)
{
    u32 status;
    int ret = clk_set_rate(mpll, original_rate);
    if (ret || clk_get_rate(mpll) != original_rate)
        return ret ? ret : -EIO;
    /* The clock driver hardcodes PLL ICTRL=7. Restore the exact original CSR
     * as well (normally ICTRL=5), after returning to the original frequency. */
    if (readl(pll_regs + 8) != original_csr) {
        writel(original_csr, pll_regs + 8);
        ret = readl_poll_timeout(pll_regs + 4, status, !(status & BIT(0)), 1, 10000);
    }
    if (!ret) mhz = original_rate / 1000000;
    pr_info("tide-clock-trial: restore mpll=%lu cpu=%lu csr=%#x ret=%d\n",
            clk_get_rate(mpll), clk_get_rate(cpu), readl(pll_regs + 8), ret);
    return ret;
}

static int set_mhz(const char *value, const struct kernel_param *param)
{
    unsigned int next;
    int temp, ret = kstrtouint(value, 0, &next);
    (void)param;
    if (ret) return ret;
    /* This board lost connectivity immediately after selecting 1150 MHz.
     * Keep subsequent experiments below that observed failure point. */
    if (next < 1000 || next > 1100 || next % 25) return -EINVAL;
    /* Parameter parsing can precede module initialization. */
    if (!mpll) { mhz = next; return 0; }
    mutex_lock(&trial_lock);
    if (next == original_rate / 1000000) {
        ret = restore();
    } else {
        ret = thermal_zone_get_temp(zone, &temp);
        if (ret || temp >= 70000) { ret = ret ? ret : -EBUSY; goto out; }
        if (!clk_is_match(clk_get_parent(cpu), mpll) || clk_get_rate(cpu) < original_rate) {
            ret = -EBUSY; goto out; /* Preserve active thermal throttling. */
        }
        ret = clk_set_rate(mpll, (unsigned long)next * 1000000);
        if (!ret && clk_get_rate(mpll) != (unsigned long)next * 1000000) ret = -EIO;
        if (!ret) {
            mhz = next;
            expires = jiffies + timeout_s * HZ;
            pr_info("tide-clock-trial: mpll=%lu cpu=%lu temp=%d expires_in=%us\n",
                    clk_get_rate(mpll), clk_get_rate(cpu), temp, timeout_s);
        } else {
            restore();
        }
    }
out:
    mutex_unlock(&trial_lock);
    return ret;
}
static const struct kernel_param_ops mhz_ops = { .set = set_mhz, .get = param_get_uint };
module_param_cb(mhz, &mhz_ops, &mhz, 0600);
MODULE_PARM_DESC(mhz, "Temporary MPLL MHz: 1000..1100 in 25 MHz steps; default 1000");

static void guard_work(struct work_struct *work)
{
    int temp, ret;
    (void)work;
    mutex_lock(&trial_lock);
    if (mhz != original_rate / 1000000) {
        ret = thermal_zone_get_temp(zone, &temp);
        if (ret || temp >= 70000 || time_after_eq(jiffies, expires)) {
            pr_info("tide-clock-trial: temperature/timeout guard restoring original clock\n");
            restore();
        }
    }
    mutex_unlock(&trial_lock);
    schedule_delayed_work(&guardian, HZ);
}

static struct clk *get_clock(struct device_node *node, unsigned int id)
{
    struct of_phandle_args spec = { .np = node, .args_count = 1, .args = { id } };
    return of_clk_get_from_provider(&spec);
}

static int __init trial_init(void)
{
    struct device_node *node;
    int ret, temp;
    unsigned int requested = mhz;
    if (timeout_s < 10 || timeout_s > 300) return -EINVAL;
    node = of_find_compatible_node(NULL, NULL, "cvitek,cv181x-clk");
    if (!node) return -ENODEV;
    mpll = get_clock(node, CV181X_CLK_MPLL);
    cpu = get_clock(node, CV181X_CLK_A53);
    of_node_put(node);
    if (IS_ERR(mpll)) { ret = PTR_ERR(mpll); mpll = NULL; goto put_cpu; }
    if (IS_ERR(cpu)) { ret = PTR_ERR(cpu); cpu = NULL; goto put_mpll; }
    original_rate = clk_get_rate(mpll);
    if (original_rate != 1000000000 || clk_get_rate(cpu) != original_rate ||
        !clk_is_match(clk_get_parent(cpu), mpll)) { ret = -EINVAL; goto put_both; }
    zone = thermal_zone_get_zone_by_name("soc_thermal_0");
    if (IS_ERR(zone)) { ret = PTR_ERR(zone); goto put_both; }
    ret = thermal_zone_get_temp(zone, &temp);
    if (ret || temp >= 70000) { ret = ret ? ret : -EBUSY; goto put_both; }
    pll_regs = ioremap(0x03002900, 12);
    if (!pll_regs) { ret = -ENOMEM; goto put_both; }
    original_csr = readl(pll_regs + 8);
    mhz = original_rate / 1000000;
    pr_info("tide-clock-trial: original mpll=%lu cpu=%lu csr=%#x temp=%d\n",
            original_rate, clk_get_rate(cpu), original_csr, temp);
    if (requested != mhz) { ret = -EINVAL; goto unmap; } /* Load at baseline first. */
    INIT_DELAYED_WORK(&guardian, guard_work);
    schedule_delayed_work(&guardian, HZ);
    return 0;
unmap:
    iounmap(pll_regs);
put_both:
    clk_put(cpu);
    cpu = NULL;
put_mpll:
    clk_put(mpll);
    mpll = NULL;
    return ret;
put_cpu:
    if (!IS_ERR(cpu)) clk_put(cpu);
    cpu = NULL;
    return ret;
}

static void __exit trial_exit(void)
{
    cancel_delayed_work_sync(&guardian);
    mutex_lock(&trial_lock);
    restore();
    mutex_unlock(&trial_lock);
    iounmap(pll_regs);
    clk_put(cpu);
    clk_put(mpll);
}
module_init(trial_init);
module_exit(trial_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Temporary guarded SG2002 A53 clock experiment (no voltage or boot changes)");
