#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>

#define CONFIG_I2C_MODULE
#include <linux/i2c.h>
#include "device.h"
#include "memory.h"
#include "serial_bus.h"
#include "main.h"
#include "event.h"
#include "hardware.h"
#include "sysfs.h"
#include "utils.h"
#include "config.h"
#include "debug.h"
#include "sonacomm.h"
#include "workqueue.h"

#include <linux/proc_fs.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <asm/uaccess.h>
//#include <linux/wakelock.h>
#include "grip_Wakelock.h"
#include "locking.h"

int SntSensor_miscOpen(struct inode *inode, struct file *file);
int SntSensor_miscRelease(struct inode *inode, struct file *file);
long SntSensor_miscIoctl(struct file *file, unsigned int cmd, unsigned long arg);

void create_GripI2cCheck_proc_file(void);
void create_GripFPCCheck_proc_file(void);
void create_Calibration_raw_data_proc_file(void);
void create_asusGripDebug_proc_file(void);
void create_GripDisable_WakeLock_proc_file(void);

//Enable interface
void create_Grip_raw_en_proc_file(void);
void create_Grip_en_proc_file(void);
void create_GripSqueezeEn_proc_file(void);
void create_GripTap1En_proc_file(void);
void create_GripTap2En_proc_file(void);
void create_GripTap3En_proc_file(void);
void create_Grip_Swipe1_En_proc_file(void);
void create_Grip_Swipe2_En_proc_file(void);
void create_Grip_Swipe3_En_proc_file(void);
void create_Grip_Swipe1_Len_proc_file(void);
void create_Grip_Swipe2_Len_proc_file(void);
void create_Grip_Swipe3_Len_proc_file(void);

//Gesture Threshold Interface
void create_GripSqueezeForce_proc_file(void);
void create_GripTap1Force_proc_file(void);
void create_GripTap2Force_proc_file(void);
void create_GripTap3Force_proc_file(void);
void create_Grip_Swipe1_Velocity_proc_file(void);
void create_Grip_Swipe2_Velocity_proc_file(void);
void create_Grip_Swipe3_Velocity_proc_file(void);

void grip_enable_func(int val);
void grip_tap1_enable_func(int val);
void grip_tap2_enable_func(int val);
void grip_tap3_enable_func(int val);
void grip_swipe1_enable_func(int val);
void grip_swipe2_enable_func(int val);
void grip_swipe3_enable_func(int val);
void grip_squeeze_enable_func(int val);

void grip_squeeze_force_func(int val);
void grip_tap1_force_func(int val);
void grip_tap2_force_func(int val);
void grip_tap3_force_func(int val);
void grip_swipe1_velocity_func(int val);
void grip_swipe2_velocity_func(int val);
void grip_swipe3_velocity_func(int val);
void grip_swipe1_len_func(int val);
void grip_swipe2_len_func(int val);
void grip_swipe3_len_func(int val);

void check_gesture_before_suspend(void);
void check_gesture_after_resume(struct work_struct *work);
void Check_Scan_Bar_Control_func(void);
int Health_Check_Enable_No_Delay(int en);
void Wait_Wake_For_RegW(void);
void Check_Tap_func(void);
void Into_DeepSleep_fun(void);
void grip_dump_status_func(struct work_struct *work);
void Reset_Func(struct work_struct *work);
void Enable_tap_sensitive(const char *buf, size_t count);
struct workqueue_struct *asus_wq;
struct delayed_work check_resume;
/* Workaround for stucked semaphore */
void check_stuck_semaphore(struct work_struct *work);
struct delayed_work check_stuck_wake;
extern int Stuck_flag;
/* Workaround for stucked semaphore */

extern int snt_activity_request(void);

int g_debugMode = 0;
int bar_test_force = 0;
int bar_test_tolerance = 0;

struct file_operations sentons_snt_fops = {
  .owner = THIS_MODULE,
  .open = SntSensor_miscOpen,
  .release = SntSensor_miscRelease,
  .unlocked_ioctl = SntSensor_miscIoctl,
  .compat_ioctl = SntSensor_miscIoctl
};
struct miscdevice sentons_snt_misc = {
  .minor = MISC_DYNAMIC_MINOR,
  .name = "sentons_grip_sensor",
  .fops = &sentons_snt_fops
};
/* init record helath check result */
uint16_t FPC_value = 0;

/* init record factory force value */
uint16_t B0_F_value = 0;
uint16_t B1_F_value = 0;
uint16_t B2_F_value = 0;

/* Use to wakeup chip by retry */
int Stuck_retry = 0;
int Stuck_retry_times = 10;


uint16_t Tap_sense_data1[3] = { 
		0x0003,
		0x0000,
		0x8062 
};
uint16_t Tap_sense_data2[3] = { 
		0x0014,
		0x0000,
		0x804b 
};
uint16_t Tap_sense_data3[3] = { 
		0x0014,
		0x0000,
		0x8053 
};

uint16_t Tap_sense_reset_data1[3] = { 
		0x0032,
		0x0000,
		0x804b 
};
uint16_t Tap_sense_reset_data2[3] = { 
		0x0032,
		0x0000,
		0x8053 
};

uint16_t Tap_sense_addr[3] = { 
		0x42, 
		0x43, 
		0x44  
};
/* 
    1: when reset_func start, prevent init be stucked by ap_lock 
    => record the status from persist and return
    0: when reset_func and reload fw over
*/
extern int chip_reset_flag;
extern int finish_boot;

/*
    9: squeeze start
    7: squeeze short
    8: squeeze long
    10: squeeze cancel
    11: squeeze end
*/

/* after chip reset,  recovery according to status which records from 
property */
struct delayed_work rst_recovery_wk;

/* reset chip when i2c error or chip can't be waked up  */
struct delayed_work rst_gpio_wk;

/* do some setting when screen on off */
struct delayed_work check_onoff_wk;

/* on off 104 when screen on off */
void grip_squeeze_force_when_onoff(int val);
int screen_flag = 0; // 0 means: display off

/* 
    param. fw_loading_status:
    0: when charger/recorver or the other mode, grip fw will load fail 
    1: load fw success
*/
int fw_loading_status = 0;
void Grip_check_Display(int val){
    if(fw_loading_status == 0){
      PRINT_INFO("Skip fw setting when display on off");
      return;
    }
    MUTEX_LOCK(&snt8100fsr_g->ap_lock);
    screen_flag = val;
    queue_delayed_work(asus_wq, &check_onoff_wk, msecs_to_jiffies(0));
}
void Check_fw_onoff(struct work_struct *work){
	int scale_force = 0;
	if(grip_status_g->G_EN == 0){
		PRINT_INFO("Skip setting when grip off");
    		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
	}
	if(grip_status_g->G_EN == 1){
		if(screen_flag == 1){
			if(grip_status_g->G_SQUEEZE_FORCE > 85){
				grip_squeeze_force_when_onoff(grip_status_g->G_SQUEEZE_FORCE);
			}
			/* close fw 201 setting */
			Enable_tap_sensitive("104 0\x0a", 6);
		}else{
			if(grip_status_g->G_SQUEEZE_FORCE > 85){
				scale_force = (((grip_status_g->G_SQUEEZE_FORCE - 85)*3)/4)+85;
				grip_squeeze_force_when_onoff(scale_force);
			}else{
				/* Don't scaling force when display off */
			}
			Enable_tap_sensitive("104 3\x0a", 6);
		}
	}
    	mutex_unlock(&snt8100fsr_g->ap_lock);
}

void grip_squeeze_force_when_onoff(int val){
	int ret;
	uint16_t RegRead_t = 0;
	if(chip_reset_flag == 1){
		return;
	}
	Wait_Wake_For_RegW();
	ret = read_register(snt8100fsr_g,
                 REGISTER_SQUEEZE_CTL,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Read reg 0x%X faill", REGISTER_SQUEEZE_CTL);	
		return;
        }else{
        	//PRINT_INFO("Read Squeeze_force: %x", RegRead_t);
        }
		
	RegRead_t = (val & 0x00FF) | (RegRead_t & 0xFF00);

	ret = write_register(snt8100fsr_g,
                 REGISTER_SQUEEZE_CTL,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Write reg 0x%X faill", REGISTER_SQUEEZE_CTL);	
        }else{
        	PRINT_INFO("Write Squeeze_force: 0x%x", RegRead_t);
        }	
	Into_DeepSleep_fun();
}

/* for squeeze/tap cancel missing when grip reset */
void Reset_Func(struct work_struct *work){
    if(chip_reset_flag == 1){
	PRINT_INFO("Chip reset is ongoing, skip request");
	return;
    }
	MUTEX_LOCK(&snt8100fsr_g->ap_lock);
    if(chip_reset_flag == 0){
		// ***** To do list ********
	    //ASUSEvtlog("[Grip] Workaround : reset chip\n");
	    chip_reset_flag = 1;
	    fw_loading_status = 0;
	    // Reset sequence
	    if (gpio_is_valid(snt8100fsr_g->rst_gpio)) {
	        PRINT_INFO("rst_gpio is valid");
	        msleep(100);
	        //gpio_direction_output(snt8100fsr_g->rst_gpio, 1);
	        //msleep(200);
		//PRINT_INFO("GPIO133: %d",gpio_get_value(RST_GPIO));
	        gpio_direction_output(snt8100fsr_g->rst_gpio, 0);
	        msleep(200);
		PRINT_INFO("GPIO133: %d",gpio_get_value(RST_GPIO));
	        gpio_direction_output(snt8100fsr_g->rst_gpio, 1);
	        msleep(200);
		PRINT_INFO("GPIO133: %d",gpio_get_value(RST_GPIO));
    		PRINT_INFO("Reset Done!");
	    } else {
		chip_reset_flag = 0;
	        PRINT_INFO("no reset pin is requested");
		mutex_unlock(&snt8100fsr_g->ap_lock);
	    }
    }else{
	PRINT_INFO("Chip reset is ongoing, skip request");
    }
}
void check_gesture_before_suspend(void){
	PRINT_INFO("check status when suspend");
	Wait_Wake_For_RegW();
	if(grip_status_g->G_EN != 0){
		if(grip_status_g->G_TAP1_EN){
			grip_tap1_enable_func(0);
		}
		if(grip_status_g->G_TAP2_EN){
			grip_tap2_enable_func(0);
		}
		if(grip_status_g->G_TAP3_EN){
			grip_tap3_enable_func(0);
		}
		if(grip_status_g->G_SWIPE1_EN){
			grip_swipe1_enable_func(0);
		}
		if(grip_status_g->G_SWIPE2_EN){
			grip_swipe2_enable_func(0);
		}
		if(grip_status_g->G_SWIPE3_EN){
			grip_swipe3_enable_func(0);
		}
	}
}
void check_stuck_semaphore(struct work_struct *work){
	int ret;
	/* prevent chip reset fail */
	if(chip_reset_flag == 1){
		workqueue_cancel_work(&check_stuck_wake);
		PRINT_INFO("Don't wake chip during chip reset & long fw");
		Stuck_retry = 0;
		return;
	}
	if(Stuck_flag == 1){
		PRINT_INFO("Used to solve wailting semaphore!!! retry times = %d", 
		Stuck_retry);
		MUTEX_LOCK(&snt8100fsr_g->sb_lock);
 		ret = sb_wake_device(snt8100fsr_g);
		mutex_unlock(&snt8100fsr_g->sb_lock);
	        if (ret) {
	            PRINT_CRIT("sb_wake_device() failed");
	        }
		//retry check
		if(Stuck_retry < Stuck_retry_times){
			workqueue_cancel_work(&check_stuck_wake);
			workqueue_queue_work(&check_stuck_wake, 200);
			Stuck_retry++;
		}else{
			// ***** To do list ********
			//ASUSEvtlog("[Grip] driver is failed to wait semaphore due to non-wakeable chip\n"); 
        		up(&snt8100fsr_g->wake_rsp);
			if (down_trylock(&snt8100fsr_g->wake_req)){
				PRINT_INFO("Wake Req alread consumed");
				if(down_trylock(&snt8100fsr_g->wake_rsp)){
					PRINT_INFO("Wake Rsq alread consumed");
				}
	    		}
    			queue_delayed_work(asus_wq, &rst_gpio_wk, msecs_to_jiffies(0));
		}
	}else{
		PRINT_INFO("None used");
		Stuck_retry = 0;
		workqueue_cancel_work(&check_stuck_wake);
	}
}

void Check_Scan_Bar_Control_func(void){
	int ret;
	uint16_t scan_bar0_value = 0;
	//Open scaned fpc when tap or raw need
	if(grip_status_g->G_TAP1_EN == 1 || grip_status_g->G_RAW_EN == 1){
		scan_bar0_value = 0x00ff;
		//PRINT_INFO("Enable bar0 scan");
	}else{ //no need scan bar0
		scan_bar0_value = 0x00fe;
		//PRINT_INFO("Disable bar0 scan");
	}
	
	ret = write_register(snt8100fsr_g,
                 0x002b,
                 &scan_bar0_value);
	if(ret < 0){	
		PRINT_ERR("Write 0x2b error");
	}else{
		PRINT_INFO("Set 0x2b to 0x%x", scan_bar0_value);
	}
}
void check_gesture_after_resume(struct work_struct *work){
	PRINT_INFO("check status when resume");
	Wait_Wake_For_RegW();
	if(grip_status_g->G_TAP1_EN){
		grip_tap1_enable_func(1);
	}
	if(grip_status_g->G_TAP2_EN){
		grip_tap2_enable_func(1);
	}
	if(grip_status_g->G_TAP3_EN){
		grip_tap3_enable_func(1);
	}
	if(grip_status_g->G_SWIPE1_EN){
		grip_swipe1_enable_func(1);
	}
	if(grip_status_g->G_SWIPE2_EN){
		grip_swipe2_enable_func(1);
	}
	if(grip_status_g->G_SWIPE3_EN){
		grip_swipe3_enable_func(1);
	}
}
int Health_Check_Enable_No_Delay(int en){
	uint16_t En_fpc;
	int ret;
	ret = read_register(snt8100fsr_g,
                 0x003d,
                 &En_fpc);
	if(ret < 0) {
		PRINT_ERR("Read Reg 3d Failed");
		return -1;	
	}
	//PRINT_INFO("0x003d:%x ", En_fpc);
	if(en == 0){
		En_fpc = En_fpc | 0x0004;
	}else if(en ==1){
		En_fpc = En_fpc & 0xfffb;
	}else{
		PRINT_ERR("en=%d, out of 1 or 2 ", en);
		return -1;
	}
	
	ret = write_register(snt8100fsr_g,
                 0x003d,
                 &En_fpc);
	if(ret < 0) {
		PRINT_ERR("Read Reg 3d Failed");
		return -1;
	}
	PRINT_INFO("Health Check EN=%d", en);
	return 0;
}


int Health_Check_Enable(int en){
	uint16_t En_fpc;
	int ret;
	ret = read_register(snt8100fsr_g,
                 0x003d,
                 &En_fpc);
	if(ret < 0) {
		PRINT_ERR("Read Reg 3d Failed");
		return -1;	
	}
	//PRINT_INFO("0x003d:%x ", En_fpc);
	if(en == 0){
		En_fpc = En_fpc | 0x0004;
	}else if(en ==1){
		En_fpc = En_fpc & 0xfffb;
	}else{
		PRINT_ERR("en=%d, out of 1 or 2 ", en);
		return -1;
	}
	
	ret = write_register(snt8100fsr_g,
                 0x003d,
                 &En_fpc);
	if(ret < 0) {
		PRINT_ERR("Read Reg 3d Failed");
		return -1;
	}
	msleep(500);
	ret = read_register(snt8100fsr_g,
                 0x003d,
                 &En_fpc);
	if(ret < 0) {
		PRINT_ERR("Read Reg 3d Failed");
		return -1;
	}
	PRINT_INFO("Health Check EN=%d", en);
	return 0;
}
int Health_Check(uint16_t val){
	int ret;
	uint16_t FPC_status;
	//Enable Health Check
	ret = Health_Check_Enable(0);
	ret = Health_Check_Enable(1);
	
	ret = read_register(snt8100fsr_g,
                 REGISTER_PHY_STAT_LSB,
                 &FPC_status);
        if(ret < 0) {
		PRINT_ERR("Read 0x03 Fail");
		Health_Check_Enable(0);
		return -1;
	}
	PRINT_INFO("0x03: 0x%x, expect: 0x%x", FPC_status, FPC_status |val);
	FPC_value = FPC_status;
	if (FPC_status != (FPC_status | val)) {
		PRINT_INFO("Health Check Fail!!!");
		Health_Check_Enable(0);
		return -1;
	}
	ret = Health_Check_Enable(0);
	return 0;
}
/*---BSP Clay proc asusGripDebug Interface---*/
void print_current_report(int i){
	PRINT_INFO("%u %u %u %u %u %u %u\n",
				snt8100fsr_g->track_reports_frame,
	                        snt8100fsr_g->track_reports[i].bar_id,
	                        snt8100fsr_g->track_reports[i].trk_id,
	                        snt8100fsr_g->track_reports[i].force_lvl,
	                        snt8100fsr_g->track_reports[i].top,
	                        snt8100fsr_g->track_reports[i].center,
	                        snt8100fsr_g->track_reports[i].bottom);
}
int check_report_force(int force){
	int force_top, force_floor;
	if(bar_test_tolerance == 100){
		return 1;
	}else if((bar_test_tolerance > 0) && (bar_test_tolerance < 100)){
		force_top = bar_test_force * (100 + bar_test_tolerance) /100;
		force_floor = bar_test_force * (100 - bar_test_tolerance) /100;
	}else{
		force_top = bar_test_force;
		force_floor = bar_test_force;
	}
	PRINT_INFO("force check: force = %d, threshould = %d, tolerance = %d percent, top = %d, floor= %d", 
				force, bar_test_force, bar_test_tolerance, force_top, force_floor);
	if(bar_test_force > 0){
		if(force >= force_floor && force <= force_top){
			return 1;
		}else{
			return 0;
		}
	} else {
		return 1;
	}
}
void Into_DeepSleep_fun(void){
	int ret = 0;
	int frame_rate=65535;
	if(grip_status_g->G_EN==0){
		ret = write_register(snt8100fsr_g,
	                 REGISTER_FRAME_RATE,
	                 &frame_rate);
	        if(ret < 0) {
			PRINT_ERR("Grip register_enable write fail");
	        }else{
			PRINT_INFO("Grip_EN = %d => Grip_Frame = %d", grip_status_g->G_EN, frame_rate);
	        }
	}
}

/*
void Wake_device_func(void){
	int ret;
	MUTEX_LOCK(&snt8100fsr_g->sb_lock);
	ret = sb_wake_device(snt8100fsr_g);
	if (ret) {
		PRINT_CRIT("sb_wake_device() failed");
		mutex_unlock(&snt8100fsr_g->sb_lock);
		return;
	}
	mutex_unlock(&snt8100fsr_g->sb_lock);
	msleep(100);
}
*/
void Wait_Wake_For_RegW(void){
	/*
	int count = 0, times = 100;
	G_Wake = 0;
	Wake_device_func();
	msleep(10);
	while(G_Wake == 0 && (count < times)){
		PRINT_INFO("Wait Wake device");
		Wake_device_func();
		msleep(10);
		count++;
	}
	//sb_wake and chip return irq => DPC from low to high
	if(G_Wake == 1){
		PRINT_INFO("DPC: change mode from low to high");
		G_Wake = 0;
	}else{
		PRINT_INFO("Wake device fail");
	}*/
#ifdef DYNAMIC_PWR_CTL
	if(fw_loading_status == 1){
	    if (snt_activity_request() != 0) {
        	PRINT_CRIT("snt_activity_request() failed");
	    }
	}else{
		PRINT_INFO("Load FW Fail, skip wakeup request");
	}
#endif

}
/* write DPC function */
void DPC_write_func(int flag){
	int ret;
	grip_status_g->G_DPC_STATUS = flag;
	if(flag == 1){
		PRINT_INFO("Enable DPC, write 0x%x, 0x%x, 0x%x", 
			DPC_status_g->High, DPC_status_g->Low, DPC_status_g->Condition);
		ret = write_register(snt8100fsr_g, REGISTER_DPC_HIGH_FRAME, &DPC_status_g->High);
		if (ret) {
		    PRINT_CRIT("set DPC 0x%x failed", REGISTER_DPC_HIGH_FRAME);
		}
		ret = write_register(snt8100fsr_g, REGISTER_DPC_LOW_FRAME, &DPC_status_g->Low);
		if (ret) {
		    PRINT_CRIT("set DPC 0x%x failed", REGISTER_DPC_LOW_FRAME);
		}
		if(grip_status_g->G_TAP1_EN == 1 ||grip_status_g->G_TAP2_EN == 1  || 
		grip_status_g->G_TAP3_EN == 1){
			PRINT_INFO("Don't Enable DPC since tap enable");
		}else{
			ret = write_register(snt8100fsr_g, REGISTER_DPC_CONDITION, &DPC_status_g->Condition);
			if (ret) {
			    PRINT_CRIT("set DPC 0x%x failed", REGISTER_DPC_CONDITION);
			}
		}
	}else{
		PRINT_INFO("Disable DPC");
		ret = write_register(snt8100fsr_g, REGISTER_DPC_CONDITION, &flag);
		if (ret) {
		    PRINT_CRIT("set DPC 0x%x failed", REGISTER_DPC_CONDITION);
		}
		ret = write_register(snt8100fsr_g, REGISTER_DPC_HIGH_FRAME, &flag);
		if (ret) {
		    PRINT_CRIT("set DPC 0x%x failed", REGISTER_DPC_HIGH_FRAME);
		}
		ret = write_register(snt8100fsr_g, REGISTER_DPC_LOW_FRAME, &flag);
		if (ret) {
		    PRINT_CRIT("set DPC 0x%x failed", REGISTER_DPC_LOW_FRAME);
		}
	}
}
void Check_Tap_sense_val(void){
	int i=0;
	uint32_t RegRead_t;
	msleep(50);
	for(i = 0; i < 3; i++){
		read_register(snt8100fsr_g, Tap_sense_addr[i], &RegRead_t);
		PRINT_INFO("Reg: 0x%x, Val: 0x%x", Tap_sense_addr[i], RegRead_t);
	}
}
void Tap_sense_write_func(int flag){
	int i = 0;
	if(grip_status_g->G_TAP_SENSE_EN != flag){
		grip_status_g->G_TAP_SENSE_EN = flag;
	}else{
		PRINT_INFO("TAP SENSE=%d, Already set before=======", flag);
		return;
	}
	if(flag == 1){
		PRINT_INFO("[Enable] Tap Sense data");
		for(i = 0; i < 3; i++){
			write_register(snt8100fsr_g, Tap_sense_addr[i], &Tap_sense_data1[i]);
		}
		Check_Tap_sense_val();
		for(i = 0; i < 3; i++){
			write_register(snt8100fsr_g, Tap_sense_addr[i], &Tap_sense_data2[i]);
		}
		Check_Tap_sense_val();
		for(i = 0; i < 3; i++){
			write_register(snt8100fsr_g, Tap_sense_addr[i], &Tap_sense_data3[i]);
		}
		Check_Tap_sense_val();
	}else{
		PRINT_INFO("[Disable] Tap Sense data");
		for(i = 0; i < 3; i++){
			write_register(snt8100fsr_g, Tap_sense_addr[i], &Tap_sense_reset_data1[i]);
		}
		Check_Tap_sense_val();
		for(i = 0; i < 3; i++){
			write_register(snt8100fsr_g, Tap_sense_addr[i], &Tap_sense_reset_data2[i]);
		}
		Check_Tap_sense_val();
	}
}

//Used in Tap function
void Check_Tap_func(void){
	int ret = 0;
	
	/********* DPC part **********/
	if(grip_status_g->G_TAP1_EN == 0 && grip_status_g->G_TAP2_EN == 0 && 
	grip_status_g->G_TAP3_EN == 0){
		if(grip_status_g->G_DPC_STATUS==0){
			PRINT_INFO("Enable DPC when all taps disable");
			DPC_write_func(1);
		}
	}else{ //there exist tap which is on
		//Disable DPC
		if(grip_status_g->G_DPC_STATUS==1){
			PRINT_INFO("Tap enable, DPC turn off from on state and set frame_rate = %d", 
			snt8100fsr_g->frame_rate);
			DPC_write_func(0);
		        ret = write_register(snt8100fsr_g, REGISTER_FRAME_RATE,
		                             &snt8100fsr_g->frame_rate);
		        if (ret) {
		            PRINT_CRIT("write_register(REGISTER_FRAME_RATE) failed");
		        }
		}else{
		  ret = write_register(snt8100fsr_g, REGISTER_FRAME_RATE,
				       &snt8100fsr_g->frame_rate);
			PRINT_INFO("DPC already off");
		}
	}

	/********* Tap sense part **********/
	if(grip_status_g->G_TAP1_EN == 0 && grip_status_g->G_TAP2_EN == 0 && 
	grip_status_g->G_TAP3_EN == 0){
		//Disable tap sense
		if(grip_status_g->G_TAP_SENSE_SET == 1){
			Tap_sense_write_func(0);
		}else{
			//Do nothing
		}
	}else if(grip_status_g->G_TAP1_EN == 1 && grip_status_g->G_TAP2_EN == 1 && 
	grip_status_g->G_TAP3_EN == 1){
		if(grip_status_g->G_TAP_SENSE_SET == 1){
			Tap_sense_write_func(1);
		}else{
			Tap_sense_write_func(0);
		}
	}else{
		//Do nothing
	}
}
void Enable_tap_sensitive(const char *buf, size_t count) {
	//size_t l = count;
	int l = (int)count;
	uint32_t val;
	uint32_t id;
	int status;
	
	PRINT_INFO("%zu bytes, buf=%s", count, buf);
	
#ifdef DYNAMIC_PWR_CTL
	if (snt_activity_request() != 0) {
		PRINT_CRIT("snt_activity_request() failed");
		return;
	}
#endif
	status = ReadNumFromBuf((const uint8_t**)&buf, &l, &id);
	if (status != 0) {
		PRINT_CRIT("Could not parse param_id %d", status);
		goto errexit;
	}

	status = ReadNumFromBuf((const uint8_t**)&buf, &l, &val);
	if (status != 0) {
		PRINT_CRIT("Could not parse param_val %d", status);
		goto errexit;
	}
	enable_set_sys_param(snt8100fsr_g, id, val);
	
errexit:
	PRINT_DEBUG("done.");
	return;
}

/**************** +++ wrtie DPC & Frame function +++ **************/
void grip_frame_rate_func(int val){
	int ret;
	uint16_t RegRead_t = 0;
	MUTEX_LOCK(&snt8100fsr_g->ap_lock);
	Wait_Wake_For_RegW();
	if(val == 10  || val == 20 || val == 25  || val == 40  || val == 50  || val 
	== 80  || val == 100){
		PRINT_INFO("val = %d", val);
		DPC_status_g->High = val;
		RegRead_t = val;
		snt8100fsr_g->frame_rate = val;
		ret = write_register(snt8100fsr_g,
	                 REGISTER_DPC_HIGH_FRAME,
	                 &RegRead_t);
	        if(ret < 0) {
			PRINT_ERR("Write reg 0x%X faill", REGISTER_DPC_HIGH_FRAME);	
	        }else{
	        	PRINT_INFO("Write DPC High: 0x%x", RegRead_t);
	        }
			
		ret = write_register(snt8100fsr_g,
	                 REGISTER_FRAME_RATE,
	                 &RegRead_t);
	        if(ret < 0) {
			PRINT_ERR("Write reg 0x%X faill", REGISTER_FRAME_RATE);	
	        }else{
	        	PRINT_INFO("Write frame rate: 0x%x", RegRead_t);
	        }
		Into_DeepSleep_fun();
		mutex_unlock(&snt8100fsr_g->ap_lock);
	}else{
		PRINT_INFO("Not in defined frame rate range, skip val = %d", val);
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
	}
}

/**************** ---wrtie DPC & Frame function --- **************/

/**************** +++ wrtie gesture function +++ **************/
int G_grip_en = 0;
void grip_enable_func(int val){
	int ret;
	if(fw_loading_status == 0){
		grip_status_g->G_EN= 0;
		PRINT_INFO("Load fw fail, skip grip enable function");
		return;
	}
	MUTEX_LOCK(&snt8100fsr_g->ap_lock);
	// ***** To do list ********
	//ASUSEvtlog("[Grip] %s\n", (val)? "Enable":"Disable");
	G_grip_en = val;
	PRINT_INFO("val = %d", val);
	if(grip_status_g == NULL){
		PRINT_INFO("grip_status_g == NULL!");
	}else{
		grip_status_g->G_EN= val;
	}
	if(chip_reset_flag == 1){
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
	}
	/*****************/
	if(val == 1){
		// We mutex lock here since we're calling sb_wake_device which never locks
		Wait_Wake_For_RegW();
		DPC_write_func(1);
		ret = write_register(snt8100fsr_g,
		        REGISTER_ENABLE,
		        &val);
		if(ret < 0) {
			PRINT_ERR("Grip register_enable write fail");
		}else{
			PRINT_INFO("Grip_Enable = %d", val);
		}
		ret = write_register(snt8100fsr_g,
	                 REGISTER_FRAME_RATE,
	                 &snt8100fsr_g->frame_rate);
	        if(ret < 0) {
			PRINT_ERR("Grip register_enable write fail");
	        }else{
			PRINT_INFO("Grip_EN = %d => Grip_Frame = %d", grip_status_g->G_EN, snt8100fsr_g->frame_rate);
	        }
	/*******************/
	}else{
		Wait_Wake_For_RegW();
		ret = write_register(snt8100fsr_g,
	                 REGISTER_ENABLE,
	                 &val);
	        if(ret < 0){
			PRINT_ERR("Grip register_enable write fail");
		}else{
			PRINT_INFO("Grip_Enable = %d", val);
		}
		DPC_write_func(0);
		Into_DeepSleep_fun();
	}
	mutex_unlock(&snt8100fsr_g->ap_lock);
}

void grip_raw_enable_func(int val){
	int ret;
	uint16_t RegRead_t = 0;
	if(grip_status_g->G_EN == 0){
		PRINT_INFO("Skip setting when grip off");
		return;
	}
	
	MUTEX_LOCK(&snt8100fsr_g->ap_lock);
	PRINT_INFO("val = %d", val);
	if(grip_status_g->G_RAW_EN == val){
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
	}
	grip_status_g->G_RAW_EN = val;
	if(chip_reset_flag == 1){
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
	}
	Wait_Wake_For_RegW();
	if(val == 0){
		val = 1;
		//Enable_tap_sensitive("104 3\x0a", 6);
	}else{
		val = 0;
		//Enable_tap_sensitive("104 0\x0a", 6);
	}
	RegRead_t = val << 3;
	ret = write_register(snt8100fsr_g,
                 REGISTR_RAW_DATA,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Write reg 0x%X faill", REGISTR_RAW_DATA);	
        }else{
        	PRINT_INFO("Write raw data: 0x%x", RegRead_t);
        }
	/* check bar scan behavior */
	Check_Scan_Bar_Control_func();
	Into_DeepSleep_fun();
	mutex_unlock(&snt8100fsr_g->ap_lock);
}

void grip_squeeze_enable_func(int val){
	int ret;
	uint16_t RegRead_t = 0;
	if(grip_status_g->G_EN == 0){
		PRINT_INFO("Skip setting when grip off");
		return;
	}
	MUTEX_LOCK(&snt8100fsr_g->ap_lock);
	PRINT_INFO("val = %d", val);
	grip_status_g->G_SQUEEZE_EN = val;
	if(chip_reset_flag == 1){
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
	}
	Wait_Wake_For_RegW();
	val = val << 15;
	ret = read_register(snt8100fsr_g,
                 REGISTER_SQUEEZE_CTL,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Read reg 0x%X faill", REGISTER_SQUEEZE_CTL);	
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
        }else{
        	PRINT_INFO("Read Squeeze_En: %x", RegRead_t);
        }
		
	RegRead_t = (val & 0x8000) | (RegRead_t & 0x7FFF);

	ret = write_register(snt8100fsr_g,
                 REGISTER_SQUEEZE_CTL,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Write reg 0x%X faill", REGISTER_SQUEEZE_CTL);	
        }else{
        	PRINT_INFO("Write Squeeze_En: 0x%x", RegRead_t);
        }
	Into_DeepSleep_fun();
	mutex_unlock(&snt8100fsr_g->ap_lock);
}

void grip_tap1_enable_func(int val){
	int ret;
	uint16_t RegRead_t;
	if(grip_status_g->G_EN == 0){
		PRINT_INFO("Skip setting when grip off");
		return;
	}
	MUTEX_LOCK(&snt8100fsr_g->ap_lock);
	PRINT_INFO("val = %d", val);
	grip_status_g->G_TAP1_EN = val;
	if(chip_reset_flag == 1){
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
	}
	Wait_Wake_For_RegW();
	ret = read_register(snt8100fsr_g,
                 REGISTER_TAP1_CTL,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Read reg 0x%X faill", REGISTER_TAP1_CTL);	
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
        }else{
        	PRINT_INFO("Read tap1 reg: %x", RegRead_t);
        }
	RegRead_t = (RegRead_t & 0xFFFE) | ( val & 0x0001);
	ret = write_register(snt8100fsr_g,
                 REGISTER_TAP1_CTL,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Write reg 0x%X faill", REGISTER_TAP1_CTL);	
        }else{
        	PRINT_INFO("Write tap1 reg: %x", RegRead_t);
        }
	/* check bar scan behavior */
	Check_Scan_Bar_Control_func();
	Check_Tap_func();
	Into_DeepSleep_fun();	
	mutex_unlock(&snt8100fsr_g->ap_lock);
}
void grip_tap2_enable_func(int val){
	int ret;
	uint16_t RegRead_t;
	int frame_rate=65535;
	
	if(grip_status_g->G_EN == 0){
		PRINT_INFO("Skip setting when grip off");
		return;
	}
	MUTEX_LOCK(&snt8100fsr_g->ap_lock);
	PRINT_INFO("val = %d", val);
	/* Add on off when tap2 on */
	if(grip_status_g->G_TAP2_EN != val){
		if(val == 1){
			PRINT_INFO("[WQ] off and on frame rate for tap hang issue");
			if(grip_status_g->G_DPC_STATUS == 1){
				Wait_Wake_For_RegW();
			}else{
				/* don't need to wakeup, do nothing*/
			}
			ret = write_register(snt8100fsr_g,
	                 REGISTER_FRAME_RATE,
	                &frame_rate);
		}else{
			/* do nothing */
		}
	}

	grip_status_g->G_TAP2_EN = val;
	if(chip_reset_flag == 1){
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
	}
	Wait_Wake_For_RegW();
	ret = read_register(snt8100fsr_g,
                 REGISTER_TAP2_CTL,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Read reg 0x%X faill", REGISTER_TAP2_CTL);	
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
        }else{
        	PRINT_INFO("Read tap2 reg: %x", RegRead_t);
        }
	RegRead_t = (RegRead_t & 0xFFFE) | ( val & 0x0001);
	
	ret = write_register(snt8100fsr_g,
                 REGISTER_TAP2_CTL,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Write reg 0x%X faill", REGISTER_TAP2_CTL);	
        }else{
        	PRINT_INFO("Write tap2 reg: %x", RegRead_t);
        }
	Check_Tap_func();
	Into_DeepSleep_fun();
	mutex_unlock(&snt8100fsr_g->ap_lock);
}

void grip_tap3_enable_func(int val){
	int ret;
	uint16_t RegRead_t;
	if(grip_status_g->G_EN == 0){
		PRINT_INFO("Skip setting when grip off");
		return;
	}
	MUTEX_LOCK(&snt8100fsr_g->ap_lock);
	PRINT_INFO("val = %d", val);
	grip_status_g->G_TAP3_EN = val;
	if(chip_reset_flag == 1){
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
	}
	Wait_Wake_For_RegW();
	ret = read_register(snt8100fsr_g,
                 REGISTER_TAP3_CTL,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Read reg 0x%X faill", REGISTER_TAP3_CTL);	
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
        }else{
        	PRINT_INFO("Read tap3 reg: %x", RegRead_t);
        }
	RegRead_t = (RegRead_t & 0xFFFE) | ( val & 0x0001);
	
	ret = write_register(snt8100fsr_g,
                 REGISTER_TAP3_CTL,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Write reg 0x%X faill", REGISTER_TAP3_CTL);	
        }else{
        	PRINT_INFO("Write tap3 reg: %x", RegRead_t);
        }
	Check_Tap_func();
	Into_DeepSleep_fun();
	mutex_unlock(&snt8100fsr_g->ap_lock);
}

void grip_tap_sense_enable_func(int val){
	if(grip_status_g->G_EN == 0){
		PRINT_INFO("Skip setting when grip off");
		return;
	}
	MUTEX_LOCK(&snt8100fsr_g->ap_lock);
	PRINT_INFO("val = %d", val);
	grip_status_g->G_TAP_SENSE_SET= val;
	if(chip_reset_flag == 1){
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
	}
	Wait_Wake_For_RegW();
	Check_Tap_func();
	mutex_unlock(&snt8100fsr_g->ap_lock);
}

void grip_swipe1_enable_func(int val){
	int ret;
	uint16_t RegRead_t = 0;
	if(grip_status_g->G_EN == 0){
		PRINT_INFO("Skip setting when grip off");
		return;
	}
	MUTEX_LOCK(&snt8100fsr_g->ap_lock);
	PRINT_INFO("val = %d", val);
	grip_status_g->G_SWIPE1_EN = val;
	if(chip_reset_flag == 1){
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
	}
	Wait_Wake_For_RegW();
	val = val << 15;
	ret = read_register(snt8100fsr_g,
                 REGISTER_SWIPE1_CTL,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Read reg 0x%X faill", REGISTER_SWIPE1_CTL);	
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
        }else{
        	PRINT_INFO("Read Swipe1_En: %x", RegRead_t);
        }
		
	RegRead_t = (val & 0x8000) | (RegRead_t & 0x7FFF);

	ret = write_register(snt8100fsr_g,
                 REGISTER_SWIPE1_CTL,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Write reg 0x%X faill", REGISTER_SWIPE1_CTL);	
        }else{
        	PRINT_INFO("Write Swipe1_En: 0x%x", RegRead_t);
        }
	Into_DeepSleep_fun();
	mutex_unlock(&snt8100fsr_g->ap_lock);
}
void grip_swipe2_enable_func(int val){
	int ret;
	uint16_t RegRead_t = 0;
	if(grip_status_g->G_EN == 0){
		PRINT_INFO("Skip setting when grip off");
		return;
	}
	MUTEX_LOCK(&snt8100fsr_g->ap_lock);
	PRINT_INFO("val = %d", val);
	grip_status_g->G_SWIPE2_EN = val;
	if(chip_reset_flag == 1){
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
	}
	Wait_Wake_For_RegW();
	val = val << 15;
	ret = read_register(snt8100fsr_g,
                 REGISTER_SWIPE2_CTL,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Read reg 0x%X faill", REGISTER_SWIPE2_CTL);	
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
        }else{
        	PRINT_INFO("Read Swipe2_En: %x", RegRead_t);
        }
		
	RegRead_t = (val & 0x8000) | (RegRead_t & 0x7FFF);

	ret = write_register(snt8100fsr_g,
                 REGISTER_SWIPE2_CTL,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Write reg 0x%X faill", REGISTER_SWIPE2_CTL);	
        }else{
        	PRINT_INFO("Write Swipe2_En: 0x%x", RegRead_t);
        }
	Into_DeepSleep_fun();
	mutex_unlock(&snt8100fsr_g->ap_lock);
}
void grip_swipe3_enable_func(int val){
	int ret;
	uint16_t RegRead_t = 0;
	if(grip_status_g->G_EN == 0){
		PRINT_INFO("Skip setting when grip off");
		return;
	}
	MUTEX_LOCK(&snt8100fsr_g->ap_lock);
	PRINT_INFO("val = %d", val);
	grip_status_g->G_SWIPE3_EN = val;
	if(chip_reset_flag == 1){
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
	}
	Wait_Wake_For_RegW();
	val = val << 15;
	ret = read_register(snt8100fsr_g,
                 REGISTER_SWIPE3_CTL,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Read reg 0x%X faill", REGISTER_SWIPE3_CTL);	
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
        }else{
        	PRINT_INFO("Read Swipe3_En: %x", RegRead_t);
        }
		
	RegRead_t = (val & 0x8000) | (RegRead_t & 0x7FFF);

	ret = write_register(snt8100fsr_g,
                 REGISTER_SWIPE3_CTL,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Write reg 0x%X faill", REGISTER_SWIPE3_CTL);	
        }else{
        	PRINT_INFO("Write Swipe3_En: 0x%x", RegRead_t);
        }
	Into_DeepSleep_fun();
	mutex_unlock(&snt8100fsr_g->ap_lock);
}

void grip_squeeze_force_func(int val){
	int ret;
	uint16_t RegRead_t = 0;
	if(grip_status_g->G_EN == 0){
		PRINT_INFO("Skip setting when grip off");
		return;
	}
	MUTEX_LOCK(&snt8100fsr_g->ap_lock);
	PRINT_INFO("val = %d", val);
	grip_status_g->G_SQUEEZE_FORCE = val;
	if(chip_reset_flag == 1){
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
	}
	Wait_Wake_For_RegW();
	ret = read_register(snt8100fsr_g,
                 REGISTER_SQUEEZE_CTL,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Read reg 0x%X faill", REGISTER_SQUEEZE_CTL);	
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
        }else{
        	PRINT_INFO("Read Squeeze_force: %x", RegRead_t);
        }
		
	RegRead_t = (val & 0x00FF) | (RegRead_t & 0xFF00);

	ret = write_register(snt8100fsr_g,
                 REGISTER_SQUEEZE_CTL,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Write reg 0x%X faill", REGISTER_SQUEEZE_CTL);	
        }else{
        	PRINT_INFO("Write Squeeze_force: 0x%x", RegRead_t);
        }	
	Into_DeepSleep_fun();
	mutex_unlock(&snt8100fsr_g->ap_lock);
}

void grip_tap1_force_func(int val){
	int ret;
	uint16_t RegRead_t;
	if(grip_status_g->G_EN == 0){
		PRINT_INFO("Skip setting when grip off");
		return;
	}
	MUTEX_LOCK(&snt8100fsr_g->ap_lock);
	PRINT_INFO("val = %d", val);
	if(val == 0){
		val = 1;
	}
	grip_status_g->G_TAP1_FORCE = val;
	if(chip_reset_flag == 1){
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
	}
	Wait_Wake_For_RegW();
	val = val << 8;
	ret = read_register(snt8100fsr_g,
                 REGISTER_TAP1_CTL,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Read reg 0x%X faill", REGISTER_TAP1_CTL);	
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
        }else{
        	PRINT_INFO("Read tap1_force: %x", RegRead_t);
        }
	RegRead_t = (val & 0xFF00) | (RegRead_t & 0x00FF);
	
	ret = write_register(snt8100fsr_g,
                 REGISTER_TAP1_CTL,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Write reg 0x%X faill", REGISTER_TAP1_CTL);	
        }else{
        	PRINT_INFO("Write tap1_force: 0x%x", RegRead_t);
        }
	Into_DeepSleep_fun();
	mutex_unlock(&snt8100fsr_g->ap_lock);
}

void grip_tap2_force_func(int val){
	int ret;
	uint16_t RegRead_t;
	if(grip_status_g->G_EN == 0){
		PRINT_INFO("Skip setting when grip off");
		return;
	}
	MUTEX_LOCK(&snt8100fsr_g->ap_lock);
	PRINT_INFO("val = %d", val);
	if(val == 0){
		val = 1;
	}
	grip_status_g->G_TAP2_FORCE = val;
	if(chip_reset_flag == 1){
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
	}
	Wait_Wake_For_RegW();
	val = val << 8;
	ret = read_register(snt8100fsr_g,
                 REGISTER_TAP2_CTL,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Read reg 0x%X faill", REGISTER_TAP2_CTL);	
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
        }else{
        	PRINT_INFO("Read tap2_force: %x", RegRead_t);
        }
	RegRead_t = (val & 0xFF00) | (RegRead_t & 0x00FF);
	
	ret = write_register(snt8100fsr_g,
                 REGISTER_TAP2_CTL,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Write reg 0x%X faill", REGISTER_TAP2_CTL);	
        }else{
        	PRINT_INFO("Write tap2_force: 0x%x", RegRead_t);
        }
	Into_DeepSleep_fun();
	mutex_unlock(&snt8100fsr_g->ap_lock);
}

void grip_tap3_force_func(int val){
	int ret;
	uint16_t RegRead_t;
	if(grip_status_g->G_EN == 0){
		PRINT_INFO("Skip setting when grip off");
		return;
	}
	MUTEX_LOCK(&snt8100fsr_g->ap_lock);
	PRINT_INFO("val = %d", val);
	if(val == 0){
		val = 1;
	}
	grip_status_g->G_TAP3_FORCE = val;
	if(chip_reset_flag == 1){
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
	}
	Wait_Wake_For_RegW();
	val = val << 8;
	ret = read_register(snt8100fsr_g,
                 REGISTER_TAP3_CTL,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Read reg 0x%X faill", REGISTER_TAP3_CTL);	
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
        }else{
        	PRINT_INFO("Read tap3_force: %x", RegRead_t);
        }
	RegRead_t = (val & 0xFF00) | (RegRead_t & 0x00FF);
	
	ret = write_register(snt8100fsr_g,
                 REGISTER_TAP3_CTL,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Write reg 0x%X faill", REGISTER_TAP3_CTL);	
        }else{
        	PRINT_INFO("Write tap3_force: 0x%x", RegRead_t);
        }
	Into_DeepSleep_fun();
	mutex_unlock(&snt8100fsr_g->ap_lock);
}
void grip_tap1_duration_func(int val){
	int ret;
	uint16_t RegRead_t;
	if(grip_status_g->G_EN == 0){
		PRINT_INFO("Skip setting when grip off");
		return;
	}
	MUTEX_LOCK(&snt8100fsr_g->ap_lock);
	PRINT_INFO("val = %d", val);
	grip_status_g->G_TAP1_DUR = val;
	if(chip_reset_flag == 1){
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
	}
	Wait_Wake_For_RegW();
	RegRead_t = val;
	
	ret = write_register(snt8100fsr_g,
                 REGISTER_TAP1_DUR,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Write reg 0x%X faill", REGISTER_TAP1_DUR);	
        }else{
        	PRINT_INFO("Write tap1_dur: 0x%x", RegRead_t);
        }
	Into_DeepSleep_fun();
	mutex_unlock(&snt8100fsr_g->ap_lock);
}
void grip_tap2_duration_func(int val){
	int ret;
	uint16_t RegRead_t;
	if(grip_status_g->G_EN == 0){
		PRINT_INFO("Skip setting when grip off");
		return;
	}
	MUTEX_LOCK(&snt8100fsr_g->ap_lock);
	PRINT_INFO("val = %d", val);
	grip_status_g->G_TAP2_DUR = val;
	if(chip_reset_flag == 1){
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
	}
	Wait_Wake_For_RegW();
	RegRead_t = val;
	
	ret = write_register(snt8100fsr_g,
                 REGISTER_TAP2_DUR,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Write reg 0x%X faill", REGISTER_TAP2_DUR);	
        }else{
        	PRINT_INFO("Write tap2_dur: 0x%x", RegRead_t);
        }
	Into_DeepSleep_fun();
	mutex_unlock(&snt8100fsr_g->ap_lock);
}
void grip_tap3_duration_func(int val){
	int ret;
	uint16_t RegRead_t;
	if(grip_status_g->G_EN == 0){
		PRINT_INFO("Skip setting when grip off");
		return;
	}
	MUTEX_LOCK(&snt8100fsr_g->ap_lock);
	PRINT_INFO("val = %d", val);
	grip_status_g->G_TAP3_DUR = val;
	if(chip_reset_flag == 1){
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
	}
	Wait_Wake_For_RegW();
	RegRead_t = val;
	
	ret = write_register(snt8100fsr_g,
                 REGISTER_TAP3_DUR,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Write reg 0x%X faill", REGISTER_TAP3_DUR);	
        }else{
        	PRINT_INFO("Write tap3_dur: 0x%x", RegRead_t);
        }
	Into_DeepSleep_fun();
	mutex_unlock(&snt8100fsr_g->ap_lock);
}

void grip_tap1_fup_force_func(int val){
	int ret;
	uint16_t RegRead_t;
	if(grip_status_g->G_EN == 0){
		PRINT_INFO("Skip setting when grip off");
		return;
	}
	MUTEX_LOCK(&snt8100fsr_g->ap_lock);
	PRINT_INFO("val = %d", val);
	grip_status_g->G_TAP1_FUP_FORCE = val;
	if(chip_reset_flag == 1){
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
	}
	Wait_Wake_For_RegW();
	RegRead_t = val;
	
	ret = write_register(snt8100fsr_g,
                 REGISTER_TAP1_FUP_FORCE,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Write reg 0x%X faill", REGISTER_TAP1_FUP_FORCE);	
        }else{
        	PRINT_INFO("Write tap1_fup_force: 0x%x", RegRead_t);
        }
	Into_DeepSleep_fun();
	mutex_unlock(&snt8100fsr_g->ap_lock);
}

void grip_tap2_fup_force_func(int val){
	int ret;
	uint16_t RegRead_t;
	if(grip_status_g->G_EN == 0){
		PRINT_INFO("Skip setting when grip off");
		return;
	}
	MUTEX_LOCK(&snt8100fsr_g->ap_lock);
	PRINT_INFO("val = %d", val);
	grip_status_g->G_TAP2_FUP_FORCE = val;
	if(chip_reset_flag == 1){
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
	}
	Wait_Wake_For_RegW();
	RegRead_t = val;
	
	ret = write_register(snt8100fsr_g,
                 REGISTER_TAP2_FUP_FORCE,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Write reg 0x%X faill", REGISTER_TAP2_FUP_FORCE);	
        }else{
        	PRINT_INFO("Write tap2_fup_force: 0x%x", RegRead_t);
        }
	Into_DeepSleep_fun();
	mutex_unlock(&snt8100fsr_g->ap_lock);
}

void grip_tap3_fup_force_func(int val){
	int ret;
	uint16_t RegRead_t;
	if(grip_status_g->G_EN == 0){
		PRINT_INFO("Skip setting when grip off");
		return;
	}
	MUTEX_LOCK(&snt8100fsr_g->ap_lock);
	PRINT_INFO("val = %d", val);
	grip_status_g->G_TAP3_FUP_FORCE = val;
	if(chip_reset_flag == 1){
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
	}
	Wait_Wake_For_RegW();
	RegRead_t = val;
	
	ret = write_register(snt8100fsr_g,
                 REGISTER_TAP3_FUP_FORCE,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Write reg 0x%X faill", REGISTER_TAP3_FUP_FORCE);	
        }else{
        	PRINT_INFO("Write tap3_fup_force: 0x%x", RegRead_t);
        }
	Into_DeepSleep_fun();
	mutex_unlock(&snt8100fsr_g->ap_lock);
}
void grip_swipe1_velocity_func(int val){
	int ret;
	uint16_t RegRead_t = 0;
	if(grip_status_g->G_EN == 0){
		PRINT_INFO("Skip setting when grip off");
		return;
	}
	MUTEX_LOCK(&snt8100fsr_g->ap_lock);
	PRINT_INFO("val = %d", val);
	grip_status_g->G_SWIPE1_VELOCITY = val;
	if(chip_reset_flag == 1){
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
	}
	Wait_Wake_For_RegW();
	ret = read_register(snt8100fsr_g,
                 REGISTER_SWIPE1_CTL,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Read reg 0x%X faill", REGISTER_SWIPE1_CTL);	
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
        }else{
        	PRINT_INFO("Read Swipe1_Velocity: %x", RegRead_t);
        }
		
	RegRead_t = (val & 0x00FF) | (RegRead_t & 0xFF00);

	ret = write_register(snt8100fsr_g,
                 REGISTER_SWIPE1_CTL,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Write reg 0x%X faill", REGISTER_SWIPE1_CTL);	
        }else{
        	PRINT_INFO("Write Swipe1_Velocity: 0x%x", RegRead_t);
        }
	Into_DeepSleep_fun();
	mutex_unlock(&snt8100fsr_g->ap_lock);
}
void grip_swipe2_velocity_func(int val){
	int ret;
	uint16_t RegRead_t = 0;
	if(grip_status_g->G_EN == 0){
		PRINT_INFO("Skip setting when grip off");
		return;
	}
	MUTEX_LOCK(&snt8100fsr_g->ap_lock);
	PRINT_INFO("val = %d", val);
	grip_status_g->G_SWIPE2_VELOCITY = val;
	if(chip_reset_flag == 1){
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
	}
	Wait_Wake_For_RegW();
	ret = read_register(snt8100fsr_g,
                 REGISTER_SWIPE2_CTL,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Read reg 0x%X faill", REGISTER_SWIPE2_CTL);	
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
        }else{
        	PRINT_INFO("Read Swipe2_Velocity: %x", RegRead_t);
        }
		
	RegRead_t = (val & 0x00FF) | (RegRead_t & 0xFF00);

	ret = write_register(snt8100fsr_g,
                 REGISTER_SWIPE2_CTL,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Write reg 0x%X faill", REGISTER_SWIPE2_CTL);	
        }else{
        	PRINT_INFO("Write Swipe2_Velocity: 0x%x", RegRead_t);
        }
	Into_DeepSleep_fun();
	mutex_unlock(&snt8100fsr_g->ap_lock);
}
void grip_swipe3_velocity_func(int val){
	int ret;
	uint16_t RegRead_t = 0;
	if(grip_status_g->G_EN == 0){
		PRINT_INFO("Skip setting when grip off");
		return;
	}
	MUTEX_LOCK(&snt8100fsr_g->ap_lock);
	PRINT_INFO("val = %d", val);
	grip_status_g->G_SWIPE3_VELOCITY = val;
	if(chip_reset_flag == 1){
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
	}
	Wait_Wake_For_RegW();
	ret = read_register(snt8100fsr_g,
                 REGISTER_SWIPE3_CTL,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Read reg 0x%X faill", REGISTER_SWIPE3_CTL);	
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
        }else{
        	PRINT_INFO("Read Swipe3_Velocity: %x", RegRead_t);
        }
		
	RegRead_t = (val & 0x00FF) | (RegRead_t & 0xFF00);

	ret = write_register(snt8100fsr_g,
                 REGISTER_SWIPE3_CTL,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Write reg 0x%X faill", REGISTER_SWIPE3_CTL);	
        }else{
        	PRINT_INFO("Write Swipe3_Velocity: 0x%x", RegRead_t);
        }
	Into_DeepSleep_fun();
	mutex_unlock(&snt8100fsr_g->ap_lock);
}

void grip_swipe1_len_func(int val){
	int ret;
	uint16_t RegRead_t = 0;
	if(grip_status_g->G_EN == 0){
		PRINT_INFO("Skip setting when grip off");
		return;
	}
	MUTEX_LOCK(&snt8100fsr_g->ap_lock);
	PRINT_INFO("val = %d", val);
	grip_status_g->G_SWIPE1_LEN = val;
	if(chip_reset_flag == 1){
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
	}
	Wait_Wake_For_RegW();
	
	ret = read_register(snt8100fsr_g,
                 REGISTER_SWIPE1_LEN,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Read reg 0x%X faill", REGISTER_SWIPE1_LEN);	
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
        }else{
        	PRINT_INFO("Read Swipe1_Len: %x", RegRead_t);
        }
		
	RegRead_t = val;

	ret = write_register(snt8100fsr_g,
                 REGISTER_SWIPE1_LEN,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Write reg 0x%X faill", REGISTER_SWIPE1_LEN);	
        }else{
        	PRINT_INFO("Write Swipe1_Len: 0x%x", RegRead_t);
        }
	Into_DeepSleep_fun();
	mutex_unlock(&snt8100fsr_g->ap_lock);
}

void grip_swipe2_len_func(int val){
	int ret;
	uint16_t RegRead_t = 0;
	if(grip_status_g->G_EN == 0){
		PRINT_INFO("Skip setting when grip off");
		return;
	}
	MUTEX_LOCK(&snt8100fsr_g->ap_lock);
	PRINT_INFO("val = %d", val);
	grip_status_g->G_SWIPE2_LEN = val;
	if(chip_reset_flag == 1){
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
	}
	Wait_Wake_For_RegW();
	
	ret = read_register(snt8100fsr_g,
                 REGISTER_SWIPE2_LEN,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Read reg 0x%X faill", REGISTER_SWIPE2_LEN);	
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
        }else{
        	PRINT_INFO("Read Swipe2_Len: %x", RegRead_t);
        }
		
	RegRead_t = val;

	ret = write_register(snt8100fsr_g,
                 REGISTER_SWIPE2_LEN,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Write reg 0x%X faill", REGISTER_SWIPE2_LEN);	
        }else{
        	PRINT_INFO("Write Swipe2_Len: 0x%x", RegRead_t);
        }
	Into_DeepSleep_fun();
	mutex_unlock(&snt8100fsr_g->ap_lock);
}

void grip_swipe3_len_func(int val){
	int ret;
	uint16_t RegRead_t = 0;
	if(grip_status_g->G_EN == 0){
		PRINT_INFO("Skip setting when grip off");
		return;
	}
	MUTEX_LOCK(&snt8100fsr_g->ap_lock);
	PRINT_INFO("val = %d", val);
	grip_status_g->G_SWIPE3_LEN = val;
	if(chip_reset_flag == 1){
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
	}
	Wait_Wake_For_RegW();
	
	ret = read_register(snt8100fsr_g,
                 REGISTER_SWIPE3_LEN,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Read reg 0x%X faill", REGISTER_SWIPE3_LEN);	
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
        }else{
        	PRINT_INFO("Read Swipe3_Len: %x", RegRead_t);
        }
		
	RegRead_t = val;

	ret = write_register(snt8100fsr_g,
                 REGISTER_SWIPE3_LEN,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Write reg 0x%X faill", REGISTER_SWIPE3_LEN);	
        }else{
        	PRINT_INFO("Write Swipe3_Len: 0x%x", RegRead_t);
        }
	Into_DeepSleep_fun();
	mutex_unlock(&snt8100fsr_g->ap_lock);
}

int squeeze_short_limit = 0;
void grip_squeeze_short_limit_func(int val){
	int ret;
	uint16_t RegRead_t = 0;
	if(grip_status_g->G_EN == 0){
		PRINT_INFO("Skip setting when grip off");
		return;
	}
	MUTEX_LOCK(&snt8100fsr_g->ap_lock);
	PRINT_INFO("val = %d", val);
	squeeze_short_limit = val;
	if(chip_reset_flag == 1){
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
	}
	Wait_Wake_For_RegW();
	ret = read_register(snt8100fsr_g,
                 REGISTER_SQUEEZE_DUR,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Read reg 0x%X faill", REGISTER_SQUEEZE_DUR);	
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
        }else{
        	PRINT_INFO("Read Squeeze_long_dur: %x", RegRead_t);
        }
	
	RegRead_t = (val/20) | (RegRead_t & 0xFF00);

	ret = write_register(snt8100fsr_g,
                 REGISTER_SQUEEZE_DUR,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Write reg 0x%X faill", REGISTER_SQUEEZE_DUR);	
        }else{
        	PRINT_INFO("Write Squeeze_long_dur: 0x%x", RegRead_t);
        }
	Into_DeepSleep_fun();
	mutex_unlock(&snt8100fsr_g->ap_lock);
}

void grip_squeeze_short_dur_func(int val){
	int ret;
	uint16_t RegRead_t = 0;
	if(grip_status_g->G_EN == 0){
		PRINT_INFO("Skip setting when grip off");
		return;
	}
	MUTEX_LOCK(&snt8100fsr_g->ap_lock);
	PRINT_INFO("val = %d", val);
	grip_status_g->G_SQUEEZE_SHORT = val;
	if(chip_reset_flag == 1){
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
	}
	Wait_Wake_For_RegW();
	
	ret = read_register(snt8100fsr_g,
                 REGISTER_SQUEEZE_DUR,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Read reg 0x%X faill", REGISTER_SQUEEZE_DUR);	
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
        }else{
        	PRINT_INFO("Read Squeeze_short_dur: %x", RegRead_t);
        }
	
	RegRead_t = ((val/20) << 8) | (RegRead_t & 0x00FF);

	ret = write_register(snt8100fsr_g,
                 REGISTER_SQUEEZE_DUR,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Write reg 0x%X faill", REGISTER_SQUEEZE_DUR);	
        }else{
        	PRINT_INFO("Write Squeeze_short_dur: 0x%x", RegRead_t);
        }
	Into_DeepSleep_fun();
	mutex_unlock(&snt8100fsr_g->ap_lock);
}

void grip_squeeze_long_dur_func(int val){
	//nt ret;
	//uint16_t RegRead_t = 0;
	if(grip_status_g->G_EN == 0){
		PRINT_INFO("Skip setting when grip off");
		return;
	}
	MUTEX_LOCK(&snt8100fsr_g->ap_lock);
	PRINT_INFO("val = %d", val);
	grip_status_g->G_SQUEEZE_LONG = val;
	/*
	Wait_Wake_For_RegW();
	
	ret = read_register(snt8100fsr_g,
                 REGISTER_SQUEEZE_DUR,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Read reg 0x%X faill", REGISTER_SQUEEZE_DUR);	
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
        }else{
        	//PRINT_INFO("Read Squeeze_long_dur: %x", RegRead_t);
        }
	
	RegRead_t = (val/20) | (RegRead_t & 0xFF00);

	ret = write_register(snt8100fsr_g,
                 REGISTER_SQUEEZE_DUR,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Write reg 0x%X faill", REGISTER_SQUEEZE_DUR);	
        }else{
        	//PRINT_INFO("Write Squeeze_long_dur: 0x%x", RegRead_t);
        }
	Into_DeepSleep_fun();
	*/
	mutex_unlock(&snt8100fsr_g->ap_lock);
}

void grip_squeeze_up_rate_func(int val){
	int ret;
	uint16_t RegRead_t = 0;
	if(grip_status_g->G_EN == 0){
		PRINT_INFO("Skip setting when grip off");
		return;
	}
	MUTEX_LOCK(&snt8100fsr_g->ap_lock);
	PRINT_INFO("val = %d", val);
	grip_status_g->G_SQUEEZE_UP_RATE= val;
	if(chip_reset_flag == 1){
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
	}
	Wait_Wake_For_RegW();
	
	ret = read_register(snt8100fsr_g,
                 REGISTER_SQUEEZE_UP,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Read reg 0x%X faill", REGISTER_SQUEEZE_UP);	
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
        }else{
        	PRINT_INFO("Read Squeeze_short_dur: %x", RegRead_t);
        }
	
	RegRead_t = (val << 8) | (RegRead_t & 0x00FF);

	ret = write_register(snt8100fsr_g,
                 REGISTER_SQUEEZE_UP,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Write reg 0x%X faill", REGISTER_SQUEEZE_UP);	
        }else{
        	PRINT_INFO("Write Squeeze_up_rate: 0x%x", RegRead_t);
        }
	Into_DeepSleep_fun();
	mutex_unlock(&snt8100fsr_g->ap_lock);
}

void grip_squeeze_up_total_func(int val){
	int ret;
	uint16_t RegRead_t = 0;
	if(grip_status_g->G_EN == 0){
		PRINT_INFO("Skip setting when grip off");
		return;
	}
	MUTEX_LOCK(&snt8100fsr_g->ap_lock);
	PRINT_INFO("val = %d", val);
	grip_status_g->G_SQUEEZE_UP_TOTAL= val;
	if(chip_reset_flag == 1){
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
	}
	Wait_Wake_For_RegW();
	
	ret = read_register(snt8100fsr_g,
                 REGISTER_SQUEEZE_UP,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Read reg 0x%X faill", REGISTER_SQUEEZE_UP);	
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
        }else{
        	PRINT_INFO("Read Squeeze_short_dur: %x", RegRead_t);
        }
	
	RegRead_t = val | (RegRead_t & 0xFF00);

	ret = write_register(snt8100fsr_g,
                 REGISTER_SQUEEZE_UP,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Write reg 0x%X faill", REGISTER_SQUEEZE_UP);	
        }else{
        	PRINT_INFO("Write Squeeze_up_total: 0x%x", RegRead_t);
        }
	Into_DeepSleep_fun();
	mutex_unlock(&snt8100fsr_g->ap_lock);
}

void grip_squeeze_drop_rate_func(int val){
	int ret;
	uint16_t RegRead_t = 0;
	if(grip_status_g->G_EN == 0){
		PRINT_INFO("Skip setting when grip off");
		return;
	}
	MUTEX_LOCK(&snt8100fsr_g->ap_lock);
	PRINT_INFO("val = %d", val);
	grip_status_g->G_SQUEEZE_DROP_RATE= val;
	if(chip_reset_flag == 1){
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
	}
	Wait_Wake_For_RegW();
	
	ret = read_register(snt8100fsr_g,
                 REGISTER_SQUEEZE_DROP,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Read reg 0x%X faill", REGISTER_SQUEEZE_DROP);	
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
        }else{
        	PRINT_INFO("Read Squeeze_short_dur: %x", RegRead_t);
        }
	
	RegRead_t = (val << 8) | (RegRead_t & 0x00FF);

	ret = write_register(snt8100fsr_g,
                 REGISTER_SQUEEZE_DROP,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Write reg 0x%X faill", REGISTER_SQUEEZE_DROP);	
        }else{
        	PRINT_INFO("Write Squeeze_drop_rate: 0x%x", RegRead_t);
        }
	Into_DeepSleep_fun();
	mutex_unlock(&snt8100fsr_g->ap_lock);
}

void grip_squeeze_drop_total_func(int val){
	int ret;
	uint16_t RegRead_t = 0;
	if(grip_status_g->G_EN == 0){
		PRINT_INFO("Skip setting when grip off");
		return;
	}
	MUTEX_LOCK(&snt8100fsr_g->ap_lock);
	PRINT_INFO("val = %d", val);
	grip_status_g->G_SQUEEZE_DROP_TOTAL= val;
	if(chip_reset_flag == 1){
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
	}
	Wait_Wake_For_RegW();
	
	ret = read_register(snt8100fsr_g,
                 REGISTER_SQUEEZE_DROP,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Read reg 0x%X faill", REGISTER_SQUEEZE_DROP);	
		mutex_unlock(&snt8100fsr_g->ap_lock);
		return;
        }else{
        	PRINT_INFO("Read Squeeze_short_dur: %x", RegRead_t);
        }
	
	RegRead_t = val | (RegRead_t & 0xFF00);

	ret = write_register(snt8100fsr_g,
                 REGISTER_SQUEEZE_DROP,
                 &RegRead_t);
        if(ret < 0) {
		PRINT_ERR("Write reg 0x%X faill", REGISTER_SQUEEZE_DROP);	
        }else{
        	PRINT_INFO("Write Squeeze_drop_total: 0x%x", RegRead_t);
        }
	Into_DeepSleep_fun();
	mutex_unlock(&snt8100fsr_g->ap_lock);
}

/**************** --- wrtie gesture function --- **************/

/* Recovery status after reset */
void grip_dump_status_func(struct work_struct *work){

	MUTEX_LOCK(&snt8100fsr_g->ap_lock);
	PRINT_INFO("Reset check: framework setting recovery");

	PRINT_INFO("Grip Status: EN:%d, RAW_EN:%d, DPC_EN:%d, SQ_EN:%d, SQ_F:%d, SQ_Short:%d, SQ_Long:%d",
		grip_status_g->G_EN, grip_status_g->G_RAW_EN, 
		grip_status_g->G_DPC_STATUS, grip_status_g->G_SQUEEZE_EN,
		grip_status_g->G_SQUEEZE_FORCE, grip_status_g->G_SQUEEZE_SHORT, 
		grip_status_g->G_SQUEEZE_LONG);
	PRINT_INFO("Grip Status: T1_EN:%d, T1_F:%d, T1_DUR:%d, T1_FUP:%d",
		grip_status_g->G_TAP1_EN, grip_status_g->G_TAP1_FORCE,
		grip_status_g->G_TAP1_DUR, grip_status_g->G_TAP1_FUP_FORCE);

	PRINT_INFO("Grip Status: T2_EN:%d, T2_F:%d, T2_DUR:%d, T2_FUP:%d",
		grip_status_g->G_TAP2_EN, grip_status_g->G_TAP2_FORCE,
		grip_status_g->G_TAP2_DUR, grip_status_g->G_TAP2_FUP_FORCE);
	
	PRINT_INFO("Grip Status: T3_EN:%d, T3_F:%d, T3_DUR:%d, T3_FUP:%d",
		grip_status_g->G_TAP3_EN, grip_status_g->G_TAP3_FORCE,
		grip_status_g->G_TAP3_DUR, grip_status_g->G_TAP3_FUP_FORCE);
	mutex_unlock(&snt8100fsr_g->ap_lock);

	
	if(grip_status_g->G_EN != 0){
		grip_enable_func(grip_status_g->G_EN);
	}
	if(grip_status_g->G_RAW_EN != 0){
		grip_raw_enable_func(grip_status_g->G_RAW_EN);
	}
	if(grip_status_g->G_SQUEEZE_EN != 0){
		grip_squeeze_enable_func(grip_status_g->G_SQUEEZE_EN);
	}
	if(grip_status_g->G_SQUEEZE_SHORT != 0){
		grip_squeeze_short_dur_func(grip_status_g->G_SQUEEZE_SHORT);
	}
	if(grip_status_g->G_SQUEEZE_LONG != 0){
		grip_squeeze_long_dur_func(grip_status_g->G_SQUEEZE_LONG);
	}
	if(squeeze_short_limit != 0){
		grip_squeeze_short_limit_func(squeeze_short_limit);
	}
	if(grip_status_g->G_SQUEEZE_FORCE != 0){
		grip_squeeze_force_func(grip_status_g->G_SQUEEZE_FORCE);
	}
	
	if(grip_status_g->G_SQUEEZE_UP_RATE != 0){
		grip_squeeze_up_rate_func(grip_status_g->G_SQUEEZE_UP_RATE);
	}
	if(grip_status_g->G_SQUEEZE_UP_TOTAL != 0){
		grip_squeeze_up_total_func(grip_status_g->G_SQUEEZE_UP_TOTAL);
	}
	if(grip_status_g->G_SQUEEZE_DROP_RATE != 0){
		grip_squeeze_drop_rate_func(grip_status_g->G_SQUEEZE_DROP_RATE);
	}
	if(grip_status_g->G_SQUEEZE_DROP_TOTAL != 0){
		grip_squeeze_drop_total_func(grip_status_g->G_SQUEEZE_DROP_TOTAL);
	}
	
	if(grip_status_g->G_TAP1_EN != 0){
		grip_tap1_enable_func(grip_status_g->G_TAP1_EN);
	}
	if(grip_status_g->G_TAP1_FORCE != 0){
		grip_tap1_force_func(grip_status_g->G_TAP1_FORCE);
	}
	if(grip_status_g->G_TAP1_FUP_FORCE != 0){
		grip_tap1_fup_force_func(grip_status_g->G_TAP1_FUP_FORCE);
	}
	if(grip_status_g->G_TAP1_DUR != 0){
		grip_tap1_duration_func(grip_status_g->G_TAP1_DUR);
	}
	if(grip_status_g->G_TAP2_EN != 0){
		grip_tap2_enable_func(grip_status_g->G_TAP2_EN);
	}
	if(grip_status_g->G_TAP2_FORCE != 0){
		grip_tap2_force_func(grip_status_g->G_TAP2_FORCE);
	}
	if(grip_status_g->G_TAP2_FUP_FORCE != 0){
		grip_tap2_fup_force_func(grip_status_g->G_TAP2_FUP_FORCE);
	}
	if(grip_status_g->G_TAP2_DUR != 0){
		grip_tap2_duration_func(grip_status_g->G_TAP2_DUR);
	}
	if(grip_status_g->G_TAP3_EN != 0){
		grip_tap3_enable_func(grip_status_g->G_TAP3_EN);
	}
	if(grip_status_g->G_TAP3_FORCE != 0){
		grip_tap3_force_func(grip_status_g->G_TAP3_FORCE);
	}
	if(grip_status_g->G_TAP3_FUP_FORCE != 0){
		grip_tap3_fup_force_func(grip_status_g->G_TAP3_FUP_FORCE);
	}
	if(grip_status_g->G_TAP3_DUR != 0){
		grip_tap3_duration_func(grip_status_g->G_TAP3_DUR);
	}
	if(grip_status_g->G_TAP_SENSE_EN!= 0){
		grip_tap_sense_enable_func(0);
		grip_tap_sense_enable_func(1);
	}

	/* Bar control, Health check and tap status*/
	PRINT_INFO("Reset check: Bar control, Health check and tap status ");
	Check_Scan_Bar_Control_func();
	Health_Check_Enable_No_Delay(0);
	Check_Tap_func();
	Into_DeepSleep_fun();
}

/*************** ASUS BSP Clay: ioctl +++ *******************/
#define ASUS_GRIP_SENSOR_DATA_SIZE 3
#define ASUS_GRIP_SENSOR_D1TEST_DATA_SIZE	784
#define ASUS_GRIP_SENSOR_NAME_SIZE 32
#define ASUS_GRIP_SENSOR_IOC_MAGIC                      ('L')///< Grip sensor ioctl magic number 
#define ASUS_GRIP_SENSOR_IOCTL_ONOFF           _IOR(ASUS_GRIP_SENSOR_IOC_MAGIC, 1, int)///< Grip sensor ioctl command - Set on/off
#define ASUS_GRIP_SENSOR_IOCTL_SET_FRAM_RATE           _IOW(ASUS_GRIP_SENSOR_IOC_MAGIC, 2, int)///< Grip sensor ioctl command - Set frame rate
#define ASUS_GRIP_SENSOR_IOCTL_SET_PRESSURE_THRESHOLD           _IOW(ASUS_GRIP_SENSOR_IOC_MAGIC, 3, int)///< Grip sensor ioctl command - Set pressure threshold
#define ASUS_GRIP_SENSOR_IOCTL_GET_DEBUG_MODE           _IOW(ASUS_GRIP_SENSOR_IOC_MAGIC, 4, int)///< Grip sensor ioctl command - Set Debug Mode
#define ASUS_GRIP_SENSOR_IOCTL_DATA_READ           _IOW(ASUS_GRIP_SENSOR_IOC_MAGIC, 5, int[ASUS_GRIP_SENSOR_DATA_SIZE])///< Grip sensor ioctl command - Data Read
#define ASUS_GRIP_SENSOR_IOCTL_MODULE_NAME           _IOR(ASUS_GRIP_SENSOR_IOC_MAGIC, 6, char[ASUS_GRIP_SENSOR_NAME_SIZE])///< GRIP sensor ioctl command - Get module name
#define ASUS_GRIP_SENSOR_IOCTL_D1TEST_DATA_READ           _IOW(ASUS_GRIP_SENSOR_IOC_MAGIC, 7, unsigned char[ASUS_GRIP_SENSOR_D1TEST_DATA_SIZE])	///< Grip sensor ioctl command - D1Test Data Read
#define ASUS_GRIP_SENSOR_IOCTL_BAR0_TEST           _IOW(ASUS_GRIP_SENSOR_IOC_MAGIC, 8, int[2])	///< Grip sensor ioctl command - Bar Test
#define ASUS_GRIP_SENSOR_IOCTL_BAR1_TEST           _IOW(ASUS_GRIP_SENSOR_IOC_MAGIC, 9, int[2])	///< Grip sensor ioctl command - Bar Test
#define ASUS_GRIP_SENSOR_IOCTL_BAR2_TEST           _IOW(ASUS_GRIP_SENSOR_IOC_MAGIC, 10, int[2])	///< Grip sensor ioctl command - Bar Test
#define ASUS_GRIP_SENSOR_IOCTL_I2C_TEST           _IOW(ASUS_GRIP_SENSOR_IOC_MAGIC, 11, bool)	///< Grip sensor ioctl command - I2C Test
#define ASUS_GRIP_SENSOR_IOCTL_BAR0_STATUS           _IOW(ASUS_GRIP_SENSOR_IOC_MAGIC, 12, bool)	///< Grip sensor ioctl command - Bar Status
#define ASUS_GRIP_SENSOR_IOCTL_BAR1_STATUS           _IOW(ASUS_GRIP_SENSOR_IOC_MAGIC, 13, bool)	///< Grip sensor ioctl command - Bar Status
#define ASUS_GRIP_SENSOR_IOCTL_BAR2_STATUS           _IOW(ASUS_GRIP_SENSOR_IOC_MAGIC, 14, bool)	///< Grip sensor ioctl command - Bar Status
#define ASUS_GRIP_SENSOR_IOCTL_BAR_TEST_FORCE           _IOW(ASUS_GRIP_SENSOR_IOC_MAGIC, 15, int)	///< Grip sensor ioctl command - Bar Test Force value
#define ASUS_GRIP_SENSOR_IOCTL_BAR_TEST_TOLERANCE           _IOW(ASUS_GRIP_SENSOR_IOC_MAGIC, 16, int)	///< Grip sensor ioctl command - Bar Test tolerance %
#define ASUS_GRIP_SENSOR_IOCTL_SWITCH_ONOFF           _IOW(ASUS_GRIP_SENSOR_IOC_MAGIC, 17, int)	///< Grip sensor ioctl command - Bar Test Force value
#define ASUS_GRIP_SENSOR_IOCTL_GET_ONOFF           _IOW(ASUS_GRIP_SENSOR_IOC_MAGIC, 18, bool)	///< Grip sensor ioctl command - Bar Test tolerance % 

int SntSensor_miscOpen(struct inode *inode, struct file *file)
{
  PRINT_INFO("Clay: misc test");
#if 0
  int ret;

  if(!snt8100fsr_g){
    PRINT_CRIT("%s: null pointer, probe might not finish!", __func__);
  }
  
  PRINT_FUNC();
  // We don't mutex lock here, due to write_register locking
  
  PRINT_DEBUG("Setting frame rate to %d",
	      snt8100fsr_g->suspended_frame_rate);
  ret = write_register(snt8100fsr_g,
		       MAX_REGISTER_ADDRESS,
		       &snt8100fsr_g->suspended_frame_rate);
  if (ret) {
    PRINT_CRIT("write_register(REGISTER_FRAME_RATE) failed");
  }

  PRINT_DEBUG("done");
#endif
  return 0;
}

int SntSensor_miscRelease(struct inode *inode, struct file *file)
{
  int ret;
  if(!snt8100fsr_g){
    PRINT_CRIT("%s: null pointer, probe might not finish!", __func__);
  }
  PRINT_FUNC();

  // We mutex lock here since we're calling sb_wake_device which never locks
  MUTEX_LOCK(&snt8100fsr_g->sb_lock);

  ret = sb_wake_device(snt8100fsr_g);

  if (ret) {
    PRINT_CRIT("sb_wake_device() failed");
    mutex_unlock(&snt8100fsr_g->sb_lock);
    return ret;
  }

  mutex_unlock(&snt8100fsr_g->sb_lock);
  PRINT_DEBUG("done");
  return 0;
}

static int d1test_size = 784;
extern struct sc_command *sc_cmd;
extern int snt_read_sc_rsp(struct snt8100fsr *snt8100fsr);
 long SntSensor_miscIoctl(struct file *file, unsigned int cmd, unsigned long arg)
{
  int ret = 0, i = 0;
  //int snt_onoff = 0;
  int snt_frame_rate = 0;
  int pressure_threshold = 0;
  bool l_debug_mode = false;
  int dataSNT[ASUS_GRIP_SENSOR_DATA_SIZE];
  char nameSNT[ASUS_GRIP_SENSOR_NAME_SIZE];
  unsigned char d1test_ioctl[784];
  int bar_test_result[2];
  int i2c_status, i2c_flag = 1, grip_en_status = 0, switch_en;
  int fpc_status = 1;
  switch (cmd) {
    /* onoff is done by open/release */
    #if 0
  case ASUS_GRIP_SENSOR_IOCTL_ONOFF:
    ret = copy_from_user(&snt_onoff, (int __user*)arg, sizeof(snt_onoff));
    if( ret < 0) {
      PRINT_CRIT("%s: cmd = ONOFF, copy_from_user error(%d)\n", __func__, ret);
      goto end;
    }
    break;
    #endif
  case ASUS_GRIP_SENSOR_IOCTL_SET_FRAM_RATE:
    ret = copy_from_user(&snt_frame_rate, (int __user*)arg, sizeof(snt_frame_rate));
    if( ret < 0) {
      PRINT_CRIT("%s: cmd = SET_FRAM_RATE, copy_from_user error(%d)\n", __func__, snt8100fsr_g->frame_rate);
      goto end;
    }
    snt8100fsr_g->frame_rate = snt_frame_rate;
    break;
  case ASUS_GRIP_SENSOR_IOCTL_SET_PRESSURE_THRESHOLD:
    ret = copy_from_user(&pressure_threshold, (int __user*)arg, sizeof(pressure_threshold));
    if( ret < 0) {
      PRINT_CRIT("%s: cmd = SET_PRESSURE_THRESHOLD, copy_from_user error(%d)\n", __func__, snt8100fsr_g->frame_rate);
      goto end;
    }
    snt8100fsr_g->pressure_threshold = pressure_threshold;
    break;
  case ASUS_GRIP_SENSOR_IOCTL_GET_DEBUG_MODE:
    if (g_debugMode) {
      l_debug_mode = 1;
    } else{
      l_debug_mode = 0;
    }
    PRINT_INFO("%s: cmd = DEBUG_MODE, result = %d\n", __func__, l_debug_mode);
    ret = copy_to_user((int __user*)arg, &l_debug_mode, sizeof(l_debug_mode));
    break;
  case ASUS_GRIP_SENSOR_IOCTL_DATA_READ:
    dataSNT[0] = snt8100fsr_g->frame_rate;
    dataSNT[1] = snt8100fsr_g->pressure_threshold;
    dataSNT[2] = snt8100fsr_g->suspended_frame_rate;
    PRINT_INFO("%s: cmd = DATA_READ, data[0] = %d, data[1] = %d, data[2] = %d\n"
	       , __func__, dataSNT[0], dataSNT[1], dataSNT[2]);
	ret = copy_to_user((int __user*)arg, &dataSNT, sizeof(dataSNT));
    break;
  case ASUS_GRIP_SENSOR_IOCTL_D1TEST_DATA_READ:
	    MUTEX_LOCK(&snt8100fsr_g->sb_lock);

	    ret = snt_read_sc_rsp(snt8100fsr_g);
        if(log_d1test_file != NULL) {
		//Clear char array
		memset(d1test_ioctl, 0, sizeof(d1test_ioctl));
		//each sc_cmd->data[] is 4bytes
		for(i = 0; i < (d1test_size/4); i++){
			strcat(&d1test_ioctl[4*i],  (unsigned char*)&sc_cmd->data[i]);
			PRINT_DEBUG("Clay_IOCTL: data[%d]: %s", i, (unsigned char*)&sc_cmd->data[i]);
		}
	}
	mutex_unlock(&snt8100fsr_g->sb_lock);
		
	ret = copy_to_user((int __user*)arg, &d1test_ioctl, sizeof(d1test_ioctl));
	PRINT_DEBUG("Clay_IOCTL: done");
    break;
  case ASUS_GRIP_SENSOR_IOCTL_MODULE_NAME:
    snprintf(nameSNT, sizeof(nameSNT), "%s", SYSFS_NAME);
    PRINT_INFO("%s: cmd = MODULE_NAME, name = %s\n", __func__, nameSNT);
    ret = copy_to_user((int __user*)arg, &nameSNT, sizeof(nameSNT));
    break;
  case ASUS_GRIP_SENSOR_IOCTL_BAR0_TEST:
  	bar_test_result[0] = 0;
 	bar_test_result[1] = 0;
  	for (i = 0; i < snt8100fsr_g->track_reports_count; i++) {
		if(snt8100fsr_g->track_reports[i].bar_id == 0 && snt8100fsr_g->track_reports[i].force_lvl != 0){
			//center 56~166
			if(snt8100fsr_g->track_reports[i].center > 0 && snt8100fsr_g->track_reports[i].center < 223 ){
				bar_test_result[1] = snt8100fsr_g->track_reports[i].force_lvl;
				bar_test_result[0] = check_report_force(bar_test_result[1]);
				print_current_report(i);
				break;
			}
		}
    	}
	PRINT_INFO("Bar_0 Result = %d, Force = %d", bar_test_result[0], bar_test_result[1]);
	ret = copy_to_user((int __user*)arg, &bar_test_result, sizeof(bar_test_result));
  	break;
  case ASUS_GRIP_SENSOR_IOCTL_BAR1_TEST:
  	bar_test_result[0] = 0;
 	bar_test_result[1] = 0;
  	for (i = 0; i < snt8100fsr_g->track_reports_count; i++) {
		if(snt8100fsr_g->track_reports[i].bar_id == 1 && snt8100fsr_g->track_reports[i].force_lvl != 0){
			//center 92~604
			if(snt8100fsr_g->track_reports[i].center > 0 && snt8100fsr_g->track_reports[i].center < 682 ){
				bar_test_result[1] = snt8100fsr_g->track_reports[i].force_lvl;
				bar_test_result[0] = check_report_force(bar_test_result[1]);
				print_current_report(i);
				break;
			}
		}
    	}
	PRINT_INFO("Bar_1 Result = %d, Force = %d", bar_test_result[0], bar_test_result[1]);
	ret = copy_to_user((int __user*)arg, &bar_test_result, sizeof(bar_test_result));
  	break;
  case ASUS_GRIP_SENSOR_IOCTL_BAR2_TEST:
  	bar_test_result[0] = 0;
 	bar_test_result[1] = 0;
  	for (i = 0; i < snt8100fsr_g->track_reports_count; i++) {
		if(snt8100fsr_g->track_reports[i].bar_id == 2 && snt8100fsr_g->track_reports[i].force_lvl != 0){
			//center 77 ~384
			if(snt8100fsr_g->track_reports[i].center > 0 && snt8100fsr_g->track_reports[i].center < 461 ){
				bar_test_result[1] = snt8100fsr_g->track_reports[i].force_lvl;
				bar_test_result[0] = check_report_force(bar_test_result[1]);
				print_current_report(i);
				break;
			}
		}
    	}
	PRINT_INFO("Bar_2 Result = %d, Force = %d", bar_test_result[0], bar_test_result[1]);
	ret = copy_to_user((int __user*)arg, &bar_test_result, sizeof(bar_test_result));
  	break;
  case ASUS_GRIP_SENSOR_IOCTL_BAR_TEST_FORCE:
	ret = copy_from_user(&bar_test_force, (int __user*)arg, sizeof(bar_test_force));
	if( ret < 0) {
		PRINT_CRIT("%s: cmd = ASUS_GRIP_SENSOR_IOCTL_BAR_TEST_FORCE, copy_from_user error(%d)\n", __func__, bar_test_force);
      		goto end;
	}
	PRINT_INFO("set bar_test force = %d", bar_test_force);
	break;
  case ASUS_GRIP_SENSOR_IOCTL_BAR_TEST_TOLERANCE:
	ret = copy_from_user(&bar_test_tolerance, (int __user*)arg, sizeof(bar_test_tolerance));
	if( ret < 0) {
		PRINT_CRIT("%s: cmd = ASUS_GRIP_SENSOR_IOCTL_BAR_TEST_TOLERANCE, copy_from_user error(%d)\n", __func__, bar_test_tolerance);
      		goto end;
	}
	PRINT_INFO("set bar_test tolerance = %d", bar_test_tolerance);
	break;
  case ASUS_GRIP_SENSOR_IOCTL_I2C_TEST:
        Wait_Wake_For_RegW();
	ret = read_register (snt8100fsr_g,
                 REGISTER_FRAME_RATE,
                 &i2c_status);
        if(ret < 0) {
		PRINT_ERR("Grip I2c no ack");	
		i2c_flag = 0;
	}
	PRINT_INFO("I2C status = %d", i2c_flag);
	ret = copy_to_user((int __user*)arg, &i2c_flag, sizeof(i2c_flag));
  	break;
  case ASUS_GRIP_SENSOR_IOCTL_GET_ONOFF:
	Wait_Wake_For_RegW();
	ret = read_register (snt8100fsr_g,
		REGISTER_ENABLE,
		 &grip_en_status);
	if(ret < 0) {
		PRINT_ERR("Grip I2c no ack");	
		i2c_flag = 0;
	}
	PRINT_INFO("Grip En = %d", grip_en_status);
	ret = copy_to_user((int __user*)arg, &grip_en_status, sizeof(i2c_flag));
  	break;
  case ASUS_GRIP_SENSOR_IOCTL_SWITCH_ONOFF:
	ret = copy_from_user(&switch_en, (int __user*)arg, sizeof(switch_en));
	if( ret < 0) {
		PRINT_CRIT("%s: cmd = ASUS_GRIP_SENSOR_IOCTL_SWITCH_ONOFF, copy_from_user error(%d)\n", __func__, bar_test_tolerance);
      		goto end;
	}
	
	Wait_Wake_For_RegW();
	ret = write_register(snt8100fsr_g,
                 REGISTER_ENABLE,
                 &switch_en);
        if(ret < 0)
		PRINT_ERR("Grip register_enable write fail");
        	
	PRINT_INFO("set reg_en = %d", switch_en);
	break;
  case ASUS_GRIP_SENSOR_IOCTL_BAR0_STATUS:	
	Wait_Wake_For_RegW();
  	if(Health_Check(0x0003)!=0){
		fpc_status = 0;
  	}
	ret = copy_to_user((int __user*)arg, &fpc_status, sizeof(fpc_status));
  	break;
  case ASUS_GRIP_SENSOR_IOCTL_BAR1_STATUS:	
	Wait_Wake_For_RegW();
  	if(Health_Check(0x003C)!=0){
		fpc_status = 0;
  	}
	ret = copy_to_user((int __user*)arg, &fpc_status, sizeof(fpc_status));
  	break;
  case ASUS_GRIP_SENSOR_IOCTL_BAR2_STATUS:	
	Wait_Wake_For_RegW();
  	if(Health_Check(0x01C0)!=0){
		fpc_status = 0;
  	}
	ret = copy_to_user((int __user*)arg, &fpc_status, sizeof(fpc_status));
  	break;
  default:
    ret = -1;
    PRINT_INFO("%s: default\n", __func__);
  }
 end:
  return ret;
}

int sntSensor_miscRegister(void)
{
  int rtn = 0;
  /* in sys/class/misc/ */
  rtn = misc_register(&sentons_snt_misc);
  if (rtn < 0) {
    PRINT_CRIT("[%s] Unable to register misc deive\n", __func__);
    misc_deregister(&sentons_snt_misc);
  }
  return rtn;
}
/*************** ASUS BSP Clay: ioctl --- *******************/

/*************** ASUS BSP Clay: proc file +++ *******************/
/*+++BSP Clay proc asusGripDebug Interface+++*/
int asusGripDebug_proc_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "%d\n", g_debugMode);
	PRINT_INFO("Rst GPIO133: %d",gpio_get_value(RST_GPIO));
	return 0;
}
int asusGripDebug_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, asusGripDebug_proc_read, NULL);
}

ssize_t asusGripDebug_proc_write(struct file *filp, const char __user *buff,
		size_t len, loff_t *data)
{
	int val, ret;
	char messages[256];
	Wait_Wake_For_RegW();
	if (len > 256) {
		len = 256;
	}
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}

	val = (int)simple_strtol(messages, NULL, 10);
	g_debugMode = val;
		PRINT_INFO("g_debugMode=%d\n", g_debugMode);
	if(g_debugMode >= 100 || g_debugMode < 0){
    		ret = write_register(snt8100fsr_g,
                         REGISTER_FRAME_RATE,
                         &snt8100fsr_g->frame_rate);
		PRINT_INFO("write reg=%x,  frame_rate=%d\n", REGISTER_FRAME_RATE, snt8100fsr_g->frame_rate);
	}else{
    		ret = write_register(snt8100fsr_g,
                         REGISTER_FRAME_RATE,
                         &g_debugMode);
		PRINT_INFO("write reg=%x,  frame_rate=%d\n", REGISTER_FRAME_RATE, g_debugMode);
	}
	return len;
}
void create_asusGripDebug_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  asusGripDebug_proc_open,
		.write = asusGripDebug_proc_write,
		.read = seq_read,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = proc_create("driver/Grip_Debug_Flag", 0666, NULL, &proc_fops);

	if (!proc_file) {
		PRINT_CRIT("[Proc]%s failed!\n", __FUNCTION__);
	}
	return;
}
/*Calibration File read operation */
int Calibration_raw_data_proc_read(struct seq_file *buf, void *v)
{
	int ret,  i;

	
	    MUTEX_LOCK(&snt8100fsr_g->sb_lock);

	    ret = snt_read_sc_rsp(snt8100fsr_g);
        if(log_d1test_file != NULL) {
		//each sc_cmd->data[] is 4bytes
		for(i = 0; i < (d1test_size/4); i++){
			PRINT_INFO("Clay_IOCTL: data[%d]: %s", i, (unsigned char*)&sc_cmd->data[i]);	
			seq_printf(buf, "%s",  (unsigned char*)&sc_cmd->data[i]);
		}
	}
	mutex_unlock(&snt8100fsr_g->sb_lock);
		
	PRINT_INFO("Clay_proc_data: done");
	
	return 0;
}

int Calibration_raw_data_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, Calibration_raw_data_proc_read, NULL);
}

void create_Calibration_raw_data_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  Calibration_raw_data_proc_open,
		.read = seq_read,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = proc_create("driver/Grip_D1test", 0444, NULL, &proc_fops);
	if (!proc_file) {
		PRINT_ERR("%s failed!\n", __func__);
	}
	return;
}
/*Calibration File read operation*/

/* +++ BSP Clay proc i2c check +++ */
int GripI2cCheck_proc_read(struct seq_file *buf, void *v)
{
	int ret, i2c_status;
	bool flag = 1;
	Wait_Wake_For_RegW();
	ret = read_register (snt8100fsr_g,
                 REGISTER_FRAME_RATE,
                 &i2c_status);
        if(ret < 0) {
		PRINT_ERR("Grip I2c no ack");	
		flag = 0;
		goto Report;
	}
		
Report:
	if(flag == 1){
		seq_printf(buf, "%d\n", flag);
	}else{
		seq_printf(buf, "%d\n", flag);
	}
	return 0;
}
int GripI2cCheck_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, GripI2cCheck_proc_read, NULL);
}
void create_GripI2cCheck_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  GripI2cCheck_proc_open,
		.read = seq_read,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = proc_create("driver/Grip_I2c_Check", 0666, NULL, &proc_fops);

	if (!proc_file) {
		PRINT_CRIT("[Proc]%s failed!\n", __FUNCTION__);
	}
	return;
}
/* --- BSP Clay proc i2c check --- */
/* +++ BSP Clay proc FPC check +++ */

extern uint16_t Cal_init_addr[7];
extern uint16_t Cal_init_data[7];
ssize_t GripFPCCheck_proc_write(struct file *filp, const char __user *buff,
		size_t len, loff_t *data)
{
	int val, i = 0, ret;
	char messages[256];
	if (len > 256) {
		len = 256;
	}
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}

	val = (int)simple_strtol(messages, NULL, 10);
	if(B0_F_value == 0x3e && B1_F_value == 0x9b && B2_F_value == 0x5b){
		PRINT_INFO("Alreadly apply golen value");
		return len;
	}else if(val == 1){
		PRINT_INFO("Panel Uniqe ID changed");
		Wait_Wake_For_RegW();
		for(i = 0; i < 7; i++){
			ret = write_register(snt8100fsr_g, Cal_init_addr[i], &Cal_init_data[i]);
			if (ret) {
			    PRINT_CRIT("set 0x%x init failed", Cal_init_addr[i]);
			}
		}
		B0_F_value = 0x3e;
		B1_F_value = 0x9b;
		B2_F_value = 0x5b;
	}else{
		PRINT_INFO("Panel Uniqe ID doesn't change");
	}
	return len;
}

int GripFPCCheck_proc_read(struct seq_file *buf, void *v)
{
	//int ret;
	//Enable Health Check
	PRINT_INFO("Proc: FPC Check");
	/*
	Wait_Wake_For_RegW();
	ret = Health_Check(0xffff);
	if(ret < 0) {
		PRINT_ERR("Enable Health Check Failed");
		goto Report;
	}*/
	seq_printf(buf, "0x%x\n", FPC_value);
	//Into_DeepSleep_fun();
	return 0;
}
int GripFPCCheck_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, GripFPCCheck_proc_read, NULL);
}
void create_GripFPCCheck_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  GripFPCCheck_proc_open,
		.write = GripFPCCheck_proc_write,
		.read = seq_read,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = proc_create("driver/Grip_FPC_Check", 0666, NULL, &proc_fops);

	if (!proc_file) {
		PRINT_CRIT("[Proc]%s failed!\n", __FUNCTION__);
	}
	return;
}
/* +++ BSP Clay proc FPC check --- */
/* +++ BSP Clay proc Squeeze factory read +++ */
int GripSQ_Bar0_factory_proc_read(struct seq_file *buf, void *v)
{
	PRINT_INFO("Proc: Grip SQ Bar0 Factory %d", B0_F_value);
	seq_printf(buf, "%d\n", B0_F_value);
	return 0;
}
int GripSQ_Bar0_factory_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, GripSQ_Bar0_factory_proc_read, NULL);
}
void create_GripSQ_Bar0_factory_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  GripSQ_Bar0_factory_proc_open,
		.read = seq_read,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = proc_create("driver/Grip_SQ_B0_factor", 0666, NULL, &proc_fops);

	if (!proc_file) {
		PRINT_CRIT("[Proc]%s failed!\n", __FUNCTION__);
	}
	return;
}
int GripSQ_Bar1_factory_proc_read(struct seq_file *buf, void *v)
{
	PRINT_INFO("Proc: Grip SQ Bar1 Factory %d", B1_F_value);
	seq_printf(buf, "%d\n", B1_F_value);
	return 0;
}
int GripSQ_Bar1_factory_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, GripSQ_Bar1_factory_proc_read, NULL);
}
void create_GripSQ_Bar1_factory_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  GripSQ_Bar1_factory_proc_open,
		.read = seq_read,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = proc_create("driver/Grip_SQ_B1_factor", 0666, NULL, &proc_fops);

	if (!proc_file) {
		PRINT_CRIT("[Proc]%s failed!\n", __FUNCTION__);
	}
	return;
}
int GripSQ_Bar2_factory_proc_read(struct seq_file *buf, void *v)
{
	PRINT_INFO("Proc: Grip SQ Bar2 Factory %d", B2_F_value);
	seq_printf(buf, "%d\n", B2_F_value);
	return 0;
}
int GripSQ_Bar2_factory_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, GripSQ_Bar2_factory_proc_read, NULL);
}
void create_GripSQ_Bar2_factory_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  GripSQ_Bar2_factory_proc_open,
		.read = seq_read,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = proc_create("driver/Grip_SQ_B2_factor", 0666, NULL, &proc_fops);

	if (!proc_file) {
		PRINT_CRIT("[Proc]%s failed!\n", __FUNCTION__);
	}
	return;
}
/* +++ BSP Clay proc FPC check --- */
/* +++ BSP Clay Disable wake_lock and grip event +++ */
bool wake_lock_disable_flag = 0;;
int GripDisable_WakeLock_proc_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "wake_lock_evt_flag: %d\n", wake_lock_disable_flag);
	return 0;
}

ssize_t GripDisable_WakeLock_proc_write(struct file *filp, const char __user *buff,
		size_t len, loff_t *data)
{
	int val, ret, reg_en = 0;
	char messages[256];
	if (len > 256) {
		len = 256;
	}
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}
	val = (int)simple_strtol(messages, NULL, 10);
	wake_lock_disable_flag = val;
	if(wake_lock_disable_flag == 0){
		reg_en = 1;
	}else{
		wake_unlock(&(snt8100fsr_g->snt_wakelock));
	}
	/*
	ret = write_register(snt8100fsr_g,
                 REGISTER_ENABLE,
                 &reg_en);
        if(ret < 0) {
		PRINT_ERR("Grip register_enable write fail");
        }else{
        	PRINT_INFO("wake_lock_evt_flag = %d", wake_lock_disable_flag);
        }
	*/
	Wait_Wake_For_RegW();
	ret = read_register(snt8100fsr_g, REGISTER_ENABLE, &reg_en);
        if(ret < 0) {
		PRINT_ERR("Grip register_enable write fail");
        }else{
        	PRINT_INFO("reg_en = %d", reg_en);
        }	
    	
	return len;
}

int GripDisable_WakeLock_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, GripDisable_WakeLock_proc_read, NULL);
}

void create_GripDisable_WakeLock_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  GripDisable_WakeLock_proc_open,
		.write =  GripDisable_WakeLock_proc_write,
		.read = seq_read,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = proc_create("driver/Grip_Disable_WakeLock", 0666, NULL, &proc_fops);

	if (!proc_file) {
		PRINT_CRIT("[Proc]%s failed!\n", __FUNCTION__);
	}
	return;
}
/* --- BSP Clay R/W Temp register value --- */
/* +++ BSP Clay Frame Rate setting  +++ */
int Grip_frame_proc_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "%d\n", DPC_status_g->High);
	return 0;
}
ssize_t Grip_frame_proc_write(struct file *filp, const char __user *buff,
		size_t len, loff_t *data)
{
	int val;
	char messages[256];
	
	memset(messages, 0, sizeof(messages));
	if (len > 256) {
		len = 256;
	}
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}
	val = (int)simple_strtol(messages, NULL, 10);
	grip_frame_rate_func(val);
	return len;
}

int Grip_frame_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, Grip_frame_proc_read, NULL);
}

void create_Grip_frame_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  Grip_frame_proc_open,
		.write =  Grip_frame_proc_write,
		.read = seq_read,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = proc_create("driver/grip_frame_rate", 0666, NULL, &proc_fops);

	if (!proc_file) {
		PRINT_CRIT("[Proc]%s failed!\n", __FUNCTION__);
	}
	return;
}

/* --- BSP Clay Frame Rate setting --- */
//==============Enable Interface=============//
int Grip_raw_en_proc_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "%d\n", grip_status_g->G_RAW_EN);
	return 0;
}
ssize_t Grip_raw_en_proc_write(struct file *filp, const char __user *buff,
		size_t len, loff_t *data)
{
	int val;
	char messages[256];
	
	memset(messages, 0, sizeof(messages));
	if (len > 256) {
		len = 256;
	}
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}
	val = (int)simple_strtol(messages, NULL, 10);
	grip_raw_enable_func(val);
	return len;
}

int Grip_raw_en_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, Grip_raw_en_proc_read, NULL);
}

void create_Grip_raw_en_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  Grip_raw_en_proc_open,
		.write =  Grip_raw_en_proc_write,
		.read = seq_read,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = proc_create("driver/grip_raw_en", 0666, NULL, &proc_fops);

	if (!proc_file) {
		PRINT_CRIT("[Proc]%s failed!\n", __FUNCTION__);
	}
	return;
}

/* Sensor Grip enable +++ */
int Grip_en_proc_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "%d\n", grip_status_g->G_EN);
	return 0;
}
ssize_t Grip_en_proc_write(struct file *filp, const char __user *buff,
		size_t len, loff_t *data)
{
	int val;
	char messages[256];
	
	memset(messages, 0, sizeof(messages));
	if (len > 256) {
		len = 256;
	}
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}
	val = (int)simple_strtol(messages, NULL, 10);
	grip_enable_func(val);
	return len;
}

int Grip_en_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, Grip_en_proc_read, NULL);
}

void create_Grip_en_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  Grip_en_proc_open,
		.write =  Grip_en_proc_write,
		.read = seq_read,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = proc_create("driver/grip_en", 0666, NULL, &proc_fops);

	if (!proc_file) {
		PRINT_CRIT("[Proc]%s failed!\n", __FUNCTION__);
	}
	return;
}
//Squeeze Enable Interface
int GripSqueezeEn_proc_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "%d\n", grip_status_g->G_SQUEEZE_EN);
	
	return 0;
}

ssize_t GripSqueezeEn_proc_write(struct file *filp, const char __user *buff,
		size_t len, loff_t *data)
{
	int val;
	char messages[256];

	memset(messages, 0, sizeof(messages));
	if (len > 256) {
		len = 256;
	}
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}
	val = (int)simple_strtol(messages, NULL, 10);
	grip_squeeze_enable_func(val);
	return len;
}

int GripSqueezeEn_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, GripSqueezeEn_proc_read, NULL);
}

void create_GripSqueezeEn_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  GripSqueezeEn_proc_open,
		.write =  GripSqueezeEn_proc_write,
		.read = seq_read,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = proc_create("driver/grip_squeeze_en", 0666, NULL, &proc_fops);

	if (!proc_file) {
		PRINT_CRIT("[Proc]%s failed!\n", __FUNCTION__);
	}
	return;
}

//Tap Enable Interface
int GripTap1En_proc_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "%d\n", grip_status_g->G_TAP1_EN);
	return 0;
}

ssize_t GripTap1En_proc_write(struct file *filp, const char __user *buff,
		size_t len, loff_t *data)
{
	int val;
	char messages[256];

	memset(messages, 0, sizeof(messages));
	if (len > 256) {
		len = 256;
	}
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}
	val = (int)simple_strtol(messages, NULL, 10);
	grip_tap1_enable_func(val);
	return len;
}

int GripTap1En_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, GripTap1En_proc_read, NULL);
}

void create_GripTap1En_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  GripTap1En_proc_open,
		.write =  GripTap1En_proc_write,
		.read = seq_read,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = proc_create("driver/grip_tap1_en", 0666, NULL, &proc_fops);

	if (!proc_file) {
		PRINT_CRIT("[Proc]%s failed!\n", __FUNCTION__);
	}
	return;
}

int GripTap2En_proc_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "%d\n", grip_status_g->G_TAP2_EN);
	return 0;
}

ssize_t GripTap2En_proc_write(struct file *filp, const char __user *buff,
		size_t len, loff_t *data)
{
	int val;
	char messages[256];

	memset(messages, 0, sizeof(messages));
	if (len > 256) {
		len = 256;
	}
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}
	val = (int)simple_strtol(messages, NULL, 10);
	grip_tap2_enable_func(val);
	return len;
}

int GripTap2En_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, GripTap2En_proc_read, NULL);
}

void create_GripTap2En_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  GripTap2En_proc_open,
		.write =  GripTap2En_proc_write,
		.read = seq_read,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = proc_create("driver/grip_tap2_en", 0666, NULL, &proc_fops);

	if (!proc_file) {
		PRINT_CRIT("[Proc]%s failed!\n", __FUNCTION__);
	}
	return;
}

int GripTap3En_proc_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "%d\n", grip_status_g->G_TAP3_EN);
	return 0;
}

ssize_t GripTap3En_proc_write(struct file *filp, const char __user *buff,
		size_t len, loff_t *data)
{
	int val;
	char messages[256];

	memset(messages, 0, sizeof(messages));
	if (len > 256) {
		len = 256;
	}
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}
	val = (int)simple_strtol(messages, NULL, 10);
	grip_tap3_enable_func(val);
	return len;
}

int GripTap3En_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, GripTap3En_proc_read, NULL);
}

void create_GripTap3En_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  GripTap3En_proc_open,
		.write =  GripTap3En_proc_write,
		.read = seq_read,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = proc_create("driver/grip_tap3_en", 0666, NULL, &proc_fops);

	if (!proc_file) {
		PRINT_CRIT("[Proc]%s failed!\n", __FUNCTION__);
	}
	return;
}

//Tap Sense Enable  Interface
int GripTap_Sense_En_proc_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "%d\n",  grip_status_g->G_TAP_SENSE_SET);
	return 0;
}

ssize_t GripTap_Sense_En_proc_write(struct file *filp, const char __user *buff,
		size_t len, loff_t *data)
{
	int val;
	char messages[256];

	memset(messages, 0, sizeof(messages));
	if (len > 256) {
		len = 256;
	}
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}
	val = (int)simple_strtol(messages, NULL, 10);
	grip_tap_sense_enable_func(val);
	return len;
}

int GripTap_Sense_En_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, GripTap_Sense_En_proc_read, NULL);
}

void create_GripTap_Sense_En_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  GripTap_Sense_En_proc_open,
		.write =  GripTap_Sense_En_proc_write,
		.read = seq_read,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = proc_create("driver/grip_tap_sense_en", 0666, NULL, &proc_fops);

	if (!proc_file) {
		PRINT_CRIT("[Proc]%s failed!\n", __FUNCTION__);
	}
	return;
}

//Swipe Enable Interface
int Grip_Swipe1_En_proc_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "%d\n", grip_status_g->G_SWIPE1_EN);
	return 0;
}

ssize_t Grip_Swipe1_En_proc_write(struct file *filp, const char __user *buff,
		size_t len, loff_t *data)
{
	int val;
	char messages[256];
	
	memset(messages, 0, sizeof(messages));
	if (len > 256) {
		len = 256;
	}
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}
	val = (int)simple_strtol(messages, NULL, 10);
	grip_swipe1_enable_func(val);
	return len;
}

int Grip_Swipe1_En_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, Grip_Swipe1_En_proc_read, NULL);
}

void create_Grip_Swipe1_En_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  Grip_Swipe1_En_proc_open,
		.write =  Grip_Swipe1_En_proc_write,
		.read = seq_read,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = proc_create("driver/grip_swipe1_en", 0666, NULL, &proc_fops);

	if (!proc_file) {
		PRINT_CRIT("[Proc]%s failed!\n", __FUNCTION__);
	}
	return;
}

int Grip_Swipe2_En_proc_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "%d\n", grip_status_g->G_SWIPE2_EN);
	return 0;
}

ssize_t Grip_Swipe2_En_proc_write(struct file *filp, const char __user *buff,
		size_t len, loff_t *data)
{
	int val;
	char messages[256];
	
	memset(messages, 0, sizeof(messages));
	if (len > 256) {
		len = 256;
	}
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}
	val = (int)simple_strtol(messages, NULL, 10);
	grip_swipe2_enable_func(val);
	return len;
}

int Grip_Swipe2_En_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, Grip_Swipe2_En_proc_read, NULL);
}

void create_Grip_Swipe2_En_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  Grip_Swipe2_En_proc_open,
		.write =  Grip_Swipe2_En_proc_write,
		.read = seq_read,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = proc_create("driver/grip_swipe2_en", 0666, NULL, &proc_fops);

	if (!proc_file) {
		PRINT_CRIT("[Proc]%s failed!\n", __FUNCTION__);
	}
	return;
}

int Grip_Swipe3_En_proc_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "%d\n", grip_status_g->G_SWIPE3_EN);
	return 0;
}

ssize_t Grip_Swipe3_En_proc_write(struct file *filp, const char __user *buff,
		size_t len, loff_t *data)
{
	int val;
	char messages[256];
	
	memset(messages, 0, sizeof(messages));
	if (len > 256) {
		len = 256;
	}
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}
	val = (int)simple_strtol(messages, NULL, 10);
	grip_swipe3_enable_func(val);
	return len;
}

int Grip_Swipe3_En_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, Grip_Swipe3_En_proc_read, NULL);
}

void create_Grip_Swipe3_En_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  Grip_Swipe3_En_proc_open,
		.write =  Grip_Swipe3_En_proc_write,
		.read = seq_read,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = proc_create("driver/grip_swipe3_en", 0666, NULL, &proc_fops);

	if (!proc_file) {
		PRINT_CRIT("[Proc]%s failed!\n", __FUNCTION__);
	}
	return;
}
/* Sensor Event enable --- */
//==============Enable Interface=============//


//==========Gesture Thershold Interface=======//
/* --- BSP Clay Write Grip Squeeze Force --- */
int GripSqueezeForce_proc_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "%d\n", grip_status_g->G_SQUEEZE_FORCE);
	return 0;
}
ssize_t GripSqueezeForce_proc_write(struct file *filp, const char __user *buff,
		size_t len, loff_t *data)
{
	int val;
	char messages[256];
	
	memset(messages, 0, sizeof(messages));
	if (len > 256) {
		len = 256;
	}
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}
	val = (int)simple_strtol(messages, NULL, 10);
	grip_squeeze_force_func(val);
	
	return len;
}

int GripSqueezeForce_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, GripSqueezeForce_proc_read, NULL);
}

void create_GripSqueezeForce_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  GripSqueezeForce_proc_open,
		.write =  GripSqueezeForce_proc_write,
		.read = seq_read,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = proc_create("driver/grip_squeeze_force", 0666, NULL, &proc_fops);

	if (!proc_file) {
		PRINT_CRIT("[Proc]%s failed!\n", __FUNCTION__);
	}
	return;
}
/* --- BSP Clay Write Grip Squeeze Force --- */

/* +++ BSP Clay Write Grip Continuous Squeeze +++ */
int GripSqueeze_up_rate_proc_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "%d\n", grip_status_g->G_SQUEEZE_UP_RATE);
	return 0;
}
ssize_t GripSqueeze_up_rate_proc_write(struct file *filp, const char __user *buff,
		size_t len, loff_t *data)
{
	int val;
	char messages[256];
	
	memset(messages, 0, sizeof(messages));
	if (len > 256) {
		len = 256;
	}
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}
	val = (int)simple_strtol(messages, NULL, 10);
	grip_squeeze_up_rate_func(val);
	
	return len;
}

int GripSqueeze_up_rate_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, GripSqueeze_up_rate_proc_read, NULL);
}

void create_GripSqueeze_up_rate_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  GripSqueeze_up_rate_proc_open,
		.write =  GripSqueeze_up_rate_proc_write,
		.read = seq_read,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = proc_create("driver/grip_squeeze_up_rate", 0666, NULL, &proc_fops);

	if (!proc_file) {
		PRINT_CRIT("[Proc]%s failed!\n", __FUNCTION__);
	}
	return;
}

int GripSqueeze_up_total_proc_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "%d\n", grip_status_g->G_SQUEEZE_UP_TOTAL);
	return 0;
}
ssize_t GripSqueeze_up_total_proc_write(struct file *filp, const char __user *buff,
		size_t len, loff_t *data)
{
	int val;
	char messages[256];
	
	memset(messages, 0, sizeof(messages));
	if (len > 256) {
		len = 256;
	}
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}
	val = (int)simple_strtol(messages, NULL, 10);
	grip_squeeze_up_total_func(val);
	
	return len;
}

int GripSqueeze_up_total_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, GripSqueeze_up_total_proc_read, NULL);
}

void create_GripSqueeze_up_total_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  GripSqueeze_up_total_proc_open,
		.write =  GripSqueeze_up_total_proc_write,
		.read = seq_read,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = proc_create("driver/grip_squeeze_up_total", 0666, NULL, &proc_fops);

	if (!proc_file) {
		PRINT_CRIT("[Proc]%s failed!\n", __FUNCTION__);
	}
	return;
}

int GripSqueeze_drop_rate_proc_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "%d\n", grip_status_g->G_SQUEEZE_DROP_RATE);
	return 0;
}
ssize_t GripSqueeze_drop_rate_proc_write(struct file *filp, const char __user *buff,
		size_t len, loff_t *data)
{
	int val;
	char messages[256];
	
	memset(messages, 0, sizeof(messages));
	if (len > 256) {
		len = 256;
	}
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}
	val = (int)simple_strtol(messages, NULL, 10);
	grip_squeeze_drop_rate_func(val);
	
	return len;
}

int GripSqueeze_drop_rate_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, GripSqueeze_drop_rate_proc_read, NULL);
}

void create_GripSqueeze_drop_rate_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  GripSqueeze_drop_rate_proc_open,
		.write =  GripSqueeze_drop_rate_proc_write,
		.read = seq_read,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = proc_create("driver/grip_squeeze_drop_rate", 0666, NULL, &proc_fops);

	if (!proc_file) {
		PRINT_CRIT("[Proc]%s failed!\n", __FUNCTION__);
	}
	return;
}

int GripSqueeze_drop_total_proc_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "%d\n", grip_status_g->G_SQUEEZE_DROP_TOTAL);
	return 0;
}
ssize_t GripSqueeze_drop_total_proc_write(struct file *filp, const char __user *buff,
		size_t len, loff_t *data)
{
	int val;
	char messages[256];
	
	memset(messages, 0, sizeof(messages));
	if (len > 256) {
		len = 256;
	}
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}
	val = (int)simple_strtol(messages, NULL, 10);
	grip_squeeze_drop_total_func(val);
	
	return len;
}

int GripSqueeze_drop_total_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, GripSqueeze_drop_total_proc_read, NULL);
}

void create_GripSqueeze_drop_total_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  GripSqueeze_drop_total_proc_open,
		.write =  GripSqueeze_drop_total_proc_write,
		.read = seq_read,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = proc_create("driver/grip_squeeze_drop_total", 0666, NULL, &proc_fops);

	if (!proc_file) {
		PRINT_CRIT("[Proc]%s failed!\n", __FUNCTION__);
	}
	return;
}

/* --- BSP Clay Write Grip Continuous Squeeze --- */

/* +++ BSP Clay Write Grip Tap Dur +++ */
int GripTap1Dur_proc_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "%d\n", grip_status_g->G_TAP1_DUR);
	return 0;
}

ssize_t GripTap1Dur_proc_write(struct file *filp, const char __user *buff,
		size_t len, loff_t *data)
{
	int val;
	char messages[256];

	memset(messages, 0, sizeof(messages));
	if (len > 256) {
		len = 256;
	}
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}
	val = (int)simple_strtol(messages, NULL, 10);
	grip_tap1_duration_func(val);
	return len;
}

int GripTap1Dur_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, GripTap1Dur_proc_read, NULL);
}

void create_GripTap1Dur_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  GripTap1Dur_proc_open,
		.write =  GripTap1Dur_proc_write,
		.read = seq_read,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = proc_create("driver/grip_tap1_dur", 0666, NULL, &proc_fops);

	if (!proc_file) {
		PRINT_CRIT("[Proc]%s failed!\n", __FUNCTION__);
	}
	return;
}

int GripTap2Dur_proc_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "%d\n", grip_status_g->G_TAP2_DUR);
	return 0;
}

ssize_t GripTap2Dur_proc_write(struct file *filp, const char __user *buff,
		size_t len, loff_t *data)
{
	int val;
	char messages[256];

	memset(messages, 0, sizeof(messages));
	if (len > 256) {
		len = 256;
	}
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}
	val = (int)simple_strtol(messages, NULL, 10);
	grip_tap2_duration_func(val);
	return len;
}

int GripTap2Dur_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, GripTap2Dur_proc_read, NULL);
}

void create_GripTap2Dur_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  GripTap2Dur_proc_open,
		.write =  GripTap2Dur_proc_write,
		.read = seq_read,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = proc_create("driver/grip_tap2_dur", 0666, NULL, &proc_fops);

	if (!proc_file) {
		PRINT_CRIT("[Proc]%s failed!\n", __FUNCTION__);
	}
	return;
}

int GripTap3Dur_proc_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "%d\n", grip_status_g->G_TAP3_DUR);
	return 0;
}

ssize_t GripTap3Dur_proc_write(struct file *filp, const char __user *buff,
		size_t len, loff_t *data)
{
	int val;
	char messages[256];

	memset(messages, 0, sizeof(messages));
	if (len > 256) {
		len = 256;
	}
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}
	val = (int)simple_strtol(messages, NULL, 10);
	grip_tap3_duration_func(val);
	return len;
}

int GripTap3Dur_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, GripTap3Dur_proc_read, NULL);
}

void create_GripTap3Dur_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  GripTap3Dur_proc_open,
		.write =  GripTap3Dur_proc_write,
		.read = seq_read,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = proc_create("driver/grip_tap3_dur", 0666, NULL, &proc_fops);

	if (!proc_file) {
		PRINT_CRIT("[Proc]%s failed!\n", __FUNCTION__);
	}
	return;
}
/* --- BSP Clay Write Grip Tap Duration --- */


/* +++ BSP Clay Write Grip Tap Force +++ */
int GripTap1Force_proc_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "%d\n", grip_status_g->G_TAP1_FORCE);
	return 0;
}

ssize_t GripTap1Force_proc_write(struct file *filp, const char __user *buff,
		size_t len, loff_t *data)
{
	int val;
	char messages[256];

	memset(messages, 0, sizeof(messages));
	if (len > 256) {
		len = 256;
	}
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}
	val = (int)simple_strtol(messages, NULL, 10);
	grip_tap1_force_func(val);
	return len;
}

int GripTap1Force_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, GripTap1Force_proc_read, NULL);
}

void create_GripTap1Force_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  GripTap1Force_proc_open,
		.write =  GripTap1Force_proc_write,
		.read = seq_read,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = proc_create("driver/grip_tap1_force", 0666, NULL, &proc_fops);

	if (!proc_file) {
		PRINT_CRIT("[Proc]%s failed!\n", __FUNCTION__);
	}
	return;
}
int GripTap2Force_proc_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "%d\n", grip_status_g->G_TAP2_FORCE);
	return 0;
}

ssize_t GripTap2Force_proc_write(struct file *filp, const char __user *buff,
		size_t len, loff_t *data)
{
	int val;
	char messages[256];
	
	memset(messages, 0, sizeof(messages));
	if (len > 256) {
		len = 256;
	}
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}
	val = (int)simple_strtol(messages, NULL, 10);
	grip_tap2_force_func(val);
	return len;
}

int GripTap2Force_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, GripTap2Force_proc_read, NULL);
}

void create_GripTap2Force_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  GripTap2Force_proc_open,
		.write =  GripTap2Force_proc_write,
		.read = seq_read,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = proc_create("driver/grip_tap2_force", 0666, NULL, &proc_fops);

	if (!proc_file) {
		PRINT_CRIT("[Proc]%s failed!\n", __FUNCTION__);
	}
	return;
}
int GripTap3Force_proc_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "%d\n", grip_status_g->G_TAP3_FORCE);
	return 0;
}

ssize_t GripTap3Force_proc_write(struct file *filp, const char __user *buff,
		size_t len, loff_t *data)
{
	int val;
	char messages[256];
	
	memset(messages, 0, sizeof(messages));
	if (len > 256) {
		len = 256;
	}
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}
	val = (int)simple_strtol(messages, NULL, 10);
	grip_tap3_force_func(val);
	return len;
}

int GripTap3Force_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, GripTap3Force_proc_read, NULL);
}

void create_GripTap3Force_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  GripTap3Force_proc_open,
		.write =  GripTap3Force_proc_write,
		.read = seq_read,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = proc_create("driver/grip_tap3_force", 0666, NULL, &proc_fops);

	if (!proc_file) {
		PRINT_CRIT("[Proc]%s failed!\n", __FUNCTION__);
	}
	return;
}

int GripTap1_TouchUpForce_proc_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "%d\n", grip_status_g->G_TAP1_FUP_FORCE);
	return 0;
}

ssize_t GripTap1_TouchUpForce_proc_write(struct file *filp, const char __user *buff,
		size_t len, loff_t *data)
{
	int val;
	char messages[256];
	
	memset(messages, 0, sizeof(messages));
	if (len > 256) {
		len = 256;
	}
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}
	val = (int)simple_strtol(messages, NULL, 10);
	grip_tap1_fup_force_func(val);
	return len;
}

int GripTap1_TouchUpForce_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, GripTap1_TouchUpForce_proc_read, NULL);
}

void create_GripTap1_TouchUpForce_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  GripTap1_TouchUpForce_proc_open,
		.write =  GripTap1_TouchUpForce_proc_write,
		.read = seq_read,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = proc_create("driver/grip_tap1_fup_force", 0666, NULL, &proc_fops);

	if (!proc_file) {
		PRINT_CRIT("[Proc]%s failed!\n", __FUNCTION__);
	}
	return;
}

int GripTap2_TouchUpForce_proc_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "%d\n", grip_status_g->G_TAP2_FUP_FORCE);
	return 0;
}

ssize_t GripTap2_TouchUpForce_proc_write(struct file *filp, const char __user *buff,
		size_t len, loff_t *data)
{
	int val;
	char messages[256];
	
	memset(messages, 0, sizeof(messages));
	if (len > 256) {
		len = 256;
	}
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}
	val = (int)simple_strtol(messages, NULL, 10);
	grip_tap2_fup_force_func(val);
	return len;
}

int GripTap2_TouchUpForce_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, GripTap2_TouchUpForce_proc_read, NULL);
}

void create_GripTap2_TouchUpForce_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  GripTap2_TouchUpForce_proc_open,
		.write =  GripTap2_TouchUpForce_proc_write,
		.read = seq_read,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = proc_create("driver/grip_tap2_fup_force", 0666, NULL, &proc_fops);

	if (!proc_file) {
		PRINT_CRIT("[Proc]%s failed!\n", __FUNCTION__);
	}
	return;
}

int GripTap3_TouchUpForce_proc_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "%d\n", grip_status_g->G_TAP3_FUP_FORCE);
	return 0;
}

ssize_t GripTap3_TouchUpForce_proc_write(struct file *filp, const char __user *buff,
		size_t len, loff_t *data)
{
	int val;
	char messages[256];
	
	memset(messages, 0, sizeof(messages));
	if (len > 256) {
		len = 256;
	}
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}
	val = (int)simple_strtol(messages, NULL, 10);
	grip_tap3_fup_force_func(val);
	return len;
}

int GripTap3_TouchUpForce_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, GripTap3_TouchUpForce_proc_read, NULL);
}

void create_GripTap3_TouchUpForce_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  GripTap3_TouchUpForce_proc_open,
		.write =  GripTap3_TouchUpForce_proc_write,
		.read = seq_read,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = proc_create("driver/grip_tap3_fup_force", 0666, NULL, &proc_fops);

	if (!proc_file) {
		PRINT_CRIT("[Proc]%s failed!\n", __FUNCTION__);
	}
	return;
}

/* --- BSP Clay Write Grip Tap Force --- */

/* +++ BSP Clay Write Grip Swipe Velocity +++ */

int Grip_Swipe1_Velocity_proc_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "%d\n", grip_status_g->G_SWIPE1_VELOCITY);
	return 0;
}

ssize_t Grip_Swipe1_Velocity_proc_write(struct file *filp, const char __user *buff,
		size_t len, loff_t *data)
{
	int val;
	char messages[256];
	
	memset(messages, 0, sizeof(messages));
	if (len > 256) {
		len = 256;
	}
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}
	val = (int)simple_strtol(messages, NULL, 10);
	grip_swipe1_velocity_func(val);
	return len;
}

int Grip_Swipe1_Velocity_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, Grip_Swipe1_Velocity_proc_read, NULL);
}

void create_Grip_Swipe1_Velocity_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  Grip_Swipe1_Velocity_proc_open,
		.write =  Grip_Swipe1_Velocity_proc_write,
		.read = seq_read,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = proc_create("driver/grip_swipe1_v", 0666, NULL, &proc_fops);

	if (!proc_file) {
		PRINT_CRIT("[Proc]%s failed!\n", __FUNCTION__);
	}
	return;
}

int Grip_Swipe2_Velocity_proc_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "%d\n", grip_status_g->G_SWIPE2_VELOCITY);
	return 0;
}

ssize_t Grip_Swipe2_Velocity_proc_write(struct file *filp, const char __user *buff,
		size_t len, loff_t *data)
{
	int val;
	char messages[256];
	
	memset(messages, 0, sizeof(messages));
	if (len > 256) {
		len = 256;
	}
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}
	val = (int)simple_strtol(messages, NULL, 10);
	grip_swipe2_velocity_func(val);
	return len;
}

int Grip_Swipe2_Velocity_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, Grip_Swipe2_Velocity_proc_read, NULL);
}

void create_Grip_Swipe2_Velocity_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  Grip_Swipe2_Velocity_proc_open,
		.write =  Grip_Swipe2_Velocity_proc_write,
		.read = seq_read,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = proc_create("driver/grip_swipe2_v", 0666, NULL, &proc_fops);

	if (!proc_file) {
		PRINT_CRIT("[Proc]%s failed!\n", __FUNCTION__);
	}
	return;
}

int Grip_Swipe3_Velocity_proc_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "%d\n", grip_status_g->G_SWIPE3_VELOCITY);
	return 0;
}

ssize_t Grip_Swipe3_Velocity_proc_write(struct file *filp, const char __user *buff,
		size_t len, loff_t *data)
{
	int val;
	char messages[256];
	
	memset(messages, 0, sizeof(messages));
	if (len > 256) {
		len = 256;
	}
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}
	val = (int)simple_strtol(messages, NULL, 10);
	grip_swipe3_velocity_func(val);
	return len;
}

int Grip_Swipe3_Velocity_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, Grip_Swipe3_Velocity_proc_read, NULL);
}

void create_Grip_Swipe3_Velocity_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  Grip_Swipe3_Velocity_proc_open,
		.write =  Grip_Swipe3_Velocity_proc_write,
		.read = seq_read,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = proc_create("driver/grip_swipe3_v", 0666, NULL, &proc_fops);

	if (!proc_file) {
		PRINT_CRIT("[Proc]%s failed!\n", __FUNCTION__);
	}
	return;
}
/* --- BSP Clay Write Grip Swipe Velocity --- */

/* +++ BSP Clay Write Grip Swipe Len +++ */
int Grip_Swipe1_Len_proc_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "%d\n", grip_status_g->G_SWIPE1_LEN);
	return 0;
}

ssize_t Grip_Swipe1_Len_proc_write(struct file *filp, const char __user *buff,
		size_t len, loff_t *data)
{
	int val;
	char messages[256];
	
	memset(messages, 0, sizeof(messages));
	if (len > 256) {
		len = 256;
	}
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}
	val = (int)simple_strtol(messages, NULL, 10);
	grip_swipe1_len_func(val);
	return len;
}

int Grip_Swipe1_Len_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, Grip_Swipe1_Len_proc_read, NULL);
}

void create_Grip_Swipe1_Len_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  Grip_Swipe1_Len_proc_open,
		.write =  Grip_Swipe1_Len_proc_write,
		.read = seq_read,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = proc_create("driver/grip_swipe1_len", 0666, NULL, &proc_fops);

	if (!proc_file) {
		PRINT_CRIT("[Proc]%s failed!\n", __FUNCTION__);
	}
	return;
}

int Grip_Swipe2_Len_proc_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "%d\n", grip_status_g->G_SWIPE2_LEN);
	return 0;
}

ssize_t Grip_Swipe2_Len_proc_write(struct file *filp, const char __user *buff,
		size_t len, loff_t *data)
{
	int val;
	char messages[256];
	
	memset(messages, 0, sizeof(messages));
	if (len > 256) {
		len = 256;
	}
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}
	val = (int)simple_strtol(messages, NULL, 10);
	grip_swipe2_len_func(val);
	return len;
}

int Grip_Swipe2_Len_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, Grip_Swipe2_Len_proc_read, NULL);
}

void create_Grip_Swipe2_Len_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  Grip_Swipe2_Len_proc_open,
		.write =  Grip_Swipe2_Len_proc_write,
		.read = seq_read,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = proc_create("driver/grip_swipe2_len", 0666, NULL, &proc_fops);

	if (!proc_file) {
		PRINT_CRIT("[Proc]%s failed!\n", __FUNCTION__);
	}
	return;
}

int Grip_Swipe3_Len_proc_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "%d\n", grip_status_g->G_SWIPE3_LEN);
	return 0;
}

ssize_t Grip_Swipe3_Len_proc_write(struct file *filp, const char __user *buff,
		size_t len, loff_t *data)
{
	int val;
	char messages[256];
	
	memset(messages, 0, sizeof(messages));
	if (len > 256) {
		len = 256;
	}
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}
	val = (int)simple_strtol(messages, NULL, 10);
	grip_swipe3_len_func(val);
	return len;
}

int Grip_Swipe3_Len_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, Grip_Swipe3_Len_proc_read, NULL);
}

void create_Grip_Swipe3_Len_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  Grip_Swipe3_Len_proc_open,
		.write =  Grip_Swipe3_Len_proc_write,
		.read = seq_read,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = proc_create("driver/grip_swipe3_len", 0666, NULL, &proc_fops);

	if (!proc_file) {
		PRINT_CRIT("[Proc]%s failed!\n", __FUNCTION__);
	}
	return;
}
/* --- BSP Clay Write Grip Swipe Len --- */

/* +++ BSP Clay Write Grip Squeeze duration +++ */
int Grip_Squeeze_short_limit_proc_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "%d\n", squeeze_short_limit);
	return 0;
}

ssize_t Grip_Squeeze_short_limit_proc_write(struct file *filp, const char __user *buff,
		size_t len, loff_t *data)
{
	int val;
	char messages[256];
	
	memset(messages, 0, sizeof(messages));
	if (len > 256) {
		len = 256;
	}
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}
	val = (int)simple_strtol(messages, NULL, 10);
	grip_squeeze_short_limit_func(val);
	return len;
}

int Grip_Squeeze_short_limit_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, Grip_Squeeze_short_limit_proc_read, NULL);
}

void create_Grip_Squeeze_short_limit_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  Grip_Squeeze_short_limit_proc_open,
		.write =  Grip_Squeeze_short_limit_proc_write,
		.read = seq_read,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = 
	proc_create("driver/grip_squeeze_short_limit", 0666, NULL, &proc_fops);

	if (!proc_file) {
		PRINT_CRIT("[Proc]%s failed!\n", __FUNCTION__);
	}
	return;
}

int Grip_Squeeze_short_dur_proc_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "%d\n", grip_status_g->G_SQUEEZE_SHORT);
	return 0;
}

ssize_t Grip_Squeeze_short_dur_proc_write(struct file *filp, const char __user *buff,
		size_t len, loff_t *data)
{
	int val;
	char messages[256];
	
	memset(messages, 0, sizeof(messages));
	if (len > 256) {
		len = 256;
	}
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}
	val = (int)simple_strtol(messages, NULL, 10);
	grip_squeeze_short_dur_func(val);
	return len;
}

int Grip_Squeeze_short_dur_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, Grip_Squeeze_short_dur_proc_read, NULL);
}

void create_Grip_Squeeze_short_dur_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  Grip_Squeeze_short_dur_proc_open,
		.write =  Grip_Squeeze_short_dur_proc_write,
		.read = seq_read,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = 
	proc_create("driver/grip_squeeze_short_dur", 0666, NULL, &proc_fops);

	if (!proc_file) {
		PRINT_CRIT("[Proc]%s failed!\n", __FUNCTION__);
	}
	return;
}
int Grip_Squeeze_long_dur_proc_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "%d\n", grip_status_g->G_SQUEEZE_LONG);
	return 0;
}

ssize_t Grip_Squeeze_long_dur_proc_write(struct file *filp, const char __user *buff,
		size_t len, loff_t *data)
{
	int val;
	char messages[256];
	
	memset(messages, 0, sizeof(messages));
	if (len > 256) {
		len = 256;
	}
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}
	val = (int)simple_strtol(messages, NULL, 10);
	grip_squeeze_long_dur_func(val);
	return len;
}

int Grip_Squeeze_long_dur_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, Grip_Squeeze_long_dur_proc_read, NULL);
}

void create_Grip_Squeeze_long_dur_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  Grip_Squeeze_long_dur_proc_open,
		.write =  Grip_Squeeze_long_dur_proc_write,
		.read = seq_read,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = 
	proc_create("driver/grip_squeeze_long_dur", 0666, NULL, &proc_fops);

	if (!proc_file) {
		PRINT_CRIT("[Proc]%s failed!\n", __FUNCTION__);
	}
	return;
}

/* --- BSP Clay Write Grip Squeeze duration --- */

//==========Gesture Thershold Interface=======//
int fw_version = 0;
/******* Dynamic Loading FW ********/
int Grip_FW_VER_proc_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "FW Version%d\n", fw_version);
	return 0;
}

ssize_t Grip_FW_VER_proc_write(struct file *filp, const char __user *buff,
		size_t len, loff_t *data)
{
	int val;
	char messages[256];
	
	memset(messages, 0, sizeof(messages));
	if (len > 256) {
		len = 256;
	}
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}
	val = (int)simple_strtol(messages, NULL, 10);
	fw_version = val;
	return len;
}

int Grip_FW_VER_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, Grip_FW_VER_proc_read, NULL);
}

void create_Grip_FW_VER_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  Grip_FW_VER_proc_open,
		.write =  Grip_FW_VER_proc_write,
		.read = seq_read,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = 
	proc_create("driver/grip_fw_ver", 0666, NULL, &proc_fops);

	if (!proc_file) {
		PRINT_CRIT("[Proc]%s failed!\n", __FUNCTION__);
	}
	return;
}
/******* Dynamic Loading FW ********/

#include <linux/of_gpio.h>
#define GRIP_GPIO1_ON_LOOKUP_STATE		"gpio1_pm845"
#define GRIP_GPIO1_OFF_LOOKUP_STATE		"gpio1_pm845_off"

static void set_pinctrl(struct device *dev, char *str)
{
	int ret;
	struct pinctrl *key_pinctrl;
	struct pinctrl_state *set_state;
	
	PRINT_INFO("Set_pinctrl start!");
	key_pinctrl = devm_pinctrl_get(dev);
	if(key_pinctrl!=NULL){
		set_state = pinctrl_lookup_state(key_pinctrl, str);
		if(set_state!=NULL){
			//dev_info("pinctrl_lookup_state: set_state=%s", set_state->name));
			ret = pinctrl_select_state(key_pinctrl, set_state);
			if(ret < 0){
				PRINT_ERR("%s: pinctrl_select_state ERROR(%d).\n", __FUNCTION__, ret);
			}
			PRINT_INFO("Set_pinctrl done!");
		}
	} else {
		PRINT_ERR("pinctrl_lookup_state: key_pinctrl=NULL!");
	}
}

int power_status=1;
int Grip_set_power_proc_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "Grip 1V2_2V8 status: %d\n", power_status);
	return 0;
}

ssize_t Grip_set_power_proc_write(struct file *filp, const char __user *buff,
		size_t len, loff_t *data)
{
	int val;
	char messages[256];
	
	memset(messages, 0, sizeof(messages));
	if (len > 256) {
		len = 256;
	}
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}
	val = (int)simple_strtol(messages, NULL, 10);
	power_status = val;
	if(val == 1){
		set_pinctrl(snt8100fsr_g->dev, GRIP_GPIO1_ON_LOOKUP_STATE);
		PRINT_INFO("Set pinctl: PM845 GPIO1 pull-up");
		msleep(500);
	}else{
		set_pinctrl(snt8100fsr_g->dev, GRIP_GPIO1_OFF_LOOKUP_STATE);
		PRINT_INFO("Set pinctl: PM845 GPIO1 pull-down");
		msleep(500);		
	}
	return len;
}

int Grip_set_power_proc_open(struct inode *inode, struct  file *file)
{
    return single_open(file, Grip_set_power_proc_read, NULL);
}

void create_Grip_set_power_proc_file(void)
{
	static const struct file_operations proc_fops = {
		.owner = THIS_MODULE,
		.open =  Grip_set_power_proc_open,
		.write =  Grip_set_power_proc_write,
		.read = seq_read,
		.release = single_release,
	};
	struct proc_dir_entry *proc_file = 
	proc_create("driver/grip_set_power", 0666, NULL, &proc_fops);

	if (!proc_file) {
		PRINT_CRIT("[Proc]%s failed!\n", __FUNCTION__);
	}
	return;
}

/*************** ASUS BSP Clay: proc file --- *******************/
