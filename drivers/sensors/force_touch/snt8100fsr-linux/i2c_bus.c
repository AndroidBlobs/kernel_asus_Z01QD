/*****************************************************************************
* File: i2c-bus.c
*
* (c) 2016 Sentons Inc. - All Rights Reserved.
*
* All information contained herein is and remains the property of Sentons
* Incorporated and its suppliers if any. The intellectual and technical
* concepts contained herein are proprietary to Sentons Incorporated and its
* suppliers and may be covered by U.S. and Foreign Patents, patents in
* process, and are protected by trade secret or copyright law. Dissemination
* of this information or reproduction of this material is strictly forbidden
* unless prior written permission is obtained from Sentons Incorporated.
*
* SENTONS PROVIDES THIS SOURCE CODE STRICTLY ON AN "AS IS" BASIS,
* WITHOUT ANY WARRANTY WHATSOEVER, AND EXPRESSLY DISCLAIMS ALL
* WARRANTIES, EXPRESS, IMPLIED OR STATUTORY WITH REGARD THERETO, INCLUDING
* THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
* PURPOSE, TITLE OR NON-INFRINGEMENT OF THIRD PARTY RIGHTS. SENTONS SHALL
* NOT BE LIABLE FOR ANY DAMAGES SUFFERED BY YOU AS A RESULT OF USING,
* MODIFYING OR DISTRIBUTING THIS SOFTWARE OR ITS DERIVATIVES.
*
*
*****************************************************************************/
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/delay.h>
#define CONFIG_I2C_MODULE
#include <linux/i2c.h>
#include "workqueue.h"

#include "config.h"
#include "device.h"
#include "memory.h"
#include "serial_bus.h"
#include "main.h"
#include "event.h"
#include "hardware.h"
#include "sysfs.h"
#include "utils.h"
#include "debug.h"

#include "sonacomm.h"

#include <linux/proc_fs.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <asm/uaccess.h>
//#include <linux/wakelock.h>
#include "grip_Wakelock.h"
#include "file_control.h"
#include "workqueue.h"
#include "locking.h"

#define asus_grip_queue "snt8100fsr-asus_queue"

/*==========================================================================*/
/* DEFINES                                                                  */
/*==========================================================================*/
#define MAX_CMD_FIFO        256
#define CMD_HDR_SIZE        8
#define MAX_CMD_BYTES       (MAX_CMD_FIFO + 2*CMD_HDR_SIZE)

#define DATA_SIZE           (17 * 1024)

/*==========================================================================*/
/* LOCAL PROTOTYPES                                                         */
/*==========================================================================*/
int snt_i2c_read(struct snt8100fsr *snt8100fsr,
                 int num_read,
                 uint8_t *data_in);
int snt_i2c_write(struct snt8100fsr *snt8100fsr,
                  int num_write,
                  uint8_t *data_out);

/*==========================================================================*/
/* STRUCTURES                                                               */
/*==========================================================================*/

/*==========================================================================*/
/* GLOBAL VARIABLES                                                         */
/*==========================================================================*/
static uint32_t *request_out;
static uint8_t  *status_in;
static uint8_t  *data_in;
static uint8_t  *data_out;
int write_fail_count = 0;
int write_fail_reset_trigger = 5;

//Reset Function
void check_i2c_error(void){
	//ASUSEvtlog("[Grip] read/write fail, count=%d\n", write_fail_count);
	write_fail_count++;
	if(write_fail_count >= write_fail_reset_trigger){
		//ASUSEvtlog("[Grip] write fail, call reset_func\n");
    		queue_delayed_work(asus_wq, &rst_gpio_wk, msecs_to_jiffies(0));
		write_fail_count = 0;
	}
}


/*==========================================================================*/
/* METHODS                                                                  */
/*==========================================================================*/
int snt_i2c_open(struct i2c_client *i2c) {
    // Allocate our data buffers in contiguous memory for DMA support
    status_in = memory_allocate(I2C_FIFO_SIZE, GFP_DMA);
    if (status_in == NULL) {
        PRINT_CRIT("status_in = memory_allocate(%d) failed", I2C_FIFO_SIZE);
        return -1;
    }

    request_out = memory_allocate(I2C_FIFO_SIZE, GFP_DMA);
    if (request_out == NULL) {
        PRINT_CRIT("request_out = memory_allocate(%d) failed", I2C_FIFO_SIZE);
        return -1;
    }

    data_in = memory_allocate(DATA_SIZE, GFP_DMA);
    if (data_in == NULL) {
        PRINT_CRIT("data_in = memory_allocate(%d) failed", DATA_SIZE);
        return -1;
    }

    data_out = memory_allocate(DATA_SIZE, GFP_DMA);
    if (data_out == NULL) {
        PRINT_CRIT("data_out = memory_allocate(%d) failed", DATA_SIZE);
        return -1;
    }

    PRINT_INFO("I2C Address: 0x%02X", i2c->addr);

    PRINT_DEBUG("done");
    return 0;
}

void snt_i2c_close(struct snt8100fsr *snt8100fsr) {
    PRINT_FUNC();

    if (request_out) {
        memory_free(request_out);
        request_out = NULL;
    }

    if (status_in) {
        memory_free(status_in);
        status_in = NULL;
    }

    if (data_in) {
        memory_free(data_in);
        data_in = NULL;
    }

    if (data_out) {
        memory_free(data_out);
        data_out = NULL;
    }

    PRINT_DEBUG("done");
}

/*
 * snt_i2c_get_boot_status() can only be called during firmware loading
 * prior to the last interrupt saying the system is ready.
 */
uint8_t snt_i2c_get_boot_status(struct snt8100fsr *snt8100fsr) {
    PRINT_CRIT("Not Implemented\n");
    return 0xFF;
}


/*
 * Wake the device up over the I2C bus by issuing a write of 4 bytes
 * to the I2C Wake Device which operates on a different I2C address.
 */
int snt_i2c_wake_device(struct snt8100fsr *snt8100fsr) {
    int ret;
    uint8_t buf[4] = {0, 0, 0, 0};

    PRINT_FUNC();
    //PRINT_INFO("DPC: Wake device");
    // We will use the I2C Wake Device for this command
    if (snt8100fsr_wake_i2c_g == NULL) {
        PRINT_CRIT("Unable to wake device due to missing wake I2C device");
        return -1;
    }

    ret = snt_i2c_write(snt8100fsr_wake_i2c_g, 4, buf);

    if (ret) {
        PRINT_DEBUG("snt_i2c_write(snt8100fsr_wake_i2c, 4, buf) failed!");
        return ret;
    }

    PRINT_DEBUG("done");
    return 0;
}

int snt_i2c_read(struct snt8100fsr *snt8100fsr,
                 int num_read,
                 uint8_t *data_in) {

    int count;
    PRINT_FUNC("%d bytes", num_read);

    count = i2c_master_recv(snt8100fsr->i2c, data_in, num_read);
    if (count < 0) {
	check_i2c_error();
        PRINT_CRIT("I2C write failed, error = %d\n", count);
        return -1;
    } else if (count != num_read) {
        PRINT_CRIT("I2C read of %d bytes only read %d bytes",
                   num_read, count);
        return -1;
    }

    PRINT_DEBUG("%d bytes read", num_read);
    return 0;
}

int snt_i2c_write(struct snt8100fsr *snt8100fsr,
                  int num_write,
                  uint8_t *data_out) {
    int count;
#ifdef SNT_I2C_WRITE_DEBUG_VERBOSE  
    PRINT_FUNC("%d bytes", num_write);
#endif
    count = i2c_master_send(snt8100fsr->i2c, data_out, num_write);
    if (count < 0) {
	check_i2c_error();
        PRINT_CRIT("I2C write failed, error = %d\n", count);
        return -1;
    } else if (count != num_write) {
        PRINT_CRIT("I2C write of %d bytes only wrote %d bytes",
                   num_write, count);
        return -1;
    }
#ifdef SNT_I2C_WRITE_DEBUG_VERBOSE
    PRINT_DEBUG("%d bytes written", num_write);
#endif  
    return 0;
}

int snt_i2c_read_fifo_pkt(struct snt8100fsr *snt8100fsr,
                      uint16_t reg,
                      uint16_t len,
                      uint8_t *in_val) {
    uint8_t addr_phase[2];
    int count;
#ifdef SNT_I2C_READ_FIFO_PKT_DEBUG_VERBOSE
    PRINT_FUNC("len %d", len);
#endif
    addr_phase[0] = (reg >= 0x100) ? (0x80 | (reg >> 8)) : reg;
    if (len > I2C_MAX_PKT_SIZE_BYTES) {
        PRINT_CRIT("Warning. Max I2C read is %d bytes, truncating.\n", I2C_MAX_PKT_SIZE_BYTES);
        len = I2C_MAX_PKT_SIZE_BYTES;
    }

    if (len == 0) {
        PRINT_CRIT("ERROR: Must read at least 1 word\n");
        return -1;
    }

    addr_phase[1] = (len / 2) - 1;
    count = i2c_master_send(snt8100fsr->i2c, addr_phase, 2);
    if (count != 2) {
	check_i2c_error();
        PRINT_CRIT("I2C header write failed len = %d\n", count);
        PRINT_CRIT("len = 0x%04x, addr_phase[0]=0x%02x, addr_phase[1]=0x%02x\n", 
			len, addr_phase[0], addr_phase[1]);
        return -1;
    }

    count = i2c_master_recv(snt8100fsr->i2c, in_val, len);
    if (count < 0) {
        PRINT_CRIT("I2C read failed, error code = %d", count);
        return count;
    } else if (count != len) {
        PRINT_CRIT("I2C Read failed len=%d (expected %d)\n",
                   len, count);
        return -1;
    }
#ifdef SNT_I2C_READ_FIFO_PKT_DEBUG_VERBOSE
    PRINT_DEBUG("done");
#endif  
    return 0;
}


int snt_i2c_read_fifo(struct snt8100fsr *snt8100fsr,
                      uint16_t reg,
                      uint16_t len,
                      uint8_t *in_val) {
    int ret = 0;

   //PRINT_FUNC("reg 0x%X", reg);

    while (len != 0 && ret == 0) {
        uint16_t pkt_len = (len > I2C_MAX_PKT_SIZE_BYTES) ? I2C_MAX_PKT_SIZE_BYTES : len;
        len -= pkt_len;
        ret = snt_i2c_read_fifo_pkt(snt8100fsr, reg, pkt_len, in_val);
        if (ret != 0) {
            PRINT_CRIT("i2c pkt read failed at len %d, reg 0x%02X", len, reg);
        }
        in_val += pkt_len;
    }
    //PRINT_DEBUG("done");
    return ret;
}

int snt_i2c_write_fifo_pkt(struct snt8100fsr *snt8100fsr,
                       uint16_t reg,
                       uint16_t len,
                       uint8_t *out_val) {
    int write_len;
    int count;

    PRINT_FUNC("len %d", len);

    data_out[0] = (reg >= 0x100) ? (0x80 | (reg >> 8)) : reg;
    if (len > I2C_MAX_PKT_SIZE_BYTES) {
      PRINT_CRIT("Warning. Max i2c register write is %d bytes, truncating.\n", I2C_MAX_PKT_SIZE_BYTES);
      len = I2C_MAX_PKT_SIZE_BYTES;
    }
    if (len == 0) {
      PRINT_CRIT("ERROR: Must write at least 1 word\n");
      return -1;
    }

    data_out[1] = (len / 2) - 1;
    memcpy(&data_out[2], out_val, len);
    write_len = len + 2;

    /* Debug output of the hex values of the data
    for(count = 0; count < write_len; count++) {
        PRINT_DEBUG("%02X (%d)", data_out[count], data_out[count]);
    }*/
    count = i2c_master_send(snt8100fsr->i2c, data_out, write_len);
    if (count < 0) {
      check_i2c_error();
        PRINT_CRIT("I2C read failed, error code = %d", count);
        return count;
    } else if (count != write_len) {
      PRINT_CRIT("ERROR: I2C Write failed len=%d (expected %d)\n", count, 
write_len);
      return -1;
    }
    PRINT_DEBUG("done");
    return 0;
}

int snt_i2c_write_fifo(struct snt8100fsr *snt8100fsr,
                       uint16_t reg,
                       uint16_t len,
                       uint8_t *out_val) {
    int ret = 0;
    PRINT_FUNC("reg 0x%X", reg);
    if(reg == 0x0000 || reg == 0x0003 || reg == 0x0004 || reg == 0x000A || 
		reg == 0x000B || reg == 0x0011 || reg == 0x0019 || reg == 0x0021){
		PRINT_INFO("reg 0x%X read only! return", reg);
		return -1;
    }
    while (len != 0 && ret == 0) {
        uint16_t pkt_len = (len > I2C_MAX_PKT_SIZE_BYTES) ? I2C_MAX_PKT_SIZE_BYTES : len;
        len -= pkt_len;
        ret = snt_i2c_write_fifo_pkt(snt8100fsr, reg, pkt_len, out_val);
        if (ret != 0) {
            PRINT_CRIT("i2c pkt write failed at len %d, reg 0x%02X", len, reg);
        }
        out_val += pkt_len;
    }
    PRINT_DEBUG("done");
    return ret;
}

#include <linux/of_gpio.h>
#define GRIP_GPIO16_LOOKUP_STATE	"grip_default"
#define GRIP_GPIO1_ON_LOOKUP_STATE		"gpio1_pm845"
#define GRIP_GPIO1_OFF_LOOKUP_STATE		"gpio1_pm845_off"
#define GRIP_GPIO133_LOOKUP_STATE	"pinctrl_reset_init"
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

/*==========================================================================*/
/* Device Probe/Remove/Resume/Suspend                                       */
/*==========================================================================*/
#ifdef USE_I2C_BUS
struct delayed_work RST_WORK;

static void set_1V2_2V8_pin_func(struct work_struct *work_orig) {
	PRINT_INFO("Set pinctl: PM845 GPIO1 pull-up");
	set_pinctrl(snt8100fsr_g->dev, GRIP_GPIO1_ON_LOOKUP_STATE);
	msleep(500);
	PRINT_INFO("Set pinctl: SOC GPIO133 pull-up");
	set_pinctrl(snt8100fsr_g->dev, GRIP_GPIO133_LOOKUP_STATE);
	msleep(500);
	PRINT_INFO("GPIO133: %d",gpio_get_value(RST_GPIO));
    return;
}
// ***** To do list ********
//extern bool g_Charger_mode;
static int snt_i2c_probe(struct i2c_client *i2c,
                         const struct i2c_device_id *id)
{
    int ret;
    struct device *dev = &i2c->dev;
    struct device_node *np = dev->of_node;
    struct snt8100fsr *snt8100fsr;
    struct DPC_status *DPC_status_t;
    struct grip_status *grip_state_t;
	// ***** To do list ********
    //if(g_Charger_mode){
	//PRINT_INFO("Charger Mode=%d, return", g_Charger_mode);
	//return -1;
    //}

    PRINT_FUNC();
    PRINT_INFO("snt_i2c_probe Enter\n");
	PRINT_INFO("Host irq GPIO128: %d",gpio_get_value(128));
	PRINT_INFO("Tap IRQ GPIO40: %d",gpio_get_value(40));
	PRINT_INFO("Tap IRQ GPIO79: %d",gpio_get_value(79));
	PRINT_INFO("Tap IRQ GPIO129: %d",gpio_get_value(129));
	PRINT_INFO("Rst GPIO133: %d",gpio_get_value(RST_GPIO));
    snt8100fsr = memory_allocate(sizeof(*snt8100fsr),
                                 GFP_KERNEL);

    if(!snt8100fsr) {
        PRINT_CRIT("failed to allocate memory for struct snt8100fsr");
        return -ENOMEM;
    }
    memset(snt8100fsr, 0, sizeof(*snt8100fsr));

    snt8100fsr->bus_type = BUS_TYPE_I2C;
    snt8100fsr->dev = dev;
    i2c_set_clientdata(i2c, snt8100fsr);
    snt8100fsr->i2c = i2c;

    //gpio_reset = of_get_named_gpio(dev->of_node, "snt,gpio_reset", 0);
    //PRINT_INFO("gpio_test = %d", gpio_reset);
    //PRINT_INFO("GPIO133: %d",gpio_get_value(RST_GPIO));
    //if(gpio_request(gpio_reset, "snt_gpio_reset")){
    //    gpio_direction_output(gpio_reset, 1);
    //}
    
    /* We use two I2C devices to communicate with the snt8100fsr chip.
     * The main address is used for general communication, the secondary
     * address is used only to awaken the device when it is in sleep mode.
     * We must detect which I2C device is being initialized, the main one,
     * or the secondary address.
     */	
    if (of_property_read_bool(np, "wake-device")) {
	// Initialization for wake i2c device only, should be put after this line
        PRINT_INFO("Wake I2C device discovered");
        snt8100fsr->wake_i2c_device = true;
        snt8100fsr_wake_i2c_g = snt8100fsr;
	//pull-up GPIO16
	if (of_property_read_bool(np, "grip_gpio16")){
		PRINT_INFO("get_GPIO16");
		set_pinctrl(dev, GRIP_GPIO16_LOOKUP_STATE);
	}

        // We don't do any more init for this device
        PRINT_DEBUG("done");
        return 0;
    }

    //Initialization for main i2c device only, should be put after this line
    DPC_status_t = memory_allocate(sizeof(*DPC_status_t),
                                 GFP_KERNEL);
    grip_state_t = memory_allocate(sizeof(*grip_state_t),
                                 GFP_KERNEL);
    DPC_status_t->Condition = 0x87D0;
    DPC_status_t->High = 0x14;
    DPC_status_t->Low = 0x5;
    DPC_status_g = DPC_status_t;
    memset(grip_state_t, 0, sizeof(*grip_state_t));
    grip_status_g = grip_state_t;
	
    asus_wq = create_workqueue(asus_grip_queue);
    if (!asus_wq) {
        PRINT_CRIT("Unable to create_workqueue(%s)", asus_grip_queue);
        return -1;
    }
    INIT_DELAYED_WORK(&check_resume, check_gesture_after_resume);
    INIT_DELAYED_WORK(&check_stuck_wake, check_stuck_semaphore);
    INIT_DELAYED_WORK(&rst_recovery_wk, grip_dump_status_func);
    INIT_DELAYED_WORK(&rst_gpio_wk, Reset_Func);
    INIT_DELAYED_WORK(&check_onoff_wk, Check_fw_onoff);
    wake_lock_init(&(snt8100fsr->snt_wakelock), WAKE_LOCK_SUSPEND, "snt_wakelock"); 


    PRINT_INFO("Main I2C device discovered");
    snt8100fsr->wake_i2c_device = false;
    snt8100fsr->en_demo = 0;
    snt8100fsr->en_sensor_evt = 1;
	
    // Save this as our main device and to be used for sysFS access
    snt8100fsr_g = snt8100fsr;

#ifdef DYNAMIC_PWR_CTL
    // create wake semaphore
    sema_init(&snt8100fsr_g->wake_rsp, 0);
    sema_init(&snt8100fsr_g->wake_req, 0);
    snt8100fsr_g->enable_dpc_flag = DYNAMIC_PWR_CTL;
#endif

    snt8100fsr_g->driver_status = GRIP_I2C_PROBE;
    PRINT_INFO("GRIP STATUS:%d", snt8100fsr_g->driver_status);
	
    ret = main_init();
    if (ret) {
        PRINT_CRIT("main_init() failed");
        return ret;
    }


    ret = snt_i2c_device_init(i2c, snt8100fsr);
    if(ret) {
        PRINT_CRIT("snt_i2c_device_init() failed");
        return ret;
    }

    ret = snt_i2c_open(snt8100fsr->i2c);
        if (ret) {
        PRINT_CRIT("snt_i2c_open() failed");
        return ret;
    }
    
    /* Feed 1v2 and 2v8 to the chip */
    if (of_property_read_bool(np, "grip_gpio1")){
	PRINT_INFO("Set lock to make sure firmware loading down");
	MUTEX_LOCK(&snt8100fsr_g->ap_lock);
	INIT_DELAYED_WORK(&RST_WORK, set_1V2_2V8_pin_func);
	workqueue_queue_work(&RST_WORK, 100);
	PRINT_INFO("WQ: call set_rst_pin_func");
    }

    /* Clay ioctl +++*/
    ret = sntSensor_miscRegister();
    if (ret < 0) {
		//return ret;
		PRINT_INFO("creat misc fail");
		//mutex_unlock(&snt8100fsr_g->ap_lock);
    }
	
    create_asusGripDebug_proc_file();
    create_GripI2cCheck_proc_file();
    create_GripFPCCheck_proc_file();
    create_Calibration_raw_data_proc_file();
    create_GripDisable_WakeLock_proc_file();

    create_GripSqueezeForce_proc_file();
    create_Grip_Squeeze_short_limit_proc_file();
    create_Grip_Squeeze_short_dur_proc_file();
    create_Grip_Squeeze_long_dur_proc_file();
	
    create_GripSqueeze_up_rate_proc_file();
    create_GripSqueeze_up_total_proc_file();
    create_GripSqueeze_drop_rate_proc_file();
    create_GripSqueeze_drop_total_proc_file();

    create_GripTap1Dur_proc_file();
    create_GripTap2Dur_proc_file();
    create_GripTap3Dur_proc_file();
    create_GripTap1Force_proc_file();
    create_GripTap2Force_proc_file();
    create_GripTap3Force_proc_file();
    create_GripTap1_TouchUpForce_proc_file();
    create_GripTap2_TouchUpForce_proc_file();
    create_GripTap3_TouchUpForce_proc_file();
/*
    create_Grip_Swipe1_En_proc_file();
    create_Grip_Swipe2_En_proc_file();
    create_Grip_Swipe3_En_proc_file();
    create_Grip_Swipe1_Velocity_proc_file();
    create_Grip_Swipe2_Velocity_proc_file();
    create_Grip_Swipe3_Velocity_proc_file();
    create_Grip_Swipe1_Len_proc_file();
    create_Grip_Swipe2_Len_proc_file();
    create_Grip_Swipe3_Len_proc_file();
*/
    create_Grip_frame_proc_file();
    create_Grip_raw_en_proc_file();
    create_Grip_en_proc_file();
    create_GripSqueezeEn_proc_file();
    create_GripTap1En_proc_file();
    create_GripTap2En_proc_file();
    create_GripTap3En_proc_file();
    create_GripTap_Sense_En_proc_file();

    /******* Dynamic Loading FW ********/
    create_Grip_FW_VER_proc_file();
    create_Grip_set_power_proc_file();

    /* Squeeze Factor */
    create_GripSQ_Bar0_factory_proc_file();
    create_GripSQ_Bar1_factory_proc_file();
    create_GripSQ_Bar2_factory_proc_file();
    /*
     * Upload the firmware asynchronously. When finished,
     * it will call start_event_processing() in event.c
     */
     
#ifdef UPLOAD_FIRMWARE
    upload_firmware(snt8100fsr, FIRMWARE_LOCATION);
#else
    start_event_processing(snt8100fsr);
#endif

    // Start our sysfs interface
    snt_sysfs_init(snt8100fsr_g, true);

    snt8100fsr_g->driver_status = GRIP_I2C_PROBE_DONE;
    PRINT_INFO("GRIP STATUS:%d", snt8100fsr_g->driver_status);
	
    PRINT_DEBUG("done");
    return 0;
}

static int snt_i2c_remove(struct i2c_client *i2c)
{
    struct snt8100fsr *snt8100fsr;
    PRINT_FUNC();

    /* Check if this is the i2c_wake_device used to awaken a sleeping
     * snt8100fsr chip.
     */
    snt8100fsr = i2c_get_clientdata(i2c);
    if (snt8100fsr && snt8100fsr->wake_i2c_device) {
        PRINT_INFO("removing wake_i2c_device");
        return 0;
    }

    // Else we are on the main i2c device, uninit the sysfs interface
    if (snt8100fsr_g) {
        snt_sysfs_init(snt8100fsr_g, false);
    }

    main_exit();
    return 0;
}

int SNT_SUSPEND_FLAG = 1;
static int snt_i2c_suspend(struct device *dev)
{
    int ret;
    PRINT_FUNC();
    PRINT_INFO("snt_i2c_suspend+++");
    ret = snt_suspend(dev);
	SNT_SUSPEND_FLAG = 0;
    PRINT_DEBUG("done");
    PRINT_INFO("snt_i2c_suspend---");
    return ret;
}

static int snt_i2c_resume(struct device *dev)
{
    int ret;
    PRINT_FUNC();
    PRINT_INFO("snt_i2c_resume+++");
     ret = snt_resume(dev);
	SNT_SUSPEND_FLAG = 1;
     PRINT_DEBUG("done");
    PRINT_INFO("snt_i2c_resume---");
    return ret;
}

/* Clay +++ */
//#ifdef CLAY_WANG_FLAG
static const struct of_device_id snt8100fsr_i2c_dt_ids[] = {
    { .compatible = "sentons, snt8100fsr-i2c" },
	{ .compatible = "qcom,grip",},
    { /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, snt8100fsr_i2c_dt_ids);
//#endif
/* Clay --- */

static struct i2c_device_id snt_i2c_id[] = {
    {SENTONS_DRIVER_NAME, 0},
    {"grip_sensor", 0},
    {},
};

static const struct dev_pm_ops snt_i2c_pm_ops = {
    .suspend = snt_i2c_suspend,
    .resume = snt_i2c_resume,
};

static struct i2c_driver snt_i2c_driver = {
    .driver = {
           .name = SENTONS_DRIVER_NAME,
           .owner = THIS_MODULE,
           //#ifdef CLAY_WANG_FLAG
	     .of_match_table = of_match_ptr(snt8100fsr_i2c_dt_ids),
	   //#endif
           .pm = &snt_i2c_pm_ops,
           },
    .probe = snt_i2c_probe,
    .remove = snt_i2c_remove,
    .id_table = snt_i2c_id,
};

MODULE_DEVICE_TABLE(i2c, snt_i2c_id);

module_i2c_driver(snt_i2c_driver);
#endif
