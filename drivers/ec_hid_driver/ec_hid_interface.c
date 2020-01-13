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

//ASUS_BSP Deeo : EXPORT g_hidraw for JEDI dongle driver
struct hidraw *g_hidraw;
EXPORT_SYMBOL_GPL(g_hidraw);

//ASUS_BSP Deeo : check ITE8910 in upgrade mode
bool ITE_upgrade_mode;
EXPORT_SYMBOL(ITE_upgrade_mode);

//ASUS_BSP : add for check ec init state +++
#include <linux/delay.h>
extern u8 hid_to_check_init_state(void);
extern int hid_to_init_state(void);
extern int hid_to_gpio_set(u8 gpio, u8 value);
extern u8 gEC_init;
extern void ec_hid_uevent(void);
extern struct completion hid_state;
extern bool hid_used;
extern int ec_mutex_lock(char *);
extern int ec_mutex_unlock(char *);
//ASUS_BSP : add for check ec init state ---
extern void asus_hid_is_connected(void);//Notify the charger, HID is ready
extern int hid_to_get_battery_cap(int *);
extern bool station_low_battery;
extern bool station_shutdown;
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
	struct usb_interface *intf;
	int ret;
	u8 retry=0;	//ASUS_BSP Deeo : add retry
	unsigned int cmask = HID_CONNECT_DEFAULT;
	//int battery_cap = 50;

	printk("[EC_USB] hid->name : %s\n", hdev->name);
	printk("[EC_USB] hid->vendor  : 0x%x\n", hdev->vendor);
	printk("[EC_USB] hid->product : 0x%x\n", hdev->product);
	ASUSEvtlog("[EC_HID] Station ITE 8910 connect\n");

	// Reset station flags
	station_shutdown = false;
	station_low_battery = false;
	ITE_upgrade_mode = false;

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "[EC_USB] parse failed\n");
		goto err_free;
	}

	ret = hid_hw_start(hdev, cmask);
	if (ret) {
		hid_err(hdev, "[EC_USB] hw start failed\n");
		goto err_free;
	}

	if (hdev->product == 0x8910)
		ITE_upgrade_mode = false;
	else
		ITE_upgrade_mode = true;

	if (ITE_upgrade_mode){
		printk("[EC_HID] In Upgrade mode, trigger update UI.");
		ec_hid_uevent();
		printk("[EC_USB] ec_usb_probe : %d\n", ITE_upgrade_mode);
		return 0;
	}

	printk("[EC_USB] usb_disable_autosuspend\n");
	intf = to_usb_interface(hdev->dev.parent);
	usb_disable_autosuspend(interface_to_usbdev(intf));

	//get g_hidraw for JEDI dongle driver
	g_hidraw = hdev->hidraw;

	// check gEC_init
	hid_to_init_state();
	do {
		hid_to_check_init_state();
		printk("[EC_HID] EC init state %d\n", gEC_init);
		msleep(100);
		retry++;
	} while(!gEC_init && retry < 10);

	// Get Station Battery Cap
	/*
	hid_to_get_battery_cap(&battery_cap);
	if ( battery_cap < 3)
		station_low_battery = true;
	else
		station_low_battery = false;
	printk("[EC_HID] station_low_battery %d\n", station_low_battery);
	*/

	//Notify the charger, the HID is ready. Charger needs to check if there is any thermal trigger
	asus_hid_is_connected();
	if (station_shutdown) {
		printk("[EC_HID] station_shutdown %d\n", station_shutdown);
		return 0;
	}

	// Send uevent if init success
	if (gEC_init) {
		ec_hid_uevent();
		hid_to_gpio_set(0x28, 1); // Disable EC uart
		complete_all(&hid_state);
	}else
		printk("[EC_HID] EC init fail, skip uevent\n");

	return 0;
err_free:
	printk("[EC_USB] ec_usb_probe fail.\n");
	return ret;
}

static void ec_usb_remove(struct hid_device *hdev)
{
	printk("[EC_USB] ec_usb_remove\n");
	ASUSEvtlog("[EC_HID] Station ITE 8910 disconnect!!!\n");

	ec_mutex_lock("hidraw");
	printk("[EC_HID] hid_used is %d\n", hid_used);
	g_hidraw = NULL;	//ASUS_BSP Deeo : clean g_hidraw for JEDI dongle driver
	gEC_init = 0;
	hid_used = false;
	station_low_battery = false;
	station_shutdown = false;
	ITE_upgrade_mode = false;
	ec_mutex_unlock("hidraw");

	complete_all(&hid_state);
	msleep(50);
	reinit_completion(&hid_state);

	printk("[EC_USB] hid_hw_stop\n");
	hid_hw_stop(hdev);

	if (hdev->product != 0x8910)
		gDongleType = 255;

	ITE_upgrade_mode = false;
	station_low_battery = false;
}

static struct hid_device_id ec_idtable[] = {
	{ HID_USB_DEVICE(0x048D, 0x8910),
		.driver_data = 0 },
	{ HID_USB_DEVICE(0x048D, 0x89DB),
		.driver_data = 0 },
	{ }
};
MODULE_DEVICE_TABLE(hid, ec_idtable);

static struct hid_driver ec_usb_driver = {
	.name		= "ec_hid_interface",
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
	printk("[EC_USB] ec_usb_init\n");

	return hid_register_driver(&ec_usb_driver);
}

static void __exit ec_usb_exit(void)
{
	printk("[EC_USB] ec_usb_exit\n");

	hid_unregister_driver(&ec_usb_driver);
}
module_init(ec_usb_init);
module_exit(ec_usb_exit);

MODULE_AUTHOR("ASUS Deeo Ho");
MODULE_DESCRIPTION("JEDI dongle EC USB driver");
MODULE_LICENSE("GPL v2");
