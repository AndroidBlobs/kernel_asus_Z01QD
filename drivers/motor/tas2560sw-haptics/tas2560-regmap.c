/*
 * ALSA SoC Texas Instruments TAS2560 High Performance 4W Smart Amplifier
 *
 * Copyright (C) 2016 Texas Instruments, Inc.
 *
 * Author: saiprasad
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 */
 
#ifdef CONFIG_TAS2560_REGMAP
//#define DEBUG
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/firmware.h>
#include <linux/regmap.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>
#include <sound/soc.h>

#include "tas2560.h"
#include "tas2560-core.h"
#include "tas2560-misc.h"
#include "tas2560-codec.h"

//compiler error for in expansion of macro 'container_of': struct tas2560_priv *pTAS2560 = container_of(cdev, struct tas2560_priv, cdev);
#include <linux/interrupt.h>
#include <linux/platform_device.h>
//end
//timer
#include <linux/timer.h>
//end
//get codec data
#include <linux/of.h>
#include <sound/soc.h>
//end
//For HID wait for completion
#include <linux/completion.h>
#include <linux/msm_drm_notify.h>

unsigned int delay_powerdown_tas2560 = 1000;

//get codec data for control i2s gpio
struct tas2560_priv *gTAS2560;
//extern int msm_cdc_pinctrl_select_active_state(struct device_node *np);
//extern int msm_cdc_pinctrl_select_sleep_state(struct device_node *np);
//struct snd_soc_codec *gCodec;
//end

//dongle external variables
extern uint8_t gDongleType;
extern bool suspend_by_hall;
extern int hid_to_gpio_set(u8 gpio, u8 value);
extern int hid_to_gpio_get(u8 gpio, u8 value);
//
//register Hid vote suspend
extern int hid_suspend_vote(int);
extern int hid_vote_register(char *);
extern int hid_vote_unregister(int, char *);
//end
//For HID wait for completion
extern int asus_wait4hid (void);
//end
static int tas2560_dev_write(struct tas2560_priv *pTAS2560, unsigned int nRegister,	unsigned int nValue);
static void tas2560_hw_reset(struct tas2560_priv *pTAS2560);
static void tas2560_sw_reset(struct tas2560_priv *pTAS2560);
static int tas2560_dev_read(	struct tas2560_priv *pTAS2560, unsigned int nRegister,	unsigned int *pValue);
static void irq_work_routine(struct work_struct *work);
extern bool get_Channel_PowerStatus(struct tas2560_priv *pTAS2560, int channel);
extern void set_Channel_powerStatus(struct tas2560_priv *pTAS2560, int channel, bool status);
extern int tas2560_simple_enable(struct tas2560_priv *pTAS2560, int channel, bool bEnable);
//for led driver interface
/* Dummy functions for brightness */
static
enum led_brightness tas2560_brightness_get(struct led_classdev *cdev)
{
	return 0;
}

static void tas2560_brightness_set(struct led_classdev *cdev,
					enum led_brightness level)
{
}
//end
//i2s interface control function
enum pinctrl_pin_state {
	STATE_DISABLE = 0, /* All pins are in sleep state */
	STATE_MI2S_ACTIVE,  /* IS2 = active, TDM = sleep */
	STATE_TDM_ACTIVE,  /* IS2 = sleep, TDM = active */
};

struct msm_pinctrl_info {
	struct pinctrl *pinctrl;
	struct pinctrl_state *mi2s_disable;
	struct pinctrl_state *tdm_disable;
	struct pinctrl_state *mi2s_active;
	struct pinctrl_state *tdm_active;
	enum pinctrl_pin_state curr_state;
};

struct msm_asoc_mach_data {
	u32 mclk_freq;
	int us_euro_gpio; /* used by gpio driver API */
	int usbc_en2_gpio; /* used by gpio driver API */
	struct device_node *us_euro_gpio_p; /* used by pinctrl API */
	struct pinctrl *usbc_en2_gpio_p; /* used by pinctrl API */
	struct device_node *hph_en1_gpio_p; /* used by pinctrl API */
	struct device_node *hph_en0_gpio_p; /* used by pinctrl API */
	struct device_node *quat_mi2s_gpio_p; /* ASUS_BSP Paul +++ */
	////ASUS_BSP_Vibrator +++, sec_mi2s, mark_guo
	struct device_node *sec_mi2s_gpio_p; /* used by pinctrl API for tas2557, tas2560 mi2s interface*/
	int (*pinctrl_select_active_state)(struct device_node *np);
	int (*pinctrl_select_sleep_state)(struct device_node *np);
	bool sec_i2s_status;//record sec_mi2s status
	bool sec_i2s_enable;//allow sec_mi2s enabling
	////ASUS_BSP_Vibrator ---
	struct snd_info_entry *codec_root;
	struct msm_pinctrl_info pinctrl_info;
	struct device_node *tert_mi2s_gpio_p; //Austin+++
};
bool tas2560_sec_mi2s_status(void){
	struct snd_soc_codec *codec=NULL;
	struct snd_soc_card *card=NULL;
	struct msm_asoc_mach_data *pdata=NULL;
	
	//printk("%s +++", __func__);	
	if(gTAS2560==NULL){
		printk("%s:gTAS2560 is NULL\n",__func__);
	}	
	if(gTAS2560->codec){
		codec=gTAS2560->codec;
	}else{
		printk("%s:enable I2S fail: codec is NULL\n",__func__);
	}
	
	if(codec){
		card = codec->component.card;
		if(card){
			pdata =	snd_soc_card_get_drvdata(card);
			//printk("%s ---", __func__);	
			return pdata->sec_i2s_status;
		}
	}
	//printk("%s ---", __func__);	
	return false;
}
void tas2560_sec_mi2s_enable(bool enable)
{
	struct snd_soc_codec *codec=NULL;
	struct snd_soc_card *card=NULL;
	struct msm_asoc_mach_data *pdata=NULL;
	int nResult = 0;
	
	printk("%s +++", __func__);	
	if(gTAS2560==NULL){
		printk("%s:gTAS2560 is NULL\n",__func__);
	}	
	if(gTAS2560->codec){
		codec=gTAS2560->codec;
	}else{
		printk("%s:enable I2S fail: codec is NULL\n",__func__);
	}
	
	if(codec){
		card = codec->component.card;
		if(card){
			pdata =	snd_soc_card_get_drvdata(card);
			if(enable){
				if(pdata->pinctrl_select_active_state){
					printk("%s:enable sec_mi2s\n",__func__);
					//pdata->pinctrl_select_active_state(pdata->sec_mi2s_gpio_p);
					if(pdata->sec_mi2s_gpio_p!=NULL){
						nResult=pdata->pinctrl_select_active_state(pdata->sec_mi2s_gpio_p);
						if(nResult>=0){
							gTAS2560->sec_mi2s_status=tas2560_sec_mi2s_status();
							//pdata->sec_i2s_status=true;
							gTAS2560->sec_mi2s_enable=true;
							pdata->sec_i2s_enable=true;
						}
						else{
							printk("%s:enable sec_mi2s fail\n",__func__);
							pdata->sec_i2s_status=false;
							gTAS2560->sec_mi2s_enable=false;
							pdata->sec_i2s_enable=false;
						}
					}
					else{
						printk("%s:enable sec_mi2s fail\n",__func__);
						pdata->sec_i2s_status=false;
						gTAS2560->sec_mi2s_enable=false;
						pdata->sec_i2s_enable=false;
					}
				}
				else
					printk("%s:enable sec_mi2s fail\n",__func__);
			}
			else{
				if(pdata->pinctrl_select_sleep_state){
					printk("%s:disable sec_mi2s\n",__func__);
					pdata->pinctrl_select_sleep_state(pdata->sec_mi2s_gpio_p);
					gTAS2560->sec_mi2s_status=tas2560_sec_mi2s_status();
					//pdata->sec_i2s_status=false;
					gTAS2560->sec_mi2s_enable=false;
					pdata->sec_i2s_enable=false;
				}
				else
					printk("%s:disable sec_mi2s fail\n",__func__);
			}
		}
	}
	printk("%s ---", __func__);	
}
EXPORT_SYMBOL(tas2560_sec_mi2s_enable);
//end

//i2s status check function
int check_i2c(struct tas2560_priv *pTAS2560){
	int nResult = 0;
	unsigned int nValue;

	if(pTAS2560->mnCurrentChannel&channel_left){
		pTAS2560->client->addr = pTAS2560->mnLAddr;
		nResult = pTAS2560->read(pTAS2560, TAS2560_FLAGS_1, &nValue);
		if(nResult<0) goto end;
	}
	if(pTAS2560->mnCurrentChannel&channel_right){
		pTAS2560->client->addr = pTAS2560->mnRAddr;
		nResult = pTAS2560->read(pTAS2560, TAS2560_FLAGS_1, &nValue);
		if(nResult<0) goto end;
	}
	if(pTAS2560->mnCurrentChannel&channel_phone){
		pTAS2560->client->addr = pTAS2560->mnPAddr;
		nResult = pTAS2560->read(pTAS2560, TAS2560_FLAGS_1, &nValue);
		if(nResult<0) goto end;
	}
end:
	printk("i2c status:0x%x %s\n", pTAS2560->client->addr, (nResult>0) ? "OK":"Fail");
	return nResult;
}
//end

// timer turnoff handler 
static void count_stop_timer(unsigned long _data)
{
	struct tas2560_priv *pTAS2560=(struct tas2560_priv *)_data;
	printk("%s +++", __func__);	
	if(pTAS2560==0)
		printk("tas2560-regmap.c:pTAS2560==null\n");
	//if (pTAS2560->mbPowerUp) {
	if (get_Channel_PowerStatus(pTAS2560, pTAS2560->mnCurrentChannel)) {
		printk("tas2560-regmap.c:power down haptic!\n");
		//tas2560_enable(pTAS2560,false);
	} else{
		printk("tas2560-regmap.c:haptic still power down...\n");	
	}
	printk("%s ---", __func__);	
}
//end

/* All sysfs show/store functions below */

#define HAP_STR_SIZE	128
/*
* tas2557_i2c_write_device : write single byte to device
* platform dependent, need platform specific support
*/
//sec_mi2s enable
static ssize_t tas2560_haptics_show_sec_mi2sEnable(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct tas2560_priv *pTAS2560 = container_of(cdev, struct tas2560_priv, cdev);
	struct snd_soc_codec *codec=NULL;
	struct snd_soc_card *card;
	struct msm_asoc_mach_data *pdata;
	
	codec=pTAS2560->codec;
	if(codec){
		card = codec->component.card;
		if(card){
			pdata =	snd_soc_card_get_drvdata(card);
			if(pdata)
				return snprintf(buf, PAGE_SIZE, "sec_mi2s_status:%s\n", pdata->sec_i2s_enable ? "Enable":"Disable");
			else
				return snprintf(buf, PAGE_SIZE, "msm_asoc_mach_data:%s\n", "NULL");
		}
		else
			return snprintf(buf, PAGE_SIZE, "sound card:%s\n", "NULL");
	}
	else 
		return snprintf(buf, PAGE_SIZE, "sound codec:%s\n", "NULL");
}

static ssize_t tas2560_haptics_store_sec_mi2sEnable(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	u32 val;
	int rc;
	
	printk("%s +++", __func__);	
	rc = kstrtouint(buf, 0, &val);
	if(val>0){
		tas2560_sec_mi2s_enable(true);
	}else{
		tas2560_sec_mi2s_enable(false);
	}	
	printk("%s ---", __func__);	
	return count;
}
//end
//read chip irq to check i2c status
static ssize_t tas2560_haptics_show_i2cStatus(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct tas2560_priv *pTAS2560 = container_of(cdev, struct tas2560_priv, cdev);
	unsigned int nValue;
	int nResult = 0;

	printk("%s +++", __func__);	
	if(pTAS2560->mnCurrentChannel&channel_left){
		pTAS2560->client->addr = pTAS2560->mnLAddr;
		nResult = pTAS2560->read(pTAS2560, TAS2560_FLAGS_1, &nValue);
		if (nResult >= 0){
			printk("Station Left Vibrator (i2c:0x4d) ok\n");
		}
	}
	if(pTAS2560->mnCurrentChannel&channel_right){
		pTAS2560->client->addr = pTAS2560->mnRAddr;
		nResult = pTAS2560->read(pTAS2560, TAS2560_FLAGS_1, &nValue);
		if (nResult >= 0){
			printk("Station Left Vibrator (i2c:0x4f) ok\n");
		}
	}
	if(pTAS2560->mnCurrentChannel&channel_phone){
		pTAS2560->client->addr = pTAS2560->mnPAddr;
		nResult = pTAS2560->read(pTAS2560, TAS2560_FLAGS_1, &nValue);
		if (nResult >= 0){
			printk("Phone Vibrator (i2c:0x4c) ok\n");
		}
	}
	printk("%s ---\n", __func__);	
	return snprintf(buf, PAGE_SIZE, "%d\n", nResult);
}

static ssize_t tas2560_haptics_store_i2cStatus(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}

static ssize_t tas2560_haptics_show_swReset(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", 0);
}

static ssize_t tas2560_haptics_store_swReset(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{

	int nResult;
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct tas2560_priv *pTAS2560 = container_of(cdev, struct tas2560_priv, cdev);
	
	printk("tas2560_haptics_store_swReset +++\n");
	/* Reset the chip */
	nResult = tas2560_dev_write(pTAS2560, TAS2560_SW_RESET_REG, 1);
	if (nResult < 0) {
		printk("swreset command fail:\n");
	}
	else{
		printk("swreset device ok");
	}
	printk("tas2560_haptics_store_swReset ---\n");
	return count;
}

static ssize_t tas2560_haptics_show_hwReset(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", 0);
}

static ssize_t tas2560_haptics_store_hwReset(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{

	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct tas2560_priv *pTAS2560 = container_of(cdev, struct tas2560_priv, cdev);
	
	printk("tas2560_haptics_store_hwReset +++\n");
	tas2560_LoadConfig(pTAS2560, true);
	printk("tas2560_haptics_store_hwReset ---\n");
	return count;
}


static ssize_t tas2560_haptics_show_state(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	/* For now nothing to show */
	return snprintf(buf, PAGE_SIZE, "%d\n", 0);
}

static ssize_t tas2560_haptics_store_state(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}

static ssize_t tas2560_haptics_show_channel(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct tas2560_priv *pTAS2560 = container_of(cdev, struct tas2560_priv, cdev);
	
	return snprintf(buf, PAGE_SIZE, "CurrentChannel=0x%0x\n", pTAS2560->mnCurrentChannel);
}

static ssize_t tas2560_haptics_store_channel(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct tas2560_priv *pTAS2560 = container_of(cdev, struct tas2560_priv, cdev);
	u32 val;
	int rc;

	printk("tas2560-regmap.c:%s +++\n", __func__);
	rc = kstrtouint(buf, 0, &val);
	pTAS2560->mnCurrentChannel=val;
	printk("tas2560-regmap.c:CurrentChannel=0x%0x\n",pTAS2560->mnCurrentChannel);	
	printk("tas2560-regmap.c:%s ---\n", __func__);
	return count;
}

static ssize_t tas2560_haptics_show_activate(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	/* For now nothing to show */
	return snprintf(buf, PAGE_SIZE, "%d\n", 0);
}


static ssize_t tas2560_haptics_store_activate(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	/* For now nothing to show */
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct tas2560_priv *pTAS2560 = container_of(cdev, struct tas2560_priv, cdev);
	u32 val;
	int rc;
	rc = kstrtouint(buf, 0, &val);
	if(val!=0){//enable
		//if (pTAS2560->mbPowerUp) {
		if (get_Channel_PowerStatus(pTAS2560, pTAS2560->mnCurrentChannel)) {
			printk("tas2560-regmap.c:tas2560_haptics_store_activate:haptic still power up...\n");
		} else{
			printk("tas2560-regmap.c:tas2560_haptics_store_activate:power up haptic!\n");	
			//tas2560_enable(pTAS2560,true);
		}
	}
	else{
			//if(!pTAS2560->mbPowerUp){
			if (!get_Channel_PowerStatus(pTAS2560, pTAS2560->mnCurrentChannel)) {
				printk("tas2560-regmap.c:tas2560_haptics_store_activate:haptic still power down...\n");
			}
			else{
				printk("tas2560-regmap.c:tas2560_haptics_store_activate:power down haptic!\n");	
				//mod_timer(&pTAS2560->count_stop_timer,jiffies + msecs_to_jiffies(delay_powerdown_tas2560));
				//tas2560_enable(pTAS2560,false);
			}
	}
	return count;
}

static ssize_t tas2560_haptics_show_vmax(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	/*
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct tas2557_priv *pTAS2557 = container_of(cdev, struct tas2557_priv, cdev);

	return snprintf(buf, PAGE_SIZE, "%d\n", pTAS2557->vmax_mv);
	*/
	return snprintf(buf, PAGE_SIZE, "%d\n", 0);
}

static ssize_t tas2560_haptics_store_vmax(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}

static ssize_t tas2560_haptics_show_duration(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", 0);
}

static ssize_t tas2560_haptics_store_duration(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}

static ssize_t tas2560_haptics_show_vibEnable(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct tas2560_priv *pTAS2560 = container_of(cdev, struct tas2560_priv, cdev);
	if(pTAS2560)
		return snprintf(buf, PAGE_SIZE, "pTAS2560->vib_en=%d\n", pTAS2560->vib_en);
	else
		return snprintf(buf, PAGE_SIZE, "data null\n");
}
static ssize_t tas2560_haptics_store_vibEnable(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct tas2560_priv *pTAS2560 = container_of(cdev, struct tas2560_priv, cdev);
	u32 val;
	int rc;

	printk("tas2560-regmap.c:%s +++\n", __func__);
	rc = kstrtouint(buf, 0, &val);
	if(val!=0){//enable
		printk("tas2560-regmap.c:%s:startup chanel=%d\n", __func__, pTAS2560->mnCurrentChannel);
		tas2560_simple_enable(pTAS2560, pTAS2560->mnCurrentChannel,true);//startup channel
		printk("tas2560-regmap.c:%s:start pTAS2560->mtimer\n", __func__);
		pTAS2560->vib_en=true;
		//if (pTAS2560->mbPowerUp) {
		if (get_Channel_PowerStatus(pTAS2560, pTAS2560->mnCurrentChannel)) {
			if (!hrtimer_active(&pTAS2560->mtimer)) {
				//dev_dbg(pTAS2560->dev, "%s, start check timer\n", __func__);
				printk("tas2560-regmap.c:%s, start check timer\n", __func__);
				hrtimer_start(&pTAS2560->mtimer,
					ns_to_ktime((u64)CHECK_PERIOD * NSEC_PER_MSEC), HRTIMER_MODE_REL);
			}
		}
	}
	else{
		//candel timer or irq_work
		pTAS2560->vib_en=false;
		printk("tas2560-regmap.c:%s:shutdown chanel=%d\n", __func__, pTAS2560->mnCurrentChannel);
		tas2560_simple_enable(pTAS2560, pTAS2560->mnCurrentChannel,false);//shutdown channel
		printk("tas2560-regmap.c:%s:cancel mtimer/irq_work\n", __func__);
		if (hrtimer_active(&pTAS2560->mtimer)) {
			//dev_dbg(pTAS2560->dev, "cancel die temp timer\n");
			printk("tas2560-regmap.c:%s:cancel die temp timer\n",__func__);
			hrtimer_cancel(&pTAS2560->mtimer);
		}

		if (delayed_work_pending(&pTAS2560->irq_work)) {
			//dev_dbg(pTAS2560->dev, "cancel IRQ work\n");
			printk("tas2560-regmap.c:%s:cancel IRQ work\n", __func__);
			cancel_delayed_work_sync(&pTAS2560->irq_work);
		}
		//end
	}
	printk("%s val=%d, ---\n", __func__, val);
	return count;
}

static ssize_t tas2560_haptics_show_dongleType(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct tas2560_priv *pTAS2560 = container_of(cdev, struct tas2560_priv, cdev);
	
	return snprintf(buf, PAGE_SIZE, "dongleType=%d\n", pTAS2560->dongleType);
}

static ssize_t tas2560_haptics_store_dongleType(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{

	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct tas2560_priv *pTAS2560 = container_of(cdev, struct tas2560_priv, cdev);
	u32 val;
	int rc;

	printk("tas2560-regmap.c:%s +++\n", __func__);
	//when plug-in station, avoiding the error: irq_work_routine, Critical ERROR B[0]_P[0]_R[42]= 0x0
	if (hrtimer_active(&pTAS2560->mtimer)) {
		dev_dbg(pTAS2560->dev, "cancel die temp timer\n");
		hrtimer_cancel(&pTAS2560->mtimer);
	}
	rc = kstrtouint(buf, 0, &val);
	//from station to phone
	if((pTAS2560->dongleType==2) && (val==0)){
		printk("unregister hid vote suspend ...\n");	
		pTAS2560->hid_suspend_id = hid_vote_unregister(pTAS2560->hid_suspend_id, "tas2560");
	}
	pTAS2560->dongleType=val;
	//check gDongleType again
	if(pTAS2560->dongleType!=gDongleType)
		pTAS2560->dongleType=gDongleType;
	if(pTAS2560->dongleType==0)//no dongle, phone only
		pTAS2560->mnCurrentChannel=channel_phone;
	if(pTAS2560->dongleType==1)//inbox, phone only
		pTAS2560->mnCurrentChannel=channel_phone;
	if(pTAS2560->dongleType==2)//Station
	{
		//switch to station, the phone power state has to set power down.
		//station must be power up again, so station is set power down first.
		set_Channel_powerStatus(pTAS2560,pTAS2560->mnCurrentChannel,false);
		pTAS2560->mnCurrentChannel=channel_both;
		printk("register hid vote suspend ...\n");	
		pTAS2560->hid_suspend_id = hid_vote_register("tas2560");
		set_Channel_powerStatus(pTAS2560,pTAS2560->mnCurrentChannel,false);
	}
	if(pTAS2560->dongleType==3)//DT, phone only
		pTAS2560->mnCurrentChannel=channel_phone;
	if(pTAS2560->dongleType==4)//Unknow dongle, phone only
		pTAS2560->mnCurrentChannel=channel_phone;
	printk("tas2560-regmap.c:dongleType=0x%0x\n",pTAS2560->dongleType);		
	printk("tas2560-regmap.c:CurrentChannel=0x%0x\n",pTAS2560->mnCurrentChannel);	
	printk("tas2560-regmap.c:%s ---\n", __func__);
	return count;
}
static ssize_t tas2560_haptics_show_enforceDongleTyle(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct tas2560_priv *pTAS2560 = container_of(cdev, struct tas2560_priv, cdev);
	
	return snprintf(buf, PAGE_SIZE, "dongleType=%d\n", pTAS2560->dongleType);
}

static ssize_t tas2560_haptics_store_enforceDongleTyle(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{

	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct tas2560_priv *pTAS2560 = container_of(cdev, struct tas2560_priv, cdev);
	u32 val;
	int rc;

	printk("tas2560-regmap.c:%s +++\n", __func__);
	rc = kstrtouint(buf, 0, &val);
	if(val>4)
		val=0;
	pTAS2560->dongleType=val;
	if(pTAS2560->dongleType==0)//no dongle, phone only
		pTAS2560->mnCurrentChannel=channel_phone;
	if(pTAS2560->dongleType==1)//inbox, phone only
		pTAS2560->mnCurrentChannel=channel_phone;
	if(pTAS2560->dongleType==2)//Station
		pTAS2560->mnCurrentChannel=channel_both;
	if(pTAS2560->dongleType==3)//DT, phone only
		pTAS2560->mnCurrentChannel=channel_phone;
	if(pTAS2560->dongleType==4)//Unknow dongle, phone only
		pTAS2560->mnCurrentChannel=channel_phone;
	printk("tas2560-regmap.c:dongleType=0x%0x\n",pTAS2560->dongleType);		
	printk("tas2560-regmap.c:CurrentChannel=0x%0x\n",pTAS2560->mnCurrentChannel);	
	printk("tas2560-regmap.c:%s ---\n", __func__);
	return count;
}


static struct device_attribute tas2560_haptics_attrs[] = {
	__ATTR(state, 				0664, tas2560_haptics_show_state, 				tas2560_haptics_store_state),
	__ATTR(channel, 			0664, tas2560_haptics_show_channel, 			tas2560_haptics_store_channel),
	__ATTR(activate, 			0664, tas2560_haptics_show_activate, 			tas2560_haptics_store_activate),
	__ATTR(vmax_mv, 			0664, tas2560_haptics_show_vmax, 				tas2560_haptics_store_vmax),
	__ATTR(hwReset,				0664, tas2560_haptics_show_hwReset,				tas2560_haptics_store_hwReset),
	__ATTR(swReset,				0664, tas2560_haptics_show_swReset,				tas2560_haptics_store_swReset),
	__ATTR(i2cStatus, 			0664, tas2560_haptics_show_i2cStatus, 			tas2560_haptics_store_i2cStatus),
	__ATTR(dongleType, 			0664, tas2560_haptics_show_dongleType, 			tas2560_haptics_store_dongleType),
	__ATTR(vibEnable, 			0664, tas2560_haptics_show_vibEnable, 			tas2560_haptics_store_vibEnable),
	__ATTR(duration, 			0664, tas2560_haptics_show_duration, 			tas2560_haptics_store_duration),
	__ATTR(enforceDongleTyle, 	0664, tas2560_haptics_show_enforceDongleTyle, 	tas2560_haptics_store_enforceDongleTyle),
	__ATTR(sec_mi2sEnable, 		0664, tas2560_haptics_show_sec_mi2sEnable, 		tas2560_haptics_store_sec_mi2sEnable),
};
//end

static int tas2560_change_book_page(struct tas2560_priv *pTAS2560,
	int book, int page)
{
	int nResult = 0;

	if ((pTAS2560->mnCurrentBook == book)
		&& (pTAS2560->mnCurrentPage == page))
		goto end;

	if (pTAS2560->mnCurrentBook != book) {
		if(pTAS2560->mnCurrentChannel&channel_left){
			pTAS2560->client->addr = pTAS2560->mnLAddr;
			nResult = regmap_write(pTAS2560->regmap, TAS2560_BOOKCTL_PAGE, 0);//station left
			if (nResult < 0) {
				dev_err(pTAS2560->dev, "%s, Station Left ERROR, L=%d, E=%d\n",
					__func__, __LINE__, nResult);
				//goto end;
			}
		}
		if(pTAS2560->mnCurrentChannel&channel_right){
			pTAS2560->client->addr = pTAS2560->mnRAddr;
			nResult = regmap_write(pTAS2560->regmap, TAS2560_BOOKCTL_PAGE, 0);//station right
			if (nResult < 0) {
				dev_err(pTAS2560->dev, "%s, Station Right ERROR, L=%d, E=%d\n",
					__func__, __LINE__, nResult);
				//goto end;
			}
		}
		if(pTAS2560->mnCurrentChannel&channel_phone){
			pTAS2560->client->addr=pTAS2560->mnPAddr;
			nResult = regmap_write(pTAS2560->regmap, TAS2560_BOOKCTL_PAGE, 0);//phone
			if (nResult < 0) {
				dev_err(pTAS2560->dev, "%s, Phone ERROR, L=%d, E=%d\n",
					__func__, __LINE__, nResult);
				//goto end;
			}
		}
		pTAS2560->mnCurrentPage = 0;
		if(pTAS2560->mnCurrentChannel&channel_left){		
			pTAS2560->client->addr = pTAS2560->mnLAddr;
			nResult = regmap_write(pTAS2560->regmap, TAS2560_BOOKCTL_REG, book); //station left
			if (nResult < 0) {
				dev_err(pTAS2560->dev, "%s, Station Left ERROR, L=%d, E=%d\n",
					__func__, __LINE__, nResult);
				//goto end;
			}
		}
		if(pTAS2560->mnCurrentChannel&channel_right){		
			pTAS2560->client->addr = pTAS2560->mnRAddr;
			nResult = regmap_write(pTAS2560->regmap, TAS2560_BOOKCTL_REG, book); //station right
			if (nResult < 0) {
				dev_err(pTAS2560->dev, "%s, Station Right ERROR, L=%d, E=%d\n",
					__func__, __LINE__, nResult);
				//goto end;
			}
		}
		if(pTAS2560->mnCurrentChannel&channel_phone){
			pTAS2560->client->addr=pTAS2560->mnPAddr;
			nResult = regmap_write(pTAS2560->regmap, TAS2560_BOOKCTL_REG, book); //phone
			if (nResult < 0) {
				dev_err(pTAS2560->dev, "%s, Phone ERROR, L=%d, E=%d\n",
					__func__, __LINE__, nResult);
				//goto end;
			}
		}
		pTAS2560->mnCurrentBook = book;
		/*
		pTAS2560->mnPCurrentBook = book; //phone
		pTAS2560->mnLCurrentBook = book; //station left
		pTAS2560->mnRCurrentBook = book; //station right
		*/
	}

	if (pTAS2560->mnCurrentPage != page) {
		if(pTAS2560->mnCurrentChannel&channel_left){		
			pTAS2560->client->addr = pTAS2560->mnLAddr;
			nResult = regmap_write(pTAS2560->regmap, TAS2560_BOOKCTL_PAGE, page); //station left
			if (nResult < 0) {
				dev_err(pTAS2560->dev, "%s, Station Left ERROR, L=%d, E=%d\n",
					__func__, __LINE__, nResult);
				//goto end;
			}
		}
		if(pTAS2560->mnCurrentChannel&channel_right){
			pTAS2560->client->addr = pTAS2560->mnRAddr;
			nResult = regmap_write(pTAS2560->regmap, TAS2560_BOOKCTL_PAGE, page); //station right
			if (nResult < 0) {
				dev_err(pTAS2560->dev, "%s, Station Right ERROR, L=%d, E=%d\n",
					__func__, __LINE__, nResult);
				//goto end;
			}
		}
		if(pTAS2560->mnCurrentChannel&channel_phone){
			pTAS2560->client->addr=pTAS2560->mnPAddr;
			nResult = regmap_write(pTAS2560->regmap, TAS2560_BOOKCTL_PAGE, page); //phone
			if (nResult < 0) {
				dev_err(pTAS2560->dev, "%s, Phone ERROR, L=%d, E=%d\n",
					__func__, __LINE__, nResult);
				//goto end;
			}
		}
		pTAS2560->mnCurrentPage = page;
		/*
		pTAS2560->mnPCurrentPage = page; //phone
		pTAS2560->mnLCurrentPage = page; //station left
		pTAS2560->mnRCurrentPage = page; //station right
		*/
	}

end:
	return nResult;
}

static int tas2560_dev_read(struct tas2560_priv *pTAS2560,
	unsigned int reg, unsigned int *pValue)
{
	int nResult = 0;
	
	mutex_lock(&pTAS2560->dev_lock);

	nResult = tas2560_change_book_page(pTAS2560,
		TAS2560_BOOK_ID(reg), TAS2560_PAGE_ID(reg));
	if (nResult < 0)
		goto end;

	nResult = regmap_read(pTAS2560->regmap, TAS2560_PAGE_REG(reg), pValue);
	if (nResult < 0)
		dev_err(pTAS2560->dev, "%s, ERROR, L=%d, E=%d\n",
			__func__, __LINE__, nResult);
	else
		dev_dbg(pTAS2560->dev, "%s: BOOK:PAGE:REG %u:%u:%u\n", __func__,
			TAS2560_BOOK_ID(reg), TAS2560_PAGE_ID(reg),
			TAS2560_PAGE_REG(reg));

end:
	mutex_unlock(&pTAS2560->dev_lock);
	return nResult;
}

static int tas2560_dev_write(struct tas2560_priv *pTAS2560,
	unsigned int reg, unsigned int value)
{
	int nResult = 0;

	mutex_lock(&pTAS2560->dev_lock);

	nResult = tas2560_change_book_page(pTAS2560,
		TAS2560_BOOK_ID(reg), TAS2560_PAGE_ID(reg));
	if (nResult < 0)
		goto end;

	if(pTAS2560->mnCurrentChannel&channel_left){
		pTAS2560->client->addr = pTAS2560->mnLAddr;
		nResult = regmap_write(pTAS2560->regmap, TAS2560_PAGE_REG(reg), value);
		if (nResult < 0)
			dev_err(pTAS2560->dev, "%s, Station Left ERROR, L=%d, E=%d\n",
				__func__, __LINE__, nResult);
		else
			dev_dbg(pTAS2560->dev, "%s: Station Left BOOK:PAGE:REG %u:%u:%u, VAL: 0x%02x\n",
				__func__, TAS2560_BOOK_ID(reg), TAS2560_PAGE_ID(reg),
				TAS2560_PAGE_REG(reg), value);
	}
	if(pTAS2560->mnCurrentChannel&channel_right){
		pTAS2560->client->addr = pTAS2560->mnRAddr;
		nResult = regmap_write(pTAS2560->regmap, TAS2560_PAGE_REG(reg), value);
		if (nResult < 0)
			dev_err(pTAS2560->dev, "%s, Station Right ERROR, L=%d, E=%d\n",
				__func__, __LINE__, nResult);
		else
			dev_dbg(pTAS2560->dev, "%s: Station Right BOOK:PAGE:REG %u:%u:%u, VAL: 0x%02x\n",
				__func__, TAS2560_BOOK_ID(reg), TAS2560_PAGE_ID(reg),
				TAS2560_PAGE_REG(reg), value);
	}
	if(pTAS2560->mnCurrentChannel&channel_phone){	
		pTAS2560->client->addr=pTAS2560->mnPAddr;
		nResult = regmap_write(pTAS2560->regmap, TAS2560_PAGE_REG(reg), value);
		if (nResult < 0)
			dev_err(pTAS2560->dev, "%s, Phone ERROR, L=%d, E=%d\n",
				__func__, __LINE__, nResult);
		else
			dev_dbg(pTAS2560->dev, "%s: Phone BOOK:PAGE:REG %u:%u:%u, VAL: 0x%02x\n",
				__func__, TAS2560_BOOK_ID(reg), TAS2560_PAGE_ID(reg),
				TAS2560_PAGE_REG(reg), value);
	}
end:
	mutex_unlock(&pTAS2560->dev_lock);
	//printk("%s:write tas2560 register(I2C)\n", __func__);
	return nResult;
}

static int tas2560_dev_bulk_write(struct tas2560_priv *pTAS2560,
	unsigned int reg, unsigned char *pData, unsigned int nLength)
{
	int nResult = 0;

	mutex_lock(&pTAS2560->dev_lock);

	nResult = tas2560_change_book_page(pTAS2560,
		TAS2560_BOOK_ID(reg), TAS2560_PAGE_ID(reg));
	if (nResult < 0)
		goto end;

	if(pTAS2560->mnCurrentChannel&channel_left){
		pTAS2560->client->addr = pTAS2560->mnLAddr;
		nResult = regmap_bulk_write(pTAS2560->regmap, TAS2560_PAGE_REG(reg), pData, nLength);
		if (nResult < 0)
			dev_err(pTAS2560->dev, "%s, Station Left ERROR, L=%d, E=%d\n",
				__func__, __LINE__, nResult);
		else
			dev_dbg(pTAS2560->dev, "%s: Station Right BOOK:PAGE:REG %u:%u:%u, len: 0x%02x\n",
				__func__, TAS2560_BOOK_ID(reg), TAS2560_PAGE_ID(reg),
				TAS2560_PAGE_REG(reg), nLength);
	}
	if(pTAS2560->mnCurrentChannel&channel_right){
		pTAS2560->client->addr = pTAS2560->mnRAddr;
		nResult = regmap_bulk_write(pTAS2560->regmap, TAS2560_PAGE_REG(reg), pData, nLength);
		if (nResult < 0)
			dev_err(pTAS2560->dev, "%s, Station Right ERROR, L=%d, E=%d\n",
				__func__, __LINE__, nResult);
		else
			dev_dbg(pTAS2560->dev, "%s: Station Right BOOK:PAGE:REG %u:%u:%u, len: 0x%02x\n",
				__func__, TAS2560_BOOK_ID(reg), TAS2560_PAGE_ID(reg),
				TAS2560_PAGE_REG(reg), nLength);
	}
	if(pTAS2560->mnCurrentChannel&channel_phone){
		pTAS2560->client->addr=pTAS2560->mnPAddr;
		nResult = regmap_bulk_write(pTAS2560->regmap, TAS2560_PAGE_REG(reg), pData, nLength);
		if (nResult < 0)
			dev_err(pTAS2560->dev, "%s, Station Right ERROR, L=%d, E=%d\n",
				__func__, __LINE__, nResult);
		else
			dev_dbg(pTAS2560->dev, "%s: Station Right BOOK:PAGE:REG %u:%u:%u, len: 0x%02x\n",
				__func__, TAS2560_BOOK_ID(reg), TAS2560_PAGE_ID(reg),
				TAS2560_PAGE_REG(reg), nLength);
	}
end:
	mutex_unlock(&pTAS2560->dev_lock);
	nResult=0;
	return nResult;
}

static int tas2560_dev_bulk_read(struct tas2560_priv *pTAS2560,
	unsigned int reg, unsigned char *pData, unsigned int nLength)
{
	int nResult = 0;

	mutex_lock(&pTAS2560->dev_lock);

	nResult = tas2560_change_book_page(pTAS2560,
		TAS2560_BOOK_ID(reg), TAS2560_PAGE_ID(reg));
	if (nResult < 0)
		goto end;

	nResult = regmap_bulk_read(pTAS2560->regmap, TAS2560_PAGE_REG(reg), pData, nLength);
	if (nResult < 0)
		dev_err(pTAS2560->dev, "%s, ERROR, L=%d, E=%d\n",
			__func__, __LINE__, nResult);
	else
		dev_dbg(pTAS2560->dev, "%s: BOOK:PAGE:REG %u:%u:%u, len: 0x%02x\n",
			__func__, TAS2560_BOOK_ID(reg), TAS2560_PAGE_ID(reg),
			TAS2560_PAGE_REG(reg), nLength);
end:
	mutex_unlock(&pTAS2560->dev_lock);
	return nResult;
}

static int tas2560_dev_update_bits(struct tas2560_priv *pTAS2560, unsigned int reg,
			 unsigned int mask, unsigned int value)
{
	int nResult = 0;

	mutex_lock(&pTAS2560->dev_lock);
	nResult = tas2560_change_book_page(pTAS2560,
		TAS2560_BOOK_ID(reg), TAS2560_PAGE_ID(reg));
	if (nResult < 0){
		printk("%s:failed\n",__func__);
		goto end;
	}

	if(pTAS2560->mnCurrentChannel&channel_left){
		pTAS2560->client->addr = pTAS2560->mnLAddr;
		nResult = regmap_update_bits(pTAS2560->regmap, TAS2560_PAGE_REG(reg), mask, value);
		if (nResult < 0)
			dev_err(pTAS2560->dev, "%s, Station Left ERROR, L=%d, E=%d\n",
				__func__, __LINE__, nResult);
		else
			dev_dbg(pTAS2560->dev, "%s: Station Right BOOK:PAGE:REG %u:%u:%u, mask: 0x%x, val=0x%x\n",
				__func__, TAS2560_BOOK_ID(reg), TAS2560_PAGE_ID(reg),
				TAS2560_PAGE_REG(reg), mask, value);
	}
	if(pTAS2560->mnCurrentChannel&channel_right){
		pTAS2560->client->addr = pTAS2560->mnRAddr;
		nResult = regmap_update_bits(pTAS2560->regmap, TAS2560_PAGE_REG(reg), mask, value);
		if (nResult < 0)
			dev_err(pTAS2560->dev, "%s, Station Right ERROR, L=%d, E=%d\n",
				__func__, __LINE__, nResult);
		else
			dev_dbg(pTAS2560->dev, "%s: Station Right BOOK:PAGE:REG %u:%u:%u, mask: 0x%x, val=0x%x\n",
				__func__, TAS2560_BOOK_ID(reg), TAS2560_PAGE_ID(reg),
				TAS2560_PAGE_REG(reg), mask, value);
	}
	if(pTAS2560->mnCurrentChannel&channel_phone){
		pTAS2560->client->addr=pTAS2560->mnPAddr;
		nResult = regmap_update_bits(pTAS2560->regmap, TAS2560_PAGE_REG(reg), mask, value);
		if (nResult < 0)
			dev_err(pTAS2560->dev, "%s, Phone ERROR, L=%d, E=%d\n",
				__func__, __LINE__, nResult);
		else
			dev_dbg(pTAS2560->dev, "%s: Station Right BOOK:PAGE:REG %u:%u:%u, mask: 0x%x, val=0x%x\n",
				__func__, TAS2560_BOOK_ID(reg), TAS2560_PAGE_ID(reg),
				TAS2560_PAGE_REG(reg), mask, value);
	}
end:
	mutex_unlock(&pTAS2560->dev_lock);
	nResult=0;
	return nResult;
}

static bool tas2560_volatile(struct device *dev, unsigned int reg)
{
	return false;
}

static bool tas2560_writeable(struct device *dev, unsigned int reg)
{
	return true;
}

static const struct regmap_config tas2560_i2c_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.writeable_reg = tas2560_writeable,
	.volatile_reg = tas2560_volatile,
	.cache_type = REGCACHE_NONE,
	.max_register = 128,
};

static void tas2560_hw_reset(struct tas2560_priv *pTAS2560)
{
	if(pTAS2560->mnCurrentChannel&channel_both){
		printk("tas2560-regmap.c:tas2560_hw_reset:do Station hw reset.\n");
		hid_to_gpio_set(pTAS2560->mnSResetGPIO,0);
		pTAS2560->station_reset_enable=false;
		msleep(5);
		hid_to_gpio_set(pTAS2560->mnSResetGPIO,1);
		pTAS2560->station_reset_enable=true;
		msleep(2);
	}
	if(pTAS2560->mnCurrentChannel&channel_phone){
		if (gpio_is_valid(pTAS2560->mnResetGPIO)) {
			printk("tas2560-regmap.c:tas2560_hw_reset:do Phone hw reset.\n");
			gpio_direction_output(pTAS2560->mnResetGPIO, 0);
			msleep(5);
			gpio_direction_output(pTAS2560->mnResetGPIO, 1);
			msleep(2);
		}
	}
	if(pTAS2560->mnCurrentChannel&channel_left){
		pTAS2560->mnLCurrentBook = -1;
		pTAS2560->mnLCurrentPage = -1;
	}
	if(pTAS2560->mnCurrentChannel&channel_right){
		pTAS2560->mnRCurrentBook = -1;
		pTAS2560->mnRCurrentPage = -1;
	}
	if(pTAS2560->mnCurrentChannel&channel_phone){
		pTAS2560->mnPCurrentBook = -1;
		pTAS2560->mnPCurrentPage = -1;
	}

	pTAS2560->mnCurrentBook = -1;
	pTAS2560->mnCurrentPage = -1;
}
static void tas2560_sw_reset(struct tas2560_priv *pTAS2560)
{
	int nResult;

	printk("tas2560-regmap.c:%s +++\n", __func__);
	/* Reset the chip */
	nResult = tas2560_dev_write(pTAS2560, TAS2560_SW_RESET_REG, 1);
	if (nResult < 0) {
		printk("tas2560-regmap.c:tas2560_sw_reset:sw reset command fail.\n");
	}
	else{
		printk("tas2560-regmap.c:tas2560_sw_reset:sw reset device ok.\n");
	}
	if(pTAS2560->mnCurrentChannel&channel_left){
		pTAS2560->mnLCurrentBook = -1;
		pTAS2560->mnLCurrentPage = -1;
	}
	if(pTAS2560->mnCurrentChannel&channel_right){
		pTAS2560->mnRCurrentBook = -1;
		pTAS2560->mnRCurrentPage = -1;
	}
	if(pTAS2560->mnCurrentChannel&channel_phone){
		pTAS2560->mnPCurrentBook = -1;
		pTAS2560->mnPCurrentPage = -1;
	}
	pTAS2560->mnCurrentBook = -1;
	pTAS2560->mnCurrentPage = -1;
	printk("tas2560-regmap.c:%s ---\n", __func__);
}

void tas2560_clearIRQ(struct tas2560_priv *pTAS2560)
{
	unsigned int nValue;
	int nResult = 0;
	//unsigned char curAddr=pTAS2560->client->addr;

	if(pTAS2560->mnCurrentChannel&channel_left){
		pTAS2560->client->addr = pTAS2560->mnLAddr;
		nResult = pTAS2560->read(pTAS2560, TAS2560_FLAGS_1, &nValue);
		if (nResult >= 0)
			pTAS2560->read(pTAS2560, TAS2560_FLAGS_2, &nValue);
		else
			printk("Left clearIRQ\n");
	}
	if(pTAS2560->mnCurrentChannel&channel_right){
		pTAS2560->client->addr = pTAS2560->mnRAddr;
		nResult = pTAS2560->read(pTAS2560, TAS2560_FLAGS_1, &nValue);
		if (nResult >= 0)
			pTAS2560->read(pTAS2560, TAS2560_FLAGS_2, &nValue);
		else
			printk("Right clearIRQ\n");
	}
	if(pTAS2560->mnCurrentChannel&channel_phone){
		pTAS2560->client->addr = pTAS2560->mnPAddr;
		nResult = pTAS2560->read(pTAS2560, TAS2560_FLAGS_1, &nValue);
		if (nResult >= 0)
			pTAS2560->read(pTAS2560, TAS2560_FLAGS_2, &nValue);
		else
			printk("Phone clearIRQ\n");
	}
}

void tas2560_enableIRQ(struct tas2560_priv *pTAS2560, bool enable)
{
	if (enable) {
		if (pTAS2560->mbIRQEnable)
			return;

		if (gpio_is_valid(pTAS2560->mnIRQGPIO))
			enable_irq(pTAS2560->mnIRQ);

		schedule_delayed_work(&pTAS2560->irq_work, msecs_to_jiffies(10));
		pTAS2560->mbIRQEnable = true;
	} else {
		if (!pTAS2560->mbIRQEnable)
			return;

		if (gpio_is_valid(pTAS2560->mnIRQGPIO))
			disable_irq_nosync(pTAS2560->mnIRQ);
		pTAS2560->mbIRQEnable = false;
	}
}
static int count_critical_error=0;
static void irq_work_routine(struct work_struct *work)
{
	struct tas2560_priv *pTAS2560 =
		container_of(work, struct tas2560_priv, irq_work.work);
	unsigned int nDevInt1Status = 0, nDevInt2Status = 0;
	int nCounter = 2;
	int nResult = 0;

	//printk("tas2560-regmap.c:%s +++\n", __func__);
#ifdef CONFIG_TAS2560_CODEC
	mutex_lock(&pTAS2560->codec_lock);
#endif

#ifdef CONFIG_TAS2560_MISC
	mutex_lock(&pTAS2560->file_lock);
#endif
	if(!pTAS2560->vib_en){
		printk("%s, Channel=%d, Not ready ...\n", __func__, pTAS2560->mnCurrentChannel);
		goto end;
	}
	if (pTAS2560->mbRuntimeSuspend) {
		//dev_info(pTAS2560->dev, "%s, Runtime Suspended\n", __func__);
		printk("%s, Channel=%d, Runtime Suspended\n", __func__, pTAS2560->mnCurrentChannel);
		goto end;
	}
	if(gDongleType==2){//only station
		if (suspend_by_hall) {
			printk("%s, Station Cover is %s\n", __func__, suspend_by_hall?"Off":"On");
			goto end;
		}
	}

	//if (!pTAS2560->mbPowerUp) {
	if (!get_Channel_PowerStatus(pTAS2560, pTAS2560->mnCurrentChannel)) {
		dev_info(pTAS2560->dev, "%s, Channel=%d, device not powered\n", __func__, pTAS2560->mnCurrentChannel);
		goto end;
	}

	nResult = tas2560_dev_write(pTAS2560, TAS2560_INT_GEN_REG, 0x00);
	if (nResult < 0)
		goto reload;

	nResult = tas2560_dev_read(pTAS2560, TAS2560_FLAGS_1, &nDevInt1Status);
	if (nResult >= 0)
		nResult = tas2560_dev_read(pTAS2560, TAS2560_FLAGS_2, &nDevInt2Status);
	else
		goto reload;

	if (((nDevInt1Status & 0xfc) != 0) || ((nDevInt2Status & 0xc0) != 0)) {
		/* in case of INT_OC, INT_UV, INT_OT, INT_BO, INT_CL, INT_CLK1, INT_CLK2 */
		dev_dbg(pTAS2560->dev, "IRQ critical Error : 0x%x, 0x%x\n",
			nDevInt1Status, nDevInt2Status);

		if (nDevInt1Status & 0x80) {
			pTAS2560->mnErrCode |= ERROR_OVER_CURRENT;
			dev_err(pTAS2560->dev, "SPK over current!\n");
		} else
			pTAS2560->mnErrCode &= ~ERROR_OVER_CURRENT;

		if (nDevInt1Status & 0x40) {
			pTAS2560->mnErrCode |= ERROR_UNDER_VOLTAGE;
			dev_err(pTAS2560->dev, "SPK under voltage!\n");
		} else
			pTAS2560->mnErrCode &= ~ERROR_UNDER_VOLTAGE;

		if (nDevInt1Status & 0x20) {
			pTAS2560->mnErrCode |= ERROR_CLK_HALT;
			dev_err(pTAS2560->dev, "clk halted!\n");
		} else
			pTAS2560->mnErrCode &= ~ERROR_CLK_HALT;

		if (nDevInt1Status & 0x10) {
			pTAS2560->mnErrCode |= ERROR_DIE_OVERTEMP;
			dev_err(pTAS2560->dev, "die over temperature!\n");
		} else
			pTAS2560->mnErrCode &= ~ERROR_DIE_OVERTEMP;

		if (nDevInt1Status & 0x08) {
			pTAS2560->mnErrCode |= ERROR_BROWNOUT;
			dev_err(pTAS2560->dev, "brownout!\n");
		} else
			pTAS2560->mnErrCode &= ~ERROR_BROWNOUT;

		if (nDevInt1Status & 0x04) {
			pTAS2560->mnErrCode |= ERROR_CLK_LOST;
		} else
			pTAS2560->mnErrCode &= ~ERROR_CLK_LOST;

		if (nDevInt2Status & 0x80) {
			pTAS2560->mnErrCode |= ERROR_CLK_DET1;
			dev_err(pTAS2560->dev, "clk detection 1!\n");
		} else
			pTAS2560->mnErrCode &= ~ERROR_CLK_DET1;

		if (nDevInt2Status & 0x40) {
			pTAS2560->mnErrCode |= ERROR_CLK_DET2;
			dev_err(pTAS2560->dev, "clk detection 2!\n");
		} else
			pTAS2560->mnErrCode &= ~ERROR_CLK_DET2;

		goto reload;
	} else {
		dev_dbg(pTAS2560->dev, "IRQ status : 0x%x, 0x%x\n",
				nDevInt1Status, nDevInt2Status);
		nCounter = 2;

		while (nCounter > 0) {
			nResult = tas2560_dev_read(pTAS2560, TAS2560_POWER_UP_FLAG_REG, &nDevInt1Status);
			if (nResult < 0)
				goto reload;

			if ((nDevInt1Status & 0xc0) == 0xc0)
				break;

			nCounter--;
			if (nCounter > 0) {
				/* in case check pow status just after power on TAS2560 */
				dev_dbg(pTAS2560->dev, "PowSts B: 0x%x, check again after 10ms\n",
					nDevInt1Status);
				msleep(10);
			}
		}
		if ((nDevInt1Status & 0xc0) != 0xc0) {
			dev_err(pTAS2560->dev, "%s, %s, Critical ERROR(%d) B[%d]_P[%d]_R[%d]= 0x%x, sec_mi2s_status=%s\n",
				__func__,
				(pTAS2560->mnCurrentChannel==channel_phone) ? "Phone":"Station",count_critical_error, 
				TAS2560_BOOK_ID(TAS2560_POWER_UP_FLAG_REG),
				TAS2560_PAGE_ID(TAS2560_POWER_UP_FLAG_REG),
				TAS2560_PAGE_REG(TAS2560_POWER_UP_FLAG_REG),
				nDevInt1Status,
				tas2560_sec_mi2s_status() ? "true":"false");
			pTAS2560->mnErrCode |= ERROR_CLASSD_PWR;
			count_critical_error++;
			goto reload;
		}
		pTAS2560->mnErrCode &= ~ERROR_CLASSD_PWR;
		if(count_critical_error){
			printk("tas2560-regmap.c:%s, Critical ERROR(%d) and recovery ok \n", __func__, count_critical_error);
			count_critical_error=0;
		}
	}

	nResult = tas2560_dev_write(pTAS2560, TAS2560_INT_GEN_REG, 0xff);
	if (nResult < 0)
		goto reload;

	goto end;

reload:
	/* hardware reset and reload */
	if(count_critical_error<10){
		if(!tas2560_sec_mi2s_status()){
			printk("[VIB]%s:tas2560_sec_mi2s_enable\n",__func__);
			tas2560_sec_mi2s_enable(true);//fixed: Critical ERROR, since i2s is in sleep state
		}
		tas2560_LoadConfig(pTAS2560, true);
	}
end:
	if(count_critical_error<10){
		if (!hrtimer_active(&pTAS2560->mtimer)) {
			dev_dbg(pTAS2560->dev, "%s, start timer\n", __func__);
			//printk("tas2560-regmap.c:%s, start timer\n", __func__);
			hrtimer_start(&pTAS2560->mtimer,
				ns_to_ktime((u64)CHECK_PERIOD * NSEC_PER_MSEC), HRTIMER_MODE_REL);
		}
	}
	else
		printk("tas2560-regmap.c:%s, count_critical_error(%d) too many...\n", __func__, count_critical_error);
#ifdef CONFIG_TAS2560_MISC
	mutex_unlock(&pTAS2560->file_lock);
#endif

#ifdef CONFIG_TAS2560_CODEC
	mutex_unlock(&pTAS2560->codec_lock);
#endif
	//printk("tas2560-regmap.c:%s ---\n", __func__);
}

static enum hrtimer_restart timer_func(struct hrtimer *timer)
{
	struct tas2560_priv *pTAS2560 = container_of(timer, struct tas2560_priv, mtimer);

	//if (pTAS2560->mbPowerUp) {
	if (get_Channel_PowerStatus(pTAS2560, pTAS2560->mnCurrentChannel)) {
		if (!delayed_work_pending(&pTAS2560->irq_work)){
			schedule_delayed_work(&pTAS2560->irq_work, msecs_to_jiffies(20));
		}
	}

	return HRTIMER_NORESTART;
}

static irqreturn_t tas2560_irq_handler(int irq, void *dev_id)
{
	struct tas2560_priv *pTAS2560 = (struct tas2560_priv *)dev_id;

	tas2560_enableIRQ(pTAS2560, false);

	printk("tas2560-regmap.c:%s\n", __func__);
	/* get IRQ status after 100 ms */
	if (!delayed_work_pending(&pTAS2560->irq_work)){
		schedule_delayed_work(&pTAS2560->irq_work, msecs_to_jiffies(100));
		//schedule_delayed_work(&pTAS2560->irq_work, msecs_to_jiffies(5000));
	}
	return IRQ_HANDLED;
}

static int tas2560_runtime_suspend(struct tas2560_priv *pTAS2560)
{
	//dev_dbg(pTAS2560->dev, "%s\n", __func__);
	printk("tas2560-regmap.c:%s+++\n", __func__);

	pTAS2560->mbRuntimeSuspend = true;

	if (hrtimer_active(&pTAS2560->mtimer)) {
		dev_dbg(pTAS2560->dev, "cancel die temp timer\n");
		hrtimer_cancel(&pTAS2560->mtimer);
	}

	if (delayed_work_pending(&pTAS2560->irq_work)) {
		dev_dbg(pTAS2560->dev, "cancel IRQ work\n");
		cancel_delayed_work_sync(&pTAS2560->irq_work);
	}
	//tas2560_enable(pTAS2560,false);
	printk("tas2560-regmap.c:%s---\n", __func__);
	return 0;
}

static int tas2560_runtime_resume(struct tas2560_priv *pTAS2560)
{
	//dev_dbg(pTAS2560->dev, "%s\n", __func__);
	printk("tas2560-regmap.c:%s:+++\n", __func__);
	//save powre
	if(pTAS2560->dongleType==2){
		printk("station:channel=%d, Station cover is %s\n", pTAS2560->mnCurrentChannel, suspend_by_hall ? "off":"on");
		if(suspend_by_hall){
			#if 0
			tas2560_sec_mi2s_enable(true);
			tas2560_simple_enable(pTAS2560, pTAS2560->mnCurrentChannel,true);
			#endif
		}
	}
	//end
	//if (pTAS2560->mbPowerUp) {
	if (get_Channel_PowerStatus(pTAS2560, pTAS2560->mnCurrentChannel)) {
		if (!hrtimer_active(&pTAS2560->mtimer)) {
			dev_dbg(pTAS2560->dev, "%s, start check timer\n", __func__);
			hrtimer_start(&pTAS2560->mtimer,
				ns_to_ktime((u64)CHECK_PERIOD * NSEC_PER_MSEC), HRTIMER_MODE_REL);
		}
	}

	pTAS2560->mbRuntimeSuspend = false;
	printk("tas2560-regmap.c:%s:---\n", __func__);
	return 0;
}
// Register FB notifier +++
static int tas2560_fb_callback(struct notifier_block *nb, unsigned long val, void *data)
{
	struct tas2560_priv *pTAS2560;
	struct msm_drm_notifier *evdata = data;
	unsigned int blank;

	if (val != MSM_DRM_EARLY_EVENT_BLANK)
		return 0;

	//printk("[VIB] go to the tas2560_fb_callback value = %d msm_drm_display_id = %d\n", (int)val, evdata->id);
	if (evdata->id != 0)	// id=0 is internal display, external is 1
		return 0;

	pTAS2560 = container_of(nb, struct tas2560_priv, notifier);
	if (evdata && evdata->data && val == MSM_DRM_EARLY_EVENT_BLANK && pTAS2560) {
		blank = *(int *)(evdata->data);

		//printk("[VIB]  go to the blank value = %d\n", (int)blank);
		switch (blank) {
			case MSM_DRM_BLANK_POWERDOWN:
				if(pTAS2560->dongleType==2){
					printk("[VIB] MSM_DRM_BLANK_POWERDOWN:Station cover is %s\n", suspend_by_hall ? "off":"on");
					if(suspend_by_hall){
						tas2560_simple_enable(pTAS2560, channel_all,false);//power down all vibrators
						printk("[VIB] %s:station_reset_pin pull low...\n",__func__);
						hid_to_gpio_set(pTAS2560->mnSResetGPIO,0);
						hid_suspend_vote(pTAS2560->hid_suspend_id);
						pTAS2560->station_reset_enable=false;
					}
				}
				break;
			case MSM_DRM_BLANK_UNBLANK:
				if(pTAS2560->dongleType==2){
					//check cover on/off, power state on/off, sec_mi2s avtive/ sleep
					printk("[VIB] MSM_DRM_BLANK_UNBLANK:Station cover is %s, Channel(%d) is power %s, sec_mi2s_status=%s, station_reset_enable=%s\n", 
						suspend_by_hall ? "off":"on",pTAS2560->mnCurrentChannel, 
						get_Channel_PowerStatus(pTAS2560, pTAS2560->mnCurrentChannel)?"On":"Off", 
						tas2560_sec_mi2s_status() ? "true":"false",
						pTAS2560->station_reset_enable ? "true":"false");
					if(!get_Channel_PowerStatus(pTAS2560, channel_both)){
						if(!pTAS2560->station_reset_enable){//When cover is opened and station_reset_pin low, pull high it.
							int err=0,i=0;
							for(i=0;i<2;i++){//wait twice, too long will block display
								err = asus_wait4hid();
								if (err){
									printk("[VIB] %s:Fail to wait HID\n", __func__);
								}
								else
									break;
							}
							if(err==0){//station HID ok
								printk("[VIB] %s:station_reset_pin pull high...\n",__func__);
								hid_to_gpio_set(pTAS2560->mnSResetGPIO,1);
								pTAS2560->station_reset_enable=true;
								//printk("[VIB] %s:tas2560_sec_mi2s_enable\n",__func__);
								//tas2560_sec_mi2s_enable(true);
								tas2560_LoadConfig(pTAS2560, true);//After hw reset, must use this function.
							}
							else
								printk("[VIB] %s:station_reset_pin pull high failed\n",__func__);
						}
					}
				}
				break;
			default:
				printk("[VIB] defalut\n");
				break;
		}
	}
	return NOTIFY_OK;
}

static struct notifier_block tas2560_noti_block = {
	.notifier_call = tas2560_fb_callback,
};
// Register FB notifier ---

static int tas2560_i2c_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct tas2560_priv *pTAS2560;
	int nResult;
	int i=0;

	printk("tas2560-regmap.c:%s , addr=0x%0x+++", __func__, client->addr);
	dev_info(&client->dev, "%s enter\n", __func__);

	printk("1).devm_kzalloc...\n");
	gTAS2560=NULL;
	pTAS2560 = devm_kzalloc(&client->dev, sizeof(struct tas2560_priv), GFP_KERNEL);
	if (pTAS2560 == NULL) {
		dev_err(&client->dev, "%s, -ENOMEM \n", __func__);
		nResult = -ENOMEM;	

		goto end;
	}
	printk("2). set pTAS2560->vib_en initial value\n");
	gTAS2560=pTAS2560;
	pTAS2560->vib_en=true; //no used
	pTAS2560->sec_mi2s_status=true;//record sec_mi2s status
	pTAS2560->sec_mi2s_enable=true;//allow sec_mi2s enabling
	pTAS2560->haptic_stop=false;//for count_stop_timer
	pTAS2560->mnPAddr=0x4c; //phone
	pTAS2560->mnLAddr=0x4d; //station left
	pTAS2560->mnRAddr=0x4f; //station right
	pTAS2560->mnSResetGPIO=58;
	pTAS2560->dongleType=gDongleType;
	//check which vibrator is: station or phone
	if(pTAS2560->dongleType==0)//no dongle, phone only
		pTAS2560->mnCurrentChannel=channel_phone;
	if(pTAS2560->dongleType==1)//inbox, phone only
		pTAS2560->mnCurrentChannel=channel_phone;
	if(pTAS2560->dongleType==2)//Station
		pTAS2560->mnCurrentChannel=channel_both;
	if(pTAS2560->dongleType==3)//DT, phone only
		pTAS2560->mnCurrentChannel=channel_phone;
	if(pTAS2560->dongleType==4)//Unknow dongle, phone only
		pTAS2560->mnCurrentChannel=channel_phone;
	//end
	//pTAS2560->mnCurrentChannel=channel_phone;
	printk("mnCurrentChannel=0x%0x\n", pTAS2560->mnCurrentChannel);
	//pTAS2560->mbPowerUp=false;//for count_stop_timer
	set_Channel_powerStatus(pTAS2560, channel_phone, false);
	set_Channel_powerStatus(pTAS2560, channel_both, false);
	pTAS2560->station_reset_enable=false;//set station reset pin status
	pTAS2560->mnLCurrentBook = -1;
	pTAS2560->mnLCurrentPage = -1;
	pTAS2560->mnRCurrentBook = -1;
	pTAS2560->mnRCurrentPage = -1;
	pTAS2560->mnPCurrentBook = -1;
	pTAS2560->mnPCurrentPage = -1;
	

	printk("3).i2c_set_clientdata ...\n");
	pTAS2560->client = client;
	pTAS2560->dev = &client->dev;
	i2c_set_clientdata(client, pTAS2560);
	dev_set_drvdata(&client->dev, pTAS2560);

	printk("4).devm_regmap_init_i2c...\n");
	pTAS2560->regmap = devm_regmap_init_i2c(client, &tas2560_i2c_regmap);
	if (IS_ERR(pTAS2560->regmap)) {
		nResult = PTR_ERR(pTAS2560->regmap);
		dev_err(&client->dev, "Failed to allocate register map: %d\n",
					nResult);
		goto end;
	}

	printk("5). parsing tas2560 device tree...\n");
	if (client->dev.of_node)
		tas2560_parse_dt(&client->dev, pTAS2560);

	printk("6). request TAS2560-RESET ...\n");
	if (gpio_is_valid(pTAS2560->mnResetGPIO)) {
		nResult = gpio_request(pTAS2560->mnResetGPIO, "TAS2560_RESET");
		if (nResult) {
			dev_err(pTAS2560->dev, "%s: Failed to request gpio %d\n", __func__,
				pTAS2560->mnResetGPIO);
			nResult = -EINVAL;
			//goto free_gpio;
		}
		/*
		else {
				//hw function
				gpio_direction_output(pTAS2560->mnResetGPIO, 0);
				msleep(5);
				gpio_direction_output(pTAS2560->mnResetGPIO, 1);
				msleep(1);
			}
		*/
	}

	printk("7.) request TAS2560-IRQ ...\n");
	if (gpio_is_valid(pTAS2560->mnIRQGPIO)) {
		nResult = gpio_request(pTAS2560->mnIRQGPIO, "TAS2560-IRQ");
		if (nResult < 0) {
			dev_err(pTAS2560->dev, "%s: GPIO %d request error\n",
				__func__, pTAS2560->mnIRQGPIO);
			//goto free_gpio;
		}
		gpio_direction_input(pTAS2560->mnIRQGPIO);
		pTAS2560->mnIRQ = gpio_to_irq(pTAS2560->mnIRQGPIO);
		dev_dbg(pTAS2560->dev, "irq = %d\n", pTAS2560->mnIRQ);
		nResult = request_threaded_irq(pTAS2560->mnIRQ, tas2560_irq_handler,
					NULL, IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
					client->name, pTAS2560);
		if (nResult < 0) {
			dev_err(pTAS2560->dev,
				"request_irq failed, %d\n", nResult);
			//goto free_gpio;
		}
		disable_irq_nosync(pTAS2560->mnIRQ);
		INIT_DELAYED_WORK(&pTAS2560->irq_work, irq_work_routine);
	}
	printk("8.) setup functions ...\n");
	pTAS2560->read = tas2560_dev_read;
	pTAS2560->write = tas2560_dev_write;
	pTAS2560->bulk_read = tas2560_dev_bulk_read;
	pTAS2560->bulk_write = tas2560_dev_bulk_write;
	pTAS2560->update_bits = tas2560_dev_update_bits;
	pTAS2560->hw_reset = tas2560_hw_reset;
	pTAS2560->sw_reset = tas2560_sw_reset;
	pTAS2560->enableIRQ = tas2560_enableIRQ;
	pTAS2560->clearIRQ = tas2560_clearIRQ;
	pTAS2560->runtime_suspend = tas2560_runtime_suspend;
	pTAS2560->runtime_resume = tas2560_runtime_resume;
	mutex_init(&pTAS2560->dev_lock);

	//register to led devices +++
	pTAS2560->cdev.name="vibrator";
	pTAS2560->cdev.brightness_set = tas2560_brightness_set;
	pTAS2560->cdev.brightness_get = tas2560_brightness_get;
	pTAS2560->cdev.max_brightness = 100;
	printk("9). register Vibrator_led node ...\n");
	nResult = devm_led_classdev_register(&client->dev, &pTAS2560->cdev);
	if (nResult) {
		//dev_info(pTAS2560->dev, "tas2560 vibrator_led registration failed\n");
		printk("tas2560 vibrator_led registration failed(%d)\n", nResult);
		//goto end;
	}
	else{
		//dev_info(pTAS2560->dev, "tas2560 vibrator_led registration successful\n");
		printk("tas2560 vibrator_led registration successful(%d)\n", nResult);
		//if nResult = error code, the sys/class/leds/vibrator_led will not be generated.
	}
	//create device node
	printk("10). create sysfs file...\n");
	for (i = 0; i < ARRAY_SIZE(tas2560_haptics_attrs); i++) {
		printk("path=%s, creating %s",pTAS2560->cdev.dev->kobj.name, tas2560_haptics_attrs[i].attr.name);
		nResult = sysfs_create_file(&pTAS2560->cdev.dev->kobj,
		&tas2560_haptics_attrs[i].attr);
		if (nResult != 0) {
			printk("Error in creating sysfs file, nResult=%d\n",	nResult);
			//goto sysfs_fail;
		}
		else{
			printk("Successful in creating sysfs file, nResult=%d",nResult);
		}
	}
	//test tas2560 driver. if return fail, the vibrator service cannot be initialized and the system will fail.
	//end


#ifdef CONFIG_TAS2560_CODEC
	mutex_init(&pTAS2560->codec_lock);
	printk("11). tas2560_register_codec(pTAS2560)...\n");
	pTAS2560->codec=NULL;
	tas2560_register_codec(pTAS2560);
	if(pTAS2560->codec){
		gTAS2560->codec=pTAS2560->codec;
	}
#endif

#ifdef CONFIG_TAS2560_MISC
	mutex_init(&pTAS2560->file_lock);
	printk("12). tas2560_register_misc(pTAS2560)...\n");
	tas2560_register_misc(pTAS2560);
#endif

	printk("13). setup timer function...\n");
	hrtimer_init(&pTAS2560->mtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	pTAS2560->mtimer.function = timer_func;
	setup_timer(&pTAS2560->count_stop_timer, count_stop_timer, (unsigned long)(&pTAS2560));
	printk("14). tas2560_LoadConfig(pTAS2560, false)...\n");
	nResult = tas2560_LoadConfig(pTAS2560, false);
	if (nResult < 0){
		printk("tas2560_LoadConfig(pTAS2560, false) fail\n");
		//goto destroy_mutex;
	}
	printk("15). register framebuffer notify ...\n");
	pTAS2560->notifier = tas2560_noti_block;
	msm_drm_register_client(&pTAS2560->notifier);
end:
	printk("%s---, (%d)\n", __func__, nResult);
	//return nResult;
	return 0;//always successful to make vibration service activate
}

static int tas2560_i2c_remove(struct i2c_client *client)
{
	struct tas2560_priv *pTAS2560 = i2c_get_clientdata(client);

	dev_info(pTAS2560->dev, "%s\n", __func__);

#ifdef CONFIG_TAS2560_CODEC
	tas2560_deregister_codec(pTAS2560);
	mutex_destroy(&pTAS2560->codec_lock);
#endif

#ifdef CONFIG_TAS2560_MISC
	tas2560_deregister_misc(pTAS2560);
	mutex_destroy(&pTAS2560->file_lock);
#endif

	if (gpio_is_valid(pTAS2560->mnResetGPIO))
		gpio_free(pTAS2560->mnResetGPIO);
	if (gpio_is_valid(pTAS2560->mnIRQGPIO))
		gpio_free(pTAS2560->mnIRQGPIO);

	return 0;
}


static const struct i2c_device_id tas2560_i2c_id[] = {
	{ "tas2560", 0},
	{ }
};
MODULE_DEVICE_TABLE(i2c, tas2560_i2c_id);

#if defined(CONFIG_OF)
static const struct of_device_id tas2560_of_match[] = {
	{ .compatible = "ti,tas2560" },
	{},
};
MODULE_DEVICE_TABLE(of, tas2560_of_match);
#endif


static struct i2c_driver tas2560_i2c_driver = {
	.driver = {
		.name   = "tas2560",
		.owner  = THIS_MODULE,
#if defined(CONFIG_OF)
		.of_match_table = of_match_ptr(tas2560_of_match),
#endif
	},
	.probe      = tas2560_i2c_probe,
	.remove     = tas2560_i2c_remove,
	.id_table   = tas2560_i2c_id,
};

module_i2c_driver(tas2560_i2c_driver);

MODULE_AUTHOR("Texas Instruments Inc.");
MODULE_DESCRIPTION("TAS2560 I2C Smart Amplifier driver");
MODULE_LICENSE("GPL v2");
#endif
