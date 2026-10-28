// SPDX-License-Identifier: GPL-2.0
/* TideSense debug loader. Fixed C906L carveout; no boot media writes. */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/ktime.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/capability.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include "rtos_cmdqu.h"
#include "loader.h"

#define MAILBOX_BASE 0x01900000u
#define BOOT_CONFIG_BYTES 64u

static void __iomem *firmware_mem, *mailbox;
static struct platform_device *loader_pdev;
static struct c906_reload_gate *gate;
static dma_addr_t gate_phys;
static bool unsafe_to_unload;
static DEFINE_MUTEX(loader_lock);
static u8 boot_config[BOOT_CONFIG_BYTES];
static bool have_config;

static int echo(void)
{
    cmdqu_t msg = {0};
    int ret;
    msg.ip_id = IP_SYSTEM;
    msg.cmd_id = SYS_CMD_INFO_LINUX;
    msg.resv.mstime = 100;
    msg.param_ptr = 0x906ca530;
    ret = rtos_cmdqu_send_wait(&msg, SYS_CMD_INFO_LINUX);
    if (ret) return ret;
    return msg.ip_id == IP_SYSTEM && msg.cmd_id == SYS_CMD_INFO_LINUX &&
           msg.param_ptr == 0x906ca530 ? 0 : -EIO;
}

static int get_config(u32 *address)
{
    cmdqu_t msg = {0};
    u8 config[BOOT_CONFIG_BYTES];
    u32 magic, bytes;
    u16 checksum, expected;
    unsigned i;
    int ret;
    msg.ip_id = IP_SYSTEM;
    msg.cmd_id = SYS_CMD_INFO_LINUX_INIT_DONE;
    msg.resv.mstime = 100;
    ret = rtos_cmdqu_send_wait(&msg, SYS_CMD_INFO_RTOS_INIT_DONE);
    if (ret) return ret;
    if (msg.ip_id != IP_SYSTEM || msg.cmd_id != SYS_CMD_INFO_RTOS_INIT_DONE ||
        msg.param_ptr < C906_FIRMWARE_BASE ||
        msg.param_ptr > C906_FIRMWARE_BASE + C906_FIRMWARE_SIZE - sizeof(config))
        return -EPROTO;
    memcpy_fromio(config, firmware_mem + msg.param_ptr - C906_FIRMWARE_BASE,
                  sizeof(config));
    memcpy(&magic, config, 4);
    memcpy(&bytes, config + 4, 4);
    memcpy(&expected, config + 36, 2);
    if (magic != 0xa55aca53 || bytes != 36) return -EPROTO;
    for (i = 0, checksum = 0; i < bytes; ++i) checksum += config[i];
    if (checksum != expected) return -EBADMSG;
    /* The experiment only supports the SDK's non-fast-image firmware. */
    if (config[34] || config[35]) return -EBUSY;
    memcpy(boot_config, config, sizeof(config));
    have_config = true;
    *address = msg.param_ptr;
    return 0;
}

static int reload_supported(void)
{
    cmdqu_t msg = {0};
    int ret;
    msg.ip_id = IP_SYSTEM;
    msg.cmd_id = C906_RELOAD_CAP_CMD;
    msg.resv.mstime = 100;
    msg.param_ptr = 0x4341503f;
    ret = rtos_cmdqu_send_wait(&msg, C906_RELOAD_CAP_CMD);
    if (ret) return ret;
    return msg.param_ptr == C906_RELOAD_MAGIC ? 1 : 0;
}

static bool valid_image(const void *data, u32 bytes)
{
    const u8 *p = data;
    u32 first, i;
    static const char signature[] = C906_RELOAD_SIGNATURE;
    if (bytes < 64 || bytes > C906_FIRMWARE_SIZE) return false;
    memcpy(&first, data, sizeof(first));
    if (first != 0xf1402573u) return false;
    for (i = 0; i + sizeof(signature) <= bytes; ++i)
        if (!memcmp(p+i, signature, sizeof(signature))) return true;
    return false;
}

static int boot_image(const void *image, u32 bytes, struct c906_reload_request *r)
{
    cmdqu_t msg = {0};
    u64 begin = ktime_get_ns(), parked, copied, started;
    unsigned long deadline;
    int ret;
    u32 address = 0;
    memset(gate, 0, PAGE_SIZE);
    gate->magic = C906_RELOAD_MAGIC;
    gate->entry = C906_FIRMWARE_BASE;
    dma_wmb();
    /* Hold a reference until the new RTOS replies. On a failed handover,
     * unloading must not free a page that C906 might still be executing. */
    if (!try_module_get(THIS_MODULE)) return -ENODEV;
    unsafe_to_unload = true;
    msg.ip_id = IP_SYSTEM;
    msg.cmd_id = C906_RELOAD_PARK_CMD;
    msg.resv.mstime = 0;
    msg.param_ptr = (u32)gate_phys;
    ret = rtos_cmdqu_send(&msg);
    if (ret) goto release_ref;
    deadline = jiffies + msecs_to_jiffies(250);
    while (READ_ONCE(gate->phase) != C906_GATE_PARKED) {
        if (time_after(jiffies, deadline)) return -ETIMEDOUT;
        usleep_range(50, 100);
    }
    dma_rmb();
    parked = ktime_get_ns();
    /* C906 is now outside its firmware, with IRQs and caches disabled.
     * No other mailbox client or camera/RTOS job may run during reload. */
    writel(0, mailbox + 0);
    writel(0, mailbox + 8);
    writel(0xff, mailbox + 0x10);
    writel(0xff, mailbox + 0x30);
    memset_io(firmware_mem, 0, C906_FIRMWARE_SIZE);
    memcpy_toio(firmware_mem, image, bytes);
    memcpy_toio(mailbox + 0x400, boot_config, BOOT_CONFIG_BYTES);
    wmb();
    readl(firmware_mem + bytes - 4);
    copied = ktime_get_ns();
    dma_wmb();
    WRITE_ONCE(gate->phase, C906_GATE_RUN);
    started = ktime_get_ns();
    deadline = jiffies + msecs_to_jiffies(500);
    while (READ_ONCE(gate->phase) != C906_GATE_READY) {
        if (time_after(jiffies, deadline)) return -ETIMEDOUT;
        usleep_range(100, 200);
    }
    ret = echo();
    if (!ret) ret = get_config(&address);
    if (!ret && reload_supported() != 1) ret = -EPROTO;
    r->park_us = (parked - begin) / 1000;
    r->copy_us = (copied - parked) / 1000;
    r->startup_us = (ktime_get_ns() - started) / 1000;
    r->transfer_config = address;
    if (ret) return ret;
release_ref:
    unsafe_to_unload = false;
    module_put(THIS_MODULE);
    return ret;
}

static long loader_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
    struct c906_reload_request r;
    struct c906_loader_status status = {0};
    void *image = NULL;
    int ret;
    u64 begin;
    if (!capable(CAP_SYS_RAWIO)) return -EPERM;
    if (cmd != C906_IOCTL_STATUS && cmd != C906_IOCTL_RELOAD) return -ENOTTY;
    if (mutex_lock_interruptible(&loader_lock)) return -ERESTARTSYS;
    if (cmd == C906_IOCTL_STATUS) {
        begin = ktime_get_ns();
        status.gate_phase = READ_ONCE(gate->phase);
        status.echo_result = echo();
        status.echo_us = (ktime_get_ns() - begin) / 1000;
        if (!status.echo_result) {
            ret = get_config(&status.transfer_config);
            if (ret) status.echo_result = ret;
            else {
                memcpy(&status.firmware_magic, boot_config, 4);
                status.reload_supported = reload_supported() == 1;
            }
        }
        ret = copy_to_user((void __user *)arg, &status, sizeof(status)) ? -EFAULT : 0;
        goto out;
    }
    if (copy_from_user(&r, (void __user *)arg, sizeof(r))) { ret = -EFAULT; goto out; }
    if (r.reserved || r.image_bytes < 64 || r.image_bytes > C906_FIRMWARE_SIZE) { ret = -EINVAL; goto out; }
    image = vmemdup_user(u64_to_user_ptr(r.image), r.image_bytes);
    if (IS_ERR(image)) { ret = PTR_ERR(image); image = NULL; goto out; }
    if (!valid_image(image, r.image_bytes)) { ret = -ENOEXEC; goto out; }
    ret = echo();
    if (ret) goto out;
    ret = get_config(&r.transfer_config);
    if (ret || !have_config) goto out;
    if (reload_supported() != 1) { ret = -EOPNOTSUPP; goto out; }
    if (unsafe_to_unload) { ret = -EBUSY; goto out; }
    begin = ktime_get_ns();
    r.result = boot_image(image, r.image_bytes, &r);
    r.total_us = (ktime_get_ns() - begin) / 1000;
    pr_info("c906_loader: result=%d park=%llu us copy=%llu us start=%llu us total=%llu us\n",
            r.result, r.park_us, r.copy_us, r.startup_us, r.total_us);
    ret = copy_to_user((void __user *)arg, &r, sizeof(r)) ? -EFAULT : 0;
out:
    kvfree(image);
    mutex_unlock(&loader_lock);
    return ret;
}

static const struct file_operations loader_fops = {
    .owner = THIS_MODULE, .unlocked_ioctl = loader_ioctl,
};
static struct miscdevice loader_device = {
    .minor = MISC_DYNAMIC_MINOR, .name = "c906-loader", .fops = &loader_fops,
};

static int __init loader_init(void)
{
    int ret;
    loader_pdev = platform_device_register_simple("tidesense-c906-loader", -1, NULL, 0);
    if (IS_ERR(loader_pdev)) return PTR_ERR(loader_pdev);
    loader_pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
    loader_pdev->dev.dma_mask = &loader_pdev->dev.coherent_dma_mask;
    gate = dma_alloc_coherent(&loader_pdev->dev, PAGE_SIZE, &gate_phys, GFP_KERNEL);
    if (!gate) { ret = -ENOMEM; goto unregister; }
    if ((gate_phys & (PAGE_SIZE-1)) || gate_phys < 0x80000000u || gate_phys > 0x8ff3f000u) {
        ret = -ERANGE; goto free_gate;
    }
    firmware_mem = ioremap_wc(C906_FIRMWARE_BASE, C906_FIRMWARE_SIZE);
    mailbox = ioremap(MAILBOX_BASE, 0x500);
    if (!firmware_mem || !mailbox) { ret = -ENOMEM; goto unmap; }
    ret = misc_register(&loader_device);
    if (ret) goto unmap;
    pr_info("c906_loader: cooperative RAM reload, firmware=%08x+%x gate=%pad\n",
            C906_FIRMWARE_BASE, C906_FIRMWARE_SIZE, &gate_phys);
    return 0;
unmap:
    if (mailbox) iounmap(mailbox);
    if (firmware_mem) iounmap(firmware_mem);
free_gate:
    dma_free_coherent(&loader_pdev->dev, PAGE_SIZE, gate, gate_phys);
unregister:
    platform_device_unregister(loader_pdev);
    return ret;
}

static void __exit loader_exit(void)
{
    misc_deregister(&loader_device);
    iounmap(mailbox);
    iounmap(firmware_mem);
    dma_free_coherent(&loader_pdev->dev, PAGE_SIZE, gate, gate_phys);
    platform_device_unregister(loader_pdev);
}
module_init(loader_init);
module_exit(loader_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TideSense SG2002 C906L cooperative firmware RAM reload");
