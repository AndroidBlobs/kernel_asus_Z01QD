#include <linux/fcntl.h> 
//#include <stdio.h>
//#include <stdlib.h>
#include <linux/unistd.h>
#include <linux/device.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/input.h>
#include <linux/hidraw.h>
#include <linux/string.h>
#include <linux/syscalls.h>
#include <linux/pinctrl/consumer.h>
#include <linux/mutex.h>
#include <linux/hid.h>
#include <linux/semaphore.h>
#include <linux/wakelock.h>

//Include extcon register
#include <linux/extcon.h>

//For HID wait for completion
#include <linux/completion.h>

#define	CLASS_NAME		    "ec_hid"
#define	TEST_STRING		    "JEDI_DONGLE"
//#define HID_PATCH			"/dev/hidraw0"

extern struct hidraw *g_hidraw;
static struct class *ec_hid_class;

struct ec_hid_data *g_hid_data;
EXPORT_SYMBOL(g_hid_data);

extern void asus_station_enable_host(int);
extern bool Stataion_sd_transfer;
extern int asp1690e_read_reg(uint8_t cmd_reg, uint8_t *store_read_val);
extern void asus_dp_reset(void);

extern bool ITE_upgrade_mode;
extern bool EC_interface_probe;

extern bool YODA_station;

// Block HID input report
bool hid_key_enable = false;
EXPORT_SYMBOL(hid_key_enable);

// Record HID used status
bool hid_used = false;
EXPORT_SYMBOL(hid_used);

// Register Hall sensor
bool suspend_by_hall = false;
EXPORT_SYMBOL(suspend_by_hall);

//  Station Low Battery mode
bool station_low_battery = false;
EXPORT_SYMBOL(station_low_battery);

//  Station station_shutdown mode
bool station_shutdown = false;
EXPORT_SYMBOL(station_shutdown);

#define MAX_MEMBERS 10
struct vote_member {
	int id;
	char *name;
	bool vote;
};

struct ec_hid_data {
	dev_t devt;
	struct device *dev;
	//struct list_head device_entry;

	//uint8_t current_dongle;
	uint8_t previous_dongle;
	uint8_t previous_event;

	u8 fw_version;

	//Simulated POGO_DET
	int pogo_det;
	int pogo_sleep;
	struct pinctrl *pinctrl;
	struct pinctrl_state *pins_active;

	bool lock;
	struct mutex report_mutex;
	struct mutex pogo_mutex;
	struct semaphore pogo_sema;

	// Register framebuffer notify
	struct notifier_block notifier;

	//struct delayed_work		dwork;
	struct workqueue_struct		*workqueue;
	struct work_struct		anx_work;

	// wake lock
	struct wake_lock	ec_wake_lock;
	bool wake_flag;
};

void hid_switch_usb_autosuspend(bool flag){
	struct hid_device *hdev;
	struct usb_interface *intf;

	if (g_hidraw == NULL || g_hid_data->lock) {
		printk("[EC_HID] g_hidraw is NULL or lock %d\n", g_hid_data->lock);
		return;
	}

	hdev = g_hidraw->hid;
	intf = to_usb_interface(hdev->dev.parent);

	printk("[EC_HID] hid_swithc_usb_autosuspend %d\n", flag);
	if(flag) {
		usb_enable_autosuspend(interface_to_usbdev(intf));
	}else {
		usb_disable_autosuspend(interface_to_usbdev(intf));
	}

	return;
}
EXPORT_SYMBOL_GPL(hid_switch_usb_autosuspend);

// For suspend vote
struct vote_member hid_member[MAX_MEMBERS];

#if 0
int hid_check_vote(struct vote_member *hid_member){
	int len=0, result=1;

	printk("[EC_HID] hid_check_vote\n");
	for(len=0 ; len<MAX_MEMBERS ; len++)
	{
		if (hid_member[len].id == -1){
			continue;
		}

		if (hid_member[len].vote == false){
			printk("[EC_HID] Find vote false!!!!\n");
			printk("[EC_HID] ID : %d, NAME : %s, VOTE : %d\n", hid_member[len].id, hid_member[len].name, hid_member[len].vote);
			result = 0;
			return result;
		}
	}
	printk("[EC_HID] Every one votes true!!\n");
	return result;
}

int hid_vote_register(char *name){
	int len;

    mutex_lock(&g_hid_data->report_mutex);
	printk("[EC_HID] hid_vote_register : %s\n", name);

	for(len=0 ; len<MAX_MEMBERS ; len++ )
	{
		if(hid_member[len].id == -1){
			hid_member[len].id = len;
			hid_member[len].name = name;
			hid_member[len].vote = false;
			printk("[EC_HID] ID : %d, NAME : %s, VOTE : %d\n", hid_member[len].id, hid_member[len].name, hid_member[len].vote);
			break;
		}

		if(len == 9){
			printk("[EC_HID] register fail...\n");
			mutex_unlock(&g_hid_data->report_mutex);
			return -1;
		}
	}

    mutex_unlock(&g_hid_data->report_mutex);
	return hid_member[len].id;
}
EXPORT_SYMBOL_GPL(hid_vote_register);

int hid_vote_unregister(int id, char *name){
    mutex_lock(&g_hid_data->report_mutex);
	printk("[EC_HID] hid_vote_unregister : %d : %s\n", id, name);

	if(hid_member[id].id == id && hid_member[id].name != NULL){
		if (!strcmp(hid_member[id].name, name)){
			hid_member[id].id = -1;
			hid_member[id].name = NULL;
			hid_member[id].vote = false;
			printk("[EC_HID] Clear ID : %d\n", id);
		} else
			printk("[EC_HID] Name mismatch. %s\n", hid_member[id].name);
	} else
		printk("[EC_HID] Id or Name mismatch. %d\n", hid_member[id].id);

    mutex_unlock(&g_hid_data->report_mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(hid_vote_unregister);

int hid_suspend_vote(int id){
	int result=0;

	if(id<0){
		printk("[EC_HID] error ID %d\n", id);
		return -1;
	}

    mutex_lock(&g_hid_data->report_mutex);

	printk("[EC_HID] hid_suspend_vote : %d\n", id);
	if (hid_member[id].id == id){
		hid_member[id].vote = true;
		printk("[EC_HID] ID : %d, NAME : %s, VOTE : %d\n", hid_member[id].id, hid_member[id].name, hid_member[id].vote);
	} else {
		printk("[EC_HID] Not register this ID. %d\n", id);
		mutex_unlock(&g_hid_data->report_mutex);
		return 0;
	}

	result = hid_check_vote(hid_member);
	//if(result && suspend_by_hall && !Stataion_sd_transfer){
	if(0){
		printk("[EC_HID] trigger USB turn off HOST, %d %d\n", result, suspend_by_hall);
		asus_station_enable_host(0);
	}

    mutex_unlock(&g_hid_data->report_mutex);
    return 1;
}
EXPORT_SYMBOL_GPL(hid_suspend_vote);

void hid_init_vote(struct vote_member *hid_member){
	int len=0;

	printk("[EC_HID] hid_init_vote\n");
	for(len=0 ; len<MAX_MEMBERS ; len++)
	{
		hid_member[len].id = -1;
		hid_member[len].name = NULL;
		hid_member[len].vote = false;
	}
}

void hid_reset_vote(struct vote_member *hid_member){
	int len=0;

	printk("[EC_HID] hid_reset_vote\n");
	for(len=0 ; len<MAX_MEMBERS ; len++)
	{
		hid_member[len].vote = false;
	}
}

void ec_hid_reset_vote(void){
	hid_reset_vote(hid_member);
}
EXPORT_SYMBOL_GPL(ec_hid_reset_vote);
#else
int hid_check_vote(struct vote_member *hid_member){
	return 0;
}

int hid_vote_register(char *name){
	return 0;
}
EXPORT_SYMBOL_GPL(hid_vote_register);

int hid_vote_unregister(int id, char *name){
	return 0;
}
EXPORT_SYMBOL_GPL(hid_vote_unregister);

int hid_suspend_vote(int id){
	return 0;
}
EXPORT_SYMBOL_GPL(hid_suspend_vote);

void hid_init_vote(struct vote_member *hid_member){
}

void hid_reset_vote(struct vote_member *hid_member){
}

void ec_hid_reset_vote(void){
}
EXPORT_SYMBOL_GPL(ec_hid_reset_vote);
#endif
