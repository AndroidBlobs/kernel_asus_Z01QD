#include <linux/proc_fs.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <asm/uaccess.h>
//#include <linux/wakelock.h>
#include "grip_Wakelock.h"

extern int SntSensor_miscOpen(struct inode *inode, struct file *file);
extern int SntSensor_miscRelease(struct inode *inode, struct file *file);
extern long SntSensor_miscIoctl(struct file *file, unsigned int cmd, unsigned long arg);

extern void create_GripI2cCheck_proc_file(void);
extern void create_GripFPCCheck_proc_file(void);
extern void create_Calibration_raw_data_proc_file(void);
extern void create_asusGripDebug_proc_file(void);
extern void create_GripDisable_WakeLock_proc_file(void);

//Enable interface
extern void create_Grip_frame_proc_file(void);
extern void create_Grip_raw_en_proc_file(void);
extern void create_Grip_en_proc_file(void);
extern void create_GripSqueezeEn_proc_file(void);
extern void create_GripTap1En_proc_file(void);
extern void create_GripTap2En_proc_file(void);
extern void create_GripTap3En_proc_file(void);
extern void create_GripTap_Sense_En_proc_file(void);
extern void create_Grip_Swipe1_En_proc_file(void);
extern void create_Grip_Swipe2_En_proc_file(void);
extern void create_Grip_Swipe3_En_proc_file(void);
extern void create_Grip_Swipe1_Len_proc_file(void);
extern void create_Grip_Swipe2_Len_proc_file(void);
extern void create_Grip_Swipe3_Len_proc_file(void);
	
//Gesture Threshold proc
extern void create_GripSqueezeForce_proc_file(void);
extern void create_GripTap1Force_proc_file(void);
extern void create_GripTap2Force_proc_file(void);
extern void create_GripTap3Force_proc_file(void);
extern void create_GripTap1_TouchUpForce_proc_file(void);
extern void create_GripTap2_TouchUpForce_proc_file(void);
extern void create_GripTap3_TouchUpForce_proc_file(void);
extern void create_GripTap1Dur_proc_file(void);
extern void create_GripTap2Dur_proc_file(void);
extern void create_GripTap3Dur_proc_file(void);
extern void create_Grip_Swipe1_Velocity_proc_file(void);
extern void create_Grip_Swipe2_Velocity_proc_file(void);
extern void create_Grip_Swipe3_Velocity_proc_file(void);
extern void create_Grip_Squeeze_short_dur_proc_file(void);
extern void create_Grip_Squeeze_long_dur_proc_file(void);
extern void create_Grip_Squeeze_short_limit_proc_file(void);

extern void create_GripSqueeze_up_rate_proc_file(void);
extern void create_GripSqueeze_up_total_proc_file(void);
extern void create_GripSqueeze_drop_rate_proc_file(void);
extern void create_GripSqueeze_drop_total_proc_file(void);



//Function: DPC wake from low power mode
extern void Wait_Wake_For_RegW(void);
extern void DPC_write_func(int flag);

// Gesture enable func
extern void grip_raw_enable_func(int val);
extern void grip_enable_func(int val);
extern void grip_tap1_enable_func(int val);
extern void grip_tap2_enable_func(int val);
extern void grip_tap3_enable_func(int val);
extern void grip_tap_sense_enable_func(int val);
extern void grip_swipe1_enable_func(int val);
extern void grip_swipe2_enable_func(int val);
extern void grip_swipe3_enable_func(int val);
extern void grip_squeeze_enable_func(int val);

//Gesture Threshold func
extern void grip_squeeze_force_func(int val);
extern void grip_tap1_force_func(int val);
extern void grip_tap2_force_func(int val);
extern void grip_tap3_force_func(int val);
extern void grip_swipe1_velocity_func(int val);
extern void grip_swipe2_velocity_func(int val);
extern void grip_swipe3_velocity_func(int val);
extern void grip_swipe1_len_func(int val);
extern void grip_swipe2_len_func(int val);
extern void grip_swipe3_len_func(int val);

extern int g_info[16];
extern struct file_operations sentons_snt_fops;
extern struct miscdevice sentons_snt_misc;
extern int sntSensor_miscRegister(void);

extern void check_gesture_before_suspend(void);
extern void check_gesture_after_resume(struct work_struct *work);
extern struct delayed_work check_resume;

#ifdef DYNAMIC_PWR_CTL
extern int snt_activity_request(void);
#endif


extern int Health_Check_Enable(int en);
extern void Into_DeepSleep_fun(void);
/******* Dynamic Loading FW ********/
extern int fw_version;
extern void create_Grip_FW_VER_proc_file(void);
extern void create_Grip_set_power_proc_file(void);
extern void create_GripSQ_Bar0_factory_proc_file(void);
extern void create_GripSQ_Bar1_factory_proc_file(void);
extern void create_GripSQ_Bar2_factory_proc_file(void);

extern uint16_t B0_F_value;
extern uint16_t B1_F_value;
extern uint16_t B2_F_value;
extern int fw_loading_status;



/* Workaround for stucked semaphore */
extern void check_stuck_semaphore(struct work_struct *work);
extern struct delayed_work check_stuck_wake;
/* Workaround for stucked semaphore */

/* reset func for write fail */
extern struct delayed_work rst_recovery_wk;
extern struct delayed_work rst_gpio_wk;
extern void Reset_Func(struct work_struct *work);
extern void grip_dump_status_func(struct work_struct *work);
extern struct workqueue_struct *asus_wq;

extern struct delayed_work check_onoff_wk;
extern void Check_fw_onoff(struct work_struct *work);
extern void Enable_tap_sensitive(const char *buf, size_t count);

