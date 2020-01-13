/* Copyright (c) 2016-2018 The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/power_supply.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/log2.h>
#include <linux/qpnp/qpnp-revid.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include "smb-reg.h"
#include "smb-lib.h"
#include "storm-watch.h"
#include <linux/pmic-voter.h>
#include "fg-core.h"

//ASUS BSP add include files +++
#include <linux/proc_fs.h>
#include <linux/of_gpio.h>
#include <linux/reboot.h>
#include <linux/qpnp/qpnp-adc.h>
#include <linux/unistd.h>
#include <linux/fcntl.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
//#include <linux/wakelock.h>
//ASUS BSP add include files ---
#include <linux/msm_drm_notify.h> //Add to get drm notifier

//ASUS BSP : Add debug log +++
#define CHARGER_TAG "[BAT][CHG]"
#define ERROR_TAG "[ERR]"
#define CHG_DBG(...)  printk(KERN_INFO CHARGER_TAG __VA_ARGS__)
#define CHG_DBG_E(...)  printk(KERN_ERR CHARGER_TAG ERROR_TAG __VA_ARGS__)
//ex:CHG_DBG("%s: %d\n", __func__, l_result);
//ASUS BSP : Add debug log ---

#define SMB2_DEFAULT_WPWR_UW	8000000

static struct smb_params v1_params = {
	.fcc			= {
		.name	= "fast charge current",
		.reg	= FAST_CHARGE_CURRENT_CFG_REG,
		.min_u	= 0,
		.max_u	= 4500000,
		.step_u	= 25000,
	},
	.fv			= {
		.name	= "float voltage",
		.reg	= FLOAT_VOLTAGE_CFG_REG,
		.min_u	= 3487500,
		.max_u	= 4920000,
		.step_u	= 7500,
	},
	.usb_icl		= {
		.name	= "usb input current limit",
		.reg	= USBIN_CURRENT_LIMIT_CFG_REG,
		.min_u	= 0,
		.max_u	= 4800000,
		.step_u	= 25000,
	},
	.icl_stat		= {
		.name	= "input current limit status",
		.reg	= ICL_STATUS_REG,
		.min_u	= 0,
		.max_u	= 4800000,
		.step_u	= 25000,
	},
	.otg_cl			= {
		.name	= "usb otg current limit",
		.reg	= OTG_CURRENT_LIMIT_CFG_REG,
		.min_u	= 250000,
		.max_u	= 2000000,
		.step_u	= 250000,
	},
	.dc_icl			= {
		.name	= "dc input current limit",
		.reg	= DCIN_CURRENT_LIMIT_CFG_REG,
		.min_u	= 0,
		.max_u	= 6000000,
		.step_u	= 25000,
	},
	.dc_icl_pt_lv		= {
		.name	= "dc icl PT <8V",
		.reg	= ZIN_ICL_PT_REG,
		.min_u	= 0,
		.max_u	= 3000000,
		.step_u	= 25000,
	},
	.dc_icl_pt_hv		= {
		.name	= "dc icl PT >8V",
		.reg	= ZIN_ICL_PT_HV_REG,
		.min_u	= 0,
		.max_u	= 3000000,
		.step_u	= 25000,
	},
	.dc_icl_div2_lv		= {
		.name	= "dc icl div2 <5.5V",
		.reg	= ZIN_ICL_LV_REG,
		.min_u	= 0,
		.max_u	= 3000000,
		.step_u	= 25000,
	},
	.dc_icl_div2_mid_lv	= {
		.name	= "dc icl div2 5.5-6.5V",
		.reg	= ZIN_ICL_MID_LV_REG,
		.min_u	= 0,
		.max_u	= 3000000,
		.step_u	= 25000,
	},
	.dc_icl_div2_mid_hv	= {
		.name	= "dc icl div2 6.5-8.0V",
		.reg	= ZIN_ICL_MID_HV_REG,
		.min_u	= 0,
		.max_u	= 3000000,
		.step_u	= 25000,
	},
	.dc_icl_div2_hv		= {
		.name	= "dc icl div2 >8.0V",
		.reg	= ZIN_ICL_HV_REG,
		.min_u	= 0,
		.max_u	= 3000000,
		.step_u	= 25000,
	},
	.jeita_cc_comp		= {
		.name	= "jeita fcc reduction",
		.reg	= JEITA_CCCOMP_CFG_REG,
		.min_u	= 0,
		.max_u	= 1575000,
		.step_u	= 25000,
	},
	.freq_buck		= {
		.name	= "buck switching frequency",
		.reg	= CFG_BUCKBOOST_FREQ_SELECT_BUCK_REG,
		.min_u	= 600,
		.max_u	= 2000,
		.step_u	= 200,
	},
	.freq_boost		= {
		.name	= "boost switching frequency",
		.reg	= CFG_BUCKBOOST_FREQ_SELECT_BOOST_REG,
		.min_u	= 600,
		.max_u	= 2000,
		.step_u	= 200,
	},
};

static struct smb_params pm660_params = {
	.freq_buck		= {
		.name	= "buck switching frequency",
		.reg	= FREQ_CLK_DIV_REG,
		.min_u	= 600,
		.max_u	= 1600,
		.set_proc = smblib_set_chg_freq,
	},
	.freq_boost		= {
		.name	= "boost switching frequency",
		.reg	= FREQ_CLK_DIV_REG,
		.min_u	= 600,
		.max_u	= 1600,
		.set_proc = smblib_set_chg_freq,
	},
};

struct smb_dt_props {
	int	usb_icl_ua;
	int	dc_icl_ua;
	int	boost_threshold_ua;
	int	wipower_max_uw;
	int	min_freq_khz;
	int	max_freq_khz;
	struct	device_node *revid_dev_node;
	int	float_option;
	int	chg_inhibit_thr_mv;
	bool	no_battery;
	bool	hvdcp_disable;
	bool	auto_recharge_soc;
	int	wd_bark_time;
};

struct smb2 {
	struct smb_charger	chg;
	struct dentry		*dfs_root;
	struct smb_dt_props	dt;
	bool			bad_part;
};

//ASUS BSP add struct functions +++

extern enum DEVICE_HWID g_ASUS_hwID;
struct smb_charger *smbchg_dev;
struct gpio_control *global_gpio;	//global gpio_control
struct timespec last_jeita_time;
struct wakeup_source asus_chg_ws;
struct wakeup_source asus_PTC_lock_ws;
extern struct fg_chip * g_fgChip;	//ASUS BSP : guage +++
volatile enum POGO_ID ASUS_POGO_ID = NO_INSERT;
volatile enum QC_BATT_STATUS QC_BATT_STATUS = NORMAL;
bool g_interrupt_enable = 1;	//ASUS_BSP Austin_T : add usb alert mode trigger
bool usb_alert_flag = 0;
bool usb_alert_keep_suspend_flag = 0;
bool usb_alert_flag_ACCY = 0;
bool usb_alert_keep_suspend_flag_ACCY = 0;
volatile bool cos_alert_once_flag = 0;
bool boot_completed_flag = 0;
int charger_limit_value = 70;
int charger_limit_enable_flag = 0;
extern int asus_CHG_TYPE;
extern bool asus_flow_done_flag;
extern bool asus_adapter_detecting_flag;
bool no_input_suspend_flag = 0;
bool smartchg_stop_flag = 0;
extern bool g_Charger_mode;
extern int g_ftm_mode;
extern int g_ST_SDP_mode;
bool demo_app_property_flag = 0;
bool cn_demo_app_flag = 0;
int demo_recharge_delta = 2;
volatile bool ptc_check_flag = 0;
volatile bool asus_suspend_cmd_flag = 0;
volatile bool is_asp1690e_off = 0;
bool boot_w_btm_plugin = 0; //WA for BTM_500mA issue
bool switcher_power_disable_flag = 0;
extern volatile bool cos_pd_reset_flag;
bool rerun_pogo_det = 0; //Rerun POGO detection if there is i2c error
extern bool is_StCap_low;
struct timespec g_last_pogo_therm_int;
extern volatile enum bat_stage last_charger_statge;
extern volatile enum bat_charger_state last_charger_state;
extern volatile bool dt_overheat_flag;
volatile bool station_cap_zero_flag = 0;
extern volatile bool dual_port_once_flag;
extern volatile int NXP_FLAG;

//ASUS BSP : Add for Ultra Bat Life +++
volatile bool ultra_bat_life_flag = 0;
volatile int g_ultra_cos_spec_time = 2880;
void write_CHGLimit_value(int input);
//ASUS BSP : Add for Ultra Bat Life ---
struct notifier_block smb2_drm_nb; //Add for station update when screen on

//ASUS BSP : Show "+" on charging icon +++
int qc_stat_registed = false;
void set_qc_stat(union power_supply_propval *val);
//ASUS BSP : Show "+" on charging icon ---

extern void smblib_asus_monitor_start(struct smb_charger *chg, int time);
extern bool asus_get_prop_usb_present(struct smb_charger *chg);
extern int asus_get_prop_batt_capacity(struct smb_charger *chg);
extern int asus_get_prop_batt_volt(struct smb_charger *chg);
extern void asus_smblib_stay_awake(struct smb_charger *chg);
extern void asus_smblib_relax(struct smb_charger *chg);
extern int smblib_set_usb_suspend(struct smb_charger *chg, bool suspend);
extern bool rt_chg_check_asus_vid(void);
extern int hid_to_get_battery_cap(int *cap);
extern bool PE_check_asus_vid(void);
extern void write_CHGLimit_value(int input);
//ASUS BSP add struct functions ---
//[+++]Add the interface for charging debug apk
extern int asus_get_prop_adapter_id(void);
extern int asus_get_prop_is_legacy_cable(void);
extern int asus_get_prop_total_fcc(void);
extern int asus_get_apsd_result_by_bit(void);
extern int asus_get_batt_status(void);
extern void fg_station_attach_notifier(bool attached);
extern void pmic_set_pca9468_charging(bool enable);
//[---]Add the interface for charging debug apk
extern int hid_to_check_interrupt(u8 *type, u8 *event);
extern int hid_to_get_charger_type(int *type, short *vol, short *cur);

//disable tas2560 sec_mi2s
extern void tas2560_sec_mi2s_enable(bool enable);
#if 0
void tas2560_sec_mi2s_enable(bool enable)
{
	return;
}
#endif
//end

//Water/Thermal ADC +++++++
int g_water_enable = 1;
int g_water_state = 0;
int g_water_side_state = 0;
int g_water_btm_state = 0;
int g_LOW_THR_DET = 1100000;
int g_HIGH_THR_DET_1 = 1300000;
int g_HIGH_THR_DET_2 = 300000;
int g_LIQUID_HIGH_BOUND = 1300000;
int g_LIQUID_LOW_BOUND = 200000;
int g_temp_state = 0;
int g_temp_side_state = 0;
int g_temp_btm_state = 0;
int g_temp_station_state = 0;
int g_temp_INBOX_DT_state = 0;
int g_temp_LOW_THR_DET = 444000;
int g_temp_HIGH_THR_DET = 580000;
static int32_t asus_get_wp_btm_vadc_voltage(void);
static int32_t asus_get_wp_side_vadc_voltage(void);
static int32_t asus_get_temp_btm_vadc_voltage(void);
//Water/Thermal ADC  --------

// ASP1690E +++
extern bool asp1690e_ready;
extern int asp1690e_mask_write_reg(uint8_t cmd_reg, uint8_t mask, uint8_t write_val);
extern int asp1690e_write_reg(uint8_t cmd_reg, uint8_t write_val);
extern int asp1690e_read_reg(uint8_t cmd_reg, uint8_t *store_read_val);
extern void detect_dongle_type(u8 adc);

// ASP1690E +++
int g_force_usb_mux = 0;//Force to set the usb_mux, skip all asus_mux_setting_1_work function

static int __debug_mask;
module_param_named(
	debug_mask, __debug_mask, int, 0600
);

static int __weak_chg_icl_ua = 500000;
module_param_named(
	weak_chg_icl_ua, __weak_chg_icl_ua, int, 0600);

static int __try_sink_enabled = 1;
module_param_named(
	try_sink_enabled, __try_sink_enabled, int, 0600
);

static int __audio_headset_drp_wait_ms = 100;
module_param_named(
	audio_headset_drp_wait_ms, __audio_headset_drp_wait_ms, int, 0600
);

#define MICRO_1P5A		1500000
#define MICRO_P1A		100000
#define OTG_DEFAULT_DEGLITCH_TIME_MS	50
#define MIN_WD_BARK_TIME		16
#define DEFAULT_WD_BARK_TIME		64
#define BITE_WDOG_TIMEOUT_8S		0x3
#define BARK_WDOG_TIMEOUT_MASK		GENMASK(3, 2)
#define BARK_WDOG_TIMEOUT_SHIFT		2
static int smb2_parse_dt(struct smb2 *chip)
{
	struct smb_charger *chg = &chip->chg;
	struct device_node *node = chg->dev->of_node;
	int rc, byte_len;

	if (!node) {
		pr_err("device tree node missing\n");
		return -EINVAL;
	}

	chg->step_chg_enabled = of_property_read_bool(node,
				"qcom,step-charging-enable");

	chg->sw_jeita_enabled = of_property_read_bool(node,
				"qcom,sw-jeita-enable");

	rc = of_property_read_u32(node, "qcom,wd-bark-time-secs",
					&chip->dt.wd_bark_time);
	if (rc < 0 || chip->dt.wd_bark_time < MIN_WD_BARK_TIME)
		chip->dt.wd_bark_time = DEFAULT_WD_BARK_TIME;

	chip->dt.no_battery = of_property_read_bool(node,
						"qcom,batteryless-platform");

	rc = of_property_read_u32(node,
				"qcom,fcc-max-ua", &chg->batt_profile_fcc_ua);
	if (rc < 0)
		chg->batt_profile_fcc_ua = -EINVAL;

	rc = of_property_read_u32(node,
				"qcom,fv-max-uv", &chg->batt_profile_fv_uv);
	if (rc < 0)
		chg->batt_profile_fv_uv = -EINVAL;

	rc = of_property_read_u32(node,
				"qcom,usb-icl-ua", &chip->dt.usb_icl_ua);
	if (rc < 0)
		chip->dt.usb_icl_ua = -EINVAL;

	rc = of_property_read_u32(node,
				"qcom,otg-cl-ua", &chg->otg_cl_ua);
	if (rc < 0)
		chg->otg_cl_ua = MICRO_1P5A;

	rc = of_property_read_u32(node,
				"qcom,dc-icl-ua", &chip->dt.dc_icl_ua);
	if (rc < 0)
		chip->dt.dc_icl_ua = -EINVAL;

	rc = of_property_read_u32(node,
				"qcom,boost-threshold-ua",
				&chip->dt.boost_threshold_ua);
	if (rc < 0)
		chip->dt.boost_threshold_ua = MICRO_P1A;

	rc = of_property_read_u32(node,
				"qcom,min-freq-khz",
				&chip->dt.min_freq_khz);
	if (rc < 0)
		chip->dt.min_freq_khz = -EINVAL;

	rc = of_property_read_u32(node,
				"qcom,max-freq-khz",
				&chip->dt.max_freq_khz);
	if (rc < 0)
		chip->dt.max_freq_khz = -EINVAL;

	rc = of_property_read_u32(node, "qcom,wipower-max-uw",
				&chip->dt.wipower_max_uw);
	if (rc < 0)
		chip->dt.wipower_max_uw = -EINVAL;

	if (of_find_property(node, "qcom,thermal-mitigation", &byte_len)) {
		chg->thermal_mitigation = devm_kzalloc(chg->dev, byte_len,
			GFP_KERNEL);

		if (chg->thermal_mitigation == NULL)
			return -ENOMEM;

		chg->thermal_levels = byte_len / sizeof(u32);
		rc = of_property_read_u32_array(node,
				"qcom,thermal-mitigation",
				chg->thermal_mitigation,
				chg->thermal_levels);
		if (rc < 0) {
			dev_err(chg->dev,
				"Couldn't read threm limits rc = %d\n", rc);
			return rc;
		}
	}

	of_property_read_u32(node, "qcom,float-option", &chip->dt.float_option);
	if (chip->dt.float_option < 0 || chip->dt.float_option > 4) {
		pr_err("qcom,float-option is out of range [0, 4]\n");
		return -EINVAL;
	}

	chip->dt.hvdcp_disable = of_property_read_bool(node,
						"qcom,hvdcp-disable");

	of_property_read_u32(node, "qcom,chg-inhibit-threshold-mv",
				&chip->dt.chg_inhibit_thr_mv);
	if ((chip->dt.chg_inhibit_thr_mv < 0 ||
		chip->dt.chg_inhibit_thr_mv > 300)) {
		pr_err("qcom,chg-inhibit-threshold-mv is incorrect\n");
		return -EINVAL;
	}

	chip->dt.auto_recharge_soc = of_property_read_bool(node,
						"qcom,auto-recharge-soc");

	//pca9468
	chg->micro_usb_mode = of_property_read_bool(node, "qcom,micro-usb");

	chg->use_extcon = of_property_read_bool(node,
						"qcom,use-extcon");

	chg->dcp_icl_ua = chip->dt.usb_icl_ua;

	chg->suspend_input_on_debug_batt = of_property_read_bool(node,
					"qcom,suspend-input-on-debug-batt");

	rc = of_property_read_u32(node, "qcom,otg-deglitch-time-ms",
					&chg->otg_delay_ms);
	if (rc < 0)
		chg->otg_delay_ms = OTG_DEFAULT_DEGLITCH_TIME_MS;

	chg->disable_stat_sw_override = of_property_read_bool(node,
					"qcom,disable-stat-sw-override");

	chg->fcc_stepper_enable = of_property_read_bool(node,
					"qcom,fcc-stepping-enable");

	return 0;
}

/************************
 * USB PSY REGISTRATION *
 ************************/

static enum power_supply_property smb2_usb_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_PD_CURRENT_MAX,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_TYPE,
	POWER_SUPPLY_PROP_TYPEC_MODE,
	POWER_SUPPLY_PROP_TYPEC_POWER_ROLE,
	POWER_SUPPLY_PROP_TYPEC_CC_ORIENTATION,
#ifdef CONFIG_USBPD_PHY_QCOM
	POWER_SUPPLY_PROP_CHARGING_ENABLED,
#endif
	POWER_SUPPLY_PROP_PD_ALLOWED,
	POWER_SUPPLY_PROP_PD_ACTIVE,
	POWER_SUPPLY_PROP_PD2_ACTIVE,
	POWER_SUPPLY_PROP_INPUT_CURRENT_SETTLED,
	POWER_SUPPLY_PROP_INPUT_CURRENT_NOW,
	POWER_SUPPLY_PROP_BOOST_CURRENT,
	POWER_SUPPLY_PROP_PE_START,
	POWER_SUPPLY_PROP_CTM_CURRENT_MAX,
	POWER_SUPPLY_PROP_HW_CURRENT_MAX,
	POWER_SUPPLY_PROP_REAL_TYPE,
	POWER_SUPPLY_PROP_PR_SWAP,
	POWER_SUPPLY_PROP_PD_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_PD_VOLTAGE_MIN,
	POWER_SUPPLY_PROP_SDP_CURRENT_MAX,
	POWER_SUPPLY_PROP_CONNECTOR_TYPE,
	POWER_SUPPLY_PROP_MOISTURE_DETECTED,
	//[+++]Add the interface for charging debug apk
	POWER_SUPPLY_PROP_ASUS_APSD_RESULT,
	POWER_SUPPLY_PROP_ASUS_ADAPTER_ID,
	POWER_SUPPLY_PROP_ASUS_IS_LEGACY_CABLE,
	POWER_SUPPLY_PROP_ASUS_ICL_SETTING,
	POWER_SUPPLY_PROP_ASUS_TOTAL_FCC,
	//[---]Add the interface for charging debug apk
};

static int smb2_usb_get_prop(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct smb2 *chip = power_supply_get_drvdata(psy);
	struct smb_charger *chg = &chip->chg;
	int rc = 0;
	u8 stat;	//Add the interface for charging debug apk

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		if (chip->bad_part)
			val->intval = 1;
		else
			rc = smblib_get_prop_usb_present(chg, val);
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		rc = smblib_get_prop_usb_online(chg, val);
		if (!val->intval)
			break;

		if (((chg->typec_mode == POWER_SUPPLY_TYPEC_SOURCE_DEFAULT)
		   || (chg->connector_type == POWER_SUPPLY_CONNECTOR_MICRO_USB))
		   && (chg->real_charger_type == POWER_SUPPLY_TYPE_USB))
			val->intval = 0;
//Add for BTM online state +++
		else if ((!gpio_get_value_cansleep(global_gpio->BTM_OVP_ACOK))
			&& (chg->real_charger_type == POWER_SUPPLY_TYPE_USB))
			val->intval = 0;
//Add for BTM online state ---
		else
			val->intval = 1;
		if (chg->real_charger_type == POWER_SUPPLY_TYPE_UNKNOWN)
			val->intval = 0;
		if (g_Charger_mode && cos_pd_reset_flag)
			val->intval = 1;
		//[+++]WA for 45W adapter WA, which will create a side-effecit too late to show charging icon
		//QCT will set online as 0 if BC 1.2 is UNKNOWN
		if (chg->pd2_active && !gpio_get_value_cansleep(global_gpio->BTM_OVP_ACOK)
			&& chg->real_charger_type == POWER_SUPPLY_TYPE_UNKNOWN) {
			printk(KERN_ERR "[CY]reset online = 1 for unknown case\n");
			val->intval = 1;
		}
		//[---]WA for 45W adapter WA, which will create a side-effecit too late to show charging icon
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		rc = smblib_get_prop_usb_voltage_max(chg, val);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		rc = smblib_get_prop_usb_voltage_max_design(chg, val);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		rc = smblib_get_prop_usb_voltage_now(chg, val);
		break;
	case POWER_SUPPLY_PROP_PD_CURRENT_MAX:
		val->intval = get_client_vote(chg->usb_icl_votable, PD_VOTER);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		rc = smblib_get_prop_input_current_settled(chg, val);
		break;
	case POWER_SUPPLY_PROP_TYPE:
		val->intval = POWER_SUPPLY_TYPE_USB_PD;
		break;
	case POWER_SUPPLY_PROP_REAL_TYPE:
		if (chip->bad_part)
			val->intval = POWER_SUPPLY_TYPE_USB_PD;
		else
			val->intval = chg->real_charger_type;
		break;
	case POWER_SUPPLY_PROP_TYPEC_MODE:
		if (chg->connector_type == POWER_SUPPLY_CONNECTOR_MICRO_USB)
			val->intval = POWER_SUPPLY_TYPEC_NONE;
		else if (chip->bad_part)
			val->intval = POWER_SUPPLY_TYPEC_SOURCE_DEFAULT;
		else
			val->intval = chg->typec_mode;
		break;
	case POWER_SUPPLY_PROP_TYPEC_POWER_ROLE:
		if (chg->connector_type == POWER_SUPPLY_CONNECTOR_MICRO_USB)
			val->intval = POWER_SUPPLY_TYPEC_PR_NONE;
		else
			rc = smblib_get_prop_typec_power_role(chg, val);
		break;
	case POWER_SUPPLY_PROP_TYPEC_CC_ORIENTATION:
		if (chg->connector_type == POWER_SUPPLY_CONNECTOR_MICRO_USB)
			val->intval = 0;
		else
			rc = smblib_get_prop_typec_cc_orientation(chg, val);
		break;
	case POWER_SUPPLY_PROP_PD_ALLOWED:
		rc = smblib_get_prop_pd_allowed(chg, val);
		break;
	case POWER_SUPPLY_PROP_PD_ACTIVE:
		val->intval = chg->pd_active;
		break;
	case POWER_SUPPLY_PROP_PD2_ACTIVE:
		val->intval = chg->pd2_active;
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_SETTLED:
		rc = smblib_get_prop_input_current_settled(chg, val);
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_NOW:
		rc = smblib_get_prop_usb_current_now(chg, val);
		break;
	case POWER_SUPPLY_PROP_BOOST_CURRENT:
		val->intval = chg->boost_current_ua;
		break;
	case POWER_SUPPLY_PROP_PD_IN_HARD_RESET:
		rc = smblib_get_prop_pd_in_hard_reset(chg, val);
		break;
	case POWER_SUPPLY_PROP_PD_USB_SUSPEND_SUPPORTED:
		val->intval = chg->system_suspend_supported;
		break;
	case POWER_SUPPLY_PROP_PE_START:
		rc = smblib_get_pe_start(chg, val);
		break;
	case POWER_SUPPLY_PROP_CTM_CURRENT_MAX:
		val->intval = get_client_vote(chg->usb_icl_votable, CTM_VOTER);
		break;
	case POWER_SUPPLY_PROP_HW_CURRENT_MAX:
		rc = smblib_get_charge_current(chg, &val->intval);
		break;
	case POWER_SUPPLY_PROP_PR_SWAP:
		rc = smblib_get_prop_pr_swap_in_progress(chg, val);
		break;
	case POWER_SUPPLY_PROP_PD_VOLTAGE_MAX:
		val->intval = chg->voltage_max_uv;
		break;
	case POWER_SUPPLY_PROP_PD_VOLTAGE_MIN:
		val->intval = chg->voltage_min_uv;
		break;
	case POWER_SUPPLY_PROP_SDP_CURRENT_MAX:
		val->intval = get_client_vote(chg->usb_icl_votable,
					      USB_PSY_VOTER);
		break;
#ifdef CONFIG_USBPD_PHY_QCOM
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		rc = smblib_get_prop_charging_enabled(chg, val);
		break;
#endif

	case POWER_SUPPLY_PROP_CONNECTOR_TYPE:
		val->intval = chg->connector_type;
		break;
	case POWER_SUPPLY_PROP_MOISTURE_DETECTED:
		val->intval = get_client_vote(chg->disable_power_role_switch,
					      MOISTURE_VOTER);
		break;
	//[+++]Add the interface for charging debug apk
	case POWER_SUPPLY_PROP_ASUS_APSD_RESULT:
		val->intval = asus_get_apsd_result_by_bit();
		break;
	case POWER_SUPPLY_PROP_ASUS_ADAPTER_ID:
		val->intval = asus_get_prop_adapter_id();
		break;
	case POWER_SUPPLY_PROP_ASUS_IS_LEGACY_CABLE:
		val->intval = asus_get_prop_is_legacy_cable();
		break;
	case POWER_SUPPLY_PROP_ASUS_ICL_SETTING:
		rc = smblib_read(chg, USBIN_CURRENT_LIMIT_CFG_REG, &stat);
		if (rc < 0) {
			pr_err("Couldn't read USBIN_CURRENT_LIMIT_CFG_REG rc=%d\n", rc);
			return rc;
		}
		val->intval = stat*25;//every stage is 25mA
		break;
	case POWER_SUPPLY_PROP_ASUS_TOTAL_FCC:
		val->intval = asus_get_prop_total_fcc();
		break;
	//[---]Add the interface for charging debug apk
	default:
		pr_err("get prop %d is not supported in usb\n", psp);
		rc = -EINVAL;
		break;
	}
	if (rc < 0) {
		pr_debug("Couldn't get prop %d rc = %d\n", psp, rc);
		return -ENODATA;
	}
	return 0;
}

static int smb2_usb_set_prop(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *val)
{
	struct smb2 *chip = power_supply_get_drvdata(psy);
	struct smb_charger *chg = &chip->chg;
	int rc = 0;
	//[+++]Do not set this prop in mutex_lock. It will cause PD hard reset
	if (psp == POWER_SUPPLY_PROP_PD_USB_SUSPEND_SUPPORTED) {
		chg->system_suspend_supported = val->intval;
		return 0;
	}
	//[---]Do not set this prop in mutex_lock. It will cause PD hard reset
	mutex_lock(&chg->lock);
	if (!chg->typec_present) {
		switch (psp) {
		case POWER_SUPPLY_PROP_MOISTURE_DETECTED:
			vote(chg->disable_power_role_switch, MOISTURE_VOTER,
			     val->intval > 0, 0);
			break;
// ASUS  BSP : For Bottom PCA set current_max property +++
		case POWER_SUPPLY_PROP_CURRENT_MAX:
			if (rt_chg_check_asus_vid()) {
				rc = smblib_set_prop_usb_current_max(chg, val);
				break;
			}
// ASUS  BSP : For Bottom PCA set current_max property ---
		default:
			rc = -EINVAL;
			break;
		}

		goto unlock;
	}

	switch (psp) {
	case POWER_SUPPLY_PROP_PD_CURRENT_MAX:
		rc = smblib_set_prop_pd_current_max(chg, val);
		break;
	case POWER_SUPPLY_PROP_TYPEC_POWER_ROLE:
		rc = smblib_set_prop_typec_power_role(chg, val);
		break;
#ifdef CONFIG_USBPD_PHY_QCOM
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		rc = smblib_set_prop_usb_current_max(chg, val);
		break;
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		rc = smblib_set_prop_charging_enabled(chg, val);
		break;
	case POWER_SUPPLY_PROP_PD_ALLOWED:
		rc = smblib_set_prop_pd_allowed(chg, val);
		break;
#endif
	case POWER_SUPPLY_PROP_PD_ACTIVE:
		rc = smblib_set_prop_pd_active(chg, val);
		break;
	case POWER_SUPPLY_PROP_PD2_ACTIVE:
		rc = smblib_set_prop_pd2_active(chg, val);
		break;
	case POWER_SUPPLY_PROP_PD_IN_HARD_RESET:
		rc = smblib_set_prop_pd_in_hard_reset(chg, val);
		break;
	//Do not set this prop in mutex_lock. It will cause PD hard reset
	//case POWER_SUPPLY_PROP_PD_USB_SUSPEND_SUPPORTED:
	//	chg->system_suspend_supported = val->intval;
	//	break;
	case POWER_SUPPLY_PROP_BOOST_CURRENT:
		rc = smblib_set_prop_boost_current(chg, val);
		break;
	case POWER_SUPPLY_PROP_CTM_CURRENT_MAX:
		rc = vote(chg->usb_icl_votable, CTM_VOTER,
						val->intval >= 0, val->intval);
		break;
	case POWER_SUPPLY_PROP_PR_SWAP:
		rc = smblib_set_prop_pr_swap_in_progress(chg, val);
		break;
	case POWER_SUPPLY_PROP_PD_VOLTAGE_MAX:
		rc = smblib_set_prop_pd_voltage_max(chg, val);
		break;
	case POWER_SUPPLY_PROP_PD_VOLTAGE_MIN:
		rc = smblib_set_prop_pd_voltage_min(chg, val);
		break;
	case POWER_SUPPLY_PROP_SDP_CURRENT_MAX:
		rc = smblib_set_prop_sdp_current_max(chg, val);
		break;
	default:
		pr_err("set prop %d is not supported\n", psp);
		rc = -EINVAL;
		break;
	}

unlock:
	mutex_unlock(&chg->lock);
	return rc;
}

static int smb2_usb_prop_is_writeable(struct power_supply *psy,
		enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_CTM_CURRENT_MAX:
		return 1;
	default:
		break;
	}

	return 0;
}

static int smb2_init_usb_psy(struct smb2 *chip)
{
	struct power_supply_config usb_cfg = {};
	struct smb_charger *chg = &chip->chg;

	chg->usb_psy_desc.name			= "usb";
	chg->usb_psy_desc.type			= POWER_SUPPLY_TYPE_USB_PD;
	chg->usb_psy_desc.properties		= smb2_usb_props;
	chg->usb_psy_desc.num_properties	= ARRAY_SIZE(smb2_usb_props);
	chg->usb_psy_desc.get_property		= smb2_usb_get_prop;
	chg->usb_psy_desc.set_property		= smb2_usb_set_prop;
	chg->usb_psy_desc.property_is_writeable	= smb2_usb_prop_is_writeable;

	usb_cfg.drv_data = chip;
	usb_cfg.of_node = chg->dev->of_node;
	chg->usb_psy = power_supply_register(chg->dev,
						  &chg->usb_psy_desc,
						  &usb_cfg);
	if (IS_ERR(chg->usb_psy)) {
		pr_err("Couldn't register USB power supply\n");
		return PTR_ERR(chg->usb_psy);
	}

	return 0;
}

int rt_charger_set_usb_property_notifier(enum power_supply_property psp, int value)
{
	union power_supply_propval val = {0};
	bool pogo_ovp_stats = gpio_get_value_cansleep(global_gpio->POGO_OVP_ACOK);
	int rc = 0;

	if (!pogo_ovp_stats && !gpio_get_value(global_gpio->WP_BTM)) {
		CHG_DBG_E("%s: Set dual_port_once_flag = 1\n", __func__);
		dual_port_once_flag = 1;
	}

	if (!pogo_ovp_stats && (psp == POWER_SUPPLY_PROP_PD_CURRENT_MAX || psp == POWER_SUPPLY_PROP_PD_VOLTAGE_MAX)) {
		CHG_DBG_E("%s: POGO_OVP exist, do not set current/voltage property from BTM\n", __func__);
		return 0;
	}

	mutex_lock(&smbchg_dev->lock);

	if (psp == POWER_SUPPLY_PROP_PD_CURRENT_MAX) {
		if (rt_chg_check_asus_vid()) {
			CHG_DBG_E("%s: BTM PCA working, do not set PD_CURRENT_MAX property from BTM\n", __func__);
			mutex_unlock(&smbchg_dev->lock);
			return 0;
		}
	}

	val.intval = value;

	switch (psp) {
	case POWER_SUPPLY_PROP_PD_ALLOWED:	//115
		rc = smblib_set_prop_pd_allowed(smbchg_dev, &val);
		break;
	case POWER_SUPPLY_PROP_PD2_ACTIVE:	//117
		rc = smblib_set_prop_pd2_active(smbchg_dev, &val);
		break;
	case POWER_SUPPLY_PROP_PD_CURRENT_MAX:	//119
		rc = smblib_set_prop_pd_current_max(smbchg_dev, &val);
		break;
	case POWER_SUPPLY_PROP_PD_VOLTAGE_MAX:	//140
		rc = smblib_set_prop_pd_voltage_max(smbchg_dev, &val);
		break;
	default:
		break;
	}
	CHG_DBG("%s: Rt-charger set property %d, set value = %d, rc = %d", __func__, psp, value, rc);

	mutex_unlock(&smbchg_dev->lock);
	return rc;
}

/********************************
 * USB PC_PORT PSY REGISTRATION *
 ********************************/
static enum power_supply_property smb2_usb_port_props[] = {
	POWER_SUPPLY_PROP_TYPE,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CURRENT_MAX,
};

static int smb2_usb_port_get_prop(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct smb2 *chip = power_supply_get_drvdata(psy);
	struct smb_charger *chg = &chip->chg;
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_TYPE:
		val->intval = POWER_SUPPLY_TYPE_USB;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		rc = smblib_get_prop_usb_online(chg, val);
		if (!val->intval)
			break;

		if (((chg->typec_mode == POWER_SUPPLY_TYPEC_SOURCE_DEFAULT)
		   || (chg->connector_type == POWER_SUPPLY_CONNECTOR_MICRO_USB))
			&& (chg->real_charger_type == POWER_SUPPLY_TYPE_USB))
			val->intval = 1;
//Add for BTM online state +++
		else if ((!gpio_get_value_cansleep(global_gpio->BTM_OVP_ACOK))
			&& (chg->real_charger_type == POWER_SUPPLY_TYPE_USB))
			val->intval = 1;
//Add for BTM online state ---
		else
			val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		val->intval = 5000000;
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		rc = smblib_get_prop_input_current_settled(chg, val);
		break;
	default:
		pr_err_ratelimited("Get prop %d is not supported in pc_port\n",
				psp);
		return -EINVAL;
	}

	if (rc < 0) {
		pr_debug("Couldn't get prop %d rc = %d\n", psp, rc);
		return -ENODATA;
	}

	return 0;
}

static int smb2_usb_port_set_prop(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *val)
{
	int rc = 0;

	switch (psp) {
	default:
		pr_err_ratelimited("Set prop %d is not supported in pc_port\n",
				psp);
		rc = -EINVAL;
		break;
	}

	return rc;
}

static const struct power_supply_desc usb_port_psy_desc = {
	.name		= "pc_port",
	.type		= POWER_SUPPLY_TYPE_USB,
	.properties	= smb2_usb_port_props,
	.num_properties	= ARRAY_SIZE(smb2_usb_port_props),
	.get_property	= smb2_usb_port_get_prop,
	.set_property	= smb2_usb_port_set_prop,
};

static int smb2_init_usb_port_psy(struct smb2 *chip)
{
	struct power_supply_config usb_port_cfg = {};
	struct smb_charger *chg = &chip->chg;

	usb_port_cfg.drv_data = chip;
	usb_port_cfg.of_node = chg->dev->of_node;
	chg->usb_port_psy = power_supply_register(chg->dev,
						  &usb_port_psy_desc,
						  &usb_port_cfg);
	if (IS_ERR(chg->usb_port_psy)) {
		pr_err("Couldn't register USB pc_port power supply\n");
		return PTR_ERR(chg->usb_port_psy);
	}

	return 0;
}

/*****************************
 * USB MAIN PSY REGISTRATION *
 *****************************/

static enum power_supply_property smb2_usb_main_props[] = {
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_TYPE,
	POWER_SUPPLY_PROP_INPUT_CURRENT_SETTLED,
	POWER_SUPPLY_PROP_INPUT_VOLTAGE_SETTLED,
	POWER_SUPPLY_PROP_FCC_DELTA,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_TOGGLE_STAT,
#ifdef CONFIG_USBPD_PHY_QCOM
	POWER_SUPPLY_PROP_CHARGING_ENABLED,
#endif
	/*
	 * TODO move the TEMP and TEMP_MAX properties here,
	 * and update the thermal balancer to look here
	 */
};

static int smb2_usb_main_get_prop(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct smb2 *chip = power_supply_get_drvdata(psy);
	struct smb_charger *chg = &chip->chg;
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		rc = smblib_get_charge_param(chg, &chg->param.fv, &val->intval);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		rc = smblib_get_charge_param(chg, &chg->param.fcc,
							&val->intval);
		break;
	case POWER_SUPPLY_PROP_TYPE:
		val->intval = POWER_SUPPLY_TYPE_MAIN;
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_SETTLED:
		rc = smblib_get_prop_input_current_settled(chg, val);
		break;
	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_SETTLED:
		rc = smblib_get_prop_input_voltage_settled(chg, val);
		break;
	case POWER_SUPPLY_PROP_FCC_DELTA:
		rc = smblib_get_prop_fcc_delta(chg, val);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		rc = smblib_get_icl_current(chg, &val->intval);
		break;
	case POWER_SUPPLY_PROP_TOGGLE_STAT:
		val->intval = 0;
		break;
#ifdef CONFIG_USBPD_PHY_QCOM
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		rc = smblib_get_prop_charging_enabled(chg, val);
		break;
#endif
	default:
		pr_debug("get prop %d is not supported in usb-main\n", psp);
		rc = -EINVAL;
		break;
	}
	if (rc < 0) {
		pr_debug("Couldn't get prop %d rc = %d\n", psp, rc);
		return -ENODATA;
	}
	return 0;
}

static int smb2_usb_main_set_prop(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *val)
{
	struct smb2 *chip = power_supply_get_drvdata(psy);
	struct smb_charger *chg = &chip->chg;
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		pr_info("[SMB2] %s: set voltage_max: %d\n", __func__, val->intval);
		rc = smblib_set_charge_param(chg, &chg->param.fv, val->intval);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		pr_info("[SMB2] %s: set constant charge_current: %d\n", __func__, val->intval);
		rc = smblib_set_charge_param(chg, &chg->param.fcc, val->intval);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		pr_info("[SMB2] %s: set current_max: %d\n", __func__, val->intval);
		rc = smblib_set_icl_current(chg, val->intval);
		break;
	case POWER_SUPPLY_PROP_TOGGLE_STAT:
		rc = smblib_toggle_stat(chg, val->intval);
		break;
#ifdef CONFIG_USBPD_PHY_QCOM
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		rc = smblib_set_prop_charging_enabled(chg, val);
		break;
#endif
	default:
		pr_err("set prop %d is not supported\n", psp);
		rc = -EINVAL;
		break;
	}

	return rc;
}

static int smb2_usb_main_prop_is_writeable(struct power_supply *psy,
				enum power_supply_property psp)
{
	int rc;

	switch (psp) {
	case POWER_SUPPLY_PROP_TOGGLE_STAT:
		rc = 1;
		break;
	default:
		rc = 0;
		break;
	}

	return rc;
}

static const struct power_supply_desc usb_main_psy_desc = {
	.name		= "main",
	.type		= POWER_SUPPLY_TYPE_MAIN,
	.properties	= smb2_usb_main_props,
	.num_properties	= ARRAY_SIZE(smb2_usb_main_props),
	.get_property	= smb2_usb_main_get_prop,
	.set_property	= smb2_usb_main_set_prop,
	.property_is_writeable = smb2_usb_main_prop_is_writeable,
};

static int smb2_init_usb_main_psy(struct smb2 *chip)
{
	struct power_supply_config usb_main_cfg = {};
	struct smb_charger *chg = &chip->chg;

	usb_main_cfg.drv_data = chip;
	usb_main_cfg.of_node = chg->dev->of_node;
	chg->usb_main_psy = power_supply_register(chg->dev,
						  &usb_main_psy_desc,
						  &usb_main_cfg);
	if (IS_ERR(chg->usb_main_psy)) {
		pr_err("Couldn't register USB main power supply\n");
		return PTR_ERR(chg->usb_main_psy);
	}

	return 0;
}

/*************************
 * DC PSY REGISTRATION   *
 *************************/

static enum power_supply_property smb2_dc_props[] = {
	POWER_SUPPLY_PROP_INPUT_SUSPEND,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_REAL_TYPE,
};

static int smb2_dc_get_prop(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct smb2 *chip = power_supply_get_drvdata(psy);
	struct smb_charger *chg = &chip->chg;
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_INPUT_SUSPEND:
		val->intval = get_effective_result(chg->dc_suspend_votable);
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		rc = smblib_get_prop_dc_present(chg, val);
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		rc = smblib_get_prop_dc_online(chg, val);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		rc = smblib_get_prop_dc_current_max(chg, val);
		break;
	case POWER_SUPPLY_PROP_REAL_TYPE:
		val->intval = POWER_SUPPLY_TYPE_WIPOWER;
		break;
	default:
		return -EINVAL;
	}
	if (rc < 0) {
		pr_debug("Couldn't get prop %d rc = %d\n", psp, rc);
		return -ENODATA;
	}
	return 0;
}

static int smb2_dc_set_prop(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *val)
{
	struct smb2 *chip = power_supply_get_drvdata(psy);
	struct smb_charger *chg = &chip->chg;
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_INPUT_SUSPEND:
		rc = vote(chg->dc_suspend_votable, WBC_VOTER,
				(bool)val->intval, 0);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		rc = smblib_set_prop_dc_current_max(chg, val);
		break;
	default:
		return -EINVAL;
	}

	return rc;
}

static int smb2_dc_prop_is_writeable(struct power_supply *psy,
		enum power_supply_property psp)
{
	int rc;

	switch (psp) {
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		rc = 1;
		break;
	default:
		rc = 0;
		break;
	}

	return rc;
}

static const struct power_supply_desc dc_psy_desc = {
	.name = "dc",
	.type = POWER_SUPPLY_TYPE_WIRELESS,
	.properties = smb2_dc_props,
	.num_properties = ARRAY_SIZE(smb2_dc_props),
	.get_property = smb2_dc_get_prop,
	.set_property = smb2_dc_set_prop,
	.property_is_writeable = smb2_dc_prop_is_writeable,
};

static int smb2_init_dc_psy(struct smb2 *chip)
{
	struct power_supply_config dc_cfg = {};
	struct smb_charger *chg = &chip->chg;

	dc_cfg.drv_data = chip;
	dc_cfg.of_node = chg->dev->of_node;
	chg->dc_psy = power_supply_register(chg->dev,
						  &dc_psy_desc,
						  &dc_cfg);
	if (IS_ERR(chg->dc_psy)) {
		pr_err("Couldn't register USB power supply\n");
		return PTR_ERR(chg->dc_psy);
	}

	return 0;
}

/*************************
 * BATT PSY REGISTRATION *
 *************************/

static enum power_supply_property smb2_batt_props[] = {
	POWER_SUPPLY_PROP_INPUT_SUSPEND,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CHARGER_TEMP,
	POWER_SUPPLY_PROP_CHARGER_TEMP_MAX,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMITED,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_QNOVO,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_QNOVO,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_STEP_CHARGING_ENABLED,
	POWER_SUPPLY_PROP_SW_JEITA_ENABLED,
	POWER_SUPPLY_PROP_CHARGE_DONE,
	POWER_SUPPLY_PROP_PARALLEL_DISABLE,
	POWER_SUPPLY_PROP_SET_SHIP_MODE,
	POWER_SUPPLY_PROP_DIE_HEALTH,
	POWER_SUPPLY_PROP_RERUN_AICL,
	POWER_SUPPLY_PROP_DP_DM,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT,
	POWER_SUPPLY_PROP_CHARGE_COUNTER,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_FCC_STEPPER_ENABLE,
};

static int smb2_batt_get_prop(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct smb_charger *chg = power_supply_get_drvdata(psy);
	int rc = 0;
	union power_supply_propval pval = {0, };

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		rc = smblib_get_prop_batt_status(chg, val);
		//ASUS BSP : Show "+" on charging icon +++
		if (qc_stat_registed && boot_completed_flag)
			set_qc_stat(val);
		//ASUS BSP : Show "+" on charging icon ---
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		rc = smblib_get_prop_batt_health(chg, val);
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		rc = smblib_get_prop_batt_present(chg, val);
		break;
	case POWER_SUPPLY_PROP_INPUT_SUSPEND:
		rc = smblib_get_prop_input_suspend(chg, val);
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		rc = smblib_get_prop_batt_charge_type(chg, val);
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		rc = smblib_get_prop_batt_capacity(chg, val);
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		rc = smblib_get_prop_system_temp_level(chg, val);
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX:
		rc = smblib_get_prop_system_temp_level_max(chg, val);
		break;
	case POWER_SUPPLY_PROP_CHARGER_TEMP:
		/* do not query RRADC if charger is not present */
		rc = smblib_get_prop_usb_present(chg, &pval);
		if (rc < 0)
			pr_err("Couldn't get usb present rc=%d\n", rc);

		rc = -ENODATA;
		if (pval.intval)
			rc = smblib_get_prop_charger_temp(chg, val);
		break;
	case POWER_SUPPLY_PROP_CHARGER_TEMP_MAX:
		rc = smblib_get_prop_charger_temp_max(chg, val);
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMITED:
		rc = smblib_get_prop_input_current_limited(chg, val);
		break;
	case POWER_SUPPLY_PROP_STEP_CHARGING_ENABLED:
		val->intval = chg->step_chg_enabled;
		break;
	case POWER_SUPPLY_PROP_SW_JEITA_ENABLED:
		val->intval = chg->sw_jeita_enabled;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		val->intval = get_client_vote(chg->fv_votable,
				BATT_PROFILE_VOTER);
		break;
	case POWER_SUPPLY_PROP_CHARGE_QNOVO_ENABLE:
		rc = smblib_get_prop_charge_qnovo_enable(chg, val);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_QNOVO:
		val->intval = get_client_vote_locked(chg->fv_votable,
				QNOVO_VOTER);
		break;
	case POWER_SUPPLY_PROP_CURRENT_QNOVO:
		val->intval = get_client_vote_locked(chg->fcc_votable,
				QNOVO_VOTER);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		val->intval = get_client_vote(chg->fcc_votable,
					      BATT_PROFILE_VOTER);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		val->intval = get_client_vote(chg->fcc_votable,
					      FG_ESR_VOTER);
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_CHARGE_DONE:
		rc = smblib_get_prop_batt_charge_done(chg, val);
		break;
	case POWER_SUPPLY_PROP_PARALLEL_DISABLE:
		val->intval = get_client_vote(chg->pl_disable_votable,
					      USER_VOTER);
		break;
	case POWER_SUPPLY_PROP_SET_SHIP_MODE:
		/* Not in ship mode as long as device is active */
		val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_DIE_HEALTH:
		if (chg->die_health == -EINVAL)
			rc = smblib_get_prop_die_health(chg, val);
		else
			val->intval = chg->die_health;
		break;
	case POWER_SUPPLY_PROP_DP_DM:
		val->intval = chg->pulse_cnt;
		break;
	case POWER_SUPPLY_PROP_RERUN_AICL:
		val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_CHARGE_COUNTER:
	case POWER_SUPPLY_PROP_CHARGE_FULL:
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
	case POWER_SUPPLY_PROP_CURRENT_NOW:
	case POWER_SUPPLY_PROP_TEMP:
		rc = smblib_get_prop_from_bms(chg, psp, val);
		break;
	case POWER_SUPPLY_PROP_FCC_STEPPER_ENABLE:
		val->intval = chg->fcc_stepper_enable;
		break;
	default:
		pr_err("batt power supply prop %d not supported\n", psp);
		return -EINVAL;
	}

	if (rc < 0) {
		pr_debug("Couldn't get prop %d rc = %d\n", psp, rc);
		return -ENODATA;
	}

	return 0;
}

static int smb2_batt_set_prop(struct power_supply *psy,
		enum power_supply_property prop,
		const union power_supply_propval *val)
{
	int rc = 0;
	struct smb_charger *chg = power_supply_get_drvdata(psy);

	switch (prop) {
	case POWER_SUPPLY_PROP_STATUS:
		rc = smblib_set_prop_batt_status(chg, val);
		break;
	case POWER_SUPPLY_PROP_INPUT_SUSPEND:
		rc = smblib_set_prop_input_suspend(chg, val);
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		rc = smblib_set_prop_system_temp_level(chg, val);
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		rc = smblib_set_prop_batt_capacity(chg, val);
		break;
	case POWER_SUPPLY_PROP_PARALLEL_DISABLE:
		vote(chg->pl_disable_votable, USER_VOTER, (bool)val->intval, 0);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		chg->batt_profile_fv_uv = val->intval;
		vote(chg->fv_votable, BATT_PROFILE_VOTER, true, val->intval);
		break;
	case POWER_SUPPLY_PROP_CHARGE_QNOVO_ENABLE:
		rc = smblib_set_prop_charge_qnovo_enable(chg, val);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_QNOVO:
		vote(chg->fv_votable, QNOVO_VOTER,
			(val->intval >= 0), val->intval);
		break;
	case POWER_SUPPLY_PROP_CURRENT_QNOVO:
		vote(chg->pl_disable_votable, PL_QNOVO_VOTER,
			val->intval != -EINVAL && val->intval < 2000000, 0);
		if (val->intval == -EINVAL) {
			vote(chg->fcc_votable, BATT_PROFILE_VOTER,
					true, chg->batt_profile_fcc_ua);
			vote(chg->fcc_votable, QNOVO_VOTER, false, 0);
		} else {
			vote(chg->fcc_votable, QNOVO_VOTER, true, val->intval);
			vote(chg->fcc_votable, BATT_PROFILE_VOTER, false, 0);
		}
		break;
	case POWER_SUPPLY_PROP_STEP_CHARGING_ENABLED:
		chg->step_chg_enabled = !!val->intval;
		break;
	case POWER_SUPPLY_PROP_SW_JEITA_ENABLED:
		if (chg->sw_jeita_enabled != (!!val->intval)) {
			rc = smblib_disable_hw_jeita(chg, !!val->intval);
			if (rc == 0)
				chg->sw_jeita_enabled = !!val->intval;
		}
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		chg->batt_profile_fcc_ua = val->intval;
		vote(chg->fcc_votable, BATT_PROFILE_VOTER, true, val->intval);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		if (val->intval)
			vote(chg->fcc_votable, FG_ESR_VOTER, true, val->intval);
		else
			vote(chg->fcc_votable, FG_ESR_VOTER, false, 0);
		break;
	case POWER_SUPPLY_PROP_SET_SHIP_MODE:
		/* Not in ship mode as long as the device is active */
		if (!val->intval)
			break;
		if (chg->pl.psy)
			power_supply_set_property(chg->pl.psy,
				POWER_SUPPLY_PROP_SET_SHIP_MODE, val);
		rc = smblib_set_prop_ship_mode(chg, val);
		break;
	case POWER_SUPPLY_PROP_RERUN_AICL:
		rc = smblib_rerun_aicl(chg);
		break;
	case POWER_SUPPLY_PROP_DP_DM:
		rc = smblib_dp_dm(chg, val->intval);
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMITED:
		rc = smblib_set_prop_input_current_limited(chg, val);
		break;
	case POWER_SUPPLY_PROP_DIE_HEALTH:
		chg->die_health = val->intval;
		power_supply_changed(chg->batt_psy);
		break;
	default:
		rc = -EINVAL;
	}

	return rc;
}

static int smb2_batt_prop_is_writeable(struct power_supply *psy,
		enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
	case POWER_SUPPLY_PROP_INPUT_SUSPEND:
	case POWER_SUPPLY_PROP_SYSTEM_TEMP_LEVEL:
	case POWER_SUPPLY_PROP_CAPACITY:
	case POWER_SUPPLY_PROP_PARALLEL_DISABLE:
	case POWER_SUPPLY_PROP_DP_DM:
	case POWER_SUPPLY_PROP_RERUN_AICL:
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMITED:
	case POWER_SUPPLY_PROP_STEP_CHARGING_ENABLED:
	case POWER_SUPPLY_PROP_SW_JEITA_ENABLED:
	case POWER_SUPPLY_PROP_DIE_HEALTH:
		return 1;
	default:
		break;
	}

	return 0;
}

static const struct power_supply_desc batt_psy_desc = {
	.name = "battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = smb2_batt_props,
	.num_properties = ARRAY_SIZE(smb2_batt_props),
	.get_property = smb2_batt_get_prop,
	.set_property = smb2_batt_set_prop,
	.property_is_writeable = smb2_batt_prop_is_writeable,
};

static int smb2_init_batt_psy(struct smb2 *chip)
{
	struct power_supply_config batt_cfg = {};
	struct smb_charger *chg = &chip->chg;
	int rc = 0;

	batt_cfg.drv_data = chg;
	batt_cfg.of_node = chg->dev->of_node;
	chg->batt_psy = power_supply_register(chg->dev,
						   &batt_psy_desc,
						   &batt_cfg);
	if (IS_ERR(chg->batt_psy)) {
		pr_err("Couldn't register battery power supply\n");
		return PTR_ERR(chg->batt_psy);
	}

	return rc;
}

/******************************
 * VBUS REGULATOR REGISTRATION *
 ******************************/

static struct regulator_ops smb2_vbus_reg_ops = {
	.enable = smblib_vbus_regulator_enable,
	.disable = smblib_vbus_regulator_disable,
	.is_enabled = smblib_vbus_regulator_is_enabled,
};

static int smb2_init_vbus_regulator(struct smb2 *chip)
{
	struct smb_charger *chg = &chip->chg;
	struct regulator_config cfg = {};
	int rc = 0;

	chg->vbus_vreg = devm_kzalloc(chg->dev, sizeof(*chg->vbus_vreg),
				      GFP_KERNEL);
	if (!chg->vbus_vreg)
		return -ENOMEM;

	cfg.dev = chg->dev;
	cfg.driver_data = chip;

	chg->vbus_vreg->rdesc.owner = THIS_MODULE;
	chg->vbus_vreg->rdesc.type = REGULATOR_VOLTAGE;
	chg->vbus_vreg->rdesc.ops = &smb2_vbus_reg_ops;
	chg->vbus_vreg->rdesc.of_match = "qcom,smb2-vbus";
	chg->vbus_vreg->rdesc.name = "qcom,smb2-vbus";

	chg->vbus_vreg->rdev = devm_regulator_register(chg->dev,
						&chg->vbus_vreg->rdesc, &cfg);
	if (IS_ERR(chg->vbus_vreg->rdev)) {
		rc = PTR_ERR(chg->vbus_vreg->rdev);
		chg->vbus_vreg->rdev = NULL;
		if (rc != -EPROBE_DEFER)
			pr_err("Couldn't register VBUS regualtor rc=%d\n", rc);
	}

	return rc;
}

/******************************
 * VCONN REGULATOR REGISTRATION *
 ******************************/

static struct regulator_ops smb2_vconn_reg_ops = {
	.enable = smblib_vconn_regulator_enable,
	.disable = smblib_vconn_regulator_disable,
	.is_enabled = smblib_vconn_regulator_is_enabled,
};

static int smb2_init_vconn_regulator(struct smb2 *chip)
{
	struct smb_charger *chg = &chip->chg;
	struct regulator_config cfg = {};
	int rc = 0;

	if (chg->connector_type == POWER_SUPPLY_CONNECTOR_MICRO_USB)
		return 0;

	chg->vconn_vreg = devm_kzalloc(chg->dev, sizeof(*chg->vconn_vreg),
				      GFP_KERNEL);
	if (!chg->vconn_vreg)
		return -ENOMEM;

	cfg.dev = chg->dev;
	cfg.driver_data = chip;

	chg->vconn_vreg->rdesc.owner = THIS_MODULE;
	chg->vconn_vreg->rdesc.type = REGULATOR_VOLTAGE;
	chg->vconn_vreg->rdesc.ops = &smb2_vconn_reg_ops;
	chg->vconn_vreg->rdesc.of_match = "qcom,smb2-vconn";
	chg->vconn_vreg->rdesc.name = "qcom,smb2-vconn";

	chg->vconn_vreg->rdev = devm_regulator_register(chg->dev,
						&chg->vconn_vreg->rdesc, &cfg);
	if (IS_ERR(chg->vconn_vreg->rdev)) {
		rc = PTR_ERR(chg->vconn_vreg->rdev);
		chg->vconn_vreg->rdev = NULL;
		if (rc != -EPROBE_DEFER)
			pr_err("Couldn't register VCONN regualtor rc=%d\n", rc);
	}

	return rc;
}

/***************************
 * HARDWARE INITIALIZATION *
 ***************************/
static int smb2_config_wipower_input_power(struct smb2 *chip, int uw)
{
	int rc;
	int ua;
	struct smb_charger *chg = &chip->chg;
	s64 nw = (s64)uw * 1000;

	if (uw < 0)
		return 0;

	ua = div_s64(nw, ZIN_ICL_PT_MAX_MV);
	rc = smblib_set_charge_param(chg, &chg->param.dc_icl_pt_lv, ua);
	if (rc < 0) {
		pr_err("Couldn't configure dc_icl_pt_lv rc = %d\n", rc);
		return rc;
	}

	ua = div_s64(nw, ZIN_ICL_PT_HV_MAX_MV);
	rc = smblib_set_charge_param(chg, &chg->param.dc_icl_pt_hv, ua);
	if (rc < 0) {
		pr_err("Couldn't configure dc_icl_pt_hv rc = %d\n", rc);
		return rc;
	}

	ua = div_s64(nw, ZIN_ICL_LV_MAX_MV);
	rc = smblib_set_charge_param(chg, &chg->param.dc_icl_div2_lv, ua);
	if (rc < 0) {
		pr_err("Couldn't configure dc_icl_div2_lv rc = %d\n", rc);
		return rc;
	}

	ua = div_s64(nw, ZIN_ICL_MID_LV_MAX_MV);
	rc = smblib_set_charge_param(chg, &chg->param.dc_icl_div2_mid_lv, ua);
	if (rc < 0) {
		pr_err("Couldn't configure dc_icl_div2_mid_lv rc = %d\n", rc);
		return rc;
	}

	ua = div_s64(nw, ZIN_ICL_MID_HV_MAX_MV);
	rc = smblib_set_charge_param(chg, &chg->param.dc_icl_div2_mid_hv, ua);
	if (rc < 0) {
		pr_err("Couldn't configure dc_icl_div2_mid_hv rc = %d\n", rc);
		return rc;
	}

	ua = div_s64(nw, ZIN_ICL_HV_MAX_MV);
	rc = smblib_set_charge_param(chg, &chg->param.dc_icl_div2_hv, ua);
	if (rc < 0) {
		pr_err("Couldn't configure dc_icl_div2_hv rc = %d\n", rc);
		return rc;
	}

	return 0;
}

static int smb2_configure_typec(struct smb_charger *chg)
{
	int rc;

	/*
	 * trigger the usb-typec-change interrupt only when the CC state
	 * changes
	 */
	rc = smblib_write(chg, TYPE_C_INTRPT_ENB_REG,
			  TYPEC_CCSTATE_CHANGE_INT_EN_BIT);
	if (rc < 0) {
		dev_err(chg->dev,
			"Couldn't configure Type-C interrupts rc=%d\n", rc);
		return rc;
	}

	/*
	 * disable Type-C factory mode and stay in Attached.SRC state when VCONN
	 * over-current happens
	 */
	rc = smblib_masked_write(chg, TYPE_C_CFG_REG,
			FACTORY_MODE_DETECTION_EN_BIT | VCONN_OC_CFG_BIT, 0);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't configure Type-C rc=%d\n", rc);
		return rc;
	}

	/* increase VCONN softstart */
	rc = smblib_masked_write(chg, TYPE_C_CFG_2_REG,
			VCONN_SOFTSTART_CFG_MASK, VCONN_SOFTSTART_CFG_MASK);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't increase VCONN softstart rc=%d\n",
			rc);
		return rc;
	}

	/* disable try.SINK mode and legacy cable IRQs */
	rc = smblib_masked_write(chg, TYPE_C_CFG_3_REG, EN_TRYSINK_MODE_BIT |
				TYPEC_NONCOMPLIANT_LEGACY_CABLE_INT_EN_BIT |
				TYPEC_LEGACY_CABLE_INT_EN_BIT, 0);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't set Type-C config rc=%d\n", rc);
		return rc;
	}

	return rc;
}

static int smb2_disable_typec(struct smb_charger *chg)
{
	int rc;

	/* Move to typeC mode */
	/* configure FSM in idle state and disable UFP_ENABLE bit */
	rc = smblib_masked_write(chg, TYPE_C_INTRPT_ENB_SOFTWARE_CTRL_REG,
			TYPEC_DISABLE_CMD_BIT | UFP_EN_CMD_BIT,
			TYPEC_DISABLE_CMD_BIT);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't put FSM in idle rc=%d\n", rc);
		return rc;
	}

	/* wait for FSM to enter idle state */
	msleep(200);
	/* configure TypeC mode */
	rc = smblib_masked_write(chg, TYPE_C_CFG_REG,
			TYPE_C_OR_U_USB_BIT, 0);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't enable micro USB mode rc=%d\n", rc);
		return rc;
	}

	/* wait for mode change before enabling FSM */
	usleep_range(10000, 11000);
	/* release FSM from idle state */
	rc = smblib_masked_write(chg, TYPE_C_INTRPT_ENB_SOFTWARE_CTRL_REG,
			TYPEC_DISABLE_CMD_BIT, 0);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't release FSM rc=%d\n", rc);
		return rc;
	}

	/* wait for FSM to start */
	msleep(100);
	/* move to uUSB mode */
	/* configure FSM in idle state */
	rc = smblib_masked_write(chg, TYPE_C_INTRPT_ENB_SOFTWARE_CTRL_REG,
			TYPEC_DISABLE_CMD_BIT, TYPEC_DISABLE_CMD_BIT);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't put FSM in idle rc=%d\n", rc);
		return rc;
	}

	/* wait for FSM to enter idle state */
	msleep(200);
	/* configure micro USB mode */
	rc = smblib_masked_write(chg, TYPE_C_CFG_REG,
			TYPE_C_OR_U_USB_BIT, TYPE_C_OR_U_USB_BIT);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't enable micro USB mode rc=%d\n", rc);
		return rc;
	}

	/* wait for mode change before enabling FSM */
	usleep_range(10000, 11000);
	/* release FSM from idle state */
	rc = smblib_masked_write(chg, TYPE_C_INTRPT_ENB_SOFTWARE_CTRL_REG,
			TYPEC_DISABLE_CMD_BIT, 0);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't release FSM rc=%d\n", rc);
		return rc;
	}

	return rc;
}

static int smb2_init_hw(struct smb2 *chip)
{
	struct smb_charger *chg = &chip->chg;
	int rc;
	u8 stat, val;

	if (chip->dt.no_battery)
		chg->fake_capacity = 50;

	if (chg->batt_profile_fcc_ua < 0)
		smblib_get_charge_param(chg, &chg->param.fcc,
				&chg->batt_profile_fcc_ua);

	if (chg->batt_profile_fv_uv < 0)
		smblib_get_charge_param(chg, &chg->param.fv,
				&chg->batt_profile_fv_uv);

	smblib_get_charge_param(chg, &chg->param.usb_icl,
				&chg->default_icl_ua);
	if (chip->dt.usb_icl_ua < 0)
		chip->dt.usb_icl_ua = chg->default_icl_ua;

	if (chip->dt.dc_icl_ua < 0)
		smblib_get_charge_param(chg, &chg->param.dc_icl,
					&chip->dt.dc_icl_ua);

	if (chip->dt.min_freq_khz > 0) {
		chg->param.freq_buck.min_u = chip->dt.min_freq_khz;
		chg->param.freq_boost.min_u = chip->dt.min_freq_khz;
	}

	if (chip->dt.max_freq_khz > 0) {
		chg->param.freq_buck.max_u = chip->dt.max_freq_khz;
		chg->param.freq_boost.max_u = chip->dt.max_freq_khz;
	}

	/* set a slower soft start setting for OTG */
	rc = smblib_masked_write(chg, DC_ENG_SSUPPLY_CFG2_REG,
				ENG_SSUPPLY_IVREF_OTG_SS_MASK, OTG_SS_SLOW);
	if (rc < 0) {
		pr_err("Couldn't set otg soft start rc=%d\n", rc);
		return rc;
	}

	/* set OTG current limit */
	rc = smblib_set_charge_param(chg, &chg->param.otg_cl,
				(chg->wa_flags & OTG_WA) ?
				chg->param.otg_cl.min_u : chg->otg_cl_ua);
	if (rc < 0) {
		pr_err("Couldn't set otg current limit rc=%d\n", rc);
		return rc;
	}

	chg->boost_threshold_ua = chip->dt.boost_threshold_ua;

	rc = smblib_read(chg, APSD_RESULT_STATUS_REG, &stat);
	if (rc < 0) {
		pr_err("Couldn't read APSD_RESULT_STATUS rc=%d\n", rc);
		return rc;
	}

	smblib_rerun_apsd_if_required(chg);

	/* clear the ICL override if it is set */
	if (smblib_icl_override(chg, false) < 0) {
		pr_err("Couldn't disable ICL override rc=%d\n", rc);
		return rc;
	}

	/* votes must be cast before configuring software control */
	/* vote 0mA on usb_icl for non battery platforms */
	vote(chg->usb_icl_votable,
		DEFAULT_VOTER, chip->dt.no_battery, 0);
	vote(chg->dc_suspend_votable,
		DEFAULT_VOTER, chip->dt.no_battery, 0);
	vote(chg->fcc_votable,
		BATT_PROFILE_VOTER, true, chg->batt_profile_fcc_ua);
	vote(chg->fv_votable,
		BATT_PROFILE_VOTER, true, chg->batt_profile_fv_uv);
	vote(chg->dc_icl_votable,
		DEFAULT_VOTER, true, chip->dt.dc_icl_ua);
	vote(chg->hvdcp_disable_votable_indirect, DEFAULT_VOTER,
		chip->dt.hvdcp_disable, 0);
	vote(chg->pd_disallowed_votable_indirect, CC_DETACHED_VOTER,
			true, 0);
	vote(chg->pd_disallowed_votable_indirect, HVDCP_TIMEOUT_VOTER,
			true, 0);
#ifdef CONFIG_USBPD_PHY_QCOM
	/* This voter only set when the direct charging started */
	vote(chg->pd_disallowed_votable_indirect, PD_DIRECT_CHARGE_VOTER,
			false, 0);
	/* enable switching charger */
	vote(chg->chg_disable_votable, DIRECT_CHARGE_VOTER, false, 0);
#endif
	vote(chg->hvdcp_enable_votable, MICRO_USB_VOTER,
		(chg->connector_type == POWER_SUPPLY_CONNECTOR_MICRO_USB), 0);

	/*
	 * AICL configuration:
	 * start from min and AICL ADC disable
	 */
	rc = smblib_masked_write(chg, USBIN_AICL_OPTIONS_CFG_REG,
			USBIN_AICL_START_AT_MAX_BIT
				| USBIN_AICL_ADC_EN_BIT, 0);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't configure AICL rc=%d\n", rc);
		return rc;
	}

	/* Configure charge enable for software control; active high */
	rc = smblib_masked_write(chg, CHGR_CFG2_REG,
				 CHG_EN_POLARITY_BIT |
				 CHG_EN_SRC_BIT, 0);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't configure charger rc=%d\n", rc);
		return rc;
	}

	/* enable the charging path */
	rc = vote(chg->chg_disable_votable, DEFAULT_VOTER, false, 0);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't enable charging rc=%d\n", rc);
		return rc;
	}

	/* Check USB connector type (typeC/microUSB) */
	rc = smblib_read(chg, RID_CC_CONTROL_7_0_REG, &val);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't read RID_CC_CONTROL_7_0 rc=%d\n",
			rc);
		return rc;
	}
	chg->connector_type = (val & EN_MICRO_USB_MODE_BIT) ?
					POWER_SUPPLY_CONNECTOR_MICRO_USB
					: POWER_SUPPLY_CONNECTOR_TYPEC;
	if (chg->connector_type == POWER_SUPPLY_CONNECTOR_MICRO_USB || g_ASUS_hwID == ZS600KL_SR1 || g_ASUS_hwID == ZS600KL_SR2)
		rc = smb2_disable_typec(chg);
	else
		rc = smb2_configure_typec(chg);
	if (rc < 0) {
		dev_err(chg->dev,
			"Couldn't configure Type-C interrupts rc=%d\n", rc);
		return rc;
	}

	/* Connector types based votes */
	vote(chg->hvdcp_disable_votable_indirect, PD_INACTIVE_VOTER,
		(chg->connector_type == POWER_SUPPLY_CONNECTOR_TYPEC), 0);
	vote(chg->hvdcp_disable_votable_indirect, VBUS_CC_SHORT_VOTER,
		(chg->connector_type == POWER_SUPPLY_CONNECTOR_TYPEC), 0);
	vote(chg->pd_disallowed_votable_indirect, MICRO_USB_VOTER,
		(chg->connector_type == POWER_SUPPLY_CONNECTOR_MICRO_USB), 0);
	vote(chg->hvdcp_enable_votable, MICRO_USB_VOTER,
		(chg->connector_type == POWER_SUPPLY_CONNECTOR_MICRO_USB), 0);

	/* configure VCONN for software control */
	rc = smblib_masked_write(chg, TYPE_C_INTRPT_ENB_SOFTWARE_CTRL_REG,
				 VCONN_EN_SRC_BIT | VCONN_EN_VALUE_BIT,
				 VCONN_EN_SRC_BIT);
	if (rc < 0) {
		dev_err(chg->dev,
			"Couldn't configure VCONN for SW control rc=%d\n", rc);
		return rc;
	}

	/* configure VBUS for software control */
	rc = smblib_masked_write(chg, OTG_CFG_REG, OTG_EN_SRC_CFG_BIT, 0);
	if (rc < 0) {
		dev_err(chg->dev,
			"Couldn't configure VBUS for SW control rc=%d\n", rc);
		return rc;
	}

	val = (ilog2(chip->dt.wd_bark_time / 16) << BARK_WDOG_TIMEOUT_SHIFT) &
						BARK_WDOG_TIMEOUT_MASK;
	val |= BITE_WDOG_TIMEOUT_8S;
	rc = smblib_masked_write(chg, SNARL_BARK_BITE_WD_CFG_REG,
			BITE_WDOG_DISABLE_CHARGING_CFG_BIT |
			BARK_WDOG_TIMEOUT_MASK | BITE_WDOG_TIMEOUT_MASK,
			val);
	if (rc) {
		pr_err("Couldn't configue WD config rc=%d\n", rc);
		return rc;
	}

	/* enable WD BARK and enable it on plugin */
	rc = smblib_masked_write(chg, WD_CFG_REG,
			WATCHDOG_TRIGGER_AFP_EN_BIT |
			WDOG_TIMER_EN_ON_PLUGIN_BIT |
			BARK_WDOG_INT_EN_BIT,
			WDOG_TIMER_EN_ON_PLUGIN_BIT |
			BARK_WDOG_INT_EN_BIT);
	if (rc) {
		pr_err("Couldn't configue WD config rc=%d\n", rc);
		return rc;
	}

	/* configure wipower watts */
	rc = smb2_config_wipower_input_power(chip, chip->dt.wipower_max_uw);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't configure wipower rc=%d\n", rc);
		return rc;
	}

	/* disable h/w autonomous parallel charging control */
	rc = smblib_masked_write(chg, MISC_CFG_REG,
				 STAT_PARALLEL_1400MA_EN_CFG_BIT, 0);
	if (rc < 0) {
		dev_err(chg->dev,
			"Couldn't disable h/w autonomous parallel control rc=%d\n",
			rc);
		return rc;
	}

	/*
	 * allow DRP.DFP time to exceed by tPDdebounce time.
	 */
	rc = smblib_masked_write(chg, TAPER_TIMER_SEL_CFG_REG,
				TYPEC_DRP_DFP_TIME_CFG_BIT,
				TYPEC_DRP_DFP_TIME_CFG_BIT);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't configure DRP.DFP time rc=%d\n",
			rc);
		return rc;
	}

	/* configure float charger options */
	switch (chip->dt.float_option) {
	case 1:
		rc = smblib_masked_write(chg, USBIN_OPTIONS_2_CFG_REG,
				FLOAT_OPTIONS_MASK, 0);
		break;
	case 2:
		rc = smblib_masked_write(chg, USBIN_OPTIONS_2_CFG_REG,
				FLOAT_OPTIONS_MASK, FORCE_FLOAT_SDP_CFG_BIT);
		break;
	case 3:
		rc = smblib_masked_write(chg, USBIN_OPTIONS_2_CFG_REG,
				FLOAT_OPTIONS_MASK, FLOAT_DIS_CHGING_CFG_BIT);
		break;
	case 4:
		rc = smblib_masked_write(chg, USBIN_OPTIONS_2_CFG_REG,
				FLOAT_OPTIONS_MASK, SUSPEND_FLOAT_CFG_BIT);
		break;
	default:
		rc = 0;
		break;
	}

	if (rc < 0) {
		dev_err(chg->dev, "Couldn't configure float charger options rc=%d\n",
			rc);
		return rc;
	}

	rc = smblib_read(chg, USBIN_OPTIONS_2_CFG_REG, &chg->float_cfg);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't read float charger options rc=%d\n",
			rc);
		return rc;
	}

	switch (chip->dt.chg_inhibit_thr_mv) {
	case 50:
		rc = smblib_masked_write(chg, CHARGE_INHIBIT_THRESHOLD_CFG_REG,
				CHARGE_INHIBIT_THRESHOLD_MASK,
				CHARGE_INHIBIT_THRESHOLD_50MV);
		break;
	case 100:
		rc = smblib_masked_write(chg, CHARGE_INHIBIT_THRESHOLD_CFG_REG,
				CHARGE_INHIBIT_THRESHOLD_MASK,
				CHARGE_INHIBIT_THRESHOLD_100MV);
		break;
	case 200:
		rc = smblib_masked_write(chg, CHARGE_INHIBIT_THRESHOLD_CFG_REG,
				CHARGE_INHIBIT_THRESHOLD_MASK,
				CHARGE_INHIBIT_THRESHOLD_200MV);
		break;
	case 300:
		rc = smblib_masked_write(chg, CHARGE_INHIBIT_THRESHOLD_CFG_REG,
				CHARGE_INHIBIT_THRESHOLD_MASK,
				CHARGE_INHIBIT_THRESHOLD_300MV);
		break;
	case 0:
		rc = smblib_masked_write(chg, CHGR_CFG2_REG,
				CHARGER_INHIBIT_BIT, 0);
	default:
		break;
	}

	if (rc < 0) {
		dev_err(chg->dev, "Couldn't configure charge inhibit threshold rc=%d\n",
			rc);
		return rc;
	}

	if (chip->dt.auto_recharge_soc) {
		rc = smblib_masked_write(chg, FG_UPDATE_CFG_2_SEL_REG,
				SOC_LT_CHG_RECHARGE_THRESH_SEL_BIT |
				VBT_LT_CHG_RECHARGE_THRESH_SEL_BIT,
				VBT_LT_CHG_RECHARGE_THRESH_SEL_BIT);
		if (rc < 0) {
			dev_err(chg->dev, "Couldn't configure FG_UPDATE_CFG2_SEL_REG rc=%d\n",
				rc);
			return rc;
		}
	} else {
		rc = smblib_masked_write(chg, FG_UPDATE_CFG_2_SEL_REG,
				SOC_LT_CHG_RECHARGE_THRESH_SEL_BIT |
				VBT_LT_CHG_RECHARGE_THRESH_SEL_BIT,
				SOC_LT_CHG_RECHARGE_THRESH_SEL_BIT);
		if (rc < 0) {
			dev_err(chg->dev, "Couldn't configure FG_UPDATE_CFG2_SEL_REG rc=%d\n",
				rc);
			return rc;
		}
	}

	if (chg->sw_jeita_enabled) {
		rc = smblib_disable_hw_jeita(chg, true);
		if (rc < 0) {
			dev_err(chg->dev, "Couldn't set hw jeita rc=%d\n", rc);
			return rc;
		}
	}

	if (chg->disable_stat_sw_override) {
		rc = smblib_masked_write(chg, STAT_CFG_REG,
				STAT_SW_OVERRIDE_CFG_BIT, 0);
		if (rc < 0) {
			dev_err(chg->dev, "Couldn't disable STAT SW override rc=%d\n",
				rc);
			return rc;
		}
	}

	return rc;
}

static int smb2_post_init(struct smb2 *chip)
{
	struct smb_charger *chg = &chip->chg;
	int rc;

	/* In case the usb path is suspended, we would have missed disabling
	 * the icl change interrupt because the interrupt could have been
	 * not requested
	 */
	rerun_election(chg->usb_icl_votable);

	/* configure power role for dual-role */
	rc = smblib_masked_write(chg, TYPE_C_INTRPT_ENB_SOFTWARE_CTRL_REG,
				 TYPEC_POWER_ROLE_CMD_MASK, 0);
	if (rc < 0) {
		dev_err(chg->dev,
			"Couldn't configure power role for DRP rc=%d\n", rc);
		return rc;
	}

	rerun_election(chg->usb_irq_enable_votable);

	return 0;
}

static int smb2_chg_config_init(struct smb2 *chip)
{
	struct smb_charger *chg = &chip->chg;
	struct pmic_revid_data *pmic_rev_id;
	struct device_node *revid_dev_node;

	revid_dev_node = of_parse_phandle(chip->chg.dev->of_node,
					  "qcom,pmic-revid", 0);
	if (!revid_dev_node) {
		pr_err("Missing qcom,pmic-revid property\n");
		return -EINVAL;
	}

	pmic_rev_id = get_revid_data(revid_dev_node);
	if (IS_ERR_OR_NULL(pmic_rev_id)) {
		/*
		 * the revid peripheral must be registered, any failure
		 * here only indicates that the rev-id module has not
		 * probed yet.
		 */
		return -EPROBE_DEFER;
	}

	switch (pmic_rev_id->pmic_subtype) {
	case PMI8998_SUBTYPE:
		chip->chg.smb_version = PMI8998_SUBTYPE;
		chip->chg.wa_flags |= BOOST_BACK_WA | QC_AUTH_INTERRUPT_WA_BIT
				| TYPEC_PBS_WA_BIT;
		if (pmic_rev_id->rev4 == PMI8998_V1P1_REV4) /* PMI rev 1.1 */
			chg->wa_flags |= QC_CHARGER_DETECTION_WA_BIT;
		if (pmic_rev_id->rev4 == PMI8998_V2P0_REV4) /* PMI rev 2.0 */
			chg->wa_flags |= TYPEC_CC2_REMOVAL_WA_BIT;
		chg->chg_freq.freq_5V		= 600;
		chg->chg_freq.freq_6V_8V	= 800;
		chg->chg_freq.freq_9V		= 1000;
		chg->chg_freq.freq_12V		= 1200;
		chg->chg_freq.freq_removal	= 1000;
		chg->chg_freq.freq_below_otg_threshold = 2000;
		chg->chg_freq.freq_above_otg_threshold = 800;
		break;
	case PM660_SUBTYPE:
		chip->chg.smb_version = PM660_SUBTYPE;
		chip->chg.wa_flags |= BOOST_BACK_WA | OTG_WA | OV_IRQ_WA_BIT
				| TYPEC_PBS_WA_BIT;
		chg->param.freq_buck = pm660_params.freq_buck;
		chg->param.freq_boost = pm660_params.freq_boost;
		chg->chg_freq.freq_5V		= 650;
		chg->chg_freq.freq_6V_8V	= 850;
		chg->chg_freq.freq_9V		= 1050;
		chg->chg_freq.freq_12V		= 1200;
		chg->chg_freq.freq_removal	= 1050;
		chg->chg_freq.freq_below_otg_threshold = 1600;
		chg->chg_freq.freq_above_otg_threshold = 800;
		break;
	default:
		pr_err("PMIC subtype %d not supported\n",
				pmic_rev_id->pmic_subtype);
		return -EINVAL;
	}

	return 0;
}

/****************************
 * DETERMINE INITIAL STATUS *
 ****************************/

static int smb2_determine_initial_status(struct smb2 *chip)
{
	struct smb_irq_data irq_data = {chip, "determine-initial-status"};
	struct smb_charger *chg = &chip->chg;

	if (chg->bms_psy)
		smblib_suspend_on_debug_battery(chg);
	smblib_handle_usb_plugin(0, &irq_data);
	smblib_handle_usb_typec_change(0, &irq_data);
	smblib_handle_usb_source_change(0, &irq_data);
	smblib_handle_chg_state_change(0, &irq_data);
	smblib_handle_icl_change(0, &irq_data);
	smblib_handle_batt_temp_changed(0, &irq_data);
	smblib_handle_wdog_bark(0, &irq_data);

	return 0;
}

/**************************
 * INTERRUPT REGISTRATION *
 **************************/

static struct smb_irq_info smb2_irqs[] = {
/* CHARGER IRQs */
	[CHG_ERROR_IRQ] = {
		.name		= "chg-error",
		.handler	= smblib_handle_debug,
	},
	[CHG_STATE_CHANGE_IRQ] = {
		.name		= "chg-state-change",
		.handler	= smblib_handle_chg_state_change,
		.wake		= true,
	},
	[STEP_CHG_STATE_CHANGE_IRQ] = {
		.name		= "step-chg-state-change",
		.handler	= NULL,
	},
	[STEP_CHG_SOC_UPDATE_FAIL_IRQ] = {
		.name		= "step-chg-soc-update-fail",
		.handler	= NULL,
	},
	[STEP_CHG_SOC_UPDATE_REQ_IRQ] = {
		.name		= "step-chg-soc-update-request",
		.handler	= NULL,
	},
/* OTG IRQs */
	[OTG_FAIL_IRQ] = {
		.name		= "otg-fail",
		.handler	= smblib_handle_debug,
	},
	[OTG_OVERCURRENT_IRQ] = {
		.name		= "otg-overcurrent",
		.handler	= smblib_handle_otg_overcurrent,
	},
	[OTG_OC_DIS_SW_STS_IRQ] = {
		.name		= "otg-oc-dis-sw-sts",
		.handler	= smblib_handle_debug,
	},
	[TESTMODE_CHANGE_DET_IRQ] = {
		.name		= "testmode-change-detect",
		.handler	= smblib_handle_debug,
	},
/* BATTERY IRQs */
	[BATT_TEMP_IRQ] = {
		.name		= "bat-temp",
		.handler	= smblib_handle_batt_temp_changed,
		.wake		= true,
	},
	[BATT_OCP_IRQ] = {
		.name		= "bat-ocp",
		.handler	= smblib_handle_batt_psy_changed,
	},
	[BATT_OV_IRQ] = {
		.name		= "bat-ov",
		.handler	= smblib_handle_batt_psy_changed,
	},
	[BATT_LOW_IRQ] = {
		.name		= "bat-low",
		.handler	= smblib_handle_batt_psy_changed,
	},
	[BATT_THERM_ID_MISS_IRQ] = {
		.name		= "bat-therm-or-id-missing",
		.handler	= smblib_handle_batt_psy_changed,
	},
	[BATT_TERM_MISS_IRQ] = {
		.name		= "bat-terminal-missing",
		.handler	= smblib_handle_batt_psy_changed,
	},
/* USB INPUT IRQs */
	[USBIN_COLLAPSE_IRQ] = {
		.name		= "usbin-collapse",
		.handler	= smblib_handle_debug,
	},
	[USBIN_LT_3P6V_IRQ] = {
		.name		= "usbin-lt-3p6v",
		.handler	= smblib_handle_debug,
	},
	[USBIN_UV_IRQ] = {
		.name		= "usbin-uv",
		.handler	= smblib_handle_usbin_uv,
	},
	[USBIN_OV_IRQ] = {
		.name		= "usbin-ov",
		.handler	= smblib_handle_debug,
	},
	[USBIN_PLUGIN_IRQ] = {
		.name		= "usbin-plugin",
		.handler	= smblib_handle_usb_plugin,
		.wake		= true,
	},
	[USBIN_SRC_CHANGE_IRQ] = {
		.name		= "usbin-src-change",
		.handler	= smblib_handle_usb_source_change,
		.wake		= true,
	},
	[USBIN_ICL_CHANGE_IRQ] = {
		.name		= "usbin-icl-change",
		.handler	= smblib_handle_icl_change,
		.wake		= true,
	},
	[TYPE_C_CHANGE_IRQ] = {
		.name		= "type-c-change",
		.handler	= smblib_handle_usb_typec_change,
		.wake		= true,
	},
/* DC INPUT IRQs */
	[DCIN_COLLAPSE_IRQ] = {
		.name		= "dcin-collapse",
		.handler	= smblib_handle_debug,
	},
	[DCIN_LT_3P6V_IRQ] = {
		.name		= "dcin-lt-3p6v",
		.handler	= smblib_handle_debug,
	},
	[DCIN_UV_IRQ] = {
		.name		= "dcin-uv",
		.handler	= smblib_handle_debug,
	},
	[DCIN_OV_IRQ] = {
		.name		= "dcin-ov",
		.handler	= smblib_handle_debug,
	},
	[DCIN_PLUGIN_IRQ] = {
		.name		= "dcin-plugin",
		.handler	= smblib_handle_dc_plugin,
		.wake		= true,
	},
	[DIV2_EN_DG_IRQ] = {
		.name		= "div2-en-dg",
		.handler	= smblib_handle_debug,
	},
	[DCIN_ICL_CHANGE_IRQ] = {
		.name		= "dcin-icl-change",
		.handler	= smblib_handle_debug,
	},
/* MISCELLANEOUS IRQs */
	[WDOG_SNARL_IRQ] = {
		.name		= "wdog-snarl",
		.handler	= NULL,
	},
	[WDOG_BARK_IRQ] = {
		.name		= "wdog-bark",
		.handler	= smblib_handle_wdog_bark,
		.wake		= true,
	},
	[AICL_FAIL_IRQ] = {
		.name		= "aicl-fail",
		.handler	= smblib_handle_debug,
	},
	[AICL_DONE_IRQ] = {
		.name		= "aicl-done",
		.handler	= smblib_handle_debug,
	},
	[HIGH_DUTY_CYCLE_IRQ] = {
		.name		= "high-duty-cycle",
		.handler	= smblib_handle_high_duty_cycle,
		.wake		= true,
	},
	[INPUT_CURRENT_LIMIT_IRQ] = {
		.name		= "input-current-limiting",
		.handler	= smblib_handle_debug,
	},
	[TEMPERATURE_CHANGE_IRQ] = {
		.name		= "temperature-change",
		.handler	= smblib_handle_debug,
	},
	[SWITCH_POWER_OK_IRQ] = {
		.name		= "switcher-power-ok",
		.handler	= smblib_handle_switcher_power_ok,
		.wake		= true,
		.storm_data	= {true, 1000, 8},
	},
};

static int smb2_get_irq_index_byname(const char *irq_name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(smb2_irqs); i++) {
		if (strcmp(smb2_irqs[i].name, irq_name) == 0)
			return i;
	}

	return -ENOENT;
}

static int smb2_request_interrupt(struct smb2 *chip,
				struct device_node *node, const char *irq_name)
{
	struct smb_charger *chg = &chip->chg;
	int rc, irq, irq_index;
	struct smb_irq_data *irq_data;

	irq = of_irq_get_byname(node, irq_name);
	if (irq < 0) {
		pr_err("Couldn't get irq %s byname\n", irq_name);
		return irq;
	}

	irq_index = smb2_get_irq_index_byname(irq_name);
	if (irq_index < 0) {
		pr_err("%s is not a defined irq\n", irq_name);
		return irq_index;
	}

	if (!smb2_irqs[irq_index].handler)
		return 0;

	irq_data = devm_kzalloc(chg->dev, sizeof(*irq_data), GFP_KERNEL);
	if (!irq_data)
		return -ENOMEM;

	irq_data->parent_data = chip;
	irq_data->name = irq_name;
	irq_data->storm_data = smb2_irqs[irq_index].storm_data;
	mutex_init(&irq_data->storm_data.storm_lock);

	rc = devm_request_threaded_irq(chg->dev, irq, NULL,
					smb2_irqs[irq_index].handler,
					IRQF_ONESHOT, irq_name, irq_data);
	if (rc < 0) {
		pr_err("Couldn't request irq %d\n", irq);
		return rc;
	}

	smb2_irqs[irq_index].irq = irq;
	smb2_irqs[irq_index].irq_data = irq_data;
	if (smb2_irqs[irq_index].wake)
		enable_irq_wake(irq);

	return rc;
}

static int smb2_request_interrupts(struct smb2 *chip)
{
	struct smb_charger *chg = &chip->chg;
	struct device_node *node = chg->dev->of_node;
	struct device_node *child;
	int rc = 0;
	const char *name;
	struct property *prop;

	for_each_available_child_of_node(node, child) {
		of_property_for_each_string(child, "interrupt-names",
					    prop, name) {
			rc = smb2_request_interrupt(chip, child, name);
			if (rc < 0)
				return rc;
		}
	}
	if (chg->irq_info[USBIN_ICL_CHANGE_IRQ].irq)
		chg->usb_icl_change_irq_enabled = true;

	return rc;
}

static void smb2_free_interrupts(struct smb_charger *chg)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(smb2_irqs); i++) {
		if (smb2_irqs[i].irq > 0) {
			if (smb2_irqs[i].wake)
				disable_irq_wake(smb2_irqs[i].irq);

			devm_free_irq(chg->dev, smb2_irqs[i].irq,
					smb2_irqs[i].irq_data);
		}
	}
}

static void smb2_disable_interrupts(struct smb_charger *chg)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(smb2_irqs); i++) {
		if (smb2_irqs[i].irq > 0)
			disable_irq(smb2_irqs[i].irq);
	}
}

#if defined(CONFIG_DEBUG_FS)

static int force_batt_psy_update_write(void *data, u64 val)
{
	struct smb_charger *chg = data;

	power_supply_changed(chg->batt_psy);
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(force_batt_psy_update_ops, NULL,
			force_batt_psy_update_write, "0x%02llx\n");

static int force_usb_psy_update_write(void *data, u64 val)
{
	struct smb_charger *chg = data;

	power_supply_changed(chg->usb_psy);
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(force_usb_psy_update_ops, NULL,
			force_usb_psy_update_write, "0x%02llx\n");

static int force_dc_psy_update_write(void *data, u64 val)
{
	struct smb_charger *chg = data;

	power_supply_changed(chg->dc_psy);
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(force_dc_psy_update_ops, NULL,
			force_dc_psy_update_write, "0x%02llx\n");

static void smb2_create_debugfs(struct smb2 *chip)
{
	struct dentry *file;

	chip->dfs_root = debugfs_create_dir("charger", NULL);
	if (IS_ERR_OR_NULL(chip->dfs_root)) {
		pr_err("Couldn't create charger debugfs rc=%ld\n",
			(long)chip->dfs_root);
		return;
	}

	file = debugfs_create_file("force_batt_psy_update", 0600,
			    chip->dfs_root, chip, &force_batt_psy_update_ops);
	if (IS_ERR_OR_NULL(file))
		pr_err("Couldn't create force_batt_psy_update file rc=%ld\n",
			(long)file);

	file = debugfs_create_file("force_usb_psy_update", 0600,
			    chip->dfs_root, chip, &force_usb_psy_update_ops);
	if (IS_ERR_OR_NULL(file))
		pr_err("Couldn't create force_usb_psy_update file rc=%ld\n",
			(long)file);

	file = debugfs_create_file("force_dc_psy_update", 0600,
			    chip->dfs_root, chip, &force_dc_psy_update_ops);
	if (IS_ERR_OR_NULL(file))
		pr_err("Couldn't create force_dc_psy_update file rc=%ld\n",
			(long)file);
}

#else

static void smb2_create_debugfs(struct smb2 *chip)
{}

#endif

// Add for PTC BACKUP FILE +++
#define PTC1000_BACKUP_FILE "/data/data/CheckPTCtool_FCC1_VBAT"
#define PTC500_BACKUP_FILE "/data/data/CheckPTCtool_FCC2_VBAT"
void backup_PTC_data(int input, char* path)
{
	struct file *fp = NULL;
	mm_segment_t old_fs;
	loff_t pos_lsts = 0;
	char buf[11] = "";

	sprintf(buf, "%d", input);

	fp = filp_open(path, O_RDWR | O_CREAT , 0666);
	if (IS_ERR_OR_NULL(fp)) {
		CHG_DBG_E("%s: open (%s) fail\n", __func__, path);
		return;
	}

	/*For purpose that can use read/write system call*/
	old_fs = get_fs();
	set_fs(KERNEL_DS);

	vfs_write(fp,buf,9,&pos_lsts);

	set_fs(old_fs);
	filp_close(fp, NULL);

	CHG_DBG("%s : %s\n", __func__, buf);
}

// ASUS BSP charger : Add attributes +++
static ssize_t boot_completed_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int tmp = 0;
	int rc;
	bool btm_pca_vid;
	bool side_pca_vid;
	union power_supply_propval prop = {0, };

	tmp = buf[0] - 48;

	if (tmp == 0) {
		boot_completed_flag = false;
		CHG_DBG("%s: boot_completed_flag = 0\n", __func__);
	} else if (tmp == 1) {
		boot_completed_flag = true;
		CHG_DBG("%s: boot_completed_flag = 1, check usb alert\n", __func__);
		if (g_interrupt_enable) {
			schedule_delayed_work(&smbchg_dev->asus_thermal_btm_work, 0);
			schedule_delayed_work(&smbchg_dev->asus_thermal_side_work, msecs_to_jiffies(100));
			//schedule_delayed_work(&smbchg_dev->asus_water_proof_btm_work, msecs_to_jiffies(200));
			//schedule_delayed_work(&smbchg_dev->asus_water_proof_side_work, msecs_to_jiffies(300));
		}

		schedule_delayed_work(&g_fgChip->asus_battery_version_work, 0);

		side_pca_vid = PE_check_asus_vid();
		btm_pca_vid = rt_chg_check_asus_vid();
		if (side_pca_vid && btm_pca_vid)
			NXP_FLAG = NXP_BOTH;
		else if (side_pca_vid && !gpio_get_value_cansleep(global_gpio->POGO_OVP_ACOK))
			NXP_FLAG = NXP_SIDE;
		else if (btm_pca_vid && !gpio_get_value_cansleep(global_gpio->BTM_OVP_ACOK))
			NXP_FLAG = NXP_BTM;
		else
			NXP_FLAG = NXP_NONE;

		rc = power_supply_get_property(smbchg_dev->batt_psy, POWER_SUPPLY_PROP_STATUS, &prop);
		if (rc < 0)
			CHG_DBG_E("%s: Error in getting charging status, rc=%d\n", __func__, rc);

		if (ASUS_POGO_ID == STATION)
			fg_station_attach_notifier(true);
	}

	return len;
}

static ssize_t boot_completed_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", boot_completed_flag);
}

static ssize_t usb_thermal_btm_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int32_t temp;

	if ((smbchg_dev->vadc_dev_temp == NULL) || (smbchg_dev->adc_tm_dev_temp == NULL))
		return sprintf(buf, "ADC FAIL\n");

	temp = asus_get_temp_btm_vadc_voltage();
	CHG_DBG("%s: usb_thermal_btm reg : %d\n", __func__, temp);

	if (temp > 444000)
		return sprintf(buf, "PASS\n");
	else
		return sprintf(buf, "FAIL\n");
}

static ssize_t usb_thermal_side_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int rc;
	u8 reg;

	rc = asp1690e_read_reg(0x48, &reg);
	CHG_DBG("%s: usb_thermal_side reg : 0x%x\n", __func__, reg);

	if (reg > 0x31)
		return sprintf(buf, "PASS\n");
	else
		return sprintf(buf, "FAIL\n");
}

static ssize_t thermal_gpio_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int status = 0;

	status = gpio_get_value(global_gpio->POGO_THML_INT);
	return sprintf(buf, "%d\n", status);
}

static ssize_t asus_usb_suspend_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int tmp = 0;
	int rc;

	tmp = buf[0] - 48;

	if (tmp == 0) {
		CHG_DBG("%s: Set EnableCharging\n", __func__);
		asus_suspend_cmd_flag = 0;
		rc = smblib_set_usb_suspend(smbchg_dev, 0);
		pmic_set_pca9468_charging(true);
	} else if (tmp == 1) {
		CHG_DBG("%s: Set DisableCharging\n", __func__);
		asus_suspend_cmd_flag = 1;
		rc = smblib_set_usb_suspend(smbchg_dev, 1);
		pmic_set_pca9468_charging(false);
	}

	return len;
}

static ssize_t asus_usb_suspend_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	u8 reg;
	int ret;
	int suspend;

	ret = smblib_read(smbchg_dev, USBIN_CMD_IL_REG, &reg);
	suspend = reg & USBIN_SUSPEND_BIT;

	return sprintf(buf, "%d\n", suspend);
}

static ssize_t charger_limit_enable_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int tmp = 0;
	int rc;

	tmp = buf[0] - 48;

	if (tmp == 0) {
		charger_limit_enable_flag = 0;
		rc = smblib_masked_write(smbchg_dev, CHARGING_ENABLE_CMD_REG, CHARGING_ENABLE_CMD_BIT, 0);
		pmic_set_pca9468_charging(true);
	} else if (tmp == 1) {
		charger_limit_enable_flag = 1;
		if (asus_get_prop_batt_capacity(smbchg_dev) >= charger_limit_value) {
			rc = smblib_masked_write(smbchg_dev, CHARGING_ENABLE_CMD_REG, CHARGING_ENABLE_CMD_BIT, 1);
			pmic_set_pca9468_charging(false);
		} else {
			rc = smblib_masked_write(smbchg_dev, CHARGING_ENABLE_CMD_REG, CHARGING_ENABLE_CMD_BIT, 0);
			pmic_set_pca9468_charging(true);
		}
	}
	CHG_DBG("%s: charger_limit_enable = %d", __func__, charger_limit_enable_flag);

	return len;
}

static ssize_t charger_limit_enable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", charger_limit_enable_flag);
}

static ssize_t charger_limit_percent_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int tmp;

	tmp = simple_strtol(buf, NULL, 10);
	charger_limit_value = tmp;
	CHG_DBG("%s: charger_limit_percent set to = %d", __func__, charger_limit_value);

	return len;
}

static ssize_t charger_limit_percent_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", charger_limit_value);
}

static ssize_t CHG_TYPE_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	if (asus_adapter_detecting_flag) {
		return sprintf(buf, "OTHERS\n");
	} else {
		if (asus_CHG_TYPE == 750)
			return sprintf(buf, "DCP_ASUS_750K_2A\n");
		else if (asus_CHG_TYPE == 200)
			return sprintf(buf, "HVDCP_ASUS_200K_2A\n");
		else
			return sprintf(buf, "OTHERS\n");
	}
}

static ssize_t TypeC_Side_Detect2_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int typec_side_detect, open, cc_pin;
	u8 reg;
	int ret = -1;

	ret = smblib_read(smbchg_dev, TYPE_C_STATUS_4_REG, &reg);
	open = reg & CC_ATTACHED_BIT;

	ret = smblib_read(smbchg_dev, TYPE_C_STATUS_4_REG, &reg);
	cc_pin = reg & CC_ORIENTATION_BIT;

	if (open == 0)
		typec_side_detect = 0;
	else if (cc_pin == 0)
		typec_side_detect = 1;
	else
		typec_side_detect = 2;

	return sprintf(buf, "%d\n", typec_side_detect);
}

static ssize_t disable_input_suspend_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int tmp = 0;
	int rc;

	tmp = buf[0] - 48;

	if (tmp == 0) {
		CHG_DBG("%s: Thermal Test over, can suspend input\n", __func__);
		no_input_suspend_flag = 0;
		rc = smblib_write(smbchg_dev, JEITA_EN_CFG_REG, 0x10);		
		if (rc < 0)
			CHG_DBG_E("%s: Failed to set JEITA_EN_CFG_REG\n", __func__);
	} else if (tmp == 1) {
		CHG_DBG("%s: Thermal Test, can not suspend input\n", __func__);
		no_input_suspend_flag = 1;
		rc = smblib_write(smbchg_dev, JEITA_EN_CFG_REG, 0x00);
		if (rc < 0)
			CHG_DBG_E("%s: Failed to set JEITA_EN_CFG_REG\n", __func__);
	}

	rc = smblib_set_usb_suspend(smbchg_dev, 0);

	return len;
}

static ssize_t disable_input_suspend_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", no_input_suspend_flag);
}

static ssize_t smartchg_stop_charging_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int tmp = 0;
	int rc;

	tmp = buf[0] - 48;

	if (tmp == 0) {
		CHG_DBG("%s: Smart charge enable charging\n", __func__);
		smartchg_stop_flag = 0;
		rc = smblib_masked_write(smbchg_dev, CHARGING_ENABLE_CMD_REG, CHARGING_ENABLE_CMD_BIT, 0);
		if (rc < 0) {
			printk("[BAT][CHG] Couldn't write charging_enable rc = %d\n", rc);
			return rc;
		}
	} else if (tmp == 1) {
		CHG_DBG("%s: Smart charge stop charging\n", __func__);
		smartchg_stop_flag = 1;
		rc = smblib_masked_write(smbchg_dev, CHARGING_ENABLE_CMD_REG, CHARGING_ENABLE_CMD_BIT, CHARGING_ENABLE_CMD_BIT);
		if (rc < 0) {
			printk("[BAT][CHG] Couldn't write charging_enable rc = %d\n", rc);
			return rc;
		}
	}

	return len;
}

static ssize_t smartchg_stop_charging_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", smartchg_stop_flag);
}

static ssize_t demo_app_property_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int tmp = 0;

	tmp = buf[0] - 48;

	if (tmp == 0) {
		demo_app_property_flag = false;
		CHG_DBG("%s: demo_app_property_flag = 0\n", __func__);
	} else if (tmp == 1) {
		demo_app_property_flag = true;
		demo_recharge_delta = 2;
		CHG_DBG("%s: demo_app_property_flag = 1\n", __func__);
    }

	return len;
}

static ssize_t demo_app_property_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", demo_app_property_flag);
}

static ssize_t cn_demo_app_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int tmp = 0;

	tmp = buf[0] - 48;

	if (tmp == 0) {
		cn_demo_app_flag = false;
		CHG_DBG("%s: cn_demo_app_flag = 0\n", __func__);
	} else if (tmp == 1) {
		cn_demo_app_flag = true;
		demo_recharge_delta = 5;
		CHG_DBG("%s: cn_demo_app_flag = 1\n", __func__);
    }

	return len;
}

static ssize_t cn_demo_app_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", cn_demo_app_flag);
}

static ssize_t INOV_enable_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int tmp = 0;
	int rc;

	tmp = buf[0] - 48;

	if (tmp == 0) {
		rc = smblib_write(smbchg_dev, THERMREG_SRC_CFG_REG, 0x00);
		if (rc < 0)
			dev_err(smbchg_dev->dev, "Couldn't set THERMREG_SRC_CFG_REG 0x0\n");
		CHG_DBG("%s: Disable INOV function\n", __func__);
	} else if (tmp == 1) {
		rc = smblib_write(smbchg_dev, THERMREG_SRC_CFG_REG, 0x07);
		if (rc < 0)
			dev_err(smbchg_dev->dev, "Couldn't set THERMREG_SRC_CFG_REG 0x7\n");
		CHG_DBG("%s: Enable INOV function\n", __func__);
    }

	return len;
}

static ssize_t INOV_enable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int rc;
	u8 reg;

    rc = smblib_read(smbchg_dev, THERMREG_SRC_CFG_REG, &reg);

	return sprintf(buf, "INOV_reg 0x1670 = 0x%x\n", reg);
}

static ssize_t water_adc_value_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int32_t temp;

	if ((smbchg_dev->vadc_dev == NULL) || (smbchg_dev->adc_tm_dev == NULL))
		return sprintf(buf, "%d\n", -1);

	temp = asus_get_wp_btm_vadc_voltage();

	if (temp == -1)
		CHG_DBG("%s: Read Water adc value fail\n", __func__);
	else
		CHG_DBG("%s: Water adc value = %d(uV)\n", __func__, temp);

	return sprintf(buf, "%d\n", temp);
}

static ssize_t water_side_adc_value_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int32_t temp;

	if ((smbchg_dev->vadc_dev == NULL) || (smbchg_dev->adc_tm_dev == NULL))
		return sprintf(buf, "%d\n", -1);

	temp = asus_get_wp_side_vadc_voltage();

	if (temp == -1)
		CHG_DBG("%s: Read Water side adc value fail\n", __func__);
	else
		CHG_DBG("%s: Water side adc value = %d(uV)\n", __func__, temp);

	return sprintf(buf, "%d\n", temp);
}

static ssize_t get_usb_mux_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	bool usb2_mux1_stats, usb2_mux2_stats, usb1_mux_stats, pmi_mux_stats;
	bool btm_otg_stats, pogo_otg_stats;
	usb2_mux1_stats = gpio_get_value_cansleep(global_gpio->USB2_MUX1_EN);
	usb2_mux2_stats = gpio_get_value_cansleep(global_gpio->USB2_MUX2_EN);
	usb1_mux_stats = gpio_get_value_cansleep(global_gpio->USB1_MUX_EN);
	pmi_mux_stats = gpio_get_value_cansleep(global_gpio->PMI_MUX_EN);

	btm_otg_stats = gpio_get_value_cansleep(global_gpio->BTM_OTG_EN);
	pogo_otg_stats = gpio_get_value_cansleep(global_gpio->POGO_OTG_EN);

	if (g_ASUS_hwID < ZS600KL_ER1)
		return sprintf(buf, "usb2_mux1_stats : %d\nusb2_mux2_stats : %d\nusb1_mux_stats : %d\nbtm_otg_stats : %d\npogo_otg_stats : %d\n",
					usb2_mux1_stats, usb2_mux2_stats, usb1_mux_stats, btm_otg_stats, pogo_otg_stats);
	else
		return sprintf(buf, "usb2_mux1_stats : %d\npmi_mux_stats : %d\nbtm_otg_stats : %d\npogo_otg_stats : %d\n",
					usb2_mux1_stats, pmi_mux_stats, btm_otg_stats, pogo_otg_stats);

	return 0;

}

//[+++]Open an interface to change the specified usb_mux value
static ssize_t set_usb_mux_store(struct device *dev, struct device_attribute *attr, const char *data, size_t count)
{
	int rc;
	int usb2_mux1_en = 0, pmi_mux_en = 0;
	bool usb2_mux1_stats, pmi_mux_stats;
	
	sscanf(data, "%x %x", &usb2_mux1_en, &pmi_mux_en);
	CHG_DBG("[USB_MUX][Set]%s. usb2_mux1_en %d, pmi_mux_en %d\n", __func__, usb2_mux1_en, pmi_mux_en);
	
	rc = gpio_direction_output(global_gpio->USB2_MUX1_EN, usb2_mux1_en);
	if (rc)
		CHG_DBG_E("%s: failed to control USB2_MUX1_EN\n", __func__);
	rc = gpio_direction_output(global_gpio->PMI_MUX_EN, pmi_mux_en);
	if (rc)
		CHG_DBG_E("%s: failed to control PMI_MUX_EN\n", __func__);
	msleep(100);
	usb2_mux1_stats = gpio_get_value_cansleep(global_gpio->USB2_MUX1_EN);
	pmi_mux_stats = gpio_get_value_cansleep(global_gpio->PMI_MUX_EN);
	CHG_DBG("[USB_MUX][Result]%s. usb2_mux1_stats %d, pmi_mux_stats %d\n", __func__, usb2_mux1_stats, pmi_mux_stats);

	if (g_force_usb_mux == 2 && usb2_mux1_en == 0 && pmi_mux_en == 1) {
		msleep(50);
		extcon_set_cable_state_(smbchg_dev->extcon, EXTCON_USB, true);
		CHG_DBG("[USB_MUX]Set EXTCON_USB = true in force usb_mux\n");
	} else {
		extcon_set_cable_state_(smbchg_dev->extcon, EXTCON_USB, false);
		CHG_DBG("[USB_MUX]Set EXTCON_USB = false in force usb_mux\n");
	}
	
	return count;
}

static ssize_t force_usb_mux_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int tmp, rc;
	int usb2_mux1_en = 0, pmi_mux_en = 0;
	
	tmp = simple_strtol(buf, NULL, 10);
	CHG_DBG("%s: Set Force_USB_MUX : %d\n", __func__, tmp);
	g_force_usb_mux = tmp;
	if (g_force_usb_mux == 2) {
		usb2_mux1_en = 0;
		pmi_mux_en = 1;
		CHG_DBG("g_force_usb_mux is 2, force to use the BTM debug\n");
		rc = gpio_direction_output(global_gpio->USB2_MUX1_EN, usb2_mux1_en);
		if (rc)
			CHG_DBG_E("%s: failed to control USB2_MUX1_EN\n", __func__);

		rc = gpio_direction_output(global_gpio->PMI_MUX_EN, pmi_mux_en);
		if (rc)
			CHG_DBG_E("%s: failed to control PMI_MUX_EN\n", __func__);
	}
	return count;
}

static ssize_t force_usb_mux_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "g_force_usb_mux : %d\n", g_force_usb_mux);
}
//[---]Open an interface to change the specified usb_mux value

static ssize_t set_pogo_id_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int tmp;

	tmp = simple_strtol(buf, NULL, 10);
	ASUS_POGO_ID = tmp;
	CHG_DBG("%s: ASUS_POGO_ID set to = %d(%s)\n", __func__, ASUS_POGO_ID, pogo_id_str[ASUS_POGO_ID]);

	schedule_delayed_work(&smbchg_dev->asus_mux_setting_1_work, msecs_to_jiffies(ASP1690_MUX_WAIT_TIME));

	return len;
}

static ssize_t set_pogo_id_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d(%s)\n", ASUS_POGO_ID, pogo_id_str[ASUS_POGO_ID]);
}

static ssize_t ptc_check_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int temp;
	int volt_1A = 0;
	int volt_0P5A = 0;
	int cnt;
	int rc;

	if (!g_ftm_mode)
		return sprintf(buf, "Not Factory build! Not support!\n");

	__pm_stay_awake(&asus_PTC_lock_ws);

	ptc_check_flag = 1;
	charger_limit_enable_flag = 0;
	rc = smblib_masked_write(smbchg_dev, CHARGING_ENABLE_CMD_REG, CHARGING_ENABLE_CMD_BIT, 0);
	rc = smblib_write(smbchg_dev, FAST_CHARGE_CURRENT_CFG_REG, 0x28);
	if (rc < 0) {
		dev_err(smbchg_dev->dev, "Couldn't set default FAST_CHARGE_CURRENT_CFG_REG rc=%d\n", rc);
	}

	for (cnt = 0; cnt < 5; cnt++) {
		msleep(5000);
		temp = asus_get_prop_batt_volt(smbchg_dev);
		volt_1A = volt_1A + temp/5;
	}

	rc = smblib_write(smbchg_dev, FAST_CHARGE_CURRENT_CFG_REG, 0x14);
	if (rc < 0) {
		dev_err(smbchg_dev->dev, "Couldn't set default FAST_CHARGE_CURRENT_CFG_REG rc=%d\n", rc);
	}
	for (cnt = 0; cnt < 5; cnt++) {
		msleep(5000);
		temp = asus_get_prop_batt_volt(smbchg_dev);
		volt_0P5A = volt_0P5A + temp/5;
	}

	backup_PTC_data(volt_1A, PTC1000_BACKUP_FILE);
	backup_PTC_data(volt_0P5A, PTC500_BACKUP_FILE);

	CHG_DBG("%s: volt_1A = %d, volt_0P5A = %d\n", __func__, volt_1A, volt_0P5A);
	ptc_check_flag = 0;
	charger_limit_enable_flag = 1;

	__pm_relax(&asus_PTC_lock_ws);

	if ((volt_1A - volt_0P5A) <= 200000 && (volt_1A - volt_0P5A) > 0)
		return sprintf(buf, "PASS\n");
	else
		return sprintf(buf, "FAIL\n");
}

static ssize_t vbus_rising_pos_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	bool vbus_bottom;
	bool vbus_side;

	vbus_side = !gpio_get_value_cansleep(global_gpio->POGO_OVP_ACOK);
	vbus_bottom = !gpio_get_value_cansleep(global_gpio->BTM_OVP_ACOK);

	if (vbus_side)
		return sprintf(buf, "2\n");
	else if (vbus_bottom)
		return sprintf(buf, "1\n");
	else
		return sprintf(buf, "0\n");
}

static ssize_t ultra_bat_life_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int tmp = 0;
	int rc;

	tmp = buf[0] - 48;

	if (tmp == 0) {
		ultra_bat_life_flag = false;
		CHG_DBG("%s: boot_completed_flag = 0, cancel CHG_Limit\n", __func__);
	} else if (tmp == 1) {
		ultra_bat_life_flag = true;
		write_CHGLimit_value(0);
		CHG_DBG("%s: ultra_bat_life_flag = 1, CHG_Limit = 60\n", __func__);
		if (asus_get_prop_batt_capacity(smbchg_dev) > 60 && asus_get_prop_usb_present(smbchg_dev)) {
			rc = smblib_set_usb_suspend(smbchg_dev, true);
			CHG_DBG("%s: Capcity > 60, input suspend\n", __func__);
		}
	}

	return len;
}

static ssize_t ultra_bat_life_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", ultra_bat_life_flag);
}

static ssize_t ultra_cos_spec_time_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int tmp;

	tmp = simple_strtol(buf, NULL, 10);
	g_ultra_cos_spec_time = tmp;
	CHG_DBG("%s: ultra_cos_spec_time set to = %d\n", __func__, g_ultra_cos_spec_time);

	return len;
}

static ssize_t ultra_cos_spec_time_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", g_ultra_cos_spec_time);
}

static ssize_t pca_enable_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int tmp = 0;
	union power_supply_propval pval = {0, };

	if (smbchg_dev->pca_psy == NULL) {
		CHG_DBG("%s: pca_psy not ready, return\n", __func__);
		return len;
	}
		
	tmp = buf[0] - 48;

	if (tmp == 0) {
		pval.intval = false;
		power_supply_set_property(smbchg_dev->pca_psy, POWER_SUPPLY_PROP_CHARGING_ENABLED, &pval);
		CHG_DBG("%s: set pca_enable false\n", __func__);
	} else if (tmp == 1) {
		pval.intval = true;
		power_supply_set_property(smbchg_dev->pca_psy, POWER_SUPPLY_PROP_CHARGING_ENABLED, &pval);		
		CHG_DBG("%s: set pca_enable true\n", __func__);
	}

	return len;
}

static ssize_t switcher_power_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int tmp = 0;

	tmp = buf[0] - 48;

	if (tmp == 0) {
		switcher_power_disable_flag = false;
		CHG_DBG("%s: set switcher_power_disable_flag = 0\n", __func__);
	} else if (tmp == 1) {
		switcher_power_disable_flag = true;
		CHG_DBG("%s: set switcher_power_disable_flag = 1\n", __func__);
	}

	return len;
}

static ssize_t switcher_power_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", switcher_power_disable_flag);
}

static DEVICE_ATTR(boot_completed, 0664, boot_completed_show, boot_completed_store);
static DEVICE_ATTR(usb_thermal_btm, 0664, usb_thermal_btm_show, NULL);
static DEVICE_ATTR(usb_thermal_side, 0664, usb_thermal_side_show, NULL);
static DEVICE_ATTR(thermal_gpio, 0664, thermal_gpio_show, NULL);
static DEVICE_ATTR(asus_usb_suspend, 0664, asus_usb_suspend_show, asus_usb_suspend_store);
static DEVICE_ATTR(charger_limit_enable, 0664, charger_limit_enable_show, charger_limit_enable_store);
static DEVICE_ATTR(charger_limit_percent, 0664, charger_limit_percent_show, charger_limit_percent_store);
static DEVICE_ATTR(CHG_TYPE, 0664, CHG_TYPE_show, NULL);
static DEVICE_ATTR(TypeC_Side_Detect2, 0664, TypeC_Side_Detect2_show, NULL);
static DEVICE_ATTR(disable_input_suspend, 0664, disable_input_suspend_show, disable_input_suspend_store);
static DEVICE_ATTR(smartchg_stop_charging, 0664, smartchg_stop_charging_show, smartchg_stop_charging_store);
static DEVICE_ATTR(demo_app_property, 0664, demo_app_property_show, demo_app_property_store);
static DEVICE_ATTR(cn_demo_app, 0664, cn_demo_app_show, cn_demo_app_store);
static DEVICE_ATTR(INOV_enable, 0664, INOV_enable_show, INOV_enable_store);
static DEVICE_ATTR(water_adc_value, 0664, water_adc_value_show, NULL);
static DEVICE_ATTR(water_side_adc_value, 0664, water_side_adc_value_show, NULL);
static DEVICE_ATTR(get_usb_mux, 0664, get_usb_mux_show, NULL);
static DEVICE_ATTR(set_usb_mux, 0664, NULL, set_usb_mux_store);
static DEVICE_ATTR(force_usb_mux, 0664, force_usb_mux_show, force_usb_mux_store);
static DEVICE_ATTR(set_pogo_id, 0664, set_pogo_id_show, set_pogo_id_store);
static DEVICE_ATTR(ptc_check, 0664, ptc_check_show, NULL);
static DEVICE_ATTR(vbus_rising_pos, 0664, vbus_rising_pos_show, NULL);
static DEVICE_ATTR(ultra_bat_life, 0664, ultra_bat_life_show, ultra_bat_life_store);
static DEVICE_ATTR(ultra_cos_spec_time, 0664, ultra_cos_spec_time_show, ultra_cos_spec_time_store);
static DEVICE_ATTR(pca_enable, 0664, NULL, pca_enable_store);
static DEVICE_ATTR(switcher_power_disable, 0664, switcher_power_show, switcher_power_store);

static struct attribute *asus_smblib_attrs[] = {
	&dev_attr_boot_completed.attr,
	&dev_attr_usb_thermal_btm.attr,
	&dev_attr_usb_thermal_side.attr,
	&dev_attr_thermal_gpio.attr,
	&dev_attr_asus_usb_suspend.attr,
	&dev_attr_CHG_TYPE.attr,
	&dev_attr_TypeC_Side_Detect2.attr,
	&dev_attr_charger_limit_enable.attr,
	&dev_attr_charger_limit_percent.attr,
	&dev_attr_disable_input_suspend.attr,
	&dev_attr_smartchg_stop_charging.attr,
	&dev_attr_demo_app_property.attr,
	&dev_attr_cn_demo_app.attr,
	&dev_attr_INOV_enable.attr,
	&dev_attr_water_adc_value.attr,
	&dev_attr_water_side_adc_value.attr,
	&dev_attr_get_usb_mux.attr,
	&dev_attr_set_usb_mux.attr,
	&dev_attr_force_usb_mux.attr,
	&dev_attr_set_pogo_id.attr,
	&dev_attr_ptc_check.attr,
	&dev_attr_vbus_rising_pos.attr,
	&dev_attr_ultra_bat_life.attr,
	&dev_attr_ultra_cos_spec_time.attr,
	&dev_attr_pca_enable.attr,
	&dev_attr_switcher_power_disable.attr,
	NULL
};

static const struct attribute_group asus_smblib_attr_group = {
	.attrs = asus_smblib_attrs,
};
// ASUS BSP charger : Add attributes ---

// ASUS BSP charger : BMMI Adb Interface +++
#define chargerIC_status_PROC_FILE	"driver/chargerIC_status"
static struct proc_dir_entry *chargerIC_status_proc_file;
static int chargerIC_status_proc_read(struct seq_file *buf, void *v)
{
	int ret = -1;
    u8 reg;
    ret = smblib_read(smbchg_dev, SHDN_CMD_REG, &reg);
    if (ret) {
		ret = 0;
    } else {
    	ret = 1;
    }
	seq_printf(buf, "%d\n", ret);
	return 0;
}

static int chargerIC_status_proc_open(struct inode *inode, struct  file *file)
{
	return single_open(file, chargerIC_status_proc_read, NULL);
}

static const struct file_operations chargerIC_status_fops = {
	.owner = THIS_MODULE,
    .open = chargerIC_status_proc_open,
    .read = seq_read,
    .release = single_release,
};

void static create_chargerIC_status_proc_file(void)
{
	chargerIC_status_proc_file = proc_create(chargerIC_status_PROC_FILE, 0644, NULL, &chargerIC_status_fops);

    if (chargerIC_status_proc_file) {
		CHG_DBG("%s: sucessed!\n", __func__);
    } else {
	    CHG_DBG("%s: failed!\n", __func__);
    }
}
// ASUS BSP charger : BMMI Adb Interface ---

// ASUS BSP charger : Water Enable Interface +++
#define water_en_PROC_FILE	"driver/water_proof_enable"
static struct proc_dir_entry *water_en_proc_file;
static int water_en_proc_read(struct seq_file *buf, void *v)
{
	seq_printf(buf, "%d\n", g_water_enable);
	return 0;
}

static ssize_t water_en_proc_write(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
	int val;
	char messages[8]="";

	len =(len > 8 ?8:len);
	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}
	val = (int)simple_strtol(messages, NULL, 10);

	if (val == 1) {		
		CHG_DBG("%s: Enable Water Proof\n", __func__);
		g_water_enable = 1;
	} else {
		CHG_DBG("%s: Disable Water Proof\n", __func__);
		g_water_enable = 0;
	}

	return len;
}

static int water_en_proc_open(struct inode *inode, struct  file *file)
{
	return single_open(file, water_en_proc_read, NULL);
}

static const struct file_operations water_en_fops = {
	.owner = THIS_MODULE,
    .open = water_en_proc_open,
    .read = seq_read,
	.write = water_en_proc_write,
    .release = single_release,
};

void static create_water_en_proc_file(void)
{
	water_en_proc_file = proc_create(water_en_PROC_FILE, 0644, NULL, &water_en_fops);

    if (water_en_proc_file) {
		CHG_DBG("%s: sucessed!\n", __func__);
    } else {
	    CHG_DBG("%s: failed!\n", __func__);
    }
}
// ASUS BSP charger : Water Enable Interface ---

extern bool is_hall_sensor_detect; //LiJen implement power bank and balance mode
//[+++]Add the interrupt handler for pogo thermal detect
void asus_pogo_thermal_work(struct work_struct *work) {
	u8 hid_interrupt_result = 0;
	u8 event = 0;
	int rc;
	bool trigger_level, vbus_status, otg_status;

	trigger_level = gpio_get_value_cansleep(global_gpio->POGO_THML_INT);
	CHG_DBG("%s. POGO devie : %s, trigger_level : %d\n", __func__, pogo_id_str[ASUS_POGO_ID], trigger_level);
	switch (ASUS_POGO_ID) {
	case STATION:
		rc = hid_to_check_interrupt(&hid_interrupt_result, &event);
		CHG_DBG("%s. hid_interrupt_result : %d, event : %d\n", __func__, hid_interrupt_result, event);
		if (rc < 0) {
			CHG_DBG("failed to get hid_to_check_interrupt!\n");
			return;
		} else {
			CHG_DBG("g_ST_SDP_mode : %d\n", g_ST_SDP_mode);
			if ((hid_interrupt_result & BIT(0)) && g_ST_SDP_mode == 1) {
				CHG_DBG("%s. set EXTCON_USB true\n", __func__);
				extcon_set_cable_state_(smbchg_dev->extcon, EXTCON_USB, true);
			}
			//[+++]This is station USB thermal alert case
			if (hid_interrupt_result & BIT(1)) {
				if (g_Charger_mode && (event == 2 || event == 1))
					cos_alert_once_flag = 1;
				if (event == 2) {
					smblib_set_usb_suspend(smbchg_dev, true);
					g_temp_station_state = 2;
					usb_alert_flag_ACCY = 1;
					CHG_DBG("%s. Notify the Station Discharging\n", __func__);
					asus_extcon_set_cable_state_(g_fgChip->st_bat_stat_extcon, 3);
				} else if (event == 1) {
					g_temp_station_state = 1;
					usb_alert_flag_ACCY = 1;
				} else if (event == 0) {
					usb_alert_flag_ACCY = 0;
					if (!gpio_get_value(global_gpio->POGO_OVP_ACOK) && (g_temp_station_state == 2))
						usb_alert_keep_suspend_flag_ACCY = 1;
					g_temp_station_state = 0;
				}
				if (g_temp_side_state == 2 || g_temp_btm_state == 2 || g_temp_station_state == 2 || g_temp_INBOX_DT_state == 2)
					g_temp_state = 2;
				else if (g_temp_side_state == 1 || g_temp_btm_state == 1 || g_temp_station_state == 1 || g_temp_INBOX_DT_state == 1)
					g_temp_state = 1;
				else
					g_temp_state = 0;
				asus_extcon_set_cable_state_(smbchg_dev->thermal_extcon, g_temp_state);

				CHG_DBG("%s  g_temp_station_state = %d, report_state = %d\n", __func__, g_temp_station_state, g_temp_state);
			}
			//[---]This is station USB thermal alert case

			//[+++] LiJen implement power bank and balance mode
			if ((hid_interrupt_result & BIT(2)) && is_hall_sensor_detect == true) {
				union power_supply_propval pval = {POWER_SUPPLY_TYPEC_PR_DUAL, };

				//set phone CC = DRP
				power_supply_set_property(smbchg_dev->usb_psy, POWER_SUPPLY_PROP_TYPEC_POWER_ROLE , &pval); 
				CHG_DBG("%s AC plugin, set phone CC to DRP",__func__);
			}
			//[---] LiJen implement power bank and balance mode

			/* For Station get 0% +++ */
			if (hid_interrupt_result & BIT(6)) {
				union power_supply_propval pval = {POWER_SUPPLY_TYPEC_PR_SINK, };
				station_cap_zero_flag = 1;
				fg_station_attach_notifier(false);	//ASUS BSP notifier fg driver +++
				//set phone CC = UFP
				power_supply_set_property(smbchg_dev->usb_psy, POWER_SUPPLY_PROP_TYPEC_POWER_ROLE , &pval);
				CHG_DBG_E("%s AC plugin, set phone CC to UFP",__func__);
			}
			/* For Station get 0% --- */
		}
		break;
	case INBOX:
	case DT:
		vbus_status = gpio_get_value(global_gpio->POGO_OVP_ACOK);
		otg_status = gpio_get_value(global_gpio->POGO_OTG_EN);
		//[+++]If GPIO44 is low, it could trigger a fake thermal interrupt
		if (ASUS_POGO_ID == DT && !gpio_get_value(44)) {
			CHG_DBG("GPIO 44 is low. Don't handle this thermal alert for DT ACC");
			return;
		}
		//[---]If GPIO44 is low, it could trigger a fake thermal interrupt
		if (g_Charger_mode && !trigger_level)
			cos_alert_once_flag = 1;
		if (!vbus_status && !trigger_level) {
			smblib_set_usb_suspend(smbchg_dev, true);
			pmic_set_pca9468_charging(false);
			//[+++]For OTG disable
			rc = smblib_write(smbchg_dev, CMD_OTG_REG, 0);
			if (rc < 0)
				CHG_DBG_E("%s: Failed to disable pmi_otg_en\n", __func__);
			rc = gpio_direction_output(global_gpio->POGO_OTG_EN, 0);
			if (rc)
				CHG_DBG_E("%s: Failed to disable gpio BTM_OTG_EN\n", __func__);
			//[---]For OTG disable
			g_temp_INBOX_DT_state = 2;
			usb_alert_flag_ACCY = 1;
		} else if (otg_status && !trigger_level) {
			rc = smblib_write(smbchg_dev, CMD_OTG_REG, 0);
			if (rc < 0)
				CHG_DBG_E("%s: Failed to disable pmi_otg_en\n", __func__);
			rc = gpio_direction_output(global_gpio->POGO_OTG_EN, 0);
			if (rc)
				CHG_DBG_E("%s: Failed to disable gpio POGO_OTG_EN\n", __func__);
			g_temp_INBOX_DT_state = 2;
			usb_alert_flag_ACCY = 1;
		} else if (!trigger_level){
			g_temp_INBOX_DT_state = 1;
			usb_alert_flag_ACCY = 1;
		} else if (trigger_level) {
			usb_alert_flag_ACCY = 0;
			if (!gpio_get_value(global_gpio->POGO_OVP_ACOK) && (g_temp_INBOX_DT_state == 2))
				usb_alert_keep_suspend_flag_ACCY = 1;
			g_temp_INBOX_DT_state = 0;
		}
		if (g_temp_side_state == 2 || g_temp_btm_state == 2 || g_temp_station_state == 2 || g_temp_INBOX_DT_state == 2)
			g_temp_state = 2;
		else if (g_temp_side_state == 1 || g_temp_btm_state == 1 || g_temp_station_state == 1 || g_temp_INBOX_DT_state == 1)
			g_temp_state = 1;
		else
			g_temp_state = 0;

		asus_extcon_set_cable_state_(smbchg_dev->thermal_extcon, g_temp_state);

		CHG_DBG("%s  g_temp_INBOX_DT_state = %d, report_state = %d\n", __func__, g_temp_INBOX_DT_state, g_temp_state);
		break;
	default:
		CHG_DBG("%s. This is not expected case\n", __func__);
		break;
	}
	CHG_DBG("%s  usb_alert_flag_ACCY = %d, usb_alert_keep_suspend_flag_ACCY = %d\n", __func__, usb_alert_flag_ACCY, usb_alert_keep_suspend_flag_ACCY);
}

//[+++]Need to check the status of pogo thermal if the device is booting
//The interrupt could be sent before the qpnp_smb2 probe finish
void asus_hid_is_connected(void)
{
	int ret;
	int station_cap;

	CHG_DBG("%s start\n", __func__);
	schedule_delayed_work(&smbchg_dev->asus_pogo_thermal_work, 0);

	ret = hid_to_get_battery_cap(&station_cap);
	if (ret < 0) {
		CHG_DBG_E("%s: Failed to get station capacity\n", __func__);
	} else if (station_cap >= 3 && station_cap <= 100) {
		asus_extcon_set_cable_state_(g_fgChip->st_bat_cap_extcon, station_cap);
		asus_extcon_set_cable_state_(g_fgChip->st_present_extcon, 1);
	}
}
//[---]Need to check the status of pogo thermal if the device is booting

//[+++]Add the interrupt handler for pogo thermal detect
static irqreturn_t pogo_thermal_interrupt(int irq, void *dev_id)
{
	struct timespec mtNow, delta_time;
	long long delta_msec;

	//Use the delta time rather than trigger_level to filter the pulse for station
	mtNow = current_kernel_time();
	delta_time = timespec_sub(mtNow, g_last_pogo_therm_int);
	g_last_pogo_therm_int = current_kernel_time();
	delta_msec = (delta_time.tv_sec * NSEC_PER_SEC + delta_time.tv_nsec)/NSEC_PER_MSEC;
	CHG_DBG("[STATION]%s. delta = %lld msec.\n", __func__, delta_msec);
	if (delta_msec < 100)
		return IRQ_HANDLED;

	schedule_delayed_work(&smbchg_dev->asus_pogo_thermal_work, 0);
	return IRQ_HANDLED;
}
//[---]Add the interrupt handler for pogo theraml detects

//[+++]Add the interrupt handler for pogo detect
static irqreturn_t pogo_detect_interrupt(int irq, void *dev_id)
{
	int status = gpio_get_value_cansleep(global_gpio->POGO_ADC_MUX_INT_N);
	int rc;
	u8 reg = 0, reg2 = 0;
	u8 pogo_mask = 0xC0;
	u8 thermal_mask = 0x30;
	bool is_dongle_case = 0;
	bool usb2_mux1_stats = 0, pmi_mux_stats = 0;
	union power_supply_propval pval = {POWER_SUPPLY_TYPEC_PR_DUAL, };

	if (!asp1690e_ready) {
		CHG_DBG_E("%s: ADC is not ready, bypass interrupt\n", __func__);
		return IRQ_HANDLED;
	}
	rerun_pogo_det = 0;
	CHG_DBG("%s: start pogo status = %d\n", __func__, status);
	rc = asp1690e_read_reg(0x42, &reg);
	rc = asp1690e_read_reg(0x41, &reg2);//Need to read and skip the interrtup from 1D+/1D-

	if (rc < 0) {
		//ASUS_POGO_ID = ERROR1;
		//asp1690e_write_reg(0x3C,0x10);
		//asp1690e_write_reg(0x3D,0x00);
		CHG_DBG_E("%s: fail to read ADC 0x42h\n", __func__);
		goto i2c_error;
	}

	if (reg & pogo_mask) {
		rc = asp1690e_read_reg(0x47, &reg);
		if (rc < 0) {
			//ASUS_POGO_ID = ERROR1;
			//asp1690e_write_reg(0x3C,0x10);
			//asp1690e_write_reg(0x3D,0x00);
			CHG_DBG_E("%s: fail to read ADC 0x47h\n", __func__);
			goto i2c_error;
		}
		is_dongle_case = 1;
		CHG_DBG("The POGO reg : 0x%x\n", reg);
		detect_dongle_type(reg);
		if (reg >= 0x43 && reg <= 0x58) {
			ASUS_POGO_ID = INBOX;
			goto pogo_attach;
		} else if (reg >= 0x2D && reg <= 0x42) {
			ASUS_POGO_ID = DT;
			goto pogo_attach;
		} else if (reg >= 0x17 && reg <= 0x2C) {
			ASUS_POGO_ID = STATION;
			goto pogo_attach;
		} else if (reg >= 0xBC) {
			ASUS_POGO_ID = NO_INSERT;
			asp1690e_write_reg(0x3C,0xDE);
			asp1690e_write_reg(0x3D,0x6F);
			goto pogo_detach;
		} else if (reg <= 0x10) {
			ASUS_POGO_ID = ERROR1;
			asp1690e_write_reg(0x3C,0x10);
			asp1690e_write_reg(0x3D,0x00);
			goto pogo_detach;
		} else {
			ASUS_POGO_ID = OTHER;
			goto pogo_attach;
		}
	}else if (reg2) {
		CHG_DBG("This is adapter_id trigger. ASP1690 0x41 reg : 0x%x\n", reg2);
		is_dongle_case = 0;
	}

	if (reg & thermal_mask) {
		CHG_DBG("%s: Side Thermal Alert interrupt\n", __func__);
		schedule_delayed_work(&smbchg_dev->asus_thermal_side_work, 0);
		if ((reg & pogo_mask) == 0)
			is_dongle_case = 0;
	}

pogo_detach:
	if (is_dongle_case == 0)
		return IRQ_HANDLED;
	CHG_DBG("%s(pogo_detach): detect ASUS_POGO_ID = %s\n", __func__, pogo_id_str[ASUS_POGO_ID]);
	fg_station_attach_notifier(false);	//ASUS BSP notifier fg driver +++
	rc = gpio_direction_output(global_gpio->POGO_DET, 0);
	if (rc < 0)
		CHG_DBG_E("%s: fail to pull POGO_DET low\n", __func__);
	if (g_force_usb_mux == 2) {
		extcon_set_cable_state_(smbchg_dev->extcon, EXTCON_USB, false);
		CHG_DBG("[USB_MUX]%s. Set EXTCON_USB = false in force usb_mux\n", __func__);
	}
	//Enable INOV
	dt_overheat_flag = 0;
	rc = smblib_write(smbchg_dev, THERMREG_SRC_CFG_REG, 0x07);
	if (rc < 0)
		CHG_DBG_E("%s: Failed to enable INOV\n", __func__);

	last_charger_state = BAT_CHARGER_NULL;	//Reset station charger state/stage +++
	last_charger_statge = BAT_STAGE_NULL;

	// For station get 0% +++
	//set phone CC = DRP
	power_supply_set_property(smbchg_dev->usb_psy, POWER_SUPPLY_PROP_TYPEC_POWER_ROLE, &pval);
	CHG_DBG("%s Station plug out, set phone CC to DRP", __func__);
	// For station get 0% ---

	//[+++]This is station USB thermal alert case
	//Need to clear the flag of station USB thermal alert after removing Station
	station_cap_zero_flag = 0;
	g_temp_station_state = 0;
	g_temp_INBOX_DT_state = 0;
	usb_alert_keep_suspend_flag_ACCY = 0;
	usb_alert_flag_ACCY = 0;
	if (g_temp_side_state == 2 || g_temp_btm_state == 2)
		g_temp_state = 2;
	else if (g_temp_side_state == 1 || g_temp_btm_state == 1)
		g_temp_state = 1;
	else
		g_temp_state = 0;
	asus_extcon_set_cable_state_(smbchg_dev->thermal_extcon, g_temp_state);
	CHG_DBG("%s  g_temp_side_state = %d, g_temp_btm_state = %d, report_state = %d\n", __func__, g_temp_side_state, g_temp_btm_state, g_temp_state);
	//[---]This is station USB thermal alert case

	return IRQ_HANDLED;
pogo_attach:
	asp1690e_write_reg(0x3C,0xBC);
	asp1690e_write_reg(0x3D,0x10);
	CHG_DBG("%s(pogo_attach): detect ASUS_POGO_ID = %s\n", __func__, pogo_id_str[ASUS_POGO_ID]);
	if (ASUS_POGO_ID == STATION)
		fg_station_attach_notifier(true);
	if (ASUS_POGO_ID == INBOX || ASUS_POGO_ID == DT)
		rc = gpio_direction_output(global_gpio->POGO_DET, 1);
	else
		rc = gpio_direction_output(global_gpio->POGO_DET, 0);
	if (rc < 0)
		CHG_DBG_E("%s: fail to pull POGO_DET high\n", __func__);
	if (g_force_usb_mux == 0) {
		//[+++]For Dongle device plug-in, also need to update Action-1
		CHG_DBG("%s: Try to run asus_write_mux_setting_1. \n", __func__);
		schedule_delayed_work(&smbchg_dev->asus_mux_setting_1_work, msecs_to_jiffies(ASP1690_MUX_WAIT_TIME));
		//[---]For Dongle device plug-in, also need to update Action-1
	} else if (g_force_usb_mux == 2) {
		usb2_mux1_stats = gpio_get_value_cansleep(global_gpio->USB2_MUX1_EN);
		pmi_mux_stats = gpio_get_value_cansleep(global_gpio->PMI_MUX_EN);
		if (usb2_mux1_stats == 0 && pmi_mux_stats == 1) {
			msleep(5000);
			extcon_set_cable_state_(smbchg_dev->extcon, EXTCON_USB, true);
			CHG_DBG("[USB_MUX]%s. Set EXTCON_USB = true in force usb_mux\n", __func__);
		}
	}
	// DT with AC: Disable INOV
	if (ASUS_POGO_ID == DT) {
	rc = smblib_write(smbchg_dev, THERMREG_SRC_CFG_REG, 0x0);
	if (rc < 0)
		CHG_DBG_E("%s: Failed to disable INOV\n", __func__);
	}
	return IRQ_HANDLED;

i2c_error:
	CHG_DBG_E("%s: I2C Access error\n", __func__);
	rerun_pogo_det = 1;
	return IRQ_HANDLED;
}
//[---]Add the interrupt handler for pogo detect

//ASUS BSP : Show "+" on charging icon +++
struct extcon_dev qc_stat;
#define SWITCH_QC_NOT_QUICK_CHARGING        4
#define SWITCH_QC_QUICK_CHARGING            3
#define SWITCH_QC_NOT_QUICK_CHARGING_PLUS   2
#define SWITCH_QC_QUICK_CHARGING_PLUS       1
#define SWITCH_QC_OTHER	                    0
void set_qc_stat(union power_supply_propval *val)
{
	int stat;
	int set = SWITCH_QC_OTHER;
	int asus_status;

	if (g_Charger_mode)
		return;

	stat = val->intval;
	asus_status = asus_get_batt_status();

	if (asus_status == NORMAL) {
		set = SWITCH_QC_OTHER;
		CHG_DBG("stat: %d, switch: %d\n", stat, set);
		asus_extcon_set_cable_state_(smbchg_dev->quickchg_extcon, set);
		return;
	}

	switch (stat) {
	//"qc" stat happends in charger mode only, refer to smblib_get_prop_batt_status
	case POWER_SUPPLY_STATUS_CHARGING:
	case POWER_SUPPLY_STATUS_NOT_CHARGING:
	case POWER_SUPPLY_STATUS_QUICK_CHARGING:
	case POWER_SUPPLY_STATUS_QUICK_CHARGING_PLUS:
		if (asus_get_prop_batt_capacity(smbchg_dev) <= 70) {
			if (asus_status == QC)
				set = SWITCH_QC_QUICK_CHARGING;
			else if (asus_status == QC_PLUS)
				set = SWITCH_QC_QUICK_CHARGING_PLUS;
		} else {
			if (asus_status == QC)
				set = SWITCH_QC_NOT_QUICK_CHARGING;
			else if (asus_status == QC_PLUS)
				set = SWITCH_QC_NOT_QUICK_CHARGING_PLUS;
		}
		asus_extcon_set_cable_state_(smbchg_dev->quickchg_extcon, set);
		break;
	default:
		set = SWITCH_QC_OTHER;
		asus_extcon_set_cable_state_(smbchg_dev->quickchg_extcon, set);
		break;
	}

	CHG_DBG("stat: %d, switch: %d\n", stat, set);
	return;
}
//ASUS BSP : Show "+" on charging icon +++

/* +++ Add Maximun Battery Lifespan +++ */
#define PERSIST_CHG_LIMIT_PATH	"/persist/charger/CHGLimit"
int read_CHGLimit_value(void)
{
	struct file *fp = NULL;
	mm_segment_t old_fs;
	loff_t pos_lsts = 0;
	char buf[8] = "";
	int l_result = -1;

	fp = filp_open(PERSIST_CHG_LIMIT_PATH, O_RDWR, 0);
	if (IS_ERR_OR_NULL(fp)) {
		CHG_DBG_E("%s: open (%s) fail\n", __func__, PERSIST_CHG_LIMIT_PATH);
		return -ENOENT;	/*No such file or directory*/
	}

	/*For purpose that can use read/write system call*/
	old_fs = get_fs();
	set_fs(KERNEL_DS);

	vfs_read(fp, buf, 8, &pos_lsts);

	set_fs(old_fs);
	filp_close(fp, NULL);

	sscanf(buf, "%d", &l_result);
	if(l_result < 0) {
		CHG_DBG_E("%s: FAIL. (%d)\n", __func__, l_result);
		return -EINVAL;	/*Invalid argument*/
	} else {
		CHG_DBG("%s: %d\n", __func__, l_result);
	}

	return l_result;
}

void write_CHGLimit_value(int input)
{
	struct file *fp = NULL;
	mm_segment_t old_fs;
	loff_t pos_lsts = 0;
	char buf[8] = "";

	sprintf(buf, "%d", input);

	fp = filp_open(PERSIST_CHG_LIMIT_PATH, O_RDWR | O_CREAT | O_SYNC, 0666);
	if (IS_ERR_OR_NULL(fp)) {
		CHG_DBG_E("%s: open (%s) fail\n", __func__, PERSIST_CHG_LIMIT_PATH);
		return;
	}

	/*For purpose that can use read/write system call*/
	old_fs = get_fs();
	set_fs(KERNEL_DS);

	vfs_write(fp, buf, 8, &pos_lsts);

	set_fs(old_fs);
	filp_close(fp, NULL);

	CHG_DBG("%s : %s\n", __func__, buf);
}
/* --- Add Maximun Battery Lifespan --- */

void asus_probe_pmic_settings(struct smb_charger *chg)
{
	int rc;

//A-1:0x1362
	rc = smblib_masked_write(chg, USBIN_OPTIONS_1_CFG_REG, INPUT_PRIORITY_BIT, 0);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't set default USBIN_OPTIONS_1_CFG_REG rc=%d\n", rc);
	}
//A-2:0x1070
	rc = smblib_write(chg, FLOAT_VOLTAGE_CFG_REG, 0x74);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't set default FLOAT_VOLTAGE_CFG_REG rc=%d\n", rc);
	}
/*A-3: set tcc & stcc in dtsi */
//A-4:0x1359
	rc = smblib_masked_write(chg, TYPE_C_CFG_2_REG, EN_80UA_180UA_CUR_SOURCE_BIT, 0);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't set default TYPE_C_CFG_2_REG rc=%d\n", rc);
	}
//A-5:0x1152
	rc = smblib_masked_write(chg, OTG_CURRENT_LIMIT_CFG_REG,
			OTG_CURRENT_LIMIT_MASK, 0x01);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't set default OTG_CURRENT_LIMIT_CFG_REG rc=%d\n", rc);
	}
//A-12:0x1090
	rc = smblib_write(chg, JEITA_EN_CFG_REG, 0x10);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't set default JEITA_EN_CFG_REG rc=%d\n", rc);
	}
	//0x1358
	//APSD starts after vbus deglitch (QCT default needs vbus deglitch and cc deglitch)
	rc = smblib_masked_write(chg, TYPE_C_CFG_REG, APSD_START_ON_CC_BIT, 0x0);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't set default TYPE_C_CFG_REG rc=%d\n", rc);
	}
	//0x1260
	//Disable the LOW_BATT_DETECT for power team request
	rc = smblib_masked_write(chg, LOW_BATT_DETECT_EN_CFG_REG, LOW_BATT_DETECT_EN_BIT, 0x0);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't set default LOW_BATT_DETECT_EN_CFG_REG rc=%d\n", rc);
	}
	//0x1368
	rc = smblib_masked_write(chg, TYPE_C_INTRPT_ENB_SOFTWARE_CTRL_REG, EXIT_SNK_BASED_ON_CC_BIT, EXIT_SNK_BASED_ON_CC_BIT);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't set default TYPE_C_INTRPT_ENB_SOFTWARE_CTRL_REG rc=%d\n", rc);
	}
	//0x45B1 (Force pmi8998_tz temperature conversion for resume too late issue
	rc = smblib_masked_write(chg, 0x45B1, BIT(0)|BIT(7) , BIT(0)|BIT(7));
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't set default FG_ADC_RR_PMI_DIE_TEMP_TRIGGER rc=%d\n", rc);
	}
	#if 0
	//0x1365
	//Set the USBIN_IN_COLLAPSE from 5ms to 30us.
	//Fix the issue. The VBUS still keeps HIGH after unplug charger
	rc = smblib_masked_write(chg, USBIN_LOAD_CFG_REG, USBIN_IN_COLLAPSE_GF, 0x3);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't set USBIN_LOAD_CFG_REG rc=%d\n", rc);
	}
	//0x1380
	//WA for air charing issue, this is from power team's request
	rc = smblib_masked_write(chg, USBIN_AICL_OPTIONS_CFG_REG, USBIN_LV_COLLAPSE_RESPONSE_BIT, USBIN_LV_COLLAPSE_RESPONSE_BIT);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't set USBIN_AICL_OPTIONS_CFG_REG rc=%d\n", rc);
	}
	#endif
}

void asus_probe_gpio_setting(struct platform_device *pdev, struct gpio_control *gpio_ctrl)
{
	struct pinctrl *chg_pc;
	struct pinctrl_state *chg_pcs;
	int chg_gpio_irq = 0, pogo_thermal_irq = 0;
	int rc = 0;

	chg_pc = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR_OR_NULL(chg_pc)) {
		g_interrupt_enable = 0;
		CHG_DBG_E("%s: failed to get charger pinctrl\n", __func__);
	}
	chg_pcs = pinctrl_lookup_state(chg_pc, "chg_gpio_default");
	if (IS_ERR_OR_NULL(chg_pcs)) {
		g_interrupt_enable = 0;
		CHG_DBG_E("%s: failed to get charger input pinctrl state from dtsi\n", __func__);
	}
	rc = pinctrl_select_state(chg_pc, chg_pcs);
	if (rc < 0) {
		g_interrupt_enable = 0;
		CHG_DBG_E("%s: failed to set charger input pinctrl state\n", __func__);
	}

	//[+++] Add the pinctrl of the mux setting
	gpio_ctrl->USB2_MUX1_EN = of_get_named_gpio(pdev->dev.of_node, "USB2_MUX1_EN-gpio102", 0);
	rc = gpio_request(gpio_ctrl->USB2_MUX1_EN, "USB2_MUX1_EN-gpio102");
	if (rc)
		CHG_DBG_E("%s: failed to request USB2_MUX1_EN-gpio102\n", __func__);
	else
		CHG_DBG("%s: Success to request USB2_MUX1_EN-gpio102\n", __func__);

	gpio_ctrl->USB2_MUX2_EN = of_get_named_gpio(pdev->dev.of_node, "USB2_MUX2_EN-gpio104", 0);
	rc = gpio_request(gpio_ctrl->USB2_MUX2_EN, "USB2_MUX2_EN-gpio104");
	if (rc)
		CHG_DBG_E("%s: failed to request USB2_MUX2_EN-gpio104\n", __func__);
	else
		CHG_DBG("%s: Success to request USB2_MUX2_EN-gpio104\n", __func__);
	
	gpio_ctrl->USB1_MUX_EN = of_get_named_gpio(pdev->dev.of_node, "USB1_MUX_EN-gpio02", 0);
	rc = gpio_request(gpio_ctrl->USB1_MUX_EN, "USB1_MUX_EN-gpio02");
	if (rc)
		CHG_DBG_E("%s: failed to request USB1_MUX_EN-gpio02\n", __func__);
	else
		CHG_DBG("%s: Success to request USB1_MUX_EN-gpio02\n", __func__);

	gpio_ctrl->PMI_MUX_EN = of_get_named_gpio(pdev->dev.of_node, "PMI_MUX_EN-gpio10", 0);
	rc = gpio_request(gpio_ctrl->PMI_MUX_EN, "PMI_MUX_EN-gpio10");
	if (rc)
		CHG_DBG_E("%s: failed to request PMI_MUX_EN-gpio10\n", __func__);
	else
		CHG_DBG("%s: Success to request PMI_MUX_EN-gpio10\n", __func__);

	gpio_ctrl->PW_ADC_EN = of_get_named_gpio(pdev->dev.of_node, "PW_ADC_EN-gpio100", 0);
	rc = gpio_request(gpio_ctrl->PW_ADC_EN, "PW_ADC_EN-gpio100");
	if (rc)
		CHG_DBG_E("%s: failed to request PW_ADC_EN-gpio100\n", __func__);
	else
		CHG_DBG("%s: Success to request PW_ADC_EN-gpio100\n", __func__);
	//[---] Add the pinctrl of the mux setting

	gpio_ctrl->POGO_THML_INT = of_get_named_gpio(pdev->dev.of_node, "POGO_THML_INT-gpio22", 0);
	rc = gpio_request(gpio_ctrl->POGO_THML_INT, "POGO_THML_INT-gpio22");
	if (rc)
		CHG_DBG_E("%s: failed to request POGO_THML_INT-gpio22\n", __func__);
	else
		CHG_DBG("%s: Success to request POGO_THML_INT-gpio22\n", __func__);
	pogo_thermal_irq = gpio_to_irq(gpio_ctrl->POGO_THML_INT);
	if (pogo_thermal_irq < 0) {
		CHG_DBG_E("%s: POGO_THML_INT-gpio22 ERROR(%d).\n", __func__, chg_gpio_irq);
	}
	rc = request_threaded_irq(pogo_thermal_irq, NULL, pogo_thermal_interrupt,
			IRQF_TRIGGER_FALLING  | IRQF_TRIGGER_RISING | IRQF_ONESHOT | IRQF_NO_SUSPEND, "pogo_thermal", NULL);	//IRQF_ONESHOT
		if (rc < 0)
			CHG_DBG_E("%s: Failed to request pogo_thermal_interrupt\n", __func__);

	gpio_ctrl->POGO_OTG_EN = of_get_named_gpio(pdev->dev.of_node, "POGO_OTG_EN-gpio11", 0);
	rc = gpio_request(gpio_ctrl->POGO_OTG_EN, "POGO_OTG_EN-gpio11");
	if (rc)
		CHG_DBG_E("%s: failed to request POGO_OTG_EN-gpio11\n", __func__);
	else
		CHG_DBG("%s: Success to request POGO_OTG_EN-gpio11\n", __func__);

	gpio_ctrl->BTM_OTG_EN = of_get_named_gpio(pdev->dev.of_node, "BTM_OTG_EN-gpio6", 0);
	rc = gpio_request(gpio_ctrl->BTM_OTG_EN, "BTM_OTG_EN-gpio6");
	if (rc)
		CHG_DBG_E("%s: failed to request BTM_OTG_EN-gpio6\n", __func__);
	else
		CHG_DBG("%s: Success to request BTM_OTG_EN-gpio6\n", __func__);

	if (g_ASUS_hwID != ZS600KL_SR1 && g_ASUS_hwID != ZS600KL_SR2) {
		gpio_ctrl->POGO_DET = of_get_named_gpio(pdev->dev.of_node, "POGO_DET-gpio10", 0);
		rc = gpio_request(gpio_ctrl->POGO_DET, "POGO_DET-gpio10");
		if (rc)
			CHG_DBG_E("%s: failed to request POGO_DET-gpio10\n", __func__);
		else
			CHG_DBG("%s: Success to request POGO_DET-gpio10\n", __func__);
	}

	gpio_ctrl->POGO_OVP_ACOK = of_get_named_gpio(pdev->dev.of_node, "POGO_OVP_ACOK-gpio32", 0);
	rc = gpio_request(gpio_ctrl->POGO_OVP_ACOK, "POGO_OVP_ACOK-gpio32");
	if (rc)
		CHG_DBG_E("%s: failed to request POGO_OVP_ACOK-gpio32\n", __func__);
	else
		CHG_DBG("%s: Success to request POGO_OVP_ACOK-gpio32\n", __func__);

	gpio_ctrl->BTM_OVP_ACOK = of_get_named_gpio(pdev->dev.of_node, "BTM_OVP_ACOK-gpio31", 0);
	rc = gpio_request(gpio_ctrl->BTM_OVP_ACOK, "BTM_OVP_ACOK-gpio31");
	if (rc)
		CHG_DBG_E("%s: failed to request BTM_OVP_ACOK-gpio31\n", __func__);
	else
		CHG_DBG("%s: Success to request BTM_OVP_ACOK-gpio31\n", __func__);

	if (g_interrupt_enable && g_ASUS_hwID != ZS600KL_SR1 && g_ASUS_hwID != ZS600KL_SR2) {
		//pogo detect
		gpio_ctrl->POGO_ADC_MUX_INT_N = of_get_named_gpio(pdev->dev.of_node, "POGO_ADC_MUX_INT_N-gpio43", 0);
		rc = gpio_request(gpio_ctrl->POGO_ADC_MUX_INT_N, "POGO_ADC_MUX_INT_N-gpio43");
		if (rc)
			CHG_DBG_E("%s: failed to request POGO_ADC_MUX_INT_N-gpio43\n", __func__);
		else
			CHG_DBG("%s: Success to request POGO_ADC_MUX_INT_N-gpio43\n", __func__);

		chg_gpio_irq = gpio_to_irq(gpio_ctrl->POGO_ADC_MUX_INT_N);
		if (chg_gpio_irq < 0) {
			CHG_DBG_E("%s: POGO_ADC_MUX_INT_N-gpio43_to_irq ERROR(%d).\n", __func__, chg_gpio_irq);
		}
		rc = request_threaded_irq(chg_gpio_irq, NULL, pogo_detect_interrupt,
			IRQF_TRIGGER_FALLING  | IRQF_ONESHOT | IRQF_NO_SUSPEND, "pogo_detect", NULL);	//IRQF_ONESHOT
		if (rc < 0)
			CHG_DBG_E("%s: Failed to request pogo_detect_interrupt\n", __func__);
	}

	gpio_ctrl->WP_POGO = of_get_named_gpio(pdev->dev.of_node, "WP_POGO-gpio9", 0);
	rc = gpio_request(gpio_ctrl->WP_POGO, "WP_POGO-gpio9");
	if (rc)
		CHG_DBG_E("%s: failed to request WP_POGO-gpio9\n", __func__);
	else
		CHG_DBG("%s: Success to request WP_POGO-gpio9\n", __func__);

	gpio_ctrl->WP_BTM = of_get_named_gpio(pdev->dev.of_node, "WP_BTM-gpio12", 0);
	rc = gpio_request(gpio_ctrl->WP_BTM, "WP_BTM-gpio12");
	if (rc)
		CHG_DBG_E("%s: failed to request WP_BTM-gpio12\n", __func__);
	else
		CHG_DBG("%s: Success to request WP_BTM-gpio12\n", __func__);
}

void asus_asp1690e_resume_setting(void)
{
	if (!asp1690e_ready) {
		CHG_DBG_E("%s: ADC is not ready, bypass interrupt\n", __func__);
		return;
	}

	asp1690e_mask_write_reg(0x31,0xC0,0xC0);
	asp1690e_write_reg(0x3C,0xDE);
	asp1690e_write_reg(0x3D,0x6F);
	asp1690e_write_reg(0x3E,0xFF);
	asp1690e_write_reg(0x3F,0x31);
}

//Side cable MisInsertion Detect +++
void asus_side_misinsertion_work(struct work_struct *work)
{
	int temp;

	if (ASUS_POGO_ID == NO_INSERT) {
		temp = asus_get_wp_side_vadc_voltage();
		if (temp < g_LIQUID_LOW_BOUND) {
			CHG_DBG_E("%s: Side Connector MisInsertion\n", __func__);
			asus_extcon_set_cable_state_(smbchg_dev->misinsertion_extcon, 1);
			tas2560_sec_mi2s_enable(false);//disable tas2560 (vibrator) sec_mi2s
		}
	} else {
		asus_extcon_set_cable_state_(smbchg_dev->misinsertion_extcon, 0);
		tas2560_sec_mi2s_enable(true);//enable tas2560 (vibrator) sec_mi2s
	}
}
//Side cable MisInsertion Detect ---

//Bottom Thermal Alert Function +++
static void qpnp_thermal_btm_adc_notification(enum qpnp_tm_state state, void *ctx)
{
	if (state == ADC_TM_HIGH_STATE) { 	//In High state
		CHG_DBG("%s: ADC_TM_HIGH_STATE\n", __func__);
	} else { 							//In low state
		CHG_DBG("%s: ADC_TM_LOW_STATE\n", __func__);
	}

	schedule_delayed_work(&smbchg_dev->asus_thermal_btm_work, 0);
}

static int32_t asus_get_temp_btm_vadc_voltage(void)
{
	struct qpnp_vadc_result result;
	int rc;
	int32_t temp;

	if (IS_ERR(smbchg_dev->vadc_dev_temp)) {
		CHG_DBG_E("%s: qpnp_get_vadc failed\n", __func__);
		return -1;
	} else {
		rc = qpnp_vadc_read(smbchg_dev->vadc_dev_temp,
				VADC_AMUX4_GPIO, &result);
		if (rc) {
			CHG_DBG_E("%s: Read vadc fail\n", __func__);
			return rc;
		}
		temp = (int) result.physical;
	}

	return temp;
}

void asus_thermal_btm_work(struct work_struct *work)
{
	int rc;
	int32_t temp;
	int vbus_status, otg_status;

	CHG_DBG("%s: work start\n", __func__);

	if ((smbchg_dev->vadc_dev_temp == NULL) || (smbchg_dev->adc_tm_dev_temp == NULL)) {
		/* Get the ADC device instance (one time) */
		smbchg_dev->vadc_dev_temp = qpnp_get_vadc(smbchg_dev->dev, "temp-btm");
		if (IS_ERR(smbchg_dev->vadc_dev_temp)) {
			CHG_DBG_E("%s: vadc error\n", __func__);
			return;
		}
		smbchg_dev->adc_tm_dev_temp = qpnp_get_adc_tm(smbchg_dev->dev, "temp-btm");
		if (IS_ERR(smbchg_dev->adc_tm_dev_temp)) {
			CHG_DBG_E("%s: adc_tm error\n", __func__);
			return;
		}
	}

	smbchg_dev->adc_param_temp.low_thr = g_temp_LOW_THR_DET;
	smbchg_dev->adc_param_temp.high_thr = g_temp_HIGH_THR_DET;
	smbchg_dev->adc_param_temp.channel = VADC_AMUX4_GPIO;
	smbchg_dev->adc_param_temp.timer_interval = ADC_MEAS2_INTERVAL_1S;
	smbchg_dev->adc_param_temp.btm_ctx = smbchg_dev;
	smbchg_dev->adc_param_temp.threshold_notification = qpnp_thermal_btm_adc_notification;

	temp = asus_get_temp_btm_vadc_voltage();

	if (temp < g_temp_LOW_THR_DET) {
		usb_alert_flag = 1;
		if (g_Charger_mode)
			cos_alert_once_flag = 1;
		vbus_status = gpio_get_value(global_gpio->BTM_OVP_ACOK);
		otg_status = gpio_get_value(global_gpio->BTM_OTG_EN);
		if (!vbus_status) {
			smblib_set_usb_suspend(smbchg_dev, true);
			pmic_set_pca9468_charging(false);
			rc = smblib_write(smbchg_dev, CMD_OTG_REG, 0);
			if (rc < 0)
				CHG_DBG_E("%s: Failed to disable pmi_otg_en\n", __func__);
			rc = gpio_direction_output(global_gpio->BTM_OTG_EN, 0);
			if (rc)
				CHG_DBG_E("%s: Failed to disable gpio BTM_OTG_EN\n", __func__);
			g_temp_btm_state = 2;
		} else if (otg_status) {
			g_temp_btm_state = 2;
			msleep(100);
			rc = smblib_write(smbchg_dev, CMD_OTG_REG, 0);
			if (rc < 0)
				CHG_DBG_E("%s: Failed to disable pmi_otg_en\n", __func__);
			rc = gpio_direction_output(global_gpio->BTM_OTG_EN, 0);
			if (rc)
				CHG_DBG_E("%s: Failed to disable gpio BTM_OTG_EN\n", __func__);
		} else {
			g_temp_btm_state = 1;
		}
		smbchg_dev->adc_param_temp.state_request = ADC_TM_HIGH_THR_ENABLE;
	} else {
		usb_alert_flag = 0;
		if (!gpio_get_value(global_gpio->BTM_OVP_ACOK) && (g_temp_btm_state == 2))
			usb_alert_keep_suspend_flag = 1;
		g_temp_btm_state = 0;
		smbchg_dev->adc_param_temp.state_request = ADC_TM_LOW_THR_ENABLE;
	}

	if (g_temp_side_state == 2 || g_temp_btm_state == 2 || g_temp_station_state == 2 || g_temp_INBOX_DT_state == 1)
		g_temp_state = 2;
	else if (g_temp_side_state == 1 || g_temp_btm_state == 1 || g_temp_station_state == 1 || g_temp_INBOX_DT_state == 1)
		g_temp_state = 1;
	else
		g_temp_state = 0;
	asus_extcon_set_cable_state_(smbchg_dev->thermal_extcon, g_temp_state);

	CHG_DBG("%s  btm_temp = %d, report_state = %d\n", __func__, temp, g_temp_state);

	rc = qpnp_adc_tm_channel_measure(smbchg_dev->adc_tm_dev_temp, &smbchg_dev->adc_param_temp);
	if (rc) {
		CHG_DBG_E("%s: qpnp_adc_tm_channel_measure fail(%d)\n", __func__, rc);
	}
}
//Bottom Thermal Alert Function ---

//Side Thermal Alert Function +++
void asus_thermal_side_work(struct work_struct *work)
{
	int rc;
	u8 reg = 0;
	int vbus_status, otg_status;

	rc = asp1690e_read_reg(0x48, &reg);
	if (rc < 0)
		CHG_DBG_E("%s: fail to read ADC 0x48h\n", __func__);
	if (reg <= 0x31) {
		CHG_DBG("%s: Side Thermal Alert trigger HIGH, reg = 0x%x\n", __func__, reg);
		usb_alert_flag = 1;
		if (g_Charger_mode)
			cos_alert_once_flag = 1;
		vbus_status = gpio_get_value(global_gpio->POGO_OVP_ACOK);
		otg_status = gpio_get_value(global_gpio->POGO_OTG_EN);
		if (!vbus_status) {
			smblib_set_usb_suspend(smbchg_dev, true);
			pmic_set_pca9468_charging(false);
			rc = smblib_write(smbchg_dev, CMD_OTG_REG, 0);
			if (rc < 0)
				CHG_DBG_E("%s: Failed to disable pmi_otg_en\n", __func__);
			rc = gpio_direction_output(global_gpio->POGO_OTG_EN, 0);
			if (rc)
				CHG_DBG_E("%s: Failed to disable gpio BTM_OTG_EN\n", __func__);
			g_temp_side_state = 2;
		} else if (otg_status) {
			g_temp_side_state = 2;
			msleep(100);
			rc = smblib_write(smbchg_dev, CMD_OTG_REG, 0);
			if (rc < 0)
				CHG_DBG_E("%s: Failed to disable pmi_otg_en\n", __func__);
			rc = gpio_direction_output(global_gpio->POGO_OTG_EN, 0);
			if (rc)
				CHG_DBG_E("%s: Failed to disable gpio POGO_OTG_EN\n", __func__);
		} else {
			g_temp_side_state = 1;
		}
		asp1690e_write_reg(0x3E,0x64);
		asp1690e_write_reg(0x3F,0x00);
	} else {
		CHG_DBG("%s: Side Thermal Alert trigger LOW, reg = 0x%x\n", __func__, reg);
		usb_alert_flag = 0;
		if (!gpio_get_value(global_gpio->POGO_OVP_ACOK) && (g_temp_side_state == 2))
			usb_alert_keep_suspend_flag = 1;
		g_temp_side_state = 0;
		asp1690e_write_reg(0x3E,0xFF);
		asp1690e_write_reg(0x3F,0x31);
	}

	if (g_temp_side_state == 2 || g_temp_btm_state == 2 || g_temp_station_state == 2 || g_temp_INBOX_DT_state == 2)
		g_temp_state = 2;
	else if (g_temp_side_state == 1 || g_temp_btm_state == 1 || g_temp_station_state == 1 || g_temp_INBOX_DT_state == 1)
		g_temp_state = 1;
	else
		g_temp_state = 0;
	asus_extcon_set_cable_state_(smbchg_dev->thermal_extcon, g_temp_state);

	CHG_DBG("%s: report_state = %d\n", __func__, g_temp_state);
}
//Side Thermal Alert Function ---

//Water Detection +++
static void qpnp_water_btm_adc_notification(enum qpnp_tm_state state, void *ctx)
{
	if (state == ADC_TM_HIGH_STATE) { 	//In High state
		CHG_DBG("%s: ADC_TM_HIGH_STATE\n", __func__);
	} else { 							//In low state
		CHG_DBG("%s: ADC_TM_LOW_STATE\n", __func__);
	}

	schedule_delayed_work(&smbchg_dev->asus_water_proof_btm_work, 0);
}

static int32_t asus_get_wp_btm_vadc_voltage(void)
{
	struct qpnp_vadc_result result;
	int rc;
	int32_t temp;

	if (IS_ERR(smbchg_dev->vadc_dev)) {
		CHG_DBG_E("%s: qpnp_get_vadc failed\n", __func__);
		return -1;
	} else {
		rc = qpnp_vadc_read(smbchg_dev->vadc_dev,
				VADC_AMUX5_GPIO_PU3, &result);
		if (rc) {
			CHG_DBG_E("%s: Read vadc fail\n", __func__);
			return rc;
		}
		temp = (int) result.physical;
	}

	return temp;
}

void asus_water_proof_btm_work(struct work_struct *work)
{
	int rc;
	int32_t temp;
	int cnt;

	CHG_DBG("%s: work start\n", __func__);

	if ((smbchg_dev->vadc_dev == NULL) || (smbchg_dev->adc_tm_dev == NULL)) {
		/* Get the ADC device instance (one time) */
		smbchg_dev->vadc_dev = qpnp_get_vadc(smbchg_dev->dev, "water-detection");
		if (IS_ERR(smbchg_dev->vadc_dev)) {
			CHG_DBG_E("%s: vadc error\n", __func__);
			return;
		}
		smbchg_dev->adc_tm_dev = qpnp_get_adc_tm(smbchg_dev->dev, "water-detection");
		if (IS_ERR(smbchg_dev->adc_tm_dev)) {
			CHG_DBG_E("%s: adc_tm error\n", __func__);
			return;
		}
	}

	for (cnt = 0; cnt < 10; ++cnt) {
		temp = asus_get_wp_btm_vadc_voltage();
		CHG_DBG("%s: voltage%d = %d\n", __func__, cnt, temp);
		if (temp > g_LIQUID_HIGH_BOUND) {           //No cable Normal
			g_water_btm_state = 0;
			break;
		} else if (temp < g_LIQUID_LOW_BOUND) {     //Cable in
			g_water_btm_state = 2;
			break;
		} else {                                    //With Liquid
			msleep(300);
			g_water_btm_state = 1;
		}
	}

	switch (g_water_btm_state) {
	case 0:
		smbchg_dev->adc_param.low_thr = g_LOW_THR_DET;
		smbchg_dev->adc_param.state_request = ADC_TM_LOW_THR_ENABLE;
		break;
	case 2:
		smbchg_dev->adc_param.high_thr = g_HIGH_THR_DET_2;
		smbchg_dev->adc_param.state_request = ADC_TM_HIGH_THR_ENABLE;
		break;
	case 1:
		smbchg_dev->adc_param.high_thr = g_HIGH_THR_DET_1;
		smbchg_dev->adc_param.state_request = ADC_TM_HIGH_THR_ENABLE;
	default:
		break;
	}

	if (g_water_side_state == 1 || g_water_btm_state == 1)
		g_water_state = 1;
	else
		g_water_state = 0;

	if (g_water_enable)
		asus_extcon_set_cable_state_(smbchg_dev->water_extcon, g_water_state);

	CHG_DBG("%s: Report Water Proof State: %d\n", __func__, g_water_state);

	smbchg_dev->adc_param.channel = VADC_AMUX5_GPIO_PU3;
	smbchg_dev->adc_param.timer_interval = ADC_MEAS2_INTERVAL_1S;
	smbchg_dev->adc_param.btm_ctx = smbchg_dev;
	smbchg_dev->adc_param.threshold_notification = qpnp_water_btm_adc_notification;

	rc = qpnp_adc_tm_channel_measure(smbchg_dev->adc_tm_dev, &smbchg_dev->adc_param);
	if (rc) {
		CHG_DBG_E("%s: qpnp_adc_tm_channel_measure fail(%d)\n", __func__, rc);
	}
}
//Water Detection ---

//Side Water Detection +++
static void qpnp_water_side_adc_notification(enum qpnp_tm_state state, void *ctx)
{
	if (state == ADC_TM_HIGH_STATE) { 	//In High state
		CHG_DBG("%s: ADC_TM_HIGH_STATE\n", __func__);
	} else { 							//In low state
		CHG_DBG("%s: ADC_TM_LOW_STATE\n", __func__);
	}

	schedule_delayed_work(&smbchg_dev->asus_water_proof_side_work, 0);
}

static int32_t asus_get_wp_side_vadc_voltage(void)
{
	struct qpnp_vadc_result result;
	int rc;
	int32_t temp;

	if (IS_ERR(smbchg_dev->vadc_dev_side)) {
		CHG_DBG_E("%s: qpnp_get_vadc failed\n", __func__);
		return -1;
	} else {
		rc = qpnp_vadc_read(smbchg_dev->vadc_dev_side,
				VADC_AMUX2_GPIO_PU3, &result);
		if (rc) {
			CHG_DBG_E("%s: Read vadc fail\n", __func__);
			return rc;
		}
		temp = (int) result.physical;
	}

	return temp;
}

void asus_water_proof_side_work(struct work_struct *work)
{
	int rc;
	int32_t temp;	
	int cnt;

	CHG_DBG("%s: work start\n", __func__);

	if ((smbchg_dev->vadc_dev_side == NULL) || (smbchg_dev->adc_tm_dev_side == NULL)) {
		/* Get the ADC device instance (one time) */
		smbchg_dev->vadc_dev_side = qpnp_get_vadc(smbchg_dev->dev, "wp-side");
		if (IS_ERR(smbchg_dev->vadc_dev_side)) {
			CHG_DBG_E("%s: vadc error\n", __func__);
			return;
		}
		smbchg_dev->adc_tm_dev_side = qpnp_get_adc_tm(smbchg_dev->dev, "wp-side");
		if (IS_ERR(smbchg_dev->adc_tm_dev_side)) {
			CHG_DBG_E("%s: adc_tm error\n", __func__);
			return;
		}
	}

	for (cnt = 0; cnt < 10; ++cnt) {
		temp = asus_get_wp_side_vadc_voltage();
		CHG_DBG("%s: voltage%d = %d\n", __func__, cnt, temp);
		if (temp > g_LIQUID_HIGH_BOUND) {           //No cable Normal
			g_water_side_state = 0;
			/*if (g_ASUS_hwID >= ZS600KL_MP && g_ASUS_hwID < ZS600KL_UNKNOWN){
				asus_extcon_set_cable_state_(smbchg_dev->misinsertion_extcon, 0);
				tas2560_sec_mi2s_enable(true);//enable tas2560(vibrator) sec_mi2s
			}*/
			break;
		} else if (temp < g_LIQUID_LOW_BOUND) {     //Cable in
			g_water_side_state = 2;
			/*if (g_ASUS_hwID >= ZS600KL_MP && g_ASUS_hwID < ZS600KL_UNKNOWN)
				schedule_delayed_work(&smbchg_dev->asus_side_misinsertion_work, msecs_to_jiffies(2000));*/
			break;
		} else {                                    //With Liquid
			msleep(300);
			g_water_side_state = 1;
		}
	}

	switch (g_water_side_state) {
	case 0:
		smbchg_dev->adc_param_side.low_thr = g_LOW_THR_DET;
		smbchg_dev->adc_param_side.state_request = ADC_TM_LOW_THR_ENABLE;
		break;
	case 2:
		smbchg_dev->adc_param_side.high_thr = g_HIGH_THR_DET_2;
		smbchg_dev->adc_param_side.state_request = ADC_TM_HIGH_THR_ENABLE;
		break;
	case 1:
		smbchg_dev->adc_param_side.high_thr = g_HIGH_THR_DET_1;
		smbchg_dev->adc_param_side.state_request = ADC_TM_HIGH_THR_ENABLE;
	default:
		break;
	}

	if (g_water_side_state == 1 || g_water_btm_state == 1)
		g_water_state = 1;
	else
		g_water_state = 0;

	if (g_water_enable)
		asus_extcon_set_cable_state_(smbchg_dev->water_extcon, g_water_state);

	CHG_DBG("%s: Report Water Proof State: %d\n", __func__, g_water_state);

	smbchg_dev->adc_param_side.channel = VADC_AMUX2_GPIO_PU3;
	smbchg_dev->adc_param_side.timer_interval = ADC_MEAS2_INTERVAL_1S;
	smbchg_dev->adc_param_side.btm_ctx = smbchg_dev;
	smbchg_dev->adc_param_side.threshold_notification = qpnp_water_side_adc_notification;

	rc = qpnp_adc_tm_channel_measure(smbchg_dev->adc_tm_dev_side, &smbchg_dev->adc_param_side);
	if (rc) {
		CHG_DBG_E("%s: qpnp_adc_tm_channel_measure fail(%d)\n", __func__, rc);
	}
}
//Side Water Detection ---

//[+++] Add for station update when screen on
static int smb2_drm_notifier (struct notifier_block *nb,
					unsigned long val, void *data){
	struct msm_drm_notifier *evdata = data;
	unsigned int blank;
	blank = *(int *)(evdata->data);

	if (val != MSM_DRM_EARLY_EVENT_BLANK)
		return 0;
	switch (blank) {
		case MSM_DRM_BLANK_POWERDOWN:
			//CHG_DBG("smb2_drm_notifier. panel power off\n");
			break;
		case MSM_DRM_BLANK_UNBLANK:
			//CHG_DBG("smb2_drm_notifier. panel power on\n");
			if (ASUS_POGO_ID == STATION)
				fg_station_attach_notifier(true);
			break;
		default:
			break;
	}
	return NOTIFY_DONE;
}
//[+++] Add for station update when screen on


//[+++]Rerun POGO detect if there is i2c error
void rerun_pogo_detect_func(void)
{
	int rc;
	u8 reg = 0, reg2 = 0;
	u8 pogo_mask = 0xC0;
	u8 thermal_mask = 0x30;
	bool is_dongle_case = 0;
	//bool usb2_mux1_stats = 0, pmi_mux_stats = 0;

	CHG_DBG_E("%s. WA for previous I2C error\n", __func__);

	if (!asp1690e_ready) {
		CHG_DBG_E("%s: ADC is not ready, bypass interrupt\n", __func__);
		return;
	}

	rc = asp1690e_read_reg(0x42, &reg);
	rc = asp1690e_read_reg(0x41, &reg2);//Need to read and skip the interrtup from 1D+/1D-

	if (rc < 0) {
		CHG_DBG_E("%s: fail to read ADC 0x42h\n", __func__);
		goto i2c_error;
	}

	if (reg & pogo_mask) {
		rc = asp1690e_read_reg(0x47, &reg);
		if (rc < 0) {
			CHG_DBG_E("%s: fail to read ADC 0x47h\n", __func__);
			goto i2c_error;
		}
		is_dongle_case = 1;
		CHG_DBG("The POGO reg : 0x%x\n", reg);
		detect_dongle_type(reg);
		if (reg >= 0x43 && reg <= 0x58) {
			ASUS_POGO_ID = INBOX;
			goto pogo_attach;
		} else if (reg >= 0x2D && reg <= 0x42) {
			ASUS_POGO_ID = DT;
			goto pogo_attach;
		} else if (reg >= 0x17 && reg <= 0x2C) {
			ASUS_POGO_ID = STATION;
			goto pogo_attach;
		} else if (reg >= 0xBC) {
			ASUS_POGO_ID = NO_INSERT;
			asp1690e_write_reg(0x3C,0xDE);
			asp1690e_write_reg(0x3D,0x6F);
			goto pogo_detach;
		} else if (reg <= 0x10) {
			ASUS_POGO_ID = ERROR1;
			asp1690e_write_reg(0x3C,0x10);
			asp1690e_write_reg(0x3D,0x00);
			goto pogo_detach;
		} else {
			ASUS_POGO_ID = OTHER;
			goto pogo_attach;
		}
	}else if (reg2) {
		CHG_DBG("This is adapter_id trigger. ASP1690 0x41 reg : 0x%x\n", reg2);
		is_dongle_case = 0;
	}

	if (reg & thermal_mask) {
		CHG_DBG("%s: Side Thermal Alert interrupt\n", __func__);
		schedule_delayed_work(&smbchg_dev->asus_thermal_side_work, 0);
		if ((reg & pogo_mask) == 0)
			is_dongle_case = 0;
	}

pogo_detach:
	if (is_dongle_case == 0)
		return;
	CHG_DBG("%s(pogo_detach): detect ASUS_POGO_ID = %s\n", __func__, pogo_id_str[ASUS_POGO_ID]);
	fg_station_attach_notifier(false);	//ASUS BSP notifier fg driver +++
	rc = gpio_direction_output(global_gpio->POGO_DET, 0);
	if (rc < 0)
		CHG_DBG_E("%s: fail to pull POGO_DET low\n", __func__);
	return;

pogo_attach:
	asp1690e_write_reg(0x3C,0xBC);
	asp1690e_write_reg(0x3D,0x10);
	CHG_DBG("%s(pogo_attach): detect ASUS_POGO_ID = %s\n", __func__, pogo_id_str[ASUS_POGO_ID]);
	if (ASUS_POGO_ID == STATION)
		fg_station_attach_notifier(true);
	if (ASUS_POGO_ID == INBOX || ASUS_POGO_ID == DT)
		rc = gpio_direction_output(global_gpio->POGO_DET, 1);
	else
		rc = gpio_direction_output(global_gpio->POGO_DET, 0);
	if (rc < 0)
		CHG_DBG_E("%s: fail to pull POGO_DET high\n", __func__);
	//[+++]For Dongle device plug-in, also need to update Action-1
	CHG_DBG("%s: Try to run asus_write_mux_setting_1. \n", __func__);
	schedule_delayed_work(&smbchg_dev->asus_mux_setting_1_work, msecs_to_jiffies(ASP1690_MUX_WAIT_TIME));
	//[---]For Dongle device plug-in, also need to update Action-1
	return;

i2c_error:
	CHG_DBG_E("%s: I2C Access error\n", __func__);
	return;
}
//[---]Rerun POGO detect if there is i2c error

extern struct extcon_dev *extcon_dongle; //ASUS_BSP Deeo : extern from ec_hid_driver.c

static int smb2_probe(struct platform_device *pdev)
{
	struct smb2 *chip;
	struct smb_charger *chg;
	struct gpio_control *gpio_ctrl;
	int rc = 0;
	union power_supply_propval val;
	int usb_present, batt_present, batt_health, batt_charge_type;

	CHG_DBG("%s: start\n", __func__);

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

//ASUS BSP allocate GPIO control +++
	gpio_ctrl = devm_kzalloc(&pdev->dev, sizeof(*gpio_ctrl), GFP_KERNEL);
	if (!gpio_ctrl)
		return -ENOMEM;
//ASUS BSP allocate GPIO control ---

	chg = &chip->chg;
	chg->dev = &pdev->dev;
	chg->param = v1_params;
	chg->debug_mask = &__debug_mask;
	chg->try_sink_enabled = &__try_sink_enabled;
	chg->weak_chg_icl_ua = &__weak_chg_icl_ua;
	chg->mode = PARALLEL_MASTER;
	chg->irq_info = smb2_irqs;
	chg->die_health = -EINVAL;
	chg->name = "PMI";
	chg->audio_headset_drp_wait_ms = &__audio_headset_drp_wait_ms;

	wakeup_source_init(&asus_chg_ws, "asus_chg_ws");
	wakeup_source_init(&asus_PTC_lock_ws, "asus_PTC_lock_ws");
	smbchg_dev = chg;			//ASUS BSP add globe device struct +++
	global_gpio = gpio_ctrl;	//ASUS BSP add gpio control struct +++

	chg->regmap = dev_get_regmap(chg->dev->parent, NULL);
	if (!chg->regmap) {
		pr_err("parent regmap is missing\n");
		return -EINVAL;
	}

	rc = smb2_chg_config_init(chip);
	if (rc < 0) {
		if (rc != -EPROBE_DEFER)
			pr_err("Couldn't setup chg_config rc=%d\n", rc);
		return rc;
	}

	rc = smb2_parse_dt(chip);
	if (rc < 0) {
		pr_err("Couldn't parse device tree rc=%d\n", rc);
		goto cleanup;
	}

	rc = smblib_init(chg);
	if (rc < 0) {
		pr_err("Smblib_init failed rc=%d\n", rc);
		goto cleanup;
	}

	/* set driver data before resources request it */
	platform_set_drvdata(pdev, chip);

//ASUS BSP : Add asus_workque +++
	INIT_DELAYED_WORK(&chg->asus_thermal_btm_work, asus_thermal_btm_work);
	INIT_DELAYED_WORK(&chg->asus_thermal_side_work, asus_thermal_side_work);
	//INIT_DELAYED_WORK(&chg->asus_water_proof_btm_work, asus_water_proof_btm_work);
	//INIT_DELAYED_WORK(&chg->asus_water_proof_side_work, asus_water_proof_side_work);
	INIT_DELAYED_WORK(&chg->asus_pogo_thermal_work, asus_pogo_thermal_work);
	/*if (g_ASUS_hwID >= ZS600KL_MP && g_ASUS_hwID < ZS600KL_UNKNOWN)
		INIT_DELAYED_WORK(&chg->asus_side_misinsertion_work, asus_side_misinsertion_work);*/

//ASUS BSP : Charger default PMIC settings +++
	asus_probe_pmic_settings(chg);

//ASUS BSP : Request charger GPIO +++
	asus_probe_gpio_setting(pdev, gpio_ctrl);

// ASUS BSP add a file for SMMI adb interface +++
	create_chargerIC_status_proc_file();
	create_water_en_proc_file();

//ASUS BSP add for ftm variable +++
	if (g_ftm_mode)
		charger_limit_enable_flag = 1;

//ASUS BSP charger : CHG_ATTRs +++
	rc = sysfs_create_group(&chg->dev->kobj, &asus_smblib_attr_group);
	if (rc)
		goto cleanup;
//ASUS BSP charger : CHG_ATTRs ---

	rc = smb2_init_vbus_regulator(chip);
	if (rc < 0) {
		pr_err("Couldn't initialize vbus regulator rc=%d\n",
			rc);
		goto cleanup;
	}

	rc = smb2_init_vconn_regulator(chip);
	if (rc < 0) {
		pr_err("Couldn't initialize vconn regulator rc=%d\n",
				rc);
		goto cleanup;
	}

	/* extcon registration */
	chg->extcon = devm_extcon_dev_allocate(chg->dev, smblib_extcon_cable);
	if (IS_ERR(chg->extcon)) {
		rc = PTR_ERR(chg->extcon);
		dev_err(chg->dev, "failed to allocate extcon device rc=%d\n",
				rc);
		goto cleanup;
	}

	rc = devm_extcon_dev_register(chg->dev, chg->extcon);
	if (rc < 0) {
		dev_err(chg->dev, "failed to register extcon device rc=%d\n",
				rc);
		goto cleanup;
	}

	/* ASUS BSP charger+++  asus extcon registration */
	if (g_interrupt_enable) {
		chg->thermal_extcon = extcon_dev_allocate(asus_extcon_cable);
		if (IS_ERR(chg->thermal_extcon)) {
			rc = PTR_ERR(chg->thermal_extcon);
			dev_err(chg->dev, "[BAT][CHG] failed to allocate ASUS thermal extcon device rc=%d\n",
					rc);
			goto cleanup;
		}
		chg->thermal_extcon->fnode_name = "usb_connector";

		rc = extcon_dev_register(chg->thermal_extcon);
		if (rc < 0) {
			dev_err(chg->dev, "[BAT][CHG] failed to register ASUS thermal extcon device rc=%d\n",
					rc);
			goto cleanup;
		}

		chg->water_extcon = extcon_dev_allocate(asus_extcon_cable);
		if (IS_ERR(chg->water_extcon)) {
			rc = PTR_ERR(chg->water_extcon);
			dev_err(chg->dev, "[BAT][CHG] failed to allocate ASUS water extcon device rc=%d\n",
					rc);
			goto cleanup;
		}
		chg->water_extcon->fnode_name = "vbus_liquid";

		rc = extcon_dev_register(chg->water_extcon);
		if (rc < 0) {
			dev_err(chg->dev, "[BAT][CHG] failed to register ASUS water extcon device rc=%d\n",
					rc);
			goto cleanup;
		}
	}

	chg->quickchg_extcon = extcon_dev_allocate(asus_extcon_cable);
	if (IS_ERR(chg->quickchg_extcon)) {
		rc = PTR_ERR(chg->quickchg_extcon);
		dev_err(chg->dev, "[BAT][CHG] failed to allocate ASUS quickchg extcon device rc=%d\n",
				rc);
		goto cleanup;
	}
	chg->quickchg_extcon->fnode_name = "quick_charging";

	rc = extcon_dev_register(chg->quickchg_extcon);
	if (rc < 0) {
		dev_err(chg->dev, "[BAT][CHG] failed to register ASUS quickchg extcon device rc=%d\n",
				rc);
		goto cleanup;
	} else {
		qc_stat_registed = true;
	}

	if (g_ASUS_hwID >= ZS600KL_MP && g_ASUS_hwID < ZS600KL_UNKNOWN) {
		chg->misinsertion_extcon = extcon_dev_allocate(asus_extcon_cable);
		if (IS_ERR(chg->misinsertion_extcon)) {
			rc = PTR_ERR(chg->misinsertion_extcon);
			dev_err(chg->dev, "[BAT][CHG] failed to allocate ASUS misinsertion extcon device rc=%d\n",
					rc);
			goto cleanup;
		}
		chg->misinsertion_extcon->fnode_name = "mis_insertion";

		rc = extcon_dev_register(chg->misinsertion_extcon);
		if (rc < 0) {
			dev_err(chg->dev, "[BAT][CHG] failed to register ASUS misinsertion extcon device rc=%d\n",
					rc);
			goto cleanup;
		}
	}
	/* ASUS BSP charger---  asus extcon registration */

	/* ASUS_BSP Deeo : register dongle_type +++ */
	chg->dongle_type = extcon_dev_allocate(asus_extcon_cable);
	if (IS_ERR(chg->dongle_type)) {
		rc = PTR_ERR(chg->dongle_type);
		dev_err(chg->dev, "[BAT][CHG] failed to allocate ASUS dongle_type device rc=%d\n", rc);
		goto cleanup;
	}
	chg->dongle_type->fnode_name = "dock";

	rc = extcon_dev_register(chg->dongle_type);
	if (rc < 0) {
		dev_err(chg->dev, "[BAT][CHG] failed to register ASUS dongle_type device rc=%d\n", rc);
	} else{
		extcon_dongle = chg->dongle_type;
	}
	/* ASUS_BSP Deeo : register dongle_type --- */

	/* Register Thermal Alert Gpio Pin in COS Mode +++ */
	if (g_Charger_mode && g_interrupt_enable) {
		schedule_delayed_work(&smbchg_dev->asus_thermal_btm_work, msecs_to_jiffies(5000));
		schedule_delayed_work(&smbchg_dev->asus_thermal_side_work, msecs_to_jiffies(5100));
	}
	/* Register Thermal Alert Gpio Pin in COS Mode --- */

	//[+++]WA for BTM_500mA issue
	if (!gpio_get_value_cansleep(global_gpio->BTM_OVP_ACOK))
		boot_w_btm_plugin = 1;
	//[---]WA for BTM_500mA issue
	rc = smb2_init_hw(chip);
	if (rc < 0) {
		pr_err("Couldn't initialize hardware rc=%d\n", rc);
		goto cleanup;
	}

	rc = smb2_init_dc_psy(chip);
	if (rc < 0) {
		pr_err("Couldn't initialize dc psy rc=%d\n", rc);
		goto cleanup;
	}

	rc = smb2_init_usb_psy(chip);
	if (rc < 0) {
		pr_err("Couldn't initialize usb psy rc=%d\n", rc);
		goto cleanup;
	}

	rc = smb2_init_usb_main_psy(chip);
	if (rc < 0) {
		pr_err("Couldn't initialize usb main psy rc=%d\n", rc);
		goto cleanup;
	}

	rc = smb2_init_usb_port_psy(chip);
	if (rc < 0) {
		pr_err("Couldn't initialize usb pc_port psy rc=%d\n", rc);
		goto cleanup;
	}

	rc = smb2_init_batt_psy(chip);
	if (rc < 0) {
		pr_err("Couldn't initialize batt psy rc=%d\n", rc);
		goto cleanup;
	}

	rc = smb2_determine_initial_status(chip);
	if (rc < 0) {
		pr_err("Couldn't determine initial status rc=%d\n",
			rc);
		goto cleanup;
	}

	rc = smb2_request_interrupts(chip);
	if (rc < 0) {
		pr_err("Couldn't request interrupts rc=%d\n", rc);
		goto cleanup;
	}

	rc = smb2_post_init(chip);
	if (rc < 0) {
		pr_err("Failed in post init rc=%d\n", rc);
		goto cleanup;
	}

	smb2_create_debugfs(chip);

	rc = smblib_get_prop_usb_present(chg, &val);
	if (rc < 0) {
		pr_err("Couldn't get usb present rc=%d\n", rc);
		goto cleanup;
	}
	usb_present = val.intval;

	rc = smblib_get_prop_batt_present(chg, &val);
	if (rc < 0) {
		pr_err("Couldn't get batt present rc=%d\n", rc);
		goto cleanup;
	}
	batt_present = val.intval;

	rc = smblib_get_prop_batt_health(chg, &val);
	if (rc < 0) {
		pr_err("Couldn't get batt health rc=%d\n", rc);
		val.intval = POWER_SUPPLY_HEALTH_UNKNOWN;
	}
	batt_health = val.intval;

	rc = smblib_get_prop_batt_charge_type(chg, &val);
	if (rc < 0) {
		pr_err("Couldn't get batt charge type rc=%d\n", rc);
		goto cleanup;
	}
	batt_charge_type = val.intval;

	device_init_wakeup(chg->dev, true);

	//[+++] Add for station update when screen on
	smb2_drm_nb.notifier_call = smb2_drm_notifier;
	msm_drm_register_client(&smb2_drm_nb);
	//[---] Add for station update when screen on

	pr_info("QPNP SMB2 probed successfully usb:present=%d type=%d batt:present = %d health = %d charge = %d\n",
		usb_present, chg->real_charger_type,
		batt_present, batt_health, batt_charge_type);
	return rc;

cleanup:
	smb2_free_interrupts(chg);
	if (chg->batt_psy)
		power_supply_unregister(chg->batt_psy);
	if (chg->usb_main_psy)
		power_supply_unregister(chg->usb_main_psy);
	if (chg->usb_psy)
		power_supply_unregister(chg->usb_psy);
	if (chg->usb_port_psy)
		power_supply_unregister(chg->usb_port_psy);
	if (chg->dc_psy)
		power_supply_unregister(chg->dc_psy);
	if (chg->vconn_vreg && chg->vconn_vreg->rdev)
		devm_regulator_unregister(chg->dev, chg->vconn_vreg->rdev);
	if (chg->vbus_vreg && chg->vbus_vreg->rdev)
		devm_regulator_unregister(chg->dev, chg->vbus_vreg->rdev);

	smblib_deinit(chg);

	platform_set_drvdata(pdev, NULL);
//ASUS BSP charger : CHG_ATTRs +++
	sysfs_remove_group(&chg->dev->kobj, &asus_smblib_attr_group);
//ASUS BSP charger : CHG_ATTRs ---
	return rc;
}

//ASUS BSP charger : Add suspend/resume function +++
#define JEITA_MINIMUM_INTERVAL (30)
static int smb2_resume(struct device *dev)
{
	struct timespec mtNow;
	int nextJEITAinterval;
	int rc;
	if (is_asp1690e_off) {
		rc = gpio_direction_output(global_gpio->PW_ADC_EN, 0);
		if (rc) {
			CHG_DBG_E("%s: Failed to enable asp1690e power when resume\n", __func__);
		} else {
			is_asp1690e_off = 0;
			CHG_DBG("%s: Enable asp1690e power when resume\n", __func__);
			asus_asp1690e_resume_setting();
		}
	}
	if (rerun_pogo_det == 1) {
		CHG_DBG("%s. There is i2c error in previous pogo detction\n", __func__);
		rerun_pogo_det = 0;
		rerun_pogo_detect_func();
	}
	if (!asus_get_prop_usb_present(smbchg_dev)) {
		return 0;
	}

	if (!asus_flow_done_flag)
		return 0;

	asus_smblib_stay_awake(smbchg_dev);
	mtNow = current_kernel_time();

	/*BSP Austin_Tseng: if next JEITA time less than 30s, do JEITA
			(next JEITA time = last JEITA time + 60s)*/
	nextJEITAinterval = 60 - (mtNow.tv_sec - last_jeita_time.tv_sec);
	CHG_DBG("%s: nextJEITAinterval = %d\n", __func__, nextJEITAinterval);
	if (nextJEITAinterval <= JEITA_MINIMUM_INTERVAL) {
		cancel_delayed_work(&smbchg_dev->asus_min_monitor_work);
		cancel_delayed_work(&smbchg_dev->asus_cable_check_work);
		cancel_delayed_work(&smbchg_dev->asus_batt_RTC_work);
		schedule_delayed_work(&smbchg_dev->asus_min_monitor_work, 0);
	} else {
		smblib_asus_monitor_start(smbchg_dev, nextJEITAinterval * 1000);
		asus_smblib_relax(smbchg_dev);
	}
	return 0;
}

static int smb2_suspend(struct device *dev)
{
	int rc;
	if (!asus_get_prop_usb_present(smbchg_dev) && ASUS_POGO_ID == NO_INSERT) {
		rc = gpio_direction_output(global_gpio->PW_ADC_EN, 1);
		if (rc)
			CHG_DBG_E("%s: Failed to disable asp1690e power when suspend\n", __func__);
		else {
			is_asp1690e_off = 1;
			CHG_DBG("%s: Disable asp1690e power when suspend\n", __func__);
		}
	}
	
	return 0;
}
//ASUS BSP charger : Add suspend/resume function +++

static int smb2_remove(struct platform_device *pdev)
{
	struct smb2 *chip = platform_get_drvdata(pdev);
	struct smb_charger *chg = &chip->chg;

	power_supply_unregister(chg->batt_psy);
	power_supply_unregister(chg->usb_psy);
	power_supply_unregister(chg->usb_port_psy);
	regulator_unregister(chg->vconn_vreg->rdev);
	regulator_unregister(chg->vbus_vreg->rdev);

	platform_set_drvdata(pdev, NULL);
	return 0;
}

static void smb2_shutdown(struct platform_device *pdev)
{
	struct smb2 *chip = platform_get_drvdata(pdev);
	struct smb_charger *chg = &chip->chg;

	/* disable all interrupts */
	smb2_disable_interrupts(chg);

	/* configure power role for UFP */
	smblib_masked_write(chg, TYPE_C_INTRPT_ENB_SOFTWARE_CTRL_REG,
				TYPEC_POWER_ROLE_CMD_MASK, UFP_EN_CMD_BIT);

	/* force HVDCP to 5V */
	smblib_masked_write(chg, USBIN_OPTIONS_1_CFG_REG,
				HVDCP_AUTONOMOUS_MODE_EN_CFG_BIT, 0);
	smblib_write(chg, CMD_HVDCP_2_REG, FORCE_5V_BIT);

	/* force enable APSD */
	smblib_masked_write(chg, USBIN_OPTIONS_1_CFG_REG,
				 AUTO_SRC_DETECT_BIT, AUTO_SRC_DETECT_BIT);
}

static const struct dev_pm_ops smb2_pm_ops = {
	.suspend	= smb2_suspend,
	.resume		= smb2_resume,
};

static const struct of_device_id match_table[] = {
	{ .compatible = "qcom,qpnp-smb2", },
	{ },
};

static struct platform_driver smb2_driver = {
	.driver		= {
		.name		= "qcom,qpnp-smb2",
		.owner		= THIS_MODULE,
		.of_match_table	= match_table,
		.pm			= &smb2_pm_ops,
	},
	.probe		= smb2_probe,
	.remove		= smb2_remove,
	.shutdown	= smb2_shutdown,
};
module_platform_driver(smb2_driver);

MODULE_DESCRIPTION("QPNP SMB2 Charger Driver");
MODULE_LICENSE("GPL v2");
