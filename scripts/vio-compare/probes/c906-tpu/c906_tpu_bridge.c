/* Linux only provisions clocks/reset and passes shared memory to C906. */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/capability.h>
#include <linux/mutex.h>
#include "rtos_cmdqu.h"
#include "shared.h"
#include "lk_shared.h"

#define PROBE_IOCTL _IOW('T', 1, __u32)
#define LK_IOCTL _IOW('T', 2, __u32)
static struct device *tpu_device;
static struct clk *axi_clock, *fab_clock;
static struct reset_control *resets[3];
static DEFINE_MUTEX(probe_lock);

static long probe_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
    __u32 phys;
    int ret;
    cmdqu_t msg = {0};
    if (!capable(CAP_SYS_RAWIO)) return -EPERM;
    if (cmd != PROBE_IOCTL && cmd != LK_IOCTL) return -EINVAL;
    if (copy_from_user(&phys, (void __user *)arg, sizeof(phys))) return -EFAULT;
    if ((phys & 63u) || phys < 0x80000000u ||
        phys > 0x8ff40000u - (cmd == LK_IOCTL ? LK_TPU_BYTES : TPU_PROBE_BYTES)) return -EINVAL;
    if (mutex_lock_interruptible(&probe_lock)) return -ERESTARTSYS;
    msg.ip_id = IP_SYSTEM;
    msg.cmd_id = TPU_PROBE_CMD;
    msg.resv.mstime = 15000;
    msg.param_ptr = phys;
    ret = rtos_cmdqu_send_wait(&msg, TPU_PROBE_CMD);
    mutex_unlock(&probe_lock);
    return ret;
}

static const struct file_operations probe_fops = {
    .owner = THIS_MODULE, .unlocked_ioctl = probe_ioctl,
};
static struct miscdevice probe_dev = {
    .minor = MISC_DYNAMIC_MINOR, .name = "c906-tpu-probe", .fops = &probe_fops,
};

static int __init probe_init(void)
{
    const char *names[3] = {"res_tdma", "res_tpu", "res_tpusys"};
    unsigned i;
    int ret;
    tpu_device = bus_find_device_by_name(&platform_bus_type, NULL, "c100000.tpu");
    if (!tpu_device) return -ENODEV;
    if (tpu_device->driver) { ret = -EBUSY; goto put_dev; }
    axi_clock = clk_get(tpu_device, "clk_tpu_axi");
    if (IS_ERR(axi_clock)) { ret = PTR_ERR(axi_clock); goto put_dev; }
    fab_clock = clk_get(tpu_device, "clk_tpu_fab");
    if (IS_ERR(fab_clock)) { ret = PTR_ERR(fab_clock); goto put_axi; }
    for (i = 0; i < 3; ++i) {
        resets[i] = reset_control_get_exclusive(tpu_device, names[i]);
        if (IS_ERR(resets[i])) { ret = PTR_ERR(resets[i]); goto put_resets; }
    }
    ret = clk_prepare_enable(axi_clock);
    if (ret) goto put_resets;
    ret = clk_prepare_enable(fab_clock);
    if (ret) goto disable_axi;
    for (i = 0; i < 3; ++i) {
        ret = reset_control_assert(resets[i]);
        if (ret) goto disable_both;
    }
    for (i = 0; i < 3; ++i) {
        ret = reset_control_deassert(resets[i]);
        if (ret) goto disable_both;
    }
    ret = misc_register(&probe_dev);
    if (!ret) {
        pr_info("c906_tpu_probe: exclusive C906 execution; clocks axi=%lu fab=%lu\n",
                clk_get_rate(axi_clock), clk_get_rate(fab_clock));
        return 0;
    }
disable_both:
    clk_disable_unprepare(fab_clock);
disable_axi:
    clk_disable_unprepare(axi_clock);
put_resets:
    for (i = 0; i < 3; ++i)
        if (!IS_ERR_OR_NULL(resets[i])) reset_control_put(resets[i]);
    clk_put(fab_clock);
put_axi:
    clk_put(axi_clock);
put_dev:
    put_device(tpu_device);
    return ret;
}

static void __exit probe_exit(void)
{
    unsigned i;
    misc_deregister(&probe_dev);
    for (i = 0; i < 3; ++i) {
        reset_control_assert(resets[i]);
        reset_control_put(resets[i]);
    }
    clk_disable_unprepare(fab_clock);
    clk_disable_unprepare(axi_clock);
    clk_put(fab_clock);
    clk_put(axi_clock);
    put_device(tpu_device);
}
module_init(probe_init);
module_exit(probe_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TideSense C906 TPU access experiment: power and mailbox only");
