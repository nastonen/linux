#define pr_fmt(fmt) "%s:%s(): " fmt, KBUILD_MODNAME, __func__

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <asm/atomic.h>

/* copy_[to|from]_user() */
#include <linux/version.h>
#if LINUX_VERSION_CODE > KERNEL_VERSION(4, 11, 0)
#include <linux/uaccess.h>
#else
#include <asm/uaccess.h>
#endif

#include "../sed_common.h"

#define DRVNAME			"sed1_drv"
#define TIMER_EXPIRE_MS		1
#define WORK_IS_ENCRYPT		1
#define WORK_IS_DECRYPT		2
#define CRYPT_OFFSET		63

/* Module parameters */
static int make_it_fail;
module_param(make_it_fail, int, 0660);
MODULE_PARM_DESC(make_it_fail, "Make timer miss deadline (default=0)");

/* The driver context */
struct st_ctx {
	struct device *dev;
	unsigned int fake;
	atomic_t timed_out;
	struct timer_list tmr;
};
static struct st_ctx *gpriv;

static void timesup(struct timer_list *timer)
{
	struct st_ctx *priv = from_timer(priv, timer, tmr);

	atomic_set(&priv->timed_out, 1);
	pr_notice("*** Timer expired! ***\n");
}

/*
 * @work  - WORK_IS_ENCRYPT / WORK_IS_DECRYPT
 * @kd    - cleartext content
 * @kdret - output
 */
static void encrypt_decrypt_payload(int work, struct sed_ds *kd, struct sed_ds *kdret)
{
	ktime_t t1, t2; /* s64 qty */
	struct st_ctx *priv = gpriv;

	pr_debug("sarting timer + processin now...\n");

	/* Start the timer; set it to expire in TIMER_EXPIRE_MS ms */
	mod_timer(&priv->tmr, jiffies + msecs_to_jiffies(TIMER_EXPIRE_MS));

	t1 = ktime_get_real_ns();

	/* Actual processing of the payload */
	memcpy(kdret, kd, sizeof(struct sed_ds));
	if (work == WORK_IS_ENCRYPT) {
		for (int i = 0; i < kd->len; i++) {
			kdret->data[i] ^= CRYPT_OFFSET;
			kdret->data[i] += CRYPT_OFFSET;
		}
	} else if (work == WORK_IS_DECRYPT) {
		for (int i = 0; i < kd->len; i++) {
			kdret->data[i] -= CRYPT_OFFSET;
			kdret->data[i] ^= CRYPT_OFFSET;
		}
	}
	kdret->len = kd->len;

	if (make_it_fail)
		msleep(TIMER_EXPIRE_MS + 1);

	t2 = ktime_get_real_ns();

	/* Work done, cancel the timeout */
	if (del_timer(&priv->tmr) == 0)
		pr_debug("cancelled the timer while it's inactive! (deadline missed?)\n");
	else
		pr_debug("processing complete, timeout cancelled\n");

	if (time_after((unsigned long)t2, (unsigned long)t1))
		pr_debug("delta: %lld ns", ktime_sub(t2, t1));
}

static void process_it(struct sed_ds *kd, struct sed_ds *kdret)
{
	switch(kd->data_xform) {
	case XF_NONE:
		pr_debug("data transformation type: XF_NONE\n");
		break;
	case XF_ENCRYPT:
		pr_debug("data transformation type: XF_ENCRYPT\n");
		encrypt_decrypt_payload(WORK_IS_ENCRYPT, kd, kdret);
		break;
	case XF_DECRYPT:
		pr_debug("data transformation type: XF_DECRYPT\n");
		encrypt_decrypt_payload(WORK_IS_DECRYPT, kd, kdret);
		break;
	}
}

/*
 * NO CONCURRENCY PROTECTION!
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36)
static long ioctl_miscdrv(struct file *filp, unsigned int cmd, unsigned long arg)
#else
static int ioctl_miscdrv(struct inode *ino, struct file *filp, unsigned int cmd, unsigned long arg)
#endif
{
	int ret = 0;
	struct sed_ds *kd, *kdret;
	struct st_ctx *priv = gpriv;

	if (_IOC_TYPE(cmd) != IOCTL_LLKD_SED_MAGIC) {
		pr_warn("ioctl fail; magic # mismatch\n");
		return -ENOTTY;
	}
	if (_IOC_NR(cmd) > IOCTL_LLKD_SED_MAXIOCTL) {
		pr_warn("ioctl fail; invalid cmd?\n");
		return -ENOTTY;
	}

	ret = -ENOMEM;
	kd = kzalloc(sizeof(struct sed_ds), GFP_KERNEL);
	if (!kd)
		goto out_mem1;
	kdret = kzalloc(sizeof(struct sed_ds), GFP_KERNEL);
	if (!kdret)
		goto out_mem2;

	switch (cmd) {
	case IOCTL_LLKD_SED_IOC_ENCRYPT_MSG:
	case IOCTL_LLKD_SED_IOC_DECRYPT_MSG:
		pr_debug("in ioctl cmd option: %s\narg=0x%lx\n",
			(cmd == IOCTL_LLKD_SED_IOC_ENCRYPT_MSG ? "ecnrypt" : "decrypt"), arg);
	
		ret = -EFAULT;
		if (copy_from_user(kd, (struct sed_ds *)arg, sizeof(struct sed_ds))) {
			pr_warn("copy_from_user() failed\n");
			goto out_cftu;
		}

		pr_debug("xform=%d, len=%d\n", kd->data_xform, kd->len);

		process_it(kd, kdret);

		if (atomic_read(&priv->timed_out) == 1) {
			kdret->timed_out = 1;
			pr_debug("** timed out **\n");
		}

		ret = -EFAULT;
		if (copy_to_user((struct sed_ds *)arg, (struct sed_ds *)kdret, sizeof(struct sed_ds))) {
			pr_warn("copy_to_user() failed\n");
			goto out_cftu;
		}
		break;
	default:
		kfree(kdret);
		kfree(kd);
		return -ENOTTY;
	}

	ret = 0;

out_cftu:
	kfree(kdret);
out_mem2:
	kfree(kd);
out_mem1:
	return ret;
}

static int open_miscdrv(struct inode *inode, struct file *filp)
{
	pr_info("opening \"%s\" now\n", filp->f_path.dentry->d_iname);

	return nonseekable_open(inode, filp);
}

static ssize_t read_miscdrv(struct file *filp, char __user *ubuf, size_t count, loff_t *off)
{
	pr_info("to read %zd bytes\n", count);
	return count;
}

static ssize_t write_miscdrv(struct file *filp, const char __user *ubuf,
			     size_t count, loff_t *off)
{
	pr_info("to write %zd bytes\n", count);
	return count;
}

static int close_miscdrv(struct inode *inode, struct file *filp)
{
	pr_info("closing \"%s\"\n", filp->f_path.dentry->d_iname);
	return 0;
}

static const struct file_operations llkd_misc_fops = {
	.open = open_miscdrv,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36)
	.unlocked_ioctl = ioctl_miscdrv,		/* use the 'unlocked' version */
#else
	.ioctl = ioctl_miscdrv,				/* old way */
#endif
	.read = read_miscdrv,
	.write = write_miscdrv,
	.llseek = no_llseek,
	.release = close_miscdrv
};

static struct miscdevice llkd_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = DRVNAME,
	.mode = 0666,
	.fops = &llkd_misc_fops
};

static int __init sed1_drv_init(void)
{
	int ret = 0;
	struct device *dev;
	struct st_ctx *priv = NULL;

	/* Register with misc kernel framework */
	ret = misc_register(&llkd_miscdev);
	if (ret) {
		pr_notice("misc device registration failed, aborting\n");
		return ret;
	}

	dev = llkd_miscdev.this_device;

	dev_info(dev, "LLKD %s misc driver (major #10) registered, minor #%d,\n"
		"dev node is /dev/%s\n", DRVNAME, llkd_miscdev.minor, DRVNAME);

	/* Accessing global structure without sync. Ignore for now.
	 * In a real driver we usually hook the private structure to
	 * a sub driver's structure:
	 * pla0.dev.platform_date = priv;
	 * Here we don't, for simplicity.
	 */
	gpriv = priv = devm_kzalloc(dev, sizeof(struct st_ctx), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = llkd_miscdev.this_device;
	atomic_set(&priv->timed_out, 0);

	/* Init kernel timer */
	timer_setup(&priv->tmr, timesup, 0);
	dev_dbg(dev, "loaded.\n");

	return ret;
}

static void __exit sed1_drv_exit(void)
{
	struct st_ctx *priv = gpriv;

	dev_dbg(priv->dev, "unloading\n");
	del_timer_sync(&priv->tmr);
	misc_deregister(&llkd_miscdev);
}

module_init(sed1_drv_init);
module_exit(sed1_drv_exit);

MODULE_AUTHOR("Niko");
MODULE_LICENSE("GPL");

