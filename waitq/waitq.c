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

static struct drv_ctx {
	struct device *dev;
	struct kmem_cache *mem_cache;
	struct task_struct *waitq_task;
	struct list_head head_node;
	struct mutex list_mtx;
	wait_queue_head_t wee_wait;
	atomic_t cnt;
	atomic_t data_ready;
} *ctx;

struct identity {
	struct list_head list;
	char name[NAME_LEN];
	int  id;
	bool busy;
};

static int identity_create(const char *name, int id)
{
	struct identity *tmp = NULL;

	tmp = kmem_cache_alloc(ctx->mem_cache, GFP_KERNEL);
	if (unlikely(!tmp))
		return -ENOMEM;

	strscpy(tmp->name, name, NAME_LEN - 1);
	tmp->id = id;
	tmp->busy = false;

	mutex_lock(&ctx->list_mtx);
	INIT_LIST_HEAD(&tmp->list);
	list_add_tail(&tmp->list, &ctx->head_node);
	mutex_unlock(&ctx->list_mtx);

	dev_info(ctx->dev, "Added node %s to the list\n", tmp->name);

	return 0;
}

static struct identity *identity_find(int id)
{
	struct identity *curr, *found = NULL;

	mutex_lock(&ctx->list_mtx);
	list_for_each_entry(curr, &ctx->head_node, list) {
		if (curr->id == id) {
			found = curr;
			break;
		}
	}
	mutex_unlock(&ctx->list_mtx);

	return found;
}

static struct identity *identity_get(void)
{
	struct identity *ret;

	mutex_lock(&ctx->list_mtx);
	ret = list_first_entry_or_null(&ctx->head_node, struct identity, list);
	if (ret)
		list_del(&ret->list);
	mutex_unlock(&ctx->list_mtx);

	return ret;
}

static void identity_destroy(int id)
{
	struct identity *curr, *tmp, *found = NULL;

	mutex_lock(&ctx->list_mtx);
	list_for_each_entry_safe(curr, tmp, &ctx->head_node, list) {
		if (curr->id == id) {
			found = curr;
			list_del(&curr->list);
			break;
		}
	}
	mutex_unlock(&ctx->list_mtx);

	if (found) {
		dev_dbg(ctx->dev, "Destroyed %d\n", found->id);
		kmem_cache_free(ctx->mem_cache, found);
	} else {
		dev_dbg(ctx->dev, "Tried to destroy %d but not found\n", id);
	}
}

static void list_destroy(void)
{
	struct identity *curr, *tmp;

	mutex_lock(&ctx->list_mtx);
	list_for_each_entry_safe(curr, tmp, &ctx->head_node, list) {
		list_del(&curr->list);
		kmem_cache_free(ctx->mem_cache, curr);
	}
	mutex_unlock(&ctx->list_mtx);
}

static int wee_kthread(void *data)
{
	while (!kthread_should_stop()) {
		/* Sleep until data becomes ready or stop thread. */
		wait_event_interruptible(ctx->wee_wait, kthread_should_stop() || atomic_read(&ctx->data_ready) != 0);

		if (kthread_should_stop()) {
			dev_dbg(ctx->dev, "kthread_should_stop()\n");
			break;
		}

		if (likely(atomic_read(&ctx->data_ready) != 0)) {
			atomic_dec(&ctx->data_ready);
			
			dev_dbg(ctx->dev, "kthread woke up, data is ready\n");

			struct identity *idnt = identity_get();
			if (unlikely(!idnt)) {
				dev_dbg(ctx->dev, "list was empty, should not happen\n");
				continue;
			}
			dev_dbg(ctx->dev, "got identity: %s number %d\n", idnt->name, idnt->id);

			/* sleep */
			ssleep(2);

			kmem_cache_free(ctx->mem_cache, idnt);
		} else {
			dev_dbg(ctx->dev, "kthread woke up, DATA NOT READY, INTERRUPT\n");
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
		dev_warn(ctx->dev, "Name exceeds max allowed 19 chars! Shrinking.\n");
		count = NAME_LEN - 1;
	}

	kbuf = kvzalloc(count + 1, GFP_KERNEL);

	if (copy_from_user(kbuf, ubuf, count)) {
		dev_warn(ctx->dev, "copy_from_user() failed, returning\n");
		kvfree(kbuf);
		return -EFAULT;
	}

	/* Add to tail of the list. */
	identity_create(kbuf, atomic_fetch_add(1, &ctx->cnt));

	/* Wake up the wait queue. */
	atomic_inc(&ctx->data_ready);
	wake_up_interruptible(&ctx->wee_wait);

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

	/* Initialize driver context. */
	ctx = devm_kzalloc(llkd_miscdev.this_device, sizeof(struct drv_ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	ctx->dev = llkd_miscdev.this_device;
	INIT_LIST_HEAD(&ctx->head_node);
	init_waitqueue_head(&ctx->wee_wait);
	mutex_init(&ctx->list_mtx);
	atomic_set(&ctx->cnt, 1);
	atomic_set(&ctx->data_ready, 0);

	dev_info(ctx->dev, "LLKD misc driver (major #10, minor #%d) registered,"
		" dev node is /dev/%s\n", llkd_miscdev.minor, llkd_miscdev.name);

	/* Create memory cache. */
	ctx->mem_cache = kmem_cache_create("list_cache",	  // name in /proc/slabinfo etc
					sizeof(struct identity),  // (min) size of each object
					sizeof(long),		  // 0 or sizeof(long) to align to size of word
					SLAB_HWCACHE_ALIGN,	  // good for performance
					NULL);
	if (!ctx->mem_cache)
		return -ENOMEM;

	/* Create kernel thread for wait queue. */
	ctx->waitq_task = kthread_run(wee_kthread, NULL, "waitq_task");
	if (IS_ERR(wee_kthread)) {
		dev_warn(ctx->dev, "Failed to create kthread\n");
		return PTR_ERR(wee_kthread);
	}


	/* Test data */
	struct identity *temp;

	identity_create("Alice", atomic_fetch_add(1, &ctx->cnt));
	identity_create("Bob", atomic_fetch_add(1, &ctx->cnt));
	identity_create("Dave", atomic_fetch_add(1, &ctx->cnt));
	identity_create("Gena", atomic_fetch_add(1, &ctx->cnt));

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

	if (likely(list_empty(&ctx->head_node)))
		dev_info(ctx->dev, "list is empty now\n");
	else
		dev_info(ctx->dev, "list is left NON-empty\n");

	if (ctx->waitq_task)
		kthread_stop(ctx->waitq_task);

	pr_info("kthread_stop() called\n");

	kmem_cache_destroy(ctx->mem_cache);

	pr_info("kmem_cache_destroy() called\n");

	misc_deregister(&llkd_miscdev);

	pr_info("waitq misc driver deregistered\n");
}

module_init(waitq_init);
module_exit(waitq_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Me");

