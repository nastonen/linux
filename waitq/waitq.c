#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/miscdevice.h>

static void __exit waitq_exit()
{
	misc_deregister(&llkd_miscdev);
	pr_info("waitq misc driver deregistered\n");
}

module_init(waitq_init);
module_exit(waitq_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Me");

