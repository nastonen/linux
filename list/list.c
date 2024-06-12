#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/list.h>

#define NAME_LEN 20

LIST_HEAD(head_node);

struct identity {
	struct list_head list;
	char name[NAME_LEN];
	int  id;
	bool busy;
};

static int identity_create(char *name, int id)
{
	struct identity *tmp = NULL;

	tmp = kzalloc(sizeof(struct identity), GFP_KERNEL);
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

static void identity_destroy(int id)
{
	struct identity *curr, *tmp;

	list_for_each_entry_safe(curr, tmp, &head_node, list) {
		if (curr->id == id) {
			list_del(&curr->list);
			kfree(curr);
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
		kfree(curr);
	}
}

static int __init list_init(void)
{
	pr_info("list module loaded!\n");

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

	pr_info("list module unloaded!\n");
}

module_init(list_init);
module_exit(list_exit);

MODULE_LICENSE("GPL");

