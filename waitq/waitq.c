#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/atomic.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/atomic.h>

#define NAME_LEN 20

static struct device *dev;
static struct kmem_cache *g_mem_cache;
static atomic_t cnt = ATOMIC_INIT(1);
static struct task_struct *waitq_task;
static atomic_t data_ready = ATOMIC_INIT(0);

static DECLARE_WAIT_QUEUE_HEAD(wee_wait);
static LIST_HEAD(head_node);
static DEFINE_MUTEX(list_mtx);

struct identity {
	struct list_head list;
	char name[NAME_LEN];
	int  id;
	bool busy;
};

static int identity_create(const char *name, int id)
{
	struct identity *tmp = NULL;

	tmp = kmem_cache_alloc(g_mem_cache, GFP_KERNEL);
	if (unlikely(!tmp))
		return -ENOMEM;

	strscpy(tmp->name, name, NAME_LEN - 1);
	tmp->id = id;
	tmp->busy = false;

	mutex_lock(&list_mtx);
	list_add_tail(&tmp->list, &head_node);
	mutex_unlock(&list_mtx);

	dev_info(dev, "Added node %s to the list\n", tmp->name);

	return 0;
}

static struct identity *identity_find(int id)
{
	struct identity *curr, *found = NULL;

	mutex_lock(&list_mtx);
	list_for_each_entry(curr, &head_node, list) {
		if (curr->id == id) {
			found = curr;
			break;
		}
	}
	mutex_unlock(&list_mtx);

	return found;
}

static struct identity *identity_get(void)
{
	struct identity *ret;

	mutex_lock(&list_mtx);
	ret = list_first_entry_or_null(&head_node, struct identity, list);
	if (ret)
		list_del(&ret->list);
	mutex_unlock(&list_mtx);

	return ret;
}

static void identity_destroy(int id)
{
	struct identity *curr, *tmp, *found = NULL;

	mutex_lock(&list_mtx);
	list_for_each_entry_safe(curr, tmp, &head_node, list) {
		if (curr->id == id) {
			found = curr;
			list_del(&curr->list);
			break;
		}
	}
	mutex_unlock(&list_mtx);

	if (found) {
		dev_dbg(dev, "Destroyed %d\n", found->id);
		kmem_cache_free(g_mem_cache, found);
	} else {
		dev_dbg(dev, "Tried to destroy %d but not found\n", id);
	}
}

static void list_destroy(void)
{
	struct identity *curr, *tmp;

	mutex_lock(&list_mtx);
	list_for_each_entry_safe(curr, tmp, &head_node, list) {
		list_del(&curr->list);
		kmem_cache_free(g_mem_cache, curr);
	}
	mutex_unlock(&list_mtx);
}

static int wee_kthread(void *data)
{
	while (!kthread_should_stop()) {
		/* Sleep until data becomes ready and notified. */
		wait_event_interruptible(wee_wait, kthread_should_stop() || atomic_read(&data_ready) != 0);

		if (kthread_should_stop()) {
			dev_dbg(dev, "kthread_should_stop()\n");
			break;
		}

		if (likely(atomic_read(&data_ready) != 0)) {
			atomic_dec(&data_ready);
			
			dev_dbg(dev, "kthread woke up, data is ready\n");

			struct identity *idnt = identity_get();
			if (unlikely(!idnt)) {
				dev_dbg(dev, "list was empty, should not happen\n");
				continue;
			}
			dev_dbg(dev, "got identity: %s number %d\n", idnt->name, idnt->id);

			/* sleep */
			ssleep(2);

			kmem_cache_free(g_mem_cache, idnt);
		} else {
			dev_dbg(dev, "kthread woke up, DATA NOT READY, INTERRUPT\n");
		}
	}

	return 0;
}

static ssize_t write_miscdev(struct file *filp, const char __user *ubuf,
				size_t count, loff_t *off)
{
	ssize_t ret = count;
	void *kbuf; 

	if (count >= NAME_LEN) {
		dev_warn(dev, "Name exceeds max allowed 19 chars! Shrinking.\n");
		count = NAME_LEN - 1;
	}

	kbuf = kvzalloc(count + 1, GFP_KERNEL);

	if (copy_from_user(kbuf, ubuf, count)) {
		dev_warn(dev, "copy_from_user() failed, returning\n");
		kvfree(kbuf);
		return -EFAULT;
	}

	/* Add to tail of the list. */
	identity_create(kbuf, atomic_fetch_add(1, &cnt));

	/* Wake up the wait queue. */
	atomic_inc(&data_ready);
	wake_up_interruptible(&wee_wait);

	return ret;
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
	.owner = THIS_MODULE,
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

static int __init waitq_init(void)
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

	/* Create kernel thread for wait queue. */
	waitq_task = kthread_run(wee_kthread, NULL, "waitq_task");
	if (IS_ERR(wee_kthread)) {
		dev_warn(dev, "Failed to create kthread\n");
		return PTR_ERR(wee_kthread);
	}


	/* Test data */
	struct identity *temp;

	identity_create("Alice", atomic_fetch_add(1, &cnt));
	identity_create("Bob", atomic_fetch_add(1, &cnt));
	identity_create("Dave", atomic_fetch_add(1, &cnt));
	identity_create("Gena", atomic_fetch_add(1, &cnt));

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

static void __exit waitq_exit(void)
{
	list_destroy();

	if (likely(list_empty(&head_node)))
		dev_info(dev, "list is empty now\n");
	else
		dev_info(dev, "list is left NON-empty\n");

	if (waitq_task)
		kthread_stop(waitq_task);

	pr_info("kthread_stop() called\n");

	kmem_cache_destroy(g_mem_cache);

	pr_info("kmem_cache_destroy() called\n");

	misc_deregister(&llkd_miscdev);

	pr_info("waitq misc driver deregistered\n");
}

module_init(waitq_init);
module_exit(waitq_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Me");

