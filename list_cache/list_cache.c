#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/mutex.h>

#define NAME_LEN 20

static LIST_HEAD(head_node);
static DEFINE_MUTEX(list_mtx);

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

	mutex_lock(&list_mtx);
	list_add_tail(&tmp->list, &head_node);
	mutex_unlock(&list_mtx);

	pr_info("Added node %s to the list\n", tmp->name);

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
		pr_debug("Destroyed %d\n", found->id);
		kmem_cache_free(g_mem_cache, found);
	} else {
		pr_debug("Tried to destroy %d, but not found\n", id);
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

static int __init list_init(void)
{
	pr_info("list module loaded!\n");

	g_mem_cache = kmem_cache_create("list_cache",		  /* name in /proc/slabinfo etc */
					sizeof(struct identity),  /* (min) size of each object */
					sizeof(long),		  /* 0 or sizeof(long) to align to size of word */
					SLAB_HWCACHE_ALIGN,	  /* good for performance */
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

	return 0;
}

static void __exit list_exit(void)
{
	list_destroy();

	if (list_empty(&head_node))
		pr_info("list is empty now\n");
	else
		pr_info("list is left NON-empty\n");

	kmem_cache_destroy(g_mem_cache);

	pr_info("list module unloaded!\n");
}

module_init(list_init);
module_exit(list_exit);

MODULE_LICENSE("GPL");

