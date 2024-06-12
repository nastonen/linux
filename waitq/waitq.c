#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/miscdevice.h>

#define MAX_SIZE 19

static struct device *dev;

static ssize_t write_miscdev(struct file *filp, const char __user *ubuf,
				size_t count, loff_t *off)
{
	int ret = count;
	void *kbuf = kvzmalloc(MAX_SIZE, GFP_KERNEL);

	if (copy_from_user(kbuf, ubuf, MAX_SIZE)) {
		dev_warn(dev, "copy_from_user() failed, returning\n");
		kvfree(kbuf);
		return -EFAULT;
	}

	
}

static int open_miscdev(struct inode *inode, struct file *filp)
{
	return nonseekable_open(inode, filp);
}

static int close_miscdev(struct inode *inode, struct file *filp)
{
	return 0;
}

static const struct file_operations llkd_misc_fops = {
	.open = open_miscdev,
	.write = write_miscdev,
	.release = close_miscdev,
	.llseek = no_llseek
};

static struct miscdevice llkd_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "eudyptula",
	.mode = 0444,
	.fops = &llkd_misc_fops
};

static int __init waitq_init()
{
	int ret = misc_register(&llkd_miscdev);
	if (ret) {
		pr_notice("misc device registration failed\n");
		return ret;
	}

	/* Get the device pointer for this device. */
	dev = llkd_miscdev.this_device;

	pr_info("LLKD misc driver (major #10, minor #%d) registered,"
		" dev node is /dev/%s\n", llkd_miscdev.minor, llkd_miscdev.name);

	return 0;
}

static void __exit waitq_exit()
{
	misc_deregister(&llkd_miscdev);
	pr_info("waitq misc driver deregistered\n");
}

module_init(waitq_init);
module_exit(waitq_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Me");

