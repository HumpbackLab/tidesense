#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/ioctl.h>
#include "rtos_cmdqu.h"
#include "lk_shared.h"

#define LK_IOCTL_RUN _IOW('k', 1, uint32_t)

static long lk_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    uint32_t phys;
    cmdqu_t msg = {0};
    (void)file;
    if (cmd != LK_IOCTL_RUN) return -EINVAL;
    if (copy_from_user(&phys, (void __user *)arg, sizeof(phys))) return -EFAULT;
    if (phys < 0x80000000u || (phys & 63u)) return -EINVAL;
    msg.ip_id = IP_SYSTEM;
    msg.cmd_id = LK_PROBE_CMD;
    msg.resv.mstime = 10000;
    msg.param_ptr = phys;
    return rtos_cmdqu_send_wait(&msg, LK_PROBE_CMD);
}

static const struct file_operations lk_fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = lk_ioctl,
};

static struct miscdevice lk_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "c906-lk-probe",
    .fops = &lk_fops,
};

static int __init lk_module_init(void) { return misc_register(&lk_device); }
static void __exit lk_module_exit(void) { misc_deregister(&lk_device); }
module_init(lk_module_init);
module_exit(lk_module_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Temporary TideSense C906 LK benchmark command device");
