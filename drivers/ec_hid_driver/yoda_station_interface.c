/* Copyright (c) 2017, The Linux Foundation. All rights reserved.
 * Edit by ASUS Deeo, deeo_ho@asus.com
 * V3
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/of_platform.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/hidraw.h>
#include <linux/usb.h>

bool YODA_station = false;
EXPORT_SYMBOL(YODA_station);

extern void ec_hid_uevent(void);
extern uint8_t gDongleType;

#ifdef CONFIG_PM
static int ec_usb_resume(struct hid_device *hdev)
{
	return 0;
}

static int ec_usb_suspend(struct hid_device *hdev, pm_message_t message)
{
	return 0;
}
#endif /* CONFIG_PM */

static int ec_usb_raw_event(struct hid_device *hdev,
		struct hid_report *report, u8 *data, int size)
{
	return 0;
}

static int ec_usb_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	//struct usb_interface *intf;
	//int ret;
	//u8 retry=0;	//ASUS_BSP Deeo : add retry
	//unsigned int cmask = HID_CONNECT_DEFAULT;
	//int battery_cap = 50;

	YODA_station = true;

	printk("[YODA_Station] hid->name : %s\n", hdev->name);
	printk("[YODA_Station] hid->vendor  : 0x%x\n", hdev->vendor);
	printk("[YODA_Station] hid->product : 0x%x\n", hdev->product);
	ASUSEvtlog("[YODA_Station] YODA Station connect\n");

	ec_hid_uevent();

	return 0;
}

static void ec_usb_remove(struct hid_device *hdev)
{
	YODA_station = false;
	printk("[YODA_Station] ec_usb_remove\n");
	ASUSEvtlog("[YODA_Station] YODA Station disconnect!!!\n");
}

static struct hid_device_id ec_idtable[] = {
	{ HID_USB_DEVICE(0x0CF2, 0x7750),
		.driver_data = 0 },
	{ HID_USB_DEVICE(0x0CF2, 0x7758),
		.driver_data = 0 },
	{ }
};
MODULE_DEVICE_TABLE(hid, ec_idtable);

static struct hid_driver ec_usb_driver = {
	.name		= "yoda_station_interface",
	.id_table		= ec_idtable,
	.probe			= ec_usb_probe,
	.remove			= ec_usb_remove,
	.raw_event		= ec_usb_raw_event,
#ifdef CONFIG_PM
	.suspend          = ec_usb_suspend,
	.resume			= ec_usb_resume,
#endif
};

static int __init ec_usb_init(void)
{
	printk("[YODA_Station] ec_usb_init\n");

	return hid_register_driver(&ec_usb_driver);
}

static void __exit ec_usb_exit(void)
{
	printk("[YODA_Station] ec_usb_exit\n");

	hid_unregister_driver(&ec_usb_driver);
}
module_init(ec_usb_init);
module_exit(ec_usb_exit);

MODULE_AUTHOR("ASUS Deeo Ho");
MODULE_DESCRIPTION("YODA Station USB HID driver");
MODULE_LICENSE("GPL v2");
