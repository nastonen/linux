#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/timer.h>

#define MODNAME "timer_simple"
#define INIT_VALUE 3

static struct st_ctx {
	struct timer_list tmr;
	int data;
} ctx;

static unsigned long exp_ms = 420;

static void ding(struct timer_list *timer)
{
	struct st_ctx *priv = from_timer(priv, timer, tmr);

	priv->data--;
	pr_info("timed_out... data = %d\n", priv->data);

	if (priv->data)
		mod_timer(&priv->tmr, jiffies + msecs_to_jiffies(exp_ms));
}

static int __init timer_simple_init(void)
{
	ctx.data = INIT_VALUE;
	
	// initialize the kernel timer
	ctx.tmr.expires = jiffies + msecs_to_jiffies(exp_ms);
	ctx.tmr.flags = 0;
	timer_setup(&ctx.tmr, ding, 0);

	pr_info("timer set to expire in %ld ms\n", exp_ms);
	
	// start the timer
	add_timer(&ctx.tmr);

	return 0;
}

static void __exit timer_simple_exit(void)
{
	// wait for possible timeouts to complete and delete the timer
	del_timer_sync(&ctx.tmr);
	pr_info("removed\n");
}

module_init(timer_simple_init);
module_exit(timer_simple_exit);

MODULE_AUTHOR("Niko");
MODULE_LICENSE("GPL");

