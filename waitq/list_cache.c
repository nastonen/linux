#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/miscdevice.h>

#define NAME_LEN 20

static struct device *dev;

LIST_HEAD(head_node);

static struct kmem_cache *g_mem_cache;

struct identity {
	struct list_head list;
	char name[NAME_LEN];
	int  id;
	bool busy;
};

static int identity_create(char *name, int id)
{
	struct identity *tmp = NULL;

	tmp = kmem_cache_alloc(g_mem_cache, GFP_KERNEL);
	if (unlikely(!tmp))
		return -ENOMEM;

	strscpy(tmp->name, name, NAME_LEN - 1);
	tmp->id = id;
	tmp->busy = false;

	INIT_LIST_HEAD(&tmp->list);
	list_add_tail(&tmp->list, &head_node);

	pr_info("Added node %s to the list\n", tmp->name);

	return 0;
}

static struct identity *identity_find(int id)
{
	struct identity *found;

	list_for_each_entry(found, &head_node, list)
		if (found->id == id)
			return found;

	return NULL;
}

static struct identity *identity_get(void)
{
	struct identity *found = NULL;

	list_for_each_entry(found, &head_node, list) {
		if (found->id == id) {
			break;
		}
	}

	return NULL;
}

static void identity_destroy(int id)
{
	struct identity *curr, *tmp;

	list_for_each_entry_safe(curr, tmp, &head_node, list) {
		if (curr->id == id) {
			list_del(&curr->list);
			kmem_cache_free(g_mem_cache, curr);
			pr_debug("Destroyed %d\n", id);
			return;
		}
	}

	pr_debug("Tried to destroy %d, but not found\n", id);
}

static void list_destroy(void)
{
	struct identity *curr, *tmp;

	list_for_each_entry_safe(curr, tmp, &head_node, list) {
		list_del(&curr->list);
		kmem_cache_free(g_mem_cache, curr);
	}
}

static int __init list_init(void)
{
	pr_info("list module loaded!\n");

}

static ssize_t write_miscdev(struct file *filp, const char __user *ubuf,
				size_t count, loff_t *off)
{
	int ret = count;
	void *kbuf = kvzmalloc(MAX_SIZE, GFP_KERNEL);

	if (copy_from_user(kbuf, ubuf, NAME_LEN - 1)) {
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

	dev_info(dev, "LLKD misc driver (major #10, minor #%d) registered,"
		" dev node is /dev/%s\n", llkd_miscdev.minor, llkd_miscdev.name);

	/* Create memory cache. */
	g_mem_cache = kmem_cache_create("list_cache",		  // name in /proc/slabinfo etc
					sizeof(struct identity),  // (min) size of each object
					sizeof(long),		  // 0 or sizeof(long) to align to size of word
					SLAB_HWCACHE_ALIGN,	  // good for performance
					NULL);
	if (!g_mem_cache)
		return -ENOMEM;

	struct identity *temp;

	identity_create("Alice", 1);
	identity_create("Bob", 2);
	identity_create("Dave", 3);
	identity_create("Gena", 10);

	temp = identity_find(3);
	if (unlikely(temp == NULL))
		pr_debug("id 3 not found\n");
	else
		pr_debug("id 3 = %s\n", temp->name);

	temp = identity_find(42);
	if (likely(temp == NULL))
		pr_debug("id 42 not found\n");

	identity_destroy(2);
	identity_destroy(2);

	temp = identity_find(2);
	if (likely(temp == NULL))
		pr_debug("id 2 not found\n");
	else
		pr_debug("id 2 = %s\n", temp->name);

	return 0;
}

static void __exit waitq_exit()
{
	list_destroy();

	if (likely(list_empty(&head_node)))
		dev_info(dev, "list is empty now\n");
	else
		dev_info(dev, "list is left NON-empty\n");

	kmem_cache_destroy(g_mem_cache);
	misc_deregister(&llkd_miscdev);

	pr_info("waitq misc driver deregistered\n");
}

module_init(waitq_init);
module_exit(waitq_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Me");

