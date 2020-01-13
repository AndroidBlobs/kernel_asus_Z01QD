/* Copyright (c) 2017, The Linux Foundation. All rights reserved.
 * Edit by ASUS Deeo, deeo_ho@asus.com
 * V4
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
#include <linux/time.h>

#include <linux/msm_drm_notify.h>

#include "ec_hid_driver.h"

extern void usb_enable_autosuspend(struct usb_device *);
extern void usb_disable_autosuspend(struct usb_device *);
extern void dwc3_station_uevent(int);
//extern void iris3_i2c_reset(void);

static u8 g_adc = 0;
static u8 pogo_mutex_state = 0;

// For HID wait for completion
struct completion hid_state;
EXPORT_SYMBOL(hid_state);

u8 gEC_init=0;
EXPORT_SYMBOL(gEC_init);

/*
 * 	gDongleType
 * 	- 1: Error
 * 	0 	: No dongle
 * 	1 	: InBox
 * 	2 	: Station
 * 	3 	: DT
 * 	4  : Other
 */
uint8_t gDongleType=0;
EXPORT_SYMBOL(gDongleType);

/*
 * 	gDongleEvent : only for Station ( gDongleType == 2 )
 *
 * 	0 	: Normal mode
 * 	1 	: Upgrade mode
 * 	2 	: Low Battery mode
 * 	3 	: ShutDown & Virtual remove mode
 */
uint8_t gDongleEvent=0;
EXPORT_SYMBOL(gDongleEvent);

//Get extcon dongle_type
struct extcon_dev *extcon_dongle;
EXPORT_SYMBOL(extcon_dongle);

static bool POGO_chg = false;

void ec_get_hal_sensor_status(int mode){
	// Disable with HID vote together
	if (mode)
		suspend_by_hall = true;
	else
		suspend_by_hall = false;

	printk("[EC_HID] hall sensor trigger %d\n", suspend_by_hall);
}
EXPORT_SYMBOL(ec_get_hal_sensor_status);

static int latestfps = 60;
void framerate_change(int fps){
    if (gDongleType == 0 || gDongleType == 2){
        if (fps == 60) {
            gpio_set_value(g_hid_data->pogo_sleep, 0);
            pr_err("[EC_HID] pull down pogo sleep pin");
        } else if (fps == 90) {
            gpio_set_value(g_hid_data->pogo_sleep, 1);
            pr_err("[EC_HID] pull high pogo sleep pin");
        }
    }
    latestfps = fps;
}
EXPORT_SYMBOL(framerate_change);

void control_pogo_sleep(u8 type){

	switch(type){
		case 0:
			if (latestfps == 60) {
				gpio_set_value(g_hid_data->pogo_sleep, 0);
				pr_err("[EC_HID] pull down pogo sleep pin");
			} else if (latestfps == 90) {
				gpio_set_value(g_hid_data->pogo_sleep, 1);
				pr_err("[EC_HID] pull high pogo sleep pin");
			}
		break;
		case 1:
			gpio_set_value(g_hid_data->pogo_sleep, 1); // Give USB codec power
		break;
		case 2:
		//	gpio_set_value(g_hid_data->pogo_sleep, 1); // Notify station EC, Soc is resume or suspend
			if (latestfps == 60) {
				gpio_set_value(g_hid_data->pogo_sleep, 0);
				pr_err("[EC_HID] pull down pogo sleep pin");
			} else if (latestfps == 90) {
				gpio_set_value(g_hid_data->pogo_sleep, 1);
				pr_err("[EC_HID] pull high pogo sleep pin");
			}
		break;
		case 3: // DT
			gpio_set_value(g_hid_data->pogo_sleep, 1); // DT power
		break;
		default:
			gpio_set_value(g_hid_data->pogo_sleep, 0); // no used
	}
	
	printk("[EC_HID] Dongle : %d, POGO_SLEEP[%d] : 0x%x\n", gDongleType, g_hid_data->pogo_sleep, gpio_get_value(g_hid_data->pogo_sleep));
}

void ec_hid_uevent(void){
	
	u8 type;
	struct timespec uptime;
	get_monotonic_boottime(&uptime);
	type = gDongleType;
	gDongleEvent = 0; // reset gDongleEvent

	// before boot time < 15s, Do not send uevent
	if ((unsigned long) uptime.tv_sec < 15){
		printk("[EC_HID] boot time too early, skip send uevent.\n");
		if (type == 2 && ITE_upgrade_mode){
			gDongleEvent = 1;
		}
		/*
		if (type == 2 && station_low_battery) {
			gDongleEvent = 2;
		}
		*/
		if (type == 2 && station_shutdown) {
			gDongleEvent = 3;
		}

		if (type == 2 && YODA_station) {
			gDongleEvent = 10;
		}

		printk("[EC_HID] gDongleType %d, gDongleEvent %d.\n", gDongleType, gDongleEvent);

		return;
	}

	// Fake dongle type
	if (type == 255){
		printk("[EC_HID] type = 255, fake dongle type\n");
		return;
	}

	// Station Shutdown mode
	if (type == 2 && station_shutdown) {
		down(&g_hid_data->pogo_sema);
		printk("[EC_HID] pogo_sema down!!!\n");

		pogo_mutex_state = 1;
		gDongleEvent = 3;
		kobject_uevent(&g_hid_data->dev->kobj, KOBJ_CHANGE);
		ASUSEvtlog("[EC_HID] gDongleEvent : %d, previous_event %d\n", gDongleEvent, g_hid_data->previous_event);
		g_hid_data->previous_event = gDongleEvent;
		return;
	}

	// Station Battery Low mode
	/*
	if (type == 2 && station_low_battery) {
		down(&g_hid_data->pogo_sema);
		printk("[EC_HID] pogo_sema down!!!\n");

		pogo_mutex_state = 1;
		gDongleEvent = 2;
		kobject_uevent(&g_hid_data->dev->kobj, KOBJ_CHANGE);
		g_hid_data->previous_event = gDongleEvent;
		return;
	}
	*/

	// For YODA station connect to JEDI
	if (type == 2 && YODA_station){
		down(&g_hid_data->pogo_sema);
		printk("[EC_HID] pogo_sema down!!!\n");

		pogo_mutex_state = 1;
		gDongleEvent = 10;
		kobject_uevent(&g_hid_data->dev->kobj, KOBJ_CHANGE);
		ASUSEvtlog("[EC_HID] gDongleEvent : %d, previous_event %d\n", gDongleEvent, g_hid_data->previous_event);
		g_hid_data->previous_event = gDongleEvent;
		return;
	}

	// For ITE 8910 Force upgrade
	if (type == 2 && ITE_upgrade_mode){
		down(&g_hid_data->pogo_sema);
		printk("[EC_HID] pogo_sema down!!!\n");

		pogo_mutex_state = 1;
		gDongleEvent = 1;
		kobject_uevent(&g_hid_data->dev->kobj, KOBJ_CHANGE);
		ASUSEvtlog("[EC_HID] gDongleEvent : %d, previous_event %d\n", gDongleEvent, g_hid_data->previous_event);
		g_hid_data->previous_event = gDongleEvent;
		return;
	}

	/*
	// Special handle for station & hall sensore
	if (type == 2 && suspend_by_hall == true){
		printk("[EC_HID] Hall sensor trigger HID disconnect & connect, bypass send uevent.\n");
		ec_get_hal_sensor_status(0);
	}
	*/
	
	// Station re-connect when station battery 0% -> 3%
	if (type == 2 && g_hid_data->previous_event == 3 && gDongleEvent == 0){
		printk("[EC_HID] Station Battery chagered over 3%% re-connect.\n");

		down(&g_hid_data->pogo_sema);
		printk("[EC_HID] pogo_sema down, %d!!!\n", type);
		pogo_mutex_state = 1;

		kobject_uevent(&g_hid_data->dev->kobj, KOBJ_CHANGE);
		ASUSEvtlog("[EC_HID] gDongleEvent : %d, previous_event %d\n", gDongleEvent, g_hid_data->previous_event);
		g_hid_data->previous_event = gDongleEvent;

	}else if (type == 2 && POGO_chg == false){		// Filter abnormal HID disconnect
		printk("[EC_HID] Abnormal disconntect, bypass send uevent.\n");
	}else{
		printk("[EC_HID] Send uevent, POGO_chg %d\n", POGO_chg);

		if (pogo_mutex_state && type == 0){
			printk("[EC_HID] type : %d, pogo_mutex_state : %d, force unlock!!\n", type, pogo_mutex_state);

			pogo_mutex_state = 0;
			printk("[EC_HID] pogo_sema up, %d!!!\n", type);
			up(&g_hid_data->pogo_sema);
		}
		
//		mutex_lock(&g_hid_data->pogo_mutex);
//		printk("[EC_HID] pogo_mutex lock!!!\n");
		down(&g_hid_data->pogo_sema);
		printk("[EC_HID] pogo_sema down, %d!!!\n", type);
		pogo_mutex_state = 1;

		kobject_uevent(&g_hid_data->dev->kobj, KOBJ_CHANGE);
		ASUSEvtlog("[EC_HID] gDongleEvent : %d, previous_event %d\n", gDongleEvent, g_hid_data->previous_event);
		g_hid_data->previous_event = gDongleEvent;
	}

	POGO_chg = false;
}
EXPORT_SYMBOL(ec_hid_uevent);

void detect_dongle_type(u8 adc)
{
	uint8_t type;

	if(g_hid_data == NULL){
		g_adc = adc;
		printk("[EC_HID] g_hid_data is NULL, save adc : 0x%x\n", g_adc);
		return;
	}

	printk("[EC_HID] adc : 0x%x\n", adc);

	if (adc >= 0xBC)
		type = 0;	//No Dongle
	else if ( adc > 0x42 && adc < 0x59 )
		type = 1;	//InBox
	else if ( adc > 0x2C && adc < 0x43 )
		type = 3;	//DT
	else if ( adc > 0x16 && adc < 0x2D )
		type = 2;	//Station
	else if (adc <= 0x10)
		type = 4;	//Error short  (-1)
	else
		type = 4;

	if (type != gDongleType){
		g_hid_data->previous_dongle = gDongleType;
		gDongleType = type;
	}else {
		printk("[EC_HID] gDongleType : %d is equal type %d\n", gDongleType , type);
		return;
	}

	ASUSEvtlog("[EC_HID] gDongleType : %d, previous_dongle %d\n", gDongleType, g_hid_data->previous_dongle);
	control_pogo_sleep(gDongleType);

	if (gDongleType != 2)
		ec_hid_uevent();
	else
		POGO_chg = true;
}
EXPORT_SYMBOL(detect_dongle_type);

int asus_wait4hid (void)
{
	int rc = 0;

    if (gDongleType != 2){
		printk("[EC_HID] Dongle remove, no need to wait\n");
		return -9;
	}

	if (!wait_for_completion_timeout(&hid_state, msecs_to_jiffies(3000))) {
		rc = -EINVAL;
		printk("[EC_HID] wait for HID complete timeout\n");
	}
    
    if (gDongleType != 2){
		printk("[EC_HID] Dongle remove, no need to wait\n");
		return -9;
	}

    return rc;
}
EXPORT_SYMBOL_GPL(asus_wait4hid);

int ec_mutex_lock(char *name)
{
	mutex_lock(&g_hid_data->report_mutex);
	printk("[EC_HID] ec_mutex_lock : %s\n", name);

    return 0;
}
EXPORT_SYMBOL_GPL(ec_mutex_lock);

int ec_mutex_unlock(char *name)
{
	printk("[EC_HID] ec_mutex_unlock : %s\n", name);
	mutex_unlock(&g_hid_data->report_mutex);

    return 0;
}
EXPORT_SYMBOL_GPL(ec_mutex_unlock);

/*
__u8 buf1[9]={0x20, 0x20, 0x01, 0x80, 0x00, 0x00, 0x02, 0x80, 0xC0};
//                     32      32      1     128     0        0       2     128    192
__u8 buf2[7]={0x20, 0x01, 0x01, 0x80, 0x81, 0x00, 0x01};
//					    32      1       1      128    129     0       1
*/

int hid_to_i2c_write(char *buffer, int len)
{
	struct hid_device *hdev;
	//int len=0, value;
	int ret=0;
	int i = 0;
	unsigned char report_type;
	char *cmd;

	printk("[EC_HID] hid_to_i2c_write : %d +++ \n", len);
	mutex_lock(&g_hid_data->report_mutex);
	hid_used = true;
	if (g_hidraw == NULL || g_hid_data->lock) {
		printk("[EC_HID] g_hidraw is NULL or lock %d\n", g_hid_data->lock);
		hid_used = false;
		mutex_unlock(&g_hid_data->report_mutex);
		return -1;
	}

	hdev = g_hidraw->hid;

	hid_hw_power(hdev, PM_HINT_FULLON);

	cmd = kmalloc((len+2) * sizeof(char), GFP_KERNEL);
	if (!cmd) {
		printk("[EC_HID] kmalloc cmd fail.\n");
		ret = -ENOMEM;
		hid_hw_power(hdev, PM_HINT_NORMAL);
		hid_used = false;
		mutex_unlock(&g_hid_data->report_mutex);
		return -1;
	}	

	cmd[0] = 0x20; // Report ID
	cmd[1] = 0x20; // U2S_Cmd_WData
	/*
	for (i = 0; i < count; i++) {
		if (buffer == NULL)
			break;
		ret = sscanf((const char *)buffer, "%u,%s", &value, buffer);
		cmd[i+2] = value;
		printk("[EC_HID] cmd[%d] : 0x%02x\n", i+2, cmd[i+2]);
		len++;
		if (ret <= 1)
			break;
	}
	*/

	//if (len < 50)
		for (i = 0; i < len; i++) {
			cmd[i+2] = buffer[i];
			//printk("[EC_HID] cmd[%d] : 0x%02x\n", i+2, cmd[i+2]);
		}

	len = len+2;
	//printk("[EC_HID] len : %d\n", len);
	report_type = HID_FEATURE_REPORT;
	ret = hid_hw_raw_request(hdev, cmd[0], cmd, len, report_type,
				HID_REQ_SET_REPORT);

	//msleep(5);
	printk("[EC_HID] hid_to_i2c_write --- %d \n", ret);

	hid_hw_power(hdev, PM_HINT_NORMAL);
	hid_used = false;
    mutex_unlock(&g_hid_data->report_mutex);
	return ret;
}
EXPORT_SYMBOL(hid_to_i2c_write);

int hid_to_i2c_read(char *buffer, int len)
{
	struct hid_device *hdev;
	//int len=0, value;
	unsigned char report_type;
	char *cmd;
	int ret=0;
	int i = 0;

	printk("[EC_HID] hid_to_i2c_read : %d +++ \n", len);
	mutex_lock(&g_hid_data->report_mutex);
	hid_used = true;
	if (g_hidraw == NULL || g_hid_data->lock) {
		printk("[EC_HID] g_hidraw is NULL or lock %d\n", g_hid_data->lock);
		hid_used = false;
		mutex_unlock(&g_hid_data->report_mutex);
		return -1;
	}

	hdev = g_hidraw->hid;

	hid_hw_power(hdev, PM_HINT_FULLON);

	cmd = kmalloc((len+2) * sizeof(char), GFP_KERNEL);
	if (!cmd) {
		printk("[EC_HID] kmalloc cmd fail.\n");
		ret = -ENOMEM;
		hid_hw_power(hdev, PM_HINT_NORMAL);
		hid_used = false;
		mutex_unlock(&g_hid_data->report_mutex);
		return -1;
	}	

	cmd[0] = 0x20; // Report ID
	cmd[1] = 0x01; // U2S_Cmd_W2R
/*
	for (i = 0; i < count; i++) {
		if (buffer == NULL)
			break;
		ret = sscanf((const char *)buffer, "%u,%s", &value, buffer);
		cmd[i+2] = value;
		printk("[EC_HID] cmd[%d] : 0x%02x\n", i+2, cmd[i+2]);
		len++;
		if (ret <= 1)
			break;
	}
*/

	//if (len < 50)
		for (i = 0; i < len; i++) {
			cmd[i+2] = buffer[i];
			//printk("[EC_HID] cmd[%d] : 0x%02x\n", i+2, cmd[i+2]);
		}

	len = len+2;
	//printk("[EC_HID] len : %d\n", len);
	report_type = HID_FEATURE_REPORT;
	ret = hid_hw_raw_request(hdev, cmd[0], cmd, len, report_type,
				HID_REQ_SET_REPORT);

	printk("[EC_HID] hid_to_i2c_read --- %d\n", ret);

	hid_hw_power(hdev, PM_HINT_NORMAL);
	hid_used = false;
    mutex_unlock(&g_hid_data->report_mutex);
	//msleep(5);
	return ret;
}
EXPORT_SYMBOL(hid_to_i2c_read);

int hid_get_i2c_data(char *buffer, int count, int *len)
{
	struct hid_device *hdev;
	unsigned char report_type;
	unsigned char report_number;
	int ret = 0;
	//int i = 0;

	printk("[EC_HID] hid_get_i2c_data : count %d , len %d\n", count, (*len));
	mutex_lock(&g_hid_data->report_mutex);
	hid_used = true;
	if (g_hidraw == NULL || g_hid_data->lock) {
		printk("[EC_HID] g_hidraw is NULL or lock %d\n", g_hid_data->lock);
		hid_used = false;
		mutex_unlock(&g_hid_data->report_mutex);
		return -1;
	}

	hdev = g_hidraw->hid;

	hid_hw_power(hdev, PM_HINT_FULLON);

	report_type = HID_FEATURE_REPORT;
	report_number = 0x20;
	ret = hid_hw_raw_request(hdev, report_number, buffer, count, report_type,
				 HID_REQ_GET_REPORT);

	(*len) = (ret < count) ? ret : count;
	//printk("[EC_HID] len : %d\n", (*len));
	//for( i=0 ; i< (*len)  ; i++ ){
	//	printk("[EC_HID] 0x%02x\n", buffer[i]);
	//}

	//msleep(5);
	printk("[EC_HID] hid_get_i2c_data --- %d\n", ret);
	hid_hw_power(hdev, PM_HINT_NORMAL);
	hid_used = false;
    mutex_unlock(&g_hid_data->report_mutex);
	return ret;
}
EXPORT_SYMBOL(hid_get_i2c_data);

int hid_to_enable_mipi(u8 enable)
{
	struct hid_device *hdev;
	unsigned char report_type;
	unsigned char report_number;
	int ret = 0;
	int len = 0;
	char buffer[4] = {0};

	printk("[EC_HID] hid_to_enable_mipi : %d\n", enable);
	mutex_lock(&g_hid_data->report_mutex);
	hid_used = true;
	if (g_hidraw == NULL) {
		printk("[EC_HID] g_hidraw is NULL\n");
		hid_used = false;
		mutex_unlock(&g_hid_data->report_mutex);
		return -1;
	}

	hdev = g_hidraw->hid;

	hid_hw_power(hdev, PM_HINT_FULLON);

// Set report
	report_number = 0x21;

	buffer[0] = 0x21; // Report ID
	buffer[1] = 0x50; // ASUSCmdID
	buffer[2] = enable; // enable bit

	len = 3;
	//printk("[EC_HID] buffer[0] : 0x%02x\n", buffer[0]);
	//printk("[EC_HID] buffer[1] : 0x%02x\n", buffer[1]);
	//printk("[EC_HID] buffer[2] : 0x%02x\n", buffer[2]);

	report_type = HID_FEATURE_REPORT;
	ret = hid_hw_raw_request(hdev, report_number, buffer, len, report_type,
				HID_REQ_SET_REPORT);

	hid_hw_power(hdev, PM_HINT_NORMAL);
	hid_used = false;
    mutex_unlock(&g_hid_data->report_mutex);
	return ret;
}
EXPORT_SYMBOL(hid_to_enable_mipi);

int hid_to_get_mipi_enable_state(u8 *state)
{
	struct hid_device *hdev;
	unsigned char report_type;
	unsigned char report_number;
	int ret = 0;
	//int i = 0;
	int count = 0, len = 0;
	char buffer[3] = {0};

	printk("[EC_HID] hid_to_get_mipi_enable_state\n");
	mutex_lock(&g_hid_data->report_mutex);
	hid_used = true;
	if (g_hidraw == NULL || g_hid_data->lock) {
		printk("[EC_HID] g_hidraw is NULL or lock %d\n", g_hid_data->lock);
		hid_used = false;
		mutex_unlock(&g_hid_data->report_mutex);
		return -1;
	}

	hdev = g_hidraw->hid;

	hid_hw_power(hdev, PM_HINT_FULLON);

// Set report
	report_number = 0x21;

	buffer[0] = 0x21; // Report ID
	buffer[1] = 0x51; // ASUSCmdID

	len = 2;

	report_type = HID_FEATURE_REPORT;
	ret = hid_hw_raw_request(hdev, report_number, buffer, len, report_type,
				HID_REQ_SET_REPORT);

// Get report
	report_number = 0x21;
	count = 2;
	len = 0;
	report_type = HID_FEATURE_REPORT;

	ret = hid_hw_raw_request(hdev, report_number, buffer, count, report_type,
				 HID_REQ_GET_REPORT);

	len= (ret < count) ? ret : count;
	//printk("[EC_HID] len : %d\n", len);
	//for( i=0 ; i< len  ; i++ ){
	//	printk("[EC_HID] 0x%02x\n", buffer[i]);
	//}

	(*state) = buffer[0];

	printk("[EC_HID] mipi enable state : %d\n", (*state));

	hid_hw_power(hdev, PM_HINT_NORMAL);
	hid_used = false;
    mutex_unlock(&g_hid_data->report_mutex);
	return ret;
}
EXPORT_SYMBOL(hid_to_get_mipi_enable_state);

int hid_to_power_down_mipi(u8 enable)	// 0:reset 1:normal
{
	struct hid_device *hdev;
	unsigned char report_type;
	unsigned char report_number;
	int ret = 0;
	int len = 0;
	char buffer[4] = {0};

	printk("[EC_HID] hid_to_power_down_mipi : %d\n", enable);
	mutex_lock(&g_hid_data->report_mutex);
	hid_used = true;
	if (g_hidraw == NULL) {
		printk("[EC_HID] g_hidraw is NULL\n");
		hid_used = false;
		mutex_unlock(&g_hid_data->report_mutex);
		return -1;
	}

	hdev = g_hidraw->hid;

	hid_hw_power(hdev, PM_HINT_FULLON);

// Set report
	report_number = 0x21;

	buffer[0] = 0x21; // Report ID
	buffer[1] = 0x52; // ASUSCmdID
	buffer[2] = enable; // enable bit

	len = 3;
	//printk("[EC_HID] buffer[0] : 0x%02x\n", buffer[0]);
	//printk("[EC_HID] buffer[1] : 0x%02x\n", buffer[1]);
	//printk("[EC_HID] buffer[2] : 0x%02x\n", buffer[2]);

	report_type = HID_FEATURE_REPORT;
	ret = hid_hw_raw_request(hdev, report_number, buffer, len, report_type,
				HID_REQ_SET_REPORT);

	hid_hw_power(hdev, PM_HINT_NORMAL);
	hid_used = false;
    mutex_unlock(&g_hid_data->report_mutex);
	return ret;
}
EXPORT_SYMBOL(hid_to_power_down_mipi);

int hid_to_init_state(void)
{
	struct hid_device *hdev;
	unsigned char report_type;
	unsigned char report_number;
	int ret = 0;
	int len = 0;
	char buffer[4] = {0};

	printk("[EC_HID] hid_to_init_state\n");
	mutex_lock(&g_hid_data->report_mutex);
	hid_used = true;
	if (g_hidraw == NULL) {
		printk("[EC_HID] g_hidraw is NULL\n");
		hid_used = false;
		mutex_unlock(&g_hid_data->report_mutex);
		return -1;
	}

	hdev = g_hidraw->hid;

	hid_hw_power(hdev, PM_HINT_FULLON);

// Set report
	report_number = 0x9A;

	buffer[0] = 0x9A; // Report ID
	buffer[1] = 0x01; // send ready flag

	len = 2;
	//printk("[EC_HID] buffer[0] : 0x%02x\n", buffer[0]);
	//printk("[EC_HID] buffer[1] : 0x%02x\n", buffer[1]);

	report_type = HID_FEATURE_REPORT;
	ret = hid_hw_raw_request(hdev, report_number, buffer, len, report_type,
				HID_REQ_SET_REPORT);

	hid_hw_power(hdev, PM_HINT_NORMAL);
	hid_used = false;
    mutex_unlock(&g_hid_data->report_mutex);
	return ret;
}
EXPORT_SYMBOL(hid_to_init_state);

u8 hid_to_check_init_state(void)
{
	struct hid_device *hdev;
	unsigned char report_type;
	unsigned char report_number;
	int ret = 0;
	//int i = 0;
	int count = 0, len = 0;
	char buffer[3] = {0};

	printk("[EC_HID] hid_to_check_init_state\n");
	mutex_lock(&g_hid_data->report_mutex);
	hid_used = true;
	if (g_hidraw == NULL) {
		printk("[EC_HID] g_hidraw is NULL\n");
		hid_used = false;
		mutex_unlock(&g_hid_data->report_mutex);
		return -1;
	}

	hdev = g_hidraw->hid;

	hid_hw_power(hdev, PM_HINT_FULLON);

// Get report
	report_number = 0x9A;
	count = 1;
	len = 0;
	report_type = HID_FEATURE_REPORT;

	ret = hid_hw_raw_request(hdev, report_number, buffer, count, report_type,
				 HID_REQ_GET_REPORT);

	len= (ret < count) ? ret : count;
	//printk("[EC_HID] len : %d\n", len);
	//for( i=0 ; i< len  ; i++ ){
	//	printk("[EC_HID] 0x%02x\n", buffer[i]);
	//}

// Give Global variable
	gEC_init = buffer[0];
	hid_hw_power(hdev, PM_HINT_NORMAL);
	hid_used = false;
    mutex_unlock(&g_hid_data->report_mutex);
	return buffer[0];
}
EXPORT_SYMBOL(hid_to_check_init_state);

int hid_to_set_freq(u8 HCPRS, u8 LCPRS)
{
	struct hid_device *hdev;
	unsigned char report_type;
	unsigned char report_number;
	int ret = 0;
	int len = 0;
	char buffer[4] = {0};

	printk("[EC_HID] hid_to_set_freq\n");
	mutex_lock(&g_hid_data->report_mutex);
	hid_used = true;
	if (g_hidraw == NULL || g_hid_data->lock) {
		printk("[EC_HID] g_hidraw is NULL or lock %d\n", g_hid_data->lock);
		hid_used = false;
		mutex_unlock(&g_hid_data->report_mutex);
		return -1;
	}

	hdev = g_hidraw->hid;

	hid_hw_power(hdev, PM_HINT_FULLON);

// Set report
	report_number = 0x21;

	buffer[0] = 0x21; // Report ID
	buffer[1] = 0x32; // ASUSCmdID
	buffer[2] = HCPRS; // HCPRS
	buffer[3] = LCPRS; // LCPRS

	len = 4;
	//printk("[EC_HID] buffer[1] : 0x%02x\n", buffer[1]);
	//printk("[EC_HID] buffer[2] : 0x%02x\n", buffer[2]);
	//printk("[EC_HID] buffer[3] : 0x%02x\n", buffer[3]);

	report_type = HID_FEATURE_REPORT;
	ret = hid_hw_raw_request(hdev, report_number, buffer, len, report_type,
				HID_REQ_SET_REPORT);

	hid_hw_power(hdev, PM_HINT_NORMAL);
	hid_used = false;
    mutex_unlock(&g_hid_data->report_mutex);
	return ret;
}
EXPORT_SYMBOL(hid_to_set_freq);

int hid_to_set_duty(u8 FCTR, u8 FDCR)
{
	struct hid_device *hdev;
	unsigned char report_type;
	unsigned char report_number;
	int ret = 0;
	int len = 0;
	char buffer[4] = {0};

	printk("[EC_HID] hid_to_set_duty\n");
	mutex_lock(&g_hid_data->report_mutex);
	hid_used = true;
	if (g_hidraw == NULL || g_hid_data->lock) {
		printk("[EC_HID] g_hidraw is NULL or lock %d\n", g_hid_data->lock);
		hid_used = false;
		mutex_unlock(&g_hid_data->report_mutex);
		return -1;
	}

	hdev = g_hidraw->hid;

	hid_hw_power(hdev, PM_HINT_FULLON);

// Set report
	report_number = 0x21;

	buffer[0] = 0x21; // Report ID
	buffer[1] = 0x33; // ASUSCmdID
	buffer[2] = FCTR; // FCTR
	buffer[3] = FDCR; // FDCR

	len = 4;
	//printk("[EC_HID] buffer[1] : 0x%02x\n", buffer[1]);
	//printk("[EC_HID] buffer[2] : 0x%02x\n", buffer[2]);
	//printk("[EC_HID] buffer[3] : 0x%02x\n", buffer[3]);

	report_type = HID_FEATURE_REPORT;
	ret = hid_hw_raw_request(hdev, report_number, buffer, len, report_type,
				HID_REQ_SET_REPORT);

	hid_hw_power(hdev, PM_HINT_NORMAL);
	hid_used = false;
    mutex_unlock(&g_hid_data->report_mutex);
	return ret;
}
EXPORT_SYMBOL(hid_to_set_duty);

int hid_to_enbale_pwm(u8 enable)
{
	struct hid_device *hdev;
	unsigned char report_type;
	unsigned char report_number;
	int ret = 0;
	int len = 0;
	char buffer[3] = {0};

	printk("[EC_HID] hid_to_enbale_pwm\n");
	mutex_lock(&g_hid_data->report_mutex);
	hid_used = true;
	if (g_hidraw == NULL || g_hid_data->lock) {
		printk("[EC_HID] g_hidraw is NULL or lock %d\n", g_hid_data->lock);
		hid_used = false;
		mutex_unlock(&g_hid_data->report_mutex);
		return -1;
	}

	hdev = g_hidraw->hid;

	hid_hw_power(hdev, PM_HINT_FULLON);

// Set report
	report_number = 0x21;

	buffer[0] = 0x21; // Report ID
	buffer[1] = 0x31; // ASUSCmdID
	if(enable > 0)
		buffer[2] = 0x1; // Enable
	else
		buffer[2] = 0x0; // Disable

	len = 3;
	//printk("[EC_HID] buffer[1] : 0x%02x\n", buffer[1]);
	//printk("[EC_HID] buffer[2] : 0x%02x\n", buffer[2]);

	report_type = HID_FEATURE_REPORT;
	ret = hid_hw_raw_request(hdev, report_number, buffer, len, report_type,
				HID_REQ_SET_REPORT);

	hid_hw_power(hdev, PM_HINT_NORMAL);
	hid_used = false;
    mutex_unlock(&g_hid_data->report_mutex);
	return ret;
}
EXPORT_SYMBOL(hid_to_enbale_pwm);

int hid_to_get_rpm(int *RPM)
{
	struct hid_device *hdev;
	unsigned char report_type;
	unsigned char report_number;
	int ret = 0;
	//int i = 0;
	int count = 0, len = 0;
	char buffer[3] = {0};
	u8 BLDR=0, BLFR=0;

	printk("[EC_HID] hid_to_get_rpm\n");
	mutex_lock(&g_hid_data->report_mutex);
	hid_used = true;
	if (g_hidraw == NULL || g_hid_data->lock) {
		printk("[EC_HID] g_hidraw is NULL or lock %d\n", g_hid_data->lock);
		hid_used = false;
		mutex_unlock(&g_hid_data->report_mutex);
		return -1;
	}

	hdev = g_hidraw->hid;

	hid_hw_power(hdev, PM_HINT_FULLON);

// Set report
	report_number = 0x21;

	buffer[0] = 0x21; // Report ID
	buffer[1] = 0x34; // ASUSCmdID

	len = 2;

	report_type = HID_FEATURE_REPORT;
	ret = hid_hw_raw_request(hdev, report_number, buffer, len, report_type,
				HID_REQ_SET_REPORT);

// Get report
	report_number = 0x21;
	count = 2;
	len = 0;
	report_type = HID_FEATURE_REPORT;

	ret = hid_hw_raw_request(hdev, report_number, buffer, count, report_type,
				 HID_REQ_GET_REPORT);

	len= (ret < count) ? ret : count;
	//printk("[EC_HID] len : %d\n", len);
	//for( i=0 ; i< len  ; i++ ){
	//	printk("[EC_HID] 0x%02x\n", buffer[i]);
	//}
	
	BLDR = buffer[0];
	BLFR = buffer[1];

	(*RPM) = BLFR*30;
	printk("[EC_HID] RPM : %d\n", (*RPM));

	hid_hw_power(hdev, PM_HINT_NORMAL);
	hid_used = false;
    mutex_unlock(&g_hid_data->report_mutex);
	return ret;
}
EXPORT_SYMBOL(hid_to_get_rpm);

int hid_to_set_charger_type(u8 type)
{
	struct hid_device *hdev;
	unsigned char report_type;
	unsigned char report_number;
	int ret = 0;
	int len = 0;
	char buffer[4] = {0};

	printk("[EC_HID] hid_to_set_charger_type : %d\n", type);

	if ( type < 0 || type > 13){
		printk("[EC_HID] type error!!!\n");
		return -2;
	}

	mutex_lock(&g_hid_data->report_mutex);
	hid_used = true;
	if (g_hidraw == NULL || g_hid_data->lock) {
		printk("[EC_HID] g_hidraw is NULL or lock %d\n", g_hid_data->lock);
		hid_used = false;
		mutex_unlock(&g_hid_data->report_mutex);
		return -1;
	}

	hdev = g_hidraw->hid;

	hid_hw_power(hdev, PM_HINT_FULLON);

// Set report
	report_number = 0x21;

	buffer[0] = 0x21; // Report ID
	buffer[1] = 0x80; // ASUSCmdID
	buffer[2] = type;  // Charger type

	len = 4;
	//printk("[EC_HID] buffer[1] : 0x%02x\n", buffer[1]);
	//printk("[EC_HID] buffer[2] : 0x%02x\n", buffer[2]);
	//printk("[EC_HID] buffer[3] : 0x%02x\n", buffer[3]);

	report_type = HID_FEATURE_REPORT;
	ret = hid_hw_raw_request(hdev, report_number, buffer, len, report_type,
				HID_REQ_SET_REPORT);

	hid_hw_power(hdev, PM_HINT_NORMAL);
	hid_used = false;
    mutex_unlock(&g_hid_data->report_mutex);
	return ret;
}
EXPORT_SYMBOL(hid_to_set_charger_type);

int hid_to_get_charger_type(int *type, short *vol, short *cur)
{
	struct hid_device *hdev;
	unsigned char report_type;
	unsigned char report_number;
	int ret = 0;
	//int i = 0;
	int count = 0, len = 0;
	char buffer[5] = {0};

	printk("[EC_HID] hid_to_get_charger_type\n");
	mutex_lock(&g_hid_data->report_mutex);
	hid_used = true;
	if (g_hidraw == NULL || g_hid_data->lock) {
		printk("[EC_HID] g_hidraw is NULL or lock %d\n", g_hid_data->lock);
		hid_used = false;
		mutex_unlock(&g_hid_data->report_mutex);
		return -1;
	}

	hdev = g_hidraw->hid;

	hid_hw_power(hdev, PM_HINT_FULLON);

// Set report
	report_number = 0x21;

	buffer[0] = 0x21; // Report ID
	buffer[1] = 0x70; // ASUSCmdID

	len = 2;

	report_type = HID_FEATURE_REPORT;
	ret = hid_hw_raw_request(hdev, report_number, buffer, len, report_type,
				HID_REQ_SET_REPORT);

// Get report
	report_number = 0x21;
	count = 5;
	len = 0;
	report_type = HID_FEATURE_REPORT;

	ret = hid_hw_raw_request(hdev, report_number, buffer, count, report_type,
				 HID_REQ_GET_REPORT);

	len= (ret < count) ? ret : count;
	//printk("[EC_HID] len : %d\n", len);
	//for( i=0 ; i< len  ; i++ ){
	//	printk("[EC_HID] 0x%02x\n", buffer[i]);
	//}
	
	(*type) = buffer[0];

	(*vol) = buffer[1] << 8;
	(*vol) += buffer[2];

	(*cur) = buffer[3] << 8;
	(*cur) += buffer[4];

	printk("[EC_HID] charger type : %d\n", (*type));
	printk("[EC_HID] station vol : %d\n", (*vol));
	printk("[EC_HID] station cur : %d\n", (*cur));

	hid_hw_power(hdev, PM_HINT_NORMAL);
	hid_used = false;
    mutex_unlock(&g_hid_data->report_mutex);
	return ret;
}
EXPORT_SYMBOL(hid_to_get_charger_type);

int hid_to_get_battery_cap(int *cap)
{
	struct hid_device *hdev;
	unsigned char report_type;
	unsigned char report_number;
	int ret = 0;
	//int i = 0;
	int count = 0, len = 0;
	char buffer[3] = {0};

	printk("[EC_HID] hid_to_get_battery_cap\n");
	mutex_lock(&g_hid_data->report_mutex);
	hid_used = true;
	if (g_hidraw == NULL || g_hid_data->lock) {
		printk("[EC_HID] g_hidraw is NULL or lock %d\n", g_hid_data->lock);
		hid_used = false;
		mutex_unlock(&g_hid_data->report_mutex);
		return -1;
	}

	hdev = g_hidraw->hid;

	hid_hw_power(hdev, PM_HINT_FULLON);

// Set report
	report_number = 0x21;

	buffer[0] = 0x21; // Report ID
	buffer[1] = 0x73; // ASUSCmdID

	len = 2;

	report_type = HID_FEATURE_REPORT;
	ret = hid_hw_raw_request(hdev, report_number, buffer, len, report_type,
				HID_REQ_SET_REPORT);

// Get report
	report_number = 0x21;
	count = 2;
	len = 0;
	report_type = HID_FEATURE_REPORT;

	ret = hid_hw_raw_request(hdev, report_number, buffer, count, report_type,
				 HID_REQ_GET_REPORT);

	len= (ret < count) ? ret : count;
	//printk("[EC_HID] len : %d\n", len);
	//for( i=0 ; i< len  ; i++ ){
	//	printk("[EC_HID] 0x%02x\n", buffer[i]);
	//}
	
	(*cap) = buffer[0];

	printk("[EC_HID] Battery cap : %d\n", (int)(*cap));

	hid_hw_power(hdev, PM_HINT_NORMAL);
	hid_used = false;
    mutex_unlock(&g_hid_data->report_mutex);
	return ret;
}
EXPORT_SYMBOL(hid_to_get_battery_cap);

//////////////////////////////////////////////////////////
int hid_to_get_reset_mipi_status(int *status)
{
	struct hid_device *hdev;
	unsigned char report_type;
	unsigned char report_number;
	int ret = 0;
	//int i = 0;
	int count = 0, len = 0;
	char buffer[3] = {0};

	printk("[EC_HID] hid_to_get_reset_mipi_status\n");
	mutex_lock(&g_hid_data->report_mutex);
	hid_used = true;
	if (g_hidraw == NULL || g_hid_data->lock) {
		printk("[EC_HID] g_hidraw is NULL or lock %d\n", g_hid_data->lock);
		hid_used = false;
		mutex_unlock(&g_hid_data->report_mutex);
		return -1;
	}

	hdev = g_hidraw->hid;

	hid_hw_power(hdev, PM_HINT_FULLON);

// Set report
	report_number = 0x21;

	buffer[0] = 0x21; // Report ID
	buffer[1] = 0x51; // ASUSCmdID

	len = 2;

	report_type = HID_FEATURE_REPORT;
	ret = hid_hw_raw_request(hdev, report_number, buffer, len, report_type,
				HID_REQ_SET_REPORT);

// Get report
	report_number = 0x21;
	count = 2;
	len = 0;
	report_type = HID_FEATURE_REPORT;

	ret = hid_hw_raw_request(hdev, report_number, buffer, count, report_type,
				 HID_REQ_GET_REPORT);

	len= (ret < count) ? ret : count;
	//printk("[EC_HID] len : %d\n", len);
	//for( i=0 ; i< len  ; i++ ){
	//	printk("[EC_HID] 0x%02x\n", buffer[i]);
	//}
	
	(*status) = buffer[0];

	printk("[EC_HID] reset_mipi_status : %d\n", (int)(*status));

	hid_hw_power(hdev, PM_HINT_NORMAL);
	hid_used = false;
	mutex_unlock(&g_hid_data->report_mutex);
	return ret;
}
EXPORT_SYMBOL(hid_to_get_reset_mipi_status);

/////////////////////////////////////////////////////////

int hid_to_get_battery_vol(int *vol)
{
	struct hid_device *hdev;
	unsigned char report_type;
	unsigned char report_number;
	int ret = 0;
	//int i = 0;
	int count = 0, len = 0;
	char buffer[3] = {0};

	printk("[EC_HID] hid_to_get_battery_vol\n");
	mutex_lock(&g_hid_data->report_mutex);
	hid_used = true;
	if (g_hidraw == NULL || g_hid_data->lock) {
		printk("[EC_HID] g_hidraw is NULL or lock %d\n", g_hid_data->lock);
		hid_used = false;
		mutex_unlock(&g_hid_data->report_mutex);
		return -1;
	}

	hdev = g_hidraw->hid;

	hid_hw_power(hdev, PM_HINT_FULLON);

// Set report
	report_number = 0x21;

	buffer[0] = 0x21; // Report ID
	buffer[1] = 0x72; // ASUSCmdID

	len = 2;

	report_type = HID_FEATURE_REPORT;
	ret = hid_hw_raw_request(hdev, report_number, buffer, len, report_type,
				HID_REQ_SET_REPORT);

// Get report
	report_number = 0x21;
	count = 2;
	len = 0;
	report_type = HID_FEATURE_REPORT;

	ret = hid_hw_raw_request(hdev, report_number, buffer, count, report_type,
				 HID_REQ_GET_REPORT);

	len= (ret < count) ? ret : count;
	//printk("[EC_HID] len : %d\n", len);
	//for( i=0 ; i< len  ; i++ ){
	//	printk("[EC_HID] 0x%02x\n", buffer[i]);
	//}
	
	(*vol) = buffer[0] << 8;
	(*vol) += buffer[1];

	printk("[EC_HID] Battery vol : %d mV\n", (int)(*vol));

	hid_hw_power(hdev, PM_HINT_NORMAL);
	hid_used = false;
    mutex_unlock(&g_hid_data->report_mutex);
	return ret;
}
EXPORT_SYMBOL(hid_to_get_battery_vol);

int hid_to_get_battery_cur(short *cur)
{
	struct hid_device *hdev;
	unsigned char report_type;
	unsigned char report_number;
	int ret = 0;
	//int i = 0;
	int count = 0, len = 0;
	char buffer[3] = {0};

	printk("[EC_HID] hid_to_get_battery_cur\n");
	mutex_lock(&g_hid_data->report_mutex);
	hid_used = true;
	if (g_hidraw == NULL || g_hid_data->lock) {
		printk("[EC_HID] g_hidraw is NULL or lock %d\n", g_hid_data->lock);
		hid_used = false;
		mutex_unlock(&g_hid_data->report_mutex);
		return -1;
	}

	hdev = g_hidraw->hid;

	hid_hw_power(hdev, PM_HINT_FULLON);

// Set report
	report_number = 0x21;

	buffer[0] = 0x21; // Report ID
	buffer[1] = 0x71; // ASUSCmdID

	len = 2;

	report_type = HID_FEATURE_REPORT;
	ret = hid_hw_raw_request(hdev, report_number, buffer, len, report_type,
				HID_REQ_SET_REPORT);

// Get report
	report_number = 0x21;
	count = 2;
	len = 0;
	report_type = HID_FEATURE_REPORT;

	ret = hid_hw_raw_request(hdev, report_number, buffer, count, report_type,
				 HID_REQ_GET_REPORT);

	len= (ret < count) ? ret : count;
	//printk("[EC_HID] len : %d\n", len);
	//for( i=0 ; i< len  ; i++ ){
	//	printk("[EC_HID] 0x%02x\n", buffer[i]);
	//}
	
	(*cur) = buffer[0] << 8;
	(*cur) += buffer[1];

	printk("[EC_HID] Battery cur : %d mA\n", (*cur));

	hid_hw_power(hdev, PM_HINT_NORMAL);
	hid_used = false;
    mutex_unlock(&g_hid_data->report_mutex);
	return ret;
}
EXPORT_SYMBOL(hid_to_get_battery_cur);

int hid_to_get_thermal_alert(int *state)
{
	struct hid_device *hdev;
	unsigned char report_type;
	unsigned char report_number;
	int ret = 0;
	//int i = 0;
	int count = 0, len = 0;
	char buffer[3] = {0};

	printk("[EC_HID] hid_to_get_thermal_alert\n");
	mutex_lock(&g_hid_data->report_mutex);
	hid_used = true;
	if (g_hidraw == NULL || g_hid_data->lock) {
		printk("[EC_HID] g_hidraw is NULL or lock %d\n", g_hid_data->lock);
		hid_used = false;
		mutex_unlock(&g_hid_data->report_mutex);
		return -1;
	}

	hdev = g_hidraw->hid;

	hid_hw_power(hdev, PM_HINT_FULLON);

// Set report
	report_number = 0x21;

	buffer[0] = 0x21; // Report ID
	buffer[1] = 0x74; // ASUSCmdID

	len = 2;

	report_type = HID_FEATURE_REPORT;
	ret = hid_hw_raw_request(hdev, report_number, buffer, len, report_type,
				HID_REQ_SET_REPORT);

// Get report
	report_number = 0x21;
	count = 2;
	len = 0;
	report_type = HID_FEATURE_REPORT;

	ret = hid_hw_raw_request(hdev, report_number, buffer, count, report_type,
				 HID_REQ_GET_REPORT);

	len= (ret < count) ? ret : count;
	//printk("[EC_HID] len : %d\n", len);
	//for( i=0 ; i< len  ; i++ ){
	//	printk("[EC_HID] 0x%02x\n", buffer[i]);
	//}
	
	(*state) = buffer[0];

	printk("[EC_HID] Thernal alert : %d\n", (*state));

	hid_hw_power(hdev, PM_HINT_NORMAL);
	hid_used = false;
    mutex_unlock(&g_hid_data->report_mutex);
	return ret;
}
EXPORT_SYMBOL(hid_to_get_thermal_alert);

int hid_to_get_u0504_state(int *state)
{
	struct hid_device *hdev;
	unsigned char report_type;
	unsigned char report_number;
	int ret = 0;
	//int i = 0;
	int count = 0, len = 0;
	char buffer[3] = {0};

	printk("[EC_HID] hid_to_get_u0504_state\n");
	mutex_lock(&g_hid_data->report_mutex);
	hid_used = true;
	if (g_hidraw == NULL || g_hid_data->lock) {
		printk("[EC_HID] g_hidraw is NULL or lock %d\n", g_hid_data->lock);
		hid_used = false;
		mutex_unlock(&g_hid_data->report_mutex);
		return -1;
	}

	hdev = g_hidraw->hid;

	hid_hw_power(hdev, PM_HINT_FULLON);

// Set report
	report_number = 0x21;

	buffer[0] = 0x21; // Report ID
	buffer[1] = 0x75; // ASUSCmdID

	len = 2;

	report_type = HID_FEATURE_REPORT;
	ret = hid_hw_raw_request(hdev, report_number, buffer, len, report_type,
				HID_REQ_SET_REPORT);

// Get report
	report_number = 0x21;
	count = 2;
	len = 0;
	report_type = HID_FEATURE_REPORT;

	ret = hid_hw_raw_request(hdev, report_number, buffer, count, report_type,
				 HID_REQ_GET_REPORT);

	len= (ret < count) ? ret : count;
	//printk("[EC_HID] len : %d\n", len);
	//for( i=0 ; i< len  ; i++ ){
	//	printk("[EC_HID] 0x%02x\n", buffer[i]);
	//}

	(*state) = buffer[0];

	printk("[EC_HID] u0504 state : %d\n", (*state));

	hid_hw_power(hdev, PM_HINT_NORMAL);
	hid_used = false;
    mutex_unlock(&g_hid_data->report_mutex);
	return ret;
}
EXPORT_SYMBOL(hid_to_get_u0504_state);

int hid_to_set_factory_mode(u8 type)
{
	struct hid_device *hdev;
	unsigned char report_type;
	unsigned char report_number;
	int ret = 0;
	int len = 0;
	char buffer[4] = {0};

	printk("[EC_HID] hid_to_set_factory_mode : %d\n", type);
	mutex_lock(&g_hid_data->report_mutex);
	hid_used = true;
	if (g_hidraw == NULL || g_hid_data->lock) {
		printk("[EC_HID] g_hidraw is NULL or lock %d\n", g_hid_data->lock);
		hid_used = false;
		mutex_unlock(&g_hid_data->report_mutex);
		return -1;
	}

	hdev = g_hidraw->hid;

	hid_hw_power(hdev, PM_HINT_FULLON);

// Set report
	report_number = 0x21;

	buffer[0] = 0x21; // Report ID
	buffer[1] = 0x90; // ASUSCmdID
	buffer[2] = type;  // Charger type

	len = 3;
	//printk("[EC_HID] buffer[1] : 0x%02x\n", buffer[1]);
	//printk("[EC_HID] buffer[2] : 0x%02x\n", buffer[2]);
	//printk("[EC_HID] buffer[3] : 0x%02x\n", buffer[3]);

	report_type = HID_FEATURE_REPORT;
	ret = hid_hw_raw_request(hdev, report_number, buffer, len, report_type,
				HID_REQ_SET_REPORT);

	hid_hw_power(hdev, PM_HINT_NORMAL);
	hid_used = false;
    mutex_unlock(&g_hid_data->report_mutex);
	return ret;
}
EXPORT_SYMBOL(hid_to_set_factory_mode);

int hid_to_get_factory_mode(u8 *data)
{
	struct hid_device *hdev;
	unsigned char report_type;
	unsigned char report_number;
	int ret = 0;
	//int i = 0;
	int count = 0, len = 0;
	char buffer[3] = {0};

	printk("[EC_HID] hid_to_get_factory_mode\n");
	mutex_lock(&g_hid_data->report_mutex);
	hid_used = true;
	if (g_hidraw == NULL || g_hid_data->lock) {
		printk("[EC_HID] g_hidraw is NULL or lock %d\n", g_hid_data->lock);
		hid_used = false;
		mutex_unlock(&g_hid_data->report_mutex);
		return -1;
	}

	hdev = g_hidraw->hid;

	hid_hw_power(hdev, PM_HINT_FULLON);

// Set report
	report_number = 0x21;

	buffer[0] = 0x21; // Report ID
	buffer[1] = 0x91; // ASUSCmdID

	len = 2;

	report_type = HID_FEATURE_REPORT;
	ret = hid_hw_raw_request(hdev, report_number, buffer, len, report_type,
				HID_REQ_SET_REPORT);

// Get report
	report_number = 0x21;
	count = 2;
	len = 0;
	report_type = HID_FEATURE_REPORT;

	ret = hid_hw_raw_request(hdev, report_number, buffer, count, report_type,
				 HID_REQ_GET_REPORT);

	len= (ret < count) ? ret : count;
	//printk("[EC_HID] len : %d\n", len);
	//for( i=0 ; i< len  ; i++ ){
	//	printk("[EC_HID] 0x%02x\n", buffer[i]);
	//}

	(*data) = buffer[0];

	printk("[EC_HID] factory mode : %d\n", (*data));

	hid_hw_power(hdev, PM_HINT_NORMAL);
	hid_used = false;
    mutex_unlock(&g_hid_data->report_mutex);
	return ret;
}
EXPORT_SYMBOL(hid_to_get_factory_mode);

int hid_to_check_interrupt(u8 *type, u8 *event)
{
	struct hid_device *hdev;
	unsigned char report_type;
	unsigned char report_number;
	int ret = 0;
	//int i = 0;
	int count = 0, len = 0;
	char buffer[3] = {0};
	bool anx_flag = false;
	bool px_flag = false;

	mutex_lock(&g_hid_data->report_mutex);
	hid_used = true;
	if (g_hidraw == NULL || g_hid_data->lock) {
		printk("[EC_HID] g_hidraw is NULL or lock %d\n", g_hid_data->lock);
		hid_used = false;
		mutex_unlock(&g_hid_data->report_mutex);
		(*type) = 255;
		(*event) = 255;
		return -1;
	}

	hdev = g_hidraw->hid;

	hid_hw_power(hdev, PM_HINT_FULLON);

// Set report
	report_number = 0x21;

	buffer[0] = 0x21; // Report ID
	buffer[1] = 0x97; // ASUSCmdID

	len = 2;

	report_type = HID_FEATURE_REPORT;
	ret = hid_hw_raw_request(hdev, report_number, buffer, len, report_type,
				HID_REQ_SET_REPORT);

// Get report
	report_number = 0x21;
	count = 2;
	len = 0;
	report_type = HID_FEATURE_REPORT;

	ret = hid_hw_raw_request(hdev, report_number, buffer, count, report_type,
				 HID_REQ_GET_REPORT);

	if (ret < 0)
		printk("[EC_HID] hid_hw_raw_request error, %d\n", ret);
	else
		len= (ret < count) ? ret : count;
	//printk("[EC_HID] len : %d\n", len);
	//for( i=0 ; i< len  ; i++ ){
	//	printk("[EC_HID] 0x%02x\n", buffer[i]);
	//}

	(*type) = buffer[0];

	printk("[EC_HID] interrupt byte : 0x%02x\n", (*type));

	if (g_hidraw == NULL || g_hid_data->lock || ret < 0) {
		printk("[EC_HID] g_hidraw is NULL or lock %d\n", g_hid_data->lock);
		(*type) = 255;
		(*event) = 255;
		hid_hw_power(hdev, PM_HINT_NORMAL);
		hid_used = false;
		mutex_unlock(&g_hid_data->report_mutex);
		return -1;
	}

	(*event) = 0;

	if((*type) & 0x1){
		printk("[EC_HID] Detect SDP\n");
	}

	if((*type) & 0x2){
		printk("[EC_HID] Detect Thermal alert.\n");

		// Get thermal alert
		// Set report
		report_number = 0x21;

		buffer[0] = 0x21; // Report ID
		buffer[1] = 0x74; // ASUSCmdID

		len = 2;

		report_type = HID_FEATURE_REPORT;
		ret = hid_hw_raw_request(hdev, report_number, buffer, len, report_type,
					HID_REQ_SET_REPORT);

		// Get report
		report_number = 0x21;
		count = 2;
		len = 0;
		report_type = HID_FEATURE_REPORT;

		ret = hid_hw_raw_request(hdev, report_number, buffer, count, report_type,
					 HID_REQ_GET_REPORT);

		len= (ret < count) ? ret : count;

		(*event) = buffer[0];
	}
	
	if((*type) & 0x4){
		printk("[EC_HID] Detect AC\n");
	}

	if((*type) & 0x40){
		printk("[EC_HID] Station EC shutdown!!!!\n");
		station_shutdown = true;
	} else
		station_shutdown = false;

	if((*type) & 0x80){
		// ANX interrupt
		report_number = 0x21;

		buffer[0] = 0x21; // Report ID
		buffer[1] = 0x95; // ASUSCmdID

		len = 2;

		report_type = HID_FEATURE_REPORT;
		ret = hid_hw_raw_request(hdev, report_number, buffer, len, report_type,
					HID_REQ_SET_REPORT);

		// Get report
		report_number = 0x21;
		count = 2;
		len = 0;
		report_type = HID_FEATURE_REPORT;

		ret = hid_hw_raw_request(hdev, report_number, buffer, count, report_type,
					 HID_REQ_GET_REPORT);

		len= (ret < count) ? ret : count;

		(*event) = 0x0;

		printk("[EC_HID] ANX interrupt Byte : 0x%02x, 0x%02x\n", buffer[0], buffer[1]);

		if ((buffer[0] & 0x1F) == 0x10)
			anx_flag = true;
		else
			anx_flag = false;

		if ((buffer[0] & 0x1F) == 0x16) {
			px_flag = true;
		} else if((buffer[0] & 0x1F) == 0x06) {
			px_flag = true;
		} else {
			px_flag = false;
		}
	}

	printk("[EC_HID] interrupt event : %d\n", (*event));

	hid_hw_power(hdev, PM_HINT_NORMAL);
	hid_used = false;
	mutex_unlock(&g_hid_data->report_mutex);

	// Deal with Station Shutdown event
	if (station_shutdown) {
		ec_hid_uevent();
	}

	if (anx_flag){
		queue_work(g_hid_data->workqueue, &g_hid_data->anx_work);
	}

	if (px_flag){
//		iris3_i2c_reset();
	}

	return ret;
}
EXPORT_SYMBOL(hid_to_check_interrupt);

int hid_to_notify_shutdown(u8 type)
{
	struct hid_device *hdev;
	unsigned char report_type;
	unsigned char report_number;
	int ret = 0;
	int len = 0;
	char buffer[4] = {0};

	printk("[EC_HID] hid_to_notify_shutdown : %d\n", type);
	mutex_lock(&g_hid_data->report_mutex);
	hid_used = true;
	if (g_hidraw == NULL || g_hid_data->lock) {
		printk("[EC_HID] g_hidraw is NULL or lock %d\n", g_hid_data->lock);
		hid_used = false;
		mutex_unlock(&g_hid_data->report_mutex);
		return -1;
	}

	hdev = g_hidraw->hid;

	hid_hw_power(hdev, PM_HINT_FULLON);

// Set report
	report_number = 0x21;

	buffer[0] = 0x21; // Report ID
	buffer[1] = 0x94; // ASUSCmdID
	buffer[2] = type;  // Charger type

	len = 3;
	//printk("[EC_HID] buffer[1] : 0x%02x\n", buffer[1]);
	//printk("[EC_HID] buffer[2] : 0x%02x\n", buffer[2]);
	//printk("[EC_HID] buffer[3] : 0x%02x\n", buffer[3]);

	report_type = HID_FEATURE_REPORT;
	ret = hid_hw_raw_request(hdev, report_number, buffer, len, report_type,
				HID_REQ_SET_REPORT);

	hid_hw_power(hdev, PM_HINT_NORMAL);
	hid_used = false;
    mutex_unlock(&g_hid_data->report_mutex);
	return ret;
}
EXPORT_SYMBOL(hid_to_notify_shutdown);

int hid_to_set_ultra_power_mode(u8 type)	// 1:in 0:out
{
	struct hid_device *hdev;
	unsigned char report_type;
	unsigned char report_number;
	int ret = 0;
	int len = 0;
	char buffer[4] = {0};

	printk("[EC_HID] hid_to_set_ultra_power_mode : %d\n", type);
	mutex_lock(&g_hid_data->report_mutex);
	hid_used = true;
	if (g_hidraw == NULL || g_hid_data->lock) {
		printk("[EC_HID] g_hidraw is NULL or lock %d\n", g_hid_data->lock);
		hid_used = false;
		mutex_unlock(&g_hid_data->report_mutex);
		return -1;
	}

	hdev = g_hidraw->hid;

	hid_hw_power(hdev, PM_HINT_FULLON);

// Set report
	report_number = 0x21;

	buffer[0] = 0x21; // Report ID
	buffer[1] = 0x99; // ASUSCmdID
	buffer[2] = type;  // Charger type

	len = 3;
	//printk("[EC_HID] buffer[1] : 0x%02x\n", buffer[1]);
	//printk("[EC_HID] buffer[2] : 0x%02x\n", buffer[2]);
	//printk("[EC_HID] buffer[3] : 0x%02x\n", buffer[3]);

	report_type = HID_FEATURE_REPORT;
	ret = hid_hw_raw_request(hdev, report_number, buffer, len, report_type,
				HID_REQ_SET_REPORT);

	hid_hw_power(hdev, PM_HINT_NORMAL);
	hid_used = false;
    mutex_unlock(&g_hid_data->report_mutex);
	return ret;
}
EXPORT_SYMBOL(hid_to_set_ultra_power_mode);

static ssize_t i2c_write_store(struct device *dev,
					  struct device_attribute *mattr,
					  const char *data, size_t count)
{
	int ret=0;
	char *tmp, *buffer;	///
	int i,value,len=0;				///
	
	printk("[EC_HID] i2c_write_store : count %d\n", (int)count);
	tmp = (char *)data;

	buffer = kmalloc(((int)count) * sizeof(char), GFP_KERNEL);
	if (!buffer) {
		printk("[EC_HID] kmalloc buffer fail.\n");
		ret = -ENOMEM;
		return -1;
	}

	for (i = 0; i < (int)count; i++) {
		if (tmp == NULL)
			break;
		ret = sscanf((const char *)tmp, "%u,%s", &value, tmp);
		buffer[i] = value;
		//printk("[EC_HID] buffer[%d] : 0x%02x\n", i, buffer[i]);
		len++;
		if (ret <= 1)
			break;
	}
	printk("[EC_HID] len : %d\n", len);

	ret = hid_to_i2c_write(buffer, len);

	printk("[EC_HID] i2c_write_store : ret %d\n", ret);
	return count;
}

static ssize_t i2c_read_show(struct device *dev,
					 struct device_attribute *mattr,
					 char *buf)
{
	char *buffer;
	int count=0;
	int ret = 0, len;

	printk("[EC_HID] i2c_read_show\n");

	count = 2;
	len = 0;

	buffer = kmalloc(count * sizeof(char), GFP_KERNEL);
	if (!buffer) {
		printk("[EC_HID] kmalloc buffer fail.\n");
		ret = -ENOMEM;
		return -1;
	}

	ret = hid_get_i2c_data(buffer, count, &len);
	if (ret < 0)
		return sprintf(buf, "%s\n", "HID not connect");

	return sprintf(buf, "len : %d, data : 0x%02x\n", len, buffer[0]);
}

static ssize_t i2c_read_store(struct device *dev,
					  struct device_attribute *mattr,
					  const char *data, size_t count)
{
	int ret=0, value, i, len=0;
	char *buffer, *tmp;

	printk("[EC_HID] i2c_read_store : count %d\n", (int)count);
	tmp = (char *)data;

	buffer = kmalloc(((int)count) * sizeof(char), GFP_KERNEL);
	if (!buffer) {
		printk("[EC_HID] kmalloc buffer fail.\n");
		ret = -ENOMEM;
		return -1;
	}

	for (i = 0; i < (int)count; i++) {
		if (tmp == NULL)
			break;
		ret = sscanf((const char *)tmp, "%u,%s", &value, tmp);
		buffer[i] = value;
		//printk("[EC_HID] buffer[%d] : 0x%02x\n", i, buffer[i]);
		len++;
		if (ret <= 1)
			break;
	}
	printk("[EC_HID] len : %d\n", len);

	ret = hid_to_i2c_read(buffer, len);

	printk("[EC_HID] i2c_read_store : ret %d\n", ret);
	return count;
}

int hid_to_gpio_set(u8 gpio, u8 value)
{
	struct hid_device *hdev;
	int len = 0;
	int ret = 0;
	//int i = 0;
	unsigned char report_type;
	unsigned char report_number;
	char cmd[5] = {0};

	printk("[EC_HID] hid_to_gpio_set : GPIO[%d] : 0x%x\n", gpio, value);
	mutex_lock(&g_hid_data->report_mutex);
	hid_used = true;
	if (g_hidraw == NULL || g_hid_data->lock) {
		printk("[EC_HID] g_hidraw is NULL or lock %d\n", g_hid_data->lock);
		hid_used = false;
		mutex_unlock(&g_hid_data->report_mutex);
		return -1;
	}

	hdev = g_hidraw->hid;

	hid_hw_power(hdev, PM_HINT_FULLON);

	report_number = 0x21;

	cmd[0] = 0x21; // Report ID
	cmd[1] = 0x21; // ASUSCmdID
	cmd[2] = 0x00;      // type write
	cmd[3] = (u8)gpio;  // GPIO num
	cmd[4] = (u8)value;// set H/L

	len = 5;
	//for (i = 0; i < len ; i++) {
	//	printk("[EC_HID] cmd[%d] : 0x%02x\n", i, cmd[i]);
	//}

	report_type = HID_FEATURE_REPORT;
	ret = hid_hw_raw_request(hdev, report_number, cmd, len, report_type,
				HID_REQ_SET_REPORT);

	//msleep(5);
	//printk("[EC_HID] hid_to_gpio_set --- %d\n", ret);

	hid_hw_power(hdev, PM_HINT_NORMAL);
	hid_used = false;
    mutex_unlock(&g_hid_data->report_mutex);
	return ret;
}
EXPORT_SYMBOL(hid_to_gpio_set);

int hid_to_gpio_get(u8 gpio)
{
	struct hid_device *hdev;
	int len = 0;
	int ret=0;
	//int i = 0;
	unsigned char report_type;
	unsigned char report_number;
	char cmd[4] = {0};

	printk("[EC_HID] hid_to_gpio_get : GPIO[%d]\n", gpio);
	mutex_lock(&g_hid_data->report_mutex);
	hid_used = true;
	if (g_hidraw == NULL || g_hid_data->lock) {
		printk("[EC_HID] g_hidraw is NULL or lock %d\n", g_hid_data->lock);
		hid_used = false;
		mutex_unlock(&g_hid_data->report_mutex);
		return -1;
	}

	hdev = g_hidraw->hid;

	hid_hw_power(hdev, PM_HINT_FULLON);

	report_number = 0x21;

	cmd[0] = 0x21; // Report ID
	cmd[1] = 0x21; // ASUSCmdID
	cmd[2] = 0x01; // type read
	cmd[3] = gpio;  // GPIO num

	len = 4;
	//for (i = 0; i < len ; i++) {
	//	printk("[EC_HID] cmd[%d] : 0x%02x\n", i, cmd[i]);
	//}

	report_type = HID_FEATURE_REPORT;
	ret = hid_hw_raw_request(hdev, report_number, cmd, len, report_type,
				HID_REQ_SET_REPORT);

	//msleep(5);
	//printk("[EC_HID] hid_to_gpio_get --- %d\n", ret);

	hid_hw_power(hdev, PM_HINT_NORMAL);
	hid_used = false;
    mutex_unlock(&g_hid_data->report_mutex);
	return ret;
}
EXPORT_SYMBOL(hid_to_gpio_get);

int hid_get_gpio_data(char *buffer, int count, int *len)
{
	struct hid_device *hdev;
	unsigned char report_type;
	unsigned char report_number;
	int ret = 0;
	//int i = 0;

	printk("[EC_HID] hid_get_gpio_data : count %d , len %d\n", count, (*len));
	mutex_lock(&g_hid_data->report_mutex);
	hid_used = true;
	if (g_hidraw == NULL || g_hid_data->lock) {
		printk("[EC_HID] g_hidraw is NULL or lock %d\n", g_hid_data->lock);
		hid_used = false;
		mutex_unlock(&g_hid_data->report_mutex);
		return -1;
	}

	hdev = g_hidraw->hid;

	hid_hw_power(hdev, PM_HINT_FULLON);

	report_type = HID_FEATURE_REPORT;
	report_number = 0x21;
	ret = hid_hw_raw_request(hdev, report_number, buffer, count, report_type,
				 HID_REQ_GET_REPORT);

	(*len) = (ret < count) ? ret : count;
	//printk("[EC_HID] len : %d\n", (*len));
	//for( i=0 ; i< (*len)  ; i++ ){
	//	printk("[EC_HID] 0x%02x\n", buffer[i]);
	//}

	//msleep(5);
	//printk("[EC_HID] hid_get_gpio_data --- %d\n", ret);

	hid_hw_power(hdev, PM_HINT_NORMAL);
	hid_used = false;
    mutex_unlock(&g_hid_data->report_mutex);
	return ret;
}
EXPORT_SYMBOL(hid_get_gpio_data);

int hid_to_get_dp_fw(u8 *fw_ver)
{
	struct hid_device *hdev;
	unsigned char report_type;
	unsigned char report_number;
	int ret = 0;
	//int i = 0;
	int count = 0, len = 0;
	char buffer[3] = {0};

	printk("[EC_HID] hid_to_get_dp_fw\n");
	mutex_lock(&g_hid_data->report_mutex);
	hid_used = true;
	if (g_hidraw == NULL || g_hid_data->lock) {
		printk("[EC_HID] g_hidraw is NULL or lock %d\n", g_hid_data->lock);
		hid_used = false;
		mutex_unlock(&g_hid_data->report_mutex);
		return -1;
	}

	hdev = g_hidraw->hid;

	hid_hw_power(hdev, PM_HINT_FULLON);

// Set report
	report_number = 0x21;

	buffer[0] = 0x21; // Report ID
	buffer[1] = 0x9B; // ASUSCmdID

	len = 2;

	report_type = HID_FEATURE_REPORT;
	ret = hid_hw_raw_request(hdev, report_number, buffer, len, report_type,
				HID_REQ_SET_REPORT);

// Get report
	report_number = 0x21;
	count = 2;
	len = 0;
	report_type = HID_FEATURE_REPORT;

	ret = hid_hw_raw_request(hdev, report_number, buffer, count, report_type,
				 HID_REQ_GET_REPORT);

	len= (ret < count) ? ret : count;
	//printk("[EC_HID] len : %d\n", len);
	//for( i=0 ; i< len  ; i++ ){
	//	printk("[EC_HID] 0x%02x\n", buffer[i]);
	//}
	
	fw_ver[0] = buffer[0];
	fw_ver[1] = buffer[1];

	printk("[EC_HID] DT FW VER : 0x%x, 0x%x\n", fw_ver[0], fw_ver[1]);

	hid_hw_power(hdev, PM_HINT_NORMAL);
	hid_used = false;
    mutex_unlock(&g_hid_data->report_mutex);
	return ret;
}
EXPORT_SYMBOL(hid_to_get_dp_fw);

static ssize_t set_gpio_store(struct device *dev,
					  struct device_attribute *mattr,
					  const char *data, size_t count)
{
	int ret=0;
	int gpio = 0, gpio_value = 0;

	sscanf(data, "%x %x", &gpio, &gpio_value);

	ret = hid_to_gpio_set((u8)gpio, (u8)gpio_value);

	printk("[EC_HID] set_gpio_store : ret %d\n", ret);
	return count;
}

static ssize_t get_gpio_store(struct device *dev,
					  struct device_attribute *mattr,
					  const char *data, size_t count)
{
	int ret=0;
	int gpio = 0;

	printk("[EC_HID] get_gpio_store : count %d\n", (int)count);

	sscanf(data, "%x", &gpio);

	ret = hid_to_gpio_get((u8)gpio);

	printk("[EC_HID] get_gpio_store : ret %d\n", ret);
	return count;
}

static ssize_t get_gpio_show(struct device *dev,
					 struct device_attribute *mattr,
					 char *buf)
{
	char buffer[16] = {0};
	int count=0;
	int ret = 0, len;

	printk("[EC_HID] get_gpio_show\n");

	count = 2;
	len = 0;

	ret = hid_get_gpio_data(buffer, count, &len);
	if (ret < 0)
		return sprintf(buf, "%s\n", "HID not connect");

	return sprintf(buf, "0x%x\n", buffer[0]);
}

static ssize_t gDongleType_show(struct device *dev,
					 struct device_attribute *mattr,
					 char *buf)
{
	printk("[EC_HID] gDongleType_show : %d\n", gDongleType);
	return sprintf(buf, "%d\n", gDongleType);
}

static ssize_t gDongleType_store(struct device *dev,
					  struct device_attribute *mattr,
					  const char *data, size_t count)
{
	int val;
	sscanf(data, "%d", &val);

	printk("[EC_HID] gDongleType_store : %d\n", val);

	switch(val){
		case 0:
			detect_dongle_type(0xBC);
		break;
		case 1:
			detect_dongle_type(0x50);
		break;
		case 2:
			detect_dongle_type(0x22);
		break;
		case 3:
			detect_dongle_type(0x33);
		break;
		case -1:
			detect_dongle_type(0x10);
		break;
/*
		case 7:
			printk("[EC_HID] re-send ITE_upgrade_mode\n");
			gDongleType = 7;
		break;
		case 9:
			printk("[EC_HID] re-send Station Low Battery\n");
			gDongleType = 9;
		break;
*/
		default:
			detect_dongle_type(0x0);
	}

	if (pogo_mutex_state){
		printk("[EC_HID] pogo_mutex_state : %d, skip send uevent.\n", pogo_mutex_state);
		return count;
	}

	down(&g_hid_data->pogo_sema);
	printk("[EC_HID] pogo_sema down!!! %d\n", val);
	pogo_mutex_state = 1;

	kobject_uevent(&g_hid_data->dev->kobj, KOBJ_CHANGE);
	g_hid_data->previous_event = gDongleEvent;
	return count;
}

static ssize_t gDongleEvent_show(struct device *dev,
					 struct device_attribute *mattr,
					 char *buf)
{
	printk("[EC_HID] gDongleEvent_show : %d\n", gDongleEvent);
	return sprintf(buf, "%d\n", gDongleEvent);
}

static ssize_t gDongleEvent_store(struct device *dev,
					  struct device_attribute *mattr,
					  const char *data, size_t count)
{
	int val;
	sscanf(data, "%d", &val);

	printk("[EC_HID] gDongleEvent_store : %d\n", val);
	gDongleEvent = val;

	return count;
}

/*
static ssize_t ec_hid_show(struct device *dev,
					 struct device_attribute *mattr,
					 char *buf)
{
	printk("[EC_HID] attr show\n");
	return sprintf(buf, "%s\n", TEST_STRING);
}

static ssize_t ec_hid_store(struct device *dev,
					  struct device_attribute *mattr,
					  const char *data, size_t count)
{
	printk("[EC_HID] attr store\n");
	return count;
}
*/

static ssize_t fw_ver_show(struct device *dev,
					 struct device_attribute *mattr,
					 char *buf)
{
	struct hid_device *hdev;
	unsigned char report_type;
	unsigned char report_number;
	int ret = 0;
	int i = 0;
	int count = 0, len = 0;
	char buffer[3] = {0};

	printk("[EC_HID] fw_ver_show\n");
	mutex_lock(&g_hid_data->report_mutex);
	hid_used = true;
	if (g_hidraw == NULL || g_hid_data->lock) {
		printk("[EC_HID] g_hidraw is NULL or lock %d\n", g_hid_data->lock);
		hid_used = false;
		mutex_unlock(&g_hid_data->report_mutex);
		return snprintf(buf, PAGE_SIZE,"HID_not_connect\n");
	}
	hdev = g_hidraw->hid;

	hid_hw_power(hdev, PM_HINT_FULLON);

// Set report
	report_number = 0x21;

	buffer[0] = 0x21; // Report ID
	buffer[1] = 0x44; // ASUSCmdID

	len = 2;

	report_type = HID_FEATURE_REPORT;
	ret = hid_hw_raw_request(hdev, report_number, buffer, len, report_type,
				HID_REQ_SET_REPORT);

	len= (ret < count) ? ret : count;
	printk("[EC_HID] Set report: len : %d\n", len);
	for( i=0 ; i< len  ; i++ ){
		printk("[EC_HID] 0x%02x\n", buffer[i]);
	}
// Get report
	report_number = 0x21;
	count = 3;
	len = 0;
	report_type = HID_FEATURE_REPORT;

	ret = hid_hw_raw_request(hdev, report_number, buffer, count, report_type,
				 HID_REQ_GET_REPORT);

	len= (ret < count) ? ret : count;
	printk("[EC_HID]Get report: len : %d\n", len);
	for( i=0 ; i< len  ; i++ ){
		printk("[EC_HID] 0x%02x\n", buffer[i]);
	}

	printk("[EC_HID] FW_VER : %c%c%c\n", buffer[0], buffer[1], buffer[2]);

	hid_hw_power(hdev, PM_HINT_NORMAL);
	hid_used = false;
	mutex_unlock(&g_hid_data->report_mutex);
	return snprintf(buf, PAGE_SIZE,"%c%c%c\n", buffer[0], buffer[1], buffer[2]);
}

static ssize_t pogo_det_show(struct device *dev,
					 struct device_attribute *mattr,
					 char *buf)
{
	int val = 0;

	printk("[EC_HID] pogo_det gpio[%d]\n", g_hid_data->pogo_det);

	if ( gpio_is_valid(g_hid_data->pogo_det) ) {
		val = gpio_get_value(g_hid_data->pogo_det);
		return snprintf(buf, PAGE_SIZE,"pogo_det[%d] :0x%x\n", g_hid_data->pogo_det, val);
	}else {
		return snprintf(buf, PAGE_SIZE,"No define power_gpio\n");
	}
}

static ssize_t pogo_det_store(struct device *dev,
					  struct device_attribute *mattr,
					  const char *data, size_t count)
{
	u32 val;
	ssize_t ret;

	printk("[EC_HID] pogo_det gpio[%d]\n", g_hid_data->pogo_det);

	ret = kstrtou32(data, 10, &val);
	if (ret)
		return ret;

	if(val>0) {
		printk("[EC_HID] pogo_det set HIGH\n");

		if ( gpio_is_valid(g_hid_data->pogo_det) ) {
			gpio_set_value(g_hid_data->pogo_det, 1);
		}
	}else {
		printk("[EC_HID] pogo_det set LOW\n");

		if ( gpio_is_valid(g_hid_data->pogo_det) ) {
			gpio_set_value(g_hid_data->pogo_det, 0);
		}
	}

	return count;
}

static ssize_t rpm_show(struct device *dev,
					 struct device_attribute *mattr,
					 char *buf)
{
	int RPM = 0;

	hid_to_get_rpm(&RPM);

	return snprintf(buf, PAGE_SIZE,"%d\n", RPM);
}

static ssize_t pwm_store(struct device *dev,
					  struct device_attribute *mattr,
					  const char *data, size_t count)
{
	int ret = 0;
	u32 val;

	ret = kstrtou32(data, 10, &val);
	if (ret)
		return ret;

	ret = hid_to_enbale_pwm(val);

	return count;
}

static ssize_t freq_store(struct device *dev,
					  struct device_attribute *mattr,
					  const char *data, size_t count)
{
	int HCPRS = 0, LCPRS = 0;

	sscanf(data, "%x %x", &HCPRS, &LCPRS);

	hid_to_set_freq(HCPRS, LCPRS);

	return count;
}

static ssize_t duty_store(struct device *dev,
					  struct device_attribute *mattr,
					  const char *data, size_t count)
{
	int FCTR = 0, FDCR = 0;

	sscanf(data, "%x %x", &FCTR, &FDCR);

	hid_to_set_duty(FCTR, FDCR);

	return count;
}

static ssize_t init_state_show(struct device *dev,
					 struct device_attribute *mattr,
					 char *buf)
{
	u8 state;

	state = hid_to_check_init_state();

	return snprintf(buf, PAGE_SIZE,"%x\n", state);
}

static ssize_t init_state_store(struct device *dev,
					  struct device_attribute *mattr,
					  const char *data, size_t count)
{
	int ret = 0;
	u32 val;

	ret = kstrtou32(data, 10, &val);
	if (ret)
		return ret;

	if (val > 0) {
		printk("[EC_HID] Send EC init cmd\n");
		hid_to_init_state();
	}else {
		printk("[EC_HID] No Send EC init cmd\n");
		return count;
	}

	return count;
}

static ssize_t enable_mipi_store(struct device *dev,
					  struct device_attribute *mattr,
					  const char *data, size_t count)
{
	int ret = 0;
	u32 val;

	ret = kstrtou32(data, 10, &val);
	if (ret)
		return ret;

	if (val > 0) {
		printk("[EC_HID] Send enable MIPI cmd\n");
		hid_to_enable_mipi((u8)val);
	}else {
		printk("[EC_HID] No send enable MIP cmd\n");
		return count;
	}

	return count;
}

static ssize_t uart_show(struct device *dev,
					 struct device_attribute *mattr,
					 char *buf)
{
	char buffer[16] = {0};
	int count = 2;
	int ret = 0, len = 0;
	u8 gpio = 0x28;

	ret = hid_to_gpio_get(gpio);

	ret = hid_get_gpio_data(buffer, count, &len);
	if (ret < 0)
		return sprintf(buf, "%s\n", "HID not connect");

	if(buffer[0])
		return snprintf(buf, PAGE_SIZE,"0x%x\n", 0);
	else
		return snprintf(buf, PAGE_SIZE,"0x%x\n", 1);
}

static ssize_t uart_store(struct device *dev,
					  struct device_attribute *mattr,
					  const char *data, size_t count)
{
	int ret = 0;
	u32 val;
	u8 gpio = 0x28;

	ret = kstrtou32(data, 10, &val);
	if (ret)
		return ret;

	if (val > 0) {
		printk("[EC_HID] Enable EC Uart.\n");
		ret = hid_to_gpio_set(gpio, 0);
	}else {
		printk("[EC_HID] Disable EC Uart\n");
		ret = hid_to_gpio_set(gpio, 1);
	}

	return count;
}

static ssize_t sync_state_store(struct device *dev,
					  struct device_attribute *mattr,
					  const char *data, size_t count)
{
	int ret = 0;
	u32 val;

	ret = kstrtou32(data, 10, &val);
	if (ret)
		return ret;

	if (val == 0){
		ASUSEvtlog("[EC_HID] asus_extcon_set_state_sync : %d\n", val);
		printk("[EC_HID][EXTCON] extcon_dongle->state : %d, val : %d\n", extcon_dongle->state, val);
		asus_extcon_set_state_sync(extcon_dongle, val);

		pogo_mutex_state = 0;
		//printk("[EC_HID] pogo_mutex unlock!!!\n");
		//mutex_unlock(&g_hid_data->pogo_mutex);
		printk("[EC_HID] pogo_sema up!!! %d\n", val);
		up(&g_hid_data->pogo_sema);
	}else if ((val > 0 && val <= 4) || val == 7 || (val >= 12 && val <= 15)){
		ASUSEvtlog("[EC_HID] asus_extcon_set_state_sync : %d\n", val+5);
		printk("[EC_HID][EXTCON] extcon_dongle->state : %d, val : %d\n", extcon_dongle->state, (val+5));
		asus_extcon_set_state_sync(extcon_dongle, (val+5));

		pogo_mutex_state = 0;
//		printk("[EC_HID] pogo_mutex unlock!!!\n");
//		mutex_unlock(&g_hid_data->pogo_mutex);
		printk("[EC_HID] pogo_sema up!!! %d\n", val);
		up(&g_hid_data->pogo_sema);
	}
	else
		printk("[EC_HID] Do not sync state!!! %d\n", val);

	return count;
}

static ssize_t charger_type_show(struct device *dev,
					 struct device_attribute *mattr,
					 char *buf)
{
	int ret = 0;
	int type;
	short vol, cur;

	ret = hid_to_get_charger_type(&type, &vol, &cur);
	if (ret < 0)
		return sprintf(buf, "%s\n", "HID not connect");

	return snprintf(buf, PAGE_SIZE,"%d\n", type);
}

static ssize_t charger_type_store(struct device *dev,
					  struct device_attribute *mattr,
					  const char *data, size_t count)
{
	int ret = 0;
	u32 val;

	printk("[EC_HID] charger_type_store\n");

	ret = kstrtou32(data, 10, &val);
	if (ret)
		return ret;

	if (val > 0) {
		ret = hid_to_set_charger_type((u8)val);
		if (ret <0)
			printk("[EC_HID] ret : %d\n", ret);
	}

	return count;
}

static ssize_t battery_cap_show(struct device *dev,
					 struct device_attribute *mattr,
					 char *buf)
{
	int ret = 0;
	int cap;

	ret = hid_to_get_battery_cap(&cap);
	if (ret < 0)
		return sprintf(buf, "%s\n", "HID not connect");

	return snprintf(buf, PAGE_SIZE,"%d\n", cap);
}

static ssize_t battery_vol_show(struct device *dev,
					 struct device_attribute *mattr,
					 char *buf)
{
	int ret = 0;
	int vol;

	ret = hid_to_get_battery_vol(&vol);
	if (ret < 0)
		return sprintf(buf, "%s\n", "HID not connect");

	return snprintf(buf, PAGE_SIZE,"%d\n", vol);
}

static ssize_t battery_cur_show(struct device *dev,
					 struct device_attribute *mattr,
					 char *buf)
{
	int ret = 0;
	short cur;

	ret = hid_to_get_battery_cur(&cur);
	if (ret < 0)
		return sprintf(buf, "%s\n", "HID not connect");

	return snprintf(buf, PAGE_SIZE,"%d\n", cur);
}

static ssize_t thermal_alert_show(struct device *dev,
					 struct device_attribute *mattr,
					 char *buf)
{
	int ret = 0;
	int state;

	ret = hid_to_get_thermal_alert(&state);
	if (ret < 0)
		return sprintf(buf, "%s\n", "HID not connect");

	return snprintf(buf, PAGE_SIZE,"%d\n", state);
}

static ssize_t lock_show(struct device *dev,
					 struct device_attribute *mattr,
					 char *buf)
{
	printk("[EC_HID] lock_show : %d\n", g_hid_data->lock);

	return snprintf(buf, PAGE_SIZE,"%d\n", g_hid_data->lock);
}

static ssize_t lock_store(struct device *dev,
					  struct device_attribute *mattr,
					  const char *data, size_t count)
{
	int ret = 0;
	u32 val;

	ret = kstrtou32(data, 10, &val);
	if (ret)
		return ret;

	if (val > 0) {
		printk("[EC_HID] Lock\n");
		g_hid_data->lock = true;
	}else {
		printk("[EC_HID] Unlock\n");
		g_hid_data->lock = false;
	}

	return count;
}

static ssize_t u0504_show(struct device *dev,
					 struct device_attribute *mattr,
					 char *buf)
{
	int ret = 0;
	int state;

	ret = hid_to_get_u0504_state(&state);
	if (ret < 0)
		return sprintf(buf, "%s\n", "HID not connect");

	return snprintf(buf, PAGE_SIZE,"%d\n", state);
}

static ssize_t factory_mode_store(struct device *dev,
					  struct device_attribute *mattr,
					  const char *data, size_t count)
{
	int ret = 0;
	u32 val;

	ret = kstrtou32(data, 10, &val);
	if (ret)
		return ret;

	if (val > 0) {
		ret = hid_to_set_factory_mode(1);
		if (ret <0)
			printk("[EC_HID] ret : %d\n", ret);
	}else {
		ret = hid_to_set_factory_mode(0);
		if (ret <0)
			printk("[EC_HID] ret : %d\n", ret);
	}

	return count;
}

static ssize_t factory_mode_show(struct device *dev,
					 struct device_attribute *mattr,
					 char *buf)
{
	int ret = 0;
	u8 data;

	ret = hid_to_get_factory_mode(&data);
	if (ret < 0)
		return sprintf(buf, "%s\n", "HID not connect");

	return snprintf(buf, PAGE_SIZE,"%d\n", data);
}

static ssize_t PDO_voltage_show(struct device *dev,
					 struct device_attribute *mattr,
					 char *buf)
{
	int ret = 0;
	int type;
	short vol, cur;

	ret = hid_to_get_charger_type(&type, &vol, &cur);
	if (ret < 0)
		return sprintf(buf, "%s\n", "HID not connect");

	return snprintf(buf, PAGE_SIZE,"%d\n", vol);
}

static ssize_t PDO_current_show(struct device *dev,
					 struct device_attribute *mattr,
					 char *buf)
{
	int ret = 0;
	int type;
	short vol, cur;

	ret = hid_to_get_charger_type(&type, &vol, &cur);
	if (ret < 0)
		return sprintf(buf, "%s\n", "HID not connect");

	return snprintf(buf, PAGE_SIZE,"%d\n", cur);
}

static ssize_t sd_power_store(struct device *dev,
					  struct device_attribute *mattr,
					  const char *data, size_t count)
{
	int ret = 0;
	u32 val;
	u8 gpio = 0x1;

	ret = kstrtou32(data, 10, &val);
	if (ret)
		return ret;

	if (val > 0) {
		printk("[EC_HID] Enable SD power.\n");
		ret = hid_to_gpio_set(gpio, 0);
	}else {
		printk("[EC_HID] Disable SD power\n");
		ret = hid_to_gpio_set(gpio, 1);
	}

	return count;
}

static ssize_t sd_power_show(struct device *dev,
					 struct device_attribute *mattr,
					 char *buf)
{
	char buffer[16] = {0};
	int count = 2;
	int ret = 0, len = 0;
	u8 gpio = 0x1;

	ret = hid_to_gpio_get(gpio);

	ret = hid_get_gpio_data(buffer, count, &len);
	if (ret < 0)
		return sprintf(buf, "%s\n", "HID not connect");

	if(buffer[0])
		return snprintf(buf, PAGE_SIZE,"0x%x\n", 0);
	else
		return snprintf(buf, PAGE_SIZE,"0x%x\n", 1);
}

static ssize_t INT_check_show(struct device *dev,
					 struct device_attribute *mattr,
					 char *buf)
{
	int ret = 0;
	u8 type, event;

	ret = hid_to_check_interrupt(&type, &event);
	if (ret < 0)
		return sprintf(buf, "%s\n", "HID not connect");

	return snprintf(buf, PAGE_SIZE,"type:%d, event:%d\n", type, event);
}

static ssize_t notufy_shutdown_store(struct device *dev,
					  struct device_attribute *mattr,
					  const char *data, size_t count)
{
	int ret = 0;
	u32 val;

	ret = kstrtou32(data, 10, &val);
	if (ret)
		return ret;

	if (val > 0) {
		ret = hid_to_notify_shutdown(0);
		if (ret <0)
			printk("[EC_HID] ret : %d\n", ret);
	}else {
		ret = hid_to_notify_shutdown(1);
		if (ret <0)
			printk("[EC_HID] ret : %d\n", ret);
	}

	return count;
}

static ssize_t register_member_show(struct device *dev,
					 struct device_attribute *mattr,
					 char *buf)
{
	int len=0;

	printk("[EC_HID] register_member_show\n");

	for(len=0; len < MAX_MEMBERS ; len ++){
		if(hid_member[len].name != NULL)
			printk("[EC_HID] [%d] %d, %s, %d\n", len, hid_member[len].id, hid_member[len].name, hid_member[len].vote);
		else
			printk("[EC_HID] [%d] %d, NULL, %d\n", len, hid_member[len].id, hid_member[len].vote);
	}

	return snprintf(buf, PAGE_SIZE,"done\n");
}

static ssize_t pogo_mutex_store(struct device *dev,
					  struct device_attribute *mattr,
					  const char *data, size_t count)
{
	int ret = 0;
	u32 val;

	ret = kstrtou32(data, 10, &val);
	if (ret)
		return ret;

	if (val > 0) {
//		if (pogo_mutex_state) {
		if(0) {
			printk("[EC_HID] pogo_mutex is already lock\n");
		} else {
//			mutex_lock(&g_hid_data->pogo_mutex);
//			printk("[EC_HID] force mutex_lock pogo_mutex\n");
			down(&g_hid_data->pogo_sema);
			printk("[EC_HID] pogo_sema down!!!\n");

			pogo_mutex_state = 1;
		}
	}else {
		//if (pogo_mutex_state) {
		if(1) {
			pogo_mutex_state = 0;
			//printk("[EC_HID] force mutex_unlock pogo_mutex\n");
			//mutex_unlock(&g_hid_data->pogo_mutex);
			printk("[EC_HID] pogo_sema up!!!\n");
			up(&g_hid_data->pogo_sema);
		} else {
			printk("[EC_HID] pogo_mutex is already unlock\n");
		}
	}

	return count;
}

static ssize_t pogo_mutex_show(struct device *dev,
					 struct device_attribute *mattr,
					 char *buf)
{
	printk("[EC_HID] pogo_mutex_show : %d\n", pogo_mutex_state);

	return snprintf(buf, PAGE_SIZE,"%d\n", pogo_mutex_state);
}

static ssize_t dp_fw_ver_show(struct device *dev,
					 struct device_attribute *mattr,
					 char *buf)
{
	int ret = 0;
	u8 fw_ver[2] = {0};

	printk("[EC_HID] dp_fw_ver_show\n");

	ret = hid_to_get_dp_fw(fw_ver);
	if (ret < 0)
		return sprintf(buf, "%s\n", "HID_not_connect");

	return snprintf(buf, PAGE_SIZE,"0x%02x%02x\n", fw_ver[0], fw_ver[1]);
}

static ssize_t hwid_show(struct device *dev,
					 struct device_attribute *mattr,
					 char *buf)
{
	struct hid_device *hdev;
	unsigned char report_type;
	unsigned char report_number;
	int ret = 0;
	//int i = 0;
	int count = 0, len = 0;
	char buffer[3] = {0};

	printk("[EC_HID] hwid_show\n");
	mutex_lock(&g_hid_data->report_mutex);
	hid_used = true;
	if (g_hidraw == NULL || g_hid_data->lock) {
		printk("[EC_HID] g_hidraw is NULL or lock %d\n", g_hid_data->lock);
		hid_used = false;
		mutex_unlock(&g_hid_data->report_mutex);
		return snprintf(buf, PAGE_SIZE,"HID_not_connect\n");
	}
	hdev = g_hidraw->hid;

	hid_hw_power(hdev, PM_HINT_FULLON);

// Set report
	report_number = 0x21;

	buffer[0] = 0x21; // Report ID
	buffer[1] = 0x9C; // ASUSCmdID

	len = 2;

	report_type = HID_FEATURE_REPORT;
	ret = hid_hw_raw_request(hdev, report_number, buffer, len, report_type,
				HID_REQ_SET_REPORT);

// Get report
	report_number = 0x21;
	count = 2;
	len = 0;
	report_type = HID_FEATURE_REPORT;

	ret = hid_hw_raw_request(hdev, report_number, buffer, count, report_type,
				 HID_REQ_GET_REPORT);

	len= (ret < count) ? ret : count;
	//printk("[EC_HID] len : %d\n", len);
	//for( i=0 ; i< len  ; i++ ){
	//	printk("[EC_HID] 0x%02x\n", buffer[i]);
	//}

	printk("[EC_HID] HWID : 0x%x\n", buffer[0]);

	hid_hw_power(hdev, PM_HINT_NORMAL);
	hid_used = false;
	mutex_unlock(&g_hid_data->report_mutex);
	return snprintf(buf, PAGE_SIZE,"0x%x\n", buffer[0]);
}

static ssize_t keyboard_enable_show(struct device *dev,
					 struct device_attribute *mattr,
					 char *buf)
{
	return snprintf(buf, PAGE_SIZE,"0x%x\n", hid_key_enable);
}

static ssize_t keyboard_enable_store(struct device *dev,
					  struct device_attribute *mattr,
					  const char *data, size_t count)
{
	int ret = 0;
	u32 val;

	ret = kstrtou32(data, 10, &val);
	if (ret)
		return ret;

	printk("[EC_HID] keyboard_enable 0x%x\n", val);
	if (val > 0) {
		hid_key_enable = true;
	}else {
		hid_key_enable = false;
	}

	return count;
}

static ssize_t wake_lock_show(struct device *dev,
					 struct device_attribute *mattr,
					 char *buf)
{
	return snprintf(buf, PAGE_SIZE,"0x%x\n", (g_hid_data->wake_flag == true) ? 1 : 0);
}

static ssize_t wake_lock_store(struct device *dev,
					  struct device_attribute *mattr,
					  const char *data, size_t count)
{
	int ret = 0;
	u32 val;

	ret = kstrtou32(data, 10, &val);
	if (ret)
		return ret;

	//printk("[EC_HID] wake_lock_store 0x%x\n", val);
	if (val > 0) {
		if (!g_hid_data->wake_flag){
			wake_lock(&g_hid_data->ec_wake_lock);
			g_hid_data->wake_flag = true;
			//printk("[EC_HID] Set wake lock!!\n");
		}else {
			//printk("[EC_HID] Already set wake lock!!\n");
		}
	}else {
		if (g_hid_data->wake_flag){
			wake_unlock(&g_hid_data->ec_wake_lock);
			g_hid_data->wake_flag = false;
			//printk("[EC_HID] Un-set wake lock!!\n");
		}else {
			//printk("[EC_HID] Already un-set wake lock!!\n");
		}
	}

	return count;
}

static DEVICE_ATTR(pogo_det, S_IRUGO | S_IWUSR, pogo_det_show, pogo_det_store);
static DEVICE_ATTR(i2c_write, S_IRUGO | S_IWUSR, NULL, i2c_write_store);
static DEVICE_ATTR(i2c_read, S_IRUGO | S_IWUSR, i2c_read_show, i2c_read_store);
static DEVICE_ATTR(set_gpio, S_IRUGO | S_IWUSR, NULL, set_gpio_store);
static DEVICE_ATTR(get_gpio, S_IRUGO | S_IWUSR, get_gpio_show, get_gpio_store);
static DEVICE_ATTR(gDongleType, S_IRUGO | S_IWUSR, gDongleType_show, gDongleType_store);
static DEVICE_ATTR(gDongleEvent, S_IRUGO | S_IWUSR, gDongleEvent_show, gDongleEvent_store);
static DEVICE_ATTR(fw_ver, S_IRUGO | S_IWUSR, fw_ver_show, NULL);
static DEVICE_ATTR(pwm, S_IRUGO | S_IWUSR, NULL, pwm_store);
static DEVICE_ATTR(rpm, S_IRUGO | S_IWUSR, rpm_show, NULL);
static DEVICE_ATTR(freq, S_IRUGO | S_IWUSR, NULL, freq_store);
static DEVICE_ATTR(duty, S_IRUGO | S_IWUSR, NULL, duty_store);
static DEVICE_ATTR(init_state, S_IRUGO | S_IWUSR, init_state_show, init_state_store);
static DEVICE_ATTR(enable_mipi, S_IRUGO | S_IWUSR, NULL, enable_mipi_store);
static DEVICE_ATTR(uart, S_IRUGO | S_IWUSR, uart_show, uart_store);
static DEVICE_ATTR(sync_state, S_IRUGO | S_IWUSR, NULL, sync_state_store);
static DEVICE_ATTR(charger_type, S_IRUGO | S_IWUSR, charger_type_show, charger_type_store);
static DEVICE_ATTR(battery_cap, S_IRUGO | S_IWUSR, battery_cap_show, NULL);
static DEVICE_ATTR(battery_vol, S_IRUGO | S_IWUSR, battery_vol_show, NULL);
static DEVICE_ATTR(battery_cur, S_IRUGO | S_IWUSR, battery_cur_show, NULL);
static DEVICE_ATTR(thermal_alert, S_IRUGO | S_IWUSR, thermal_alert_show, NULL);
static DEVICE_ATTR(lock, S_IRUGO | S_IWUSR, lock_show, lock_store);
static DEVICE_ATTR(u0504, S_IRUGO | S_IWUSR, u0504_show, NULL);
static DEVICE_ATTR(factory_mode, S_IRUGO | S_IWUSR, factory_mode_show, factory_mode_store);
static DEVICE_ATTR(StationPDO_voltage_max, S_IRUGO | S_IWUSR, PDO_voltage_show, NULL);
static DEVICE_ATTR(StationPDO_current_max, S_IRUGO | S_IWUSR, PDO_current_show, NULL);
static DEVICE_ATTR(sd_power, S_IRUGO | S_IWUSR, sd_power_show, sd_power_store);
static DEVICE_ATTR(INT_check, S_IRUGO | S_IWUSR, INT_check_show, NULL);
static DEVICE_ATTR(shutdown, S_IRUGO | S_IWUSR, NULL, notufy_shutdown_store);
static DEVICE_ATTR(members, S_IRUGO | S_IWUSR, register_member_show, NULL);
static DEVICE_ATTR(pogo_mutex, S_IRUGO | S_IWUSR, pogo_mutex_show, pogo_mutex_store);
static DEVICE_ATTR(DP_FW, S_IRUGO | S_IWUSR, dp_fw_ver_show, NULL);
static DEVICE_ATTR(HWID, S_IRUGO | S_IWUSR, hwid_show, NULL);
static DEVICE_ATTR(keyboard_enable, S_IRUGO | S_IWUSR, keyboard_enable_show, keyboard_enable_store);
//static DEVICE_ATTR(test_node, S_IRUGO | S_IWUSR, ec_hid_show, ec_hid_store);
static DEVICE_ATTR(wake_lock, S_IRUGO | S_IWUSR, wake_lock_show, wake_lock_store);

static struct attribute *ec_hid_attrs[] = {
	&dev_attr_i2c_write.attr,
	&dev_attr_i2c_read.attr,
	&dev_attr_set_gpio.attr,
	&dev_attr_get_gpio.attr,
	&dev_attr_pogo_det.attr,
	&dev_attr_gDongleType.attr,
	&dev_attr_gDongleEvent.attr,
	&dev_attr_fw_ver.attr,
	&dev_attr_pwm.attr,
	&dev_attr_rpm.attr,
	&dev_attr_freq.attr,
	&dev_attr_duty.attr,
	&dev_attr_init_state.attr,
	&dev_attr_enable_mipi.attr,
	&dev_attr_uart.attr,
	&dev_attr_sync_state.attr,
	&dev_attr_charger_type.attr,
	&dev_attr_battery_cap.attr,
	&dev_attr_battery_vol.attr,
	&dev_attr_battery_cur.attr,
	&dev_attr_thermal_alert.attr,
	&dev_attr_lock.attr,
	&dev_attr_u0504.attr,
	&dev_attr_factory_mode.attr,
	&dev_attr_StationPDO_voltage_max.attr,
	&dev_attr_StationPDO_current_max.attr,
	&dev_attr_sd_power.attr,
	&dev_attr_INT_check.attr,
	&dev_attr_shutdown.attr,
	&dev_attr_members.attr,
	&dev_attr_pogo_mutex.attr,
	&dev_attr_DP_FW.attr,
	&dev_attr_HWID.attr,
	&dev_attr_keyboard_enable.attr,
	//&dev_attr_test_node.attr,
	&dev_attr_wake_lock.attr,
	NULL
};

const struct attribute_group ec_hid_group = {
	.attrs = ec_hid_attrs,
};

static void ec_anx_reset_work(struct work_struct *work)
{
	if(!suspend_by_hall){
		printk("[EC_HID] ec_anx_reset_work.\n");
		asus_dp_reset();
	}else
		printk("[EC_HID] Skip ec_anx_reset_work because hall sensor.\n");
}

// Register FB notifier +++
static int ec_hid_fb_callback(struct notifier_block *nb, unsigned long val, void *data)
{
	//struct hid_device *hdev;
	//struct usb_interface *intf;
	struct ec_hid_data *ec_hid_device;
	struct msm_drm_notifier *evdata = data;
	unsigned int blank;

	if (val != MSM_DRM_EARLY_EVENT_BLANK)
		return 0;

	if (evdata->id != 0)	// id=0 is internal display, external is 1
		return 0;

	if (gDongleType == 2){
		asus_wait4hid();
	}

	if (g_hidraw == NULL) {
		printk("[EC_HID] g_hidraw is NULL, bypass ec_hid_fb_callback\n");
		return 0;
	}

	printk("[EC_HID] go to the ec_hid_fb_callback value = %d msm_drm_display_id = %d\n", (int)val, evdata->id);
	//hdev = g_hidraw->hid;
	//intf = to_usb_interface(hdev->dev.parent);


	ec_hid_device = container_of(nb, struct ec_hid_data, notifier);
	if (evdata && evdata->data && val == MSM_DRM_EARLY_EVENT_BLANK && ec_hid_device) {
		blank = *(int *)(evdata->data);

	printk("[EC_HID]  go to the blank value = %d\n", (int)blank);

	switch (blank) {
		case MSM_DRM_BLANK_POWERDOWN:
			printk("[EC_HID] MSM_DRM_BLANK_POWERDOWN : usb_enable_autosuspend\n");
			//usb_enable_autosuspend(interface_to_usbdev(intf));
			hid_switch_usb_autosuspend(true);
			printk("[EC_HID] keyboard_enable false\n");
			hid_key_enable = false;
			dwc3_station_uevent(2);
			break;
		case MSM_DRM_BLANK_UNBLANK:
			printk("[EC_HID] MSM_DRM_BLANK_UNBLANK : usb_disable_autosuspend\n");
			//usb_disable_autosuspend(interface_to_usbdev(intf));
			hid_switch_usb_autosuspend(false);
			hid_reset_vote(hid_member);
			printk("[EC_HID] keyboard_enable true\n");
			hid_key_enable = true;
			dwc3_station_uevent(1);
			break;
		default:
			break;
		}
	}

	return NOTIFY_OK;
}

static struct notifier_block ec_hid_noti_block = {
	.notifier_call = ec_hid_fb_callback,
};
// Register FB notifier ---

static int ec_hid_probe(struct platform_device *pdev)
{
	int status = 0;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct ec_hid_data *ec_hid_device;
	int retval;
	
	printk("[EC_HID] ec_hid_probe.\n");
	ec_hid_device = kzalloc(sizeof(*ec_hid_device), GFP_KERNEL);
	if (ec_hid_device == NULL) {
		printk("[EC_HID] alloc EC_HID data fail.\r\n");
		goto kmalloc_failed;
	}

	ec_hid_device->previous_dongle = 0;

	ec_hid_class = class_create(THIS_MODULE, CLASS_NAME);
	if (IS_ERR(ec_hid_class)) {
		printk("[EC_HID] ec_hid_probe: class_create() is failed - unregister chrdev.\n");
		goto class_create_failed;
	}

	dev = device_create(ec_hid_class, &pdev->dev,
			    ec_hid_device->devt, ec_hid_device, "dongle");
	status = IS_ERR(dev) ? PTR_ERR(dev) : 0;
	printk("[EC_HID] ec_hid_probe: device_create() status %d\n", status);

	status = sysfs_create_group(&pdev->dev.kobj, &ec_hid_group);
	printk("[EC_HID] ec_hid_probe: sysfs_create_group() status %d\n", status);

	ec_hid_device->pogo_sleep = of_get_named_gpio_flags(np, "dongle,pogo-sleep", 0, NULL);
	printk("[EC_HID] pogo_sleep : %d\n", ec_hid_device->pogo_sleep);

	ec_hid_device->pogo_det = of_get_named_gpio_flags(np, "dongle,pogo-det", 0, NULL);
	printk("[EC_HID] pogo_det : %d\n", ec_hid_device->pogo_det);

	// Get the pinctrl node
	ec_hid_device->pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR_OR_NULL(ec_hid_device->pinctrl)) {
	     dev_err(&pdev->dev, "%s: Failed to get pinctrl\n", __func__);
	     goto skip_pinctrl;
	}

	// Get the active setting
	printk("[EC_HID] Get pogo_sleep state\n");
	ec_hid_device->pins_active = pinctrl_lookup_state(ec_hid_device->pinctrl, "pogo_sleep");
	if (IS_ERR_OR_NULL(ec_hid_device->pins_active)) {
		dev_err(&pdev->dev, "%s: Failed to get pinctrl state active\n", __func__);
	}

	// Set the active setting
	printk("[EC_HID] set the active state\n");
	retval = pinctrl_select_state(ec_hid_device->pinctrl, ec_hid_device->pins_active);
	if (retval)
		dev_err(&pdev->dev, "%s: pinctrl_select_state retval:%d\n", __func__, retval);

	// Set POGO_SLEEP
	if ( gpio_is_valid(ec_hid_device->pogo_sleep) ) {
		printk("[EC_HID] Request pogo_sleep config.\n");
		retval = gpio_request(ec_hid_device->pogo_sleep, "pogo_sleep");
		if (retval)
			printk("[EC_HID] pogo_sleep gpio_request, err %d\n", retval);

		printk("[EC_HID] pogo_sleep default on.\n");
		retval = gpio_direction_output(ec_hid_device->pogo_sleep, 0);
		if (retval)
			printk("[EC_HID] pogo_sleep output low, err %d\n", retval);

		gpio_set_value(ec_hid_device->pogo_sleep, 1);
	}

	// Set POGO_DET
	if ( gpio_is_valid(ec_hid_device->pogo_det) ) {
		printk("[EC_HID] Request pogo_det config.\n");
		retval = gpio_request(ec_hid_device->pogo_det, "pogo_det");
		if (retval)
			printk("[EC_HID] pogo_det gpio_request, err %d\n", retval);

		printk("[EC_HID] pogo_det default on.\n");
		retval = gpio_direction_output(ec_hid_device->pogo_det, 1);
		if (retval)
			printk("[EC_HID] pogo_det output high, err %d\n", retval);

		gpio_set_value(ec_hid_device->pogo_det, 1);
	}

skip_pinctrl:

	ec_hid_device->workqueue = alloc_ordered_workqueue("ec_workqueue", 0);
	if (!ec_hid_device->workqueue) {
		retval = -ENOMEM;
		goto workqueue_create_failed;
	}
	INIT_WORK(&ec_hid_device->anx_work, ec_anx_reset_work);

// Init wake lock
	wake_lock_init(&ec_hid_device->ec_wake_lock, WAKE_LOCK_SUSPEND, "ec_wake_lock");
	ec_hid_device->wake_flag = false;

	ec_hid_device->notifier = ec_hid_noti_block;
	msm_drm_register_client(&ec_hid_device->notifier);

	mutex_init(&ec_hid_device->report_mutex);
	//mutex_init(&ec_hid_device->pogo_mutex);
    sema_init(&ec_hid_device->pogo_sema, 1);

	ec_hid_device->lock = false;
	ec_hid_device->dev = &pdev->dev;
	g_hid_data = ec_hid_device;

	init_completion(&hid_state);
	hid_init_vote(hid_member);

	hid_key_enable = false;

	if(g_adc){
		printk("[EC_HID] detcet g_adc, set dongle type\n");
		detect_dongle_type(g_adc);
		g_adc = 0;
	}
	return 0;

workqueue_create_failed:
	destroy_workqueue(ec_hid_device->workqueue);
class_create_failed:
kmalloc_failed:
	return -1;
}

static int ec_hid_remove(struct platform_device *pdev)
{
	printk("[EC_HID] ec_hid_remove.\n");
	destroy_workqueue(g_hid_data->workqueue);
	return 0;
}

int ec_hid_suspend(struct device *dev)
{
	int err = 0;

	//printk("[EC_HID] ec_hid_suspend. gDongleType : %d\n", gDongleType);
	//if (gDongleType == 2) {
	//	gpio_set_value(g_hid_data->pogo_sleep, 0);	// Notify station EC, phone is suspend.
	//	printk("[EC_HID] dongle sleep : 0x%x\n", gpio_get_value(g_hid_data->pogo_sleep));
	//}
	return err;
}

int ec_hid_resume(struct device *dev)
{
	int err = 0;

	//printk("[EC_HID] ec_hid_resume. gDongleType : %d\n", gDongleType);
	//if (gDongleType == 2) {
	//	gpio_set_value(g_hid_data->pogo_sleep, 1);// Notify station EC, phone is resume
	//	printk("[EC_HID] dongle sleep : 0x%x\n", gpio_get_value(g_hid_data->pogo_sleep));
	//}
	return err;
}

static const struct dev_pm_ops ec_hid_pm_ops = {
	.suspend	= ec_hid_suspend,
	.resume	= ec_hid_resume,
};

static struct of_device_id dongle_match_table[] = {
	{ .compatible = "asus,ec_hid",},
	{ },
};

static struct platform_driver dongle_hid_driver = {
	.driver = {
		.name = "ec_hid",
		.owner = THIS_MODULE,
		.pm	= &ec_hid_pm_ops,
		.of_match_table = dongle_match_table,
	},
	.probe         	= ec_hid_probe,
	.remove			= ec_hid_remove,
};

static int __init dongle_hid_init(void)
{
	int ret;

	ret = platform_driver_register(&dongle_hid_driver);
	if (ret != 0) {
		printk("[EC_HID] dongle_hid_init fail, Error : %d\n", ret);
	}
	
	return ret;
}
module_init(dongle_hid_init);

static void __exit dongle_hid_exit(void)
{
	platform_driver_unregister(&dongle_hid_driver);
}
module_exit(dongle_hid_exit);

MODULE_AUTHOR("ASUS Deeo Ho");
MODULE_DESCRIPTION("JEDI dongle EC HID driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("asus:ec_hid");
