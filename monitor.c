#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>

#define IOCTL_REGISTER_PID _IOW('a', 'a', int)

static long monitor_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
    int pid;

    if (cmd == IOCTL_REGISTER_PID) {
        if (copy_from_user(&pid, (int *)arg, sizeof(int))) {
            return -EFAULT;
        }

        printk(KERN_INFO "Received PID from user: %d\n", pid);
    }

    return 0;
}

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = monitor_ioctl,
};

static struct miscdevice monitor_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "container_monitor",
    .fops = &fops,
};

static int __init monitor_init(void) {
    misc_register(&monitor_device);
    printk(KERN_INFO "Container Monitor Loaded\n");
    return 0;
}

static void __exit monitor_exit(void) {
    misc_deregister(&monitor_device);
    printk(KERN_INFO "Container Monitor Unloaded\n");
}

module_init(monitor_init);
module_exit(monitor_exit);

MODULE_LICENSE("GPL");