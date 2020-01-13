#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/of_platform.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/printk.h>
#include <linux/delay.h>
#include "iris3_i2c.h"
#include <linux/workqueue.h>
#include <linux/slab.h>

#define PX_RST_N_SHIFT   0x30
#define LCD_RESET_N      0x1D
#define IRIS3_SUSPEND    0
#define IRIS3_RESUME     1

extern bool g_enter_AOD;
extern int dp_display_commit_cnt; 

extern int hid_to_enable_mipi(u8 enable);
extern int hid_to_get_reset_mipi_status(int *status);
extern int hid_to_gpio_set(u8 gpio, u8 value);
extern int hid_to_gpio_get(u8 gpio);
extern void dsi_display_cb_register(
            void (*set_stn_bl)(int),
            void (*set_stn_init_bl)(void),
            void (*stn_suspend)(void),
            void (*stn_resume)(void),
            void (*set_stn_hbm)(int),
            void (*set_power_off)(void));
extern int get_last_backlight_value(void);
extern bool get_dp_status(void);
extern int wait4dp(void);
//extern u8 hid_to_check_init_state(void);
extern uint8_t gDongleType;
extern u8 gEC_init;
extern void iris3_i2c_cb_register(int (*iris3_i2c_read_cb)(u32, u32*), int (*iris3_i2c_write_cb)(u32, u32), int (*iris3_i2c_burst_write_cb)(u32, u32*, u16));
extern int iris3_update_gamestation_fw(int);
extern int iris3_i2c_read_panel_data( u32 reg_addr, u32 size);

static struct i2c_client *iris3_i2c_handle = NULL;
static struct addr_val dbg_readback;

static int bl_level = 1008;
static int iris3_init_finish = 0;
static int iris3_init_bl_finish = 0;
static int iris3_plugin_init_finish = 0;
static int iris3_stat = IRIS3_SUSPEND;
static int station_attached = 0;
static int stn_cancel_resume = 0;
static int last_hbm_setting = 0;
int iris3_stn_update_fw_complete = 0;

extern int hid_suspend_vote(int);
extern int hid_vote_register(char *);
extern int hid_vote_unregister(int, char *);

int check_EC_status(void) {

    //int count = 0;
    //u8 EC_init_stat;

    if(gDongleType != 2)
        pr_err("[iris3] %s: Dongle Type %d is not expected\n", __func__, gDongleType);
    else {
        /*EC_init_stat = hid_to_check_init_state();
        while(EC_init_stat != 1 && count < 5) {
            pr_err("[iris3] %s: EC not initialize yet(%d), count = %d\n", __func__, EC_init_stat, count);
            msleep(50);
            EC_init_stat = hid_to_check_init_state();
            count++;
        }

        return EC_init_stat;*/
        return 1;
    }

    return 0;
}

int remap_bl_value(int level) {
    return level*16;
}

static bool i2c_dongle_break = false;

static int iris3_i2c_cmd_four_write(struct addr_val *val)
{
	int ret = -1;
	const int reg_len = 9;
	const uint8_t cmd = 0xcc;
	uint8_t slave_addr = 0;
	uint8_t iris3_payload[9] = {0,};
	struct i2c_msg msgs;
	struct i2c_client * client = iris3_i2c_handle;
    int i = 0;

	slave_addr = (client->addr) & 0xff;

	memset(&msgs, 0x00, sizeof(msgs));

	iris3_payload[0] = cmd;
	iris3_payload[1] = (val->addr & 0xff);
	iris3_payload[2] = ((val->addr >> 8) & 0xff);
	iris3_payload[3] = ((val->addr >> 16) & 0xff);
	iris3_payload[4] = ((val->addr >> 24) & 0xff);
	iris3_payload[5] = ((val->data >> 24) &0xff);
	iris3_payload[6] = ((val->data >> 16) &0xff);
	iris3_payload[7] = ((val->data >> 8) &0xff);
	iris3_payload[8] = (val->data & 0xff);

	msgs.addr = slave_addr;
	msgs.flags = 0;
	msgs.buf = iris3_payload;
	msgs.len = reg_len;

	if (i2c_dongle_break && gDongleType != 2) {
		pr_err("[iris3] station does not exit, abort i2c write process, i2c_dongle_break=%d, gDongleType=%d\n", i2c_dongle_break, gDongleType);
		ret = -EIO;
	} else {
		while (i++ < 5) {
			if(gDongleType == 2) {
				ret = i2c_transfer(client->adapter, &msgs, 1);
				if(ret == 1) {
					ret = 0;
				} else {
					ret = ret < 0 ? ret : -EIO;
					pr_err("[iris3] %s: i2c_transfer failed, ret=%d\n", __func__, ret);
				}
				i2c_dongle_break = false;
				break;
			} else {
				pr_err("[iris3] POGO != STATION, retry iris i2c write\n");
				msleep(100);
				ret = -EIO;
				i2c_dongle_break = true;
			}
		}
	}

	return ret;
}

static int iris3_i2c_cmd_four_read(struct addr_val * val)
{
	int ret = -1;
	const int reg_len = 5;
	const int ret_len = 4;
	const uint8_t cmd = 0xcc;
	uint8_t slave_addr = 0;
	uint8_t iris3_payload[5] = {0,};
	uint8_t readBackData[4] = {0,};
	struct i2c_msg msgs[2];
	struct i2c_client * client = iris3_i2c_handle;
    int i = 0;

	slave_addr = (client->addr & 0xff);
	memset(msgs, 0x00, sizeof(msgs));

	iris3_payload[0] = cmd;
	iris3_payload[1] = (val->addr & 0xff);
	iris3_payload[2] = ((val->addr >> 8) & 0xff);
	iris3_payload[3] = ((val->addr >> 16) & 0xff);
	iris3_payload[4] = ((val->addr >> 24) & 0xff);

	msgs[0].addr = slave_addr;
	msgs[0].flags = 0;
	msgs[0].buf = iris3_payload;
	msgs[0].len = reg_len;

	msgs[1].addr = slave_addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].buf = readBackData;
	msgs[1].len = ret_len;

	if (i2c_dongle_break && gDongleType != 2) {
		pr_err("[iris3] station does not exit, abort i2c read process, i2c_dongle_break=%d, gDongleType=%d\n", i2c_dongle_break, gDongleType);
		ret = -EIO;
	} else {
		while (i++ < 5) {
			if(gDongleType == 2) {
				ret = i2c_transfer(client->adapter, msgs, 2);
				if(ret == 2) {
					ret = 0;
					//pr_err("%s: readBackData=%x %x %x %x\n", __func__, readBackData[0], readBackData[1], readBackData[2], readBackData[3]);
					val->data = readBackData[0] << 24 |
								readBackData[1] << 16 |
								readBackData[2] << 8 |
								readBackData[3];
				} else {
					ret = ret < 0 ? ret : -EIO;
					pr_err("[iris3] %s: i2c_transfer failed, ret=%d\n", __func__, ret);
				}
				i2c_dongle_break = false;
				break;
			} else {
				pr_err("[iris3] POGO != STATION, retry iris i2c read\n");
				msleep(100);
				ret = -EIO;
				i2c_dongle_break = true;
			}
		}
	}

	return ret;
}

int iris3_i2c_burst_write(u32 start_addr, u32 *lut_buffer, u16 reg_num)
{
	int ret = -1;
	const uint8_t cmd = 0xfc;
	u16 msg_len = 0;
	u8 slave_addr = 0;
	u8 *iris3_payload = NULL;
	struct i2c_msg msgs;
	struct i2c_client * client = iris3_i2c_handle;
	int i;

	if (NULL == client || NULL == client->adapter || 0x0 == client->addr)
		return -1;

	slave_addr = (client->addr) & 0xff;

	memset(&msgs, 0x00, sizeof(msgs));

	msg_len = 5 + reg_num * 4;
	if (NULL == iris3_payload) {
		iris3_payload = kmalloc(sizeof(u8) * msg_len, GFP_KERNEL);
		if (NULL == iris3_payload) {
			pr_err("[iris3] %s: allocate memory fails\n", __func__);
			return -1;
		}
	}

	iris3_payload[0] = cmd;
	iris3_payload[1] = (start_addr & 0xff);
	iris3_payload[2] = ((start_addr >> 8) & 0xff);
	iris3_payload[3] = ((start_addr >> 16) & 0xff);
	iris3_payload[4] = ((start_addr >> 24) & 0xff);

	for (i = 0; i < reg_num; i++) {
		iris3_payload[i*4 + 5] = ((lut_buffer[i] >> 24) &0xff);
		iris3_payload[i*4 + 6] = ((lut_buffer[i] >> 16) &0xff);
		iris3_payload[i*4 + 7] = ((lut_buffer[i] >> 8) &0xff);
		iris3_payload[i*4 + 8] = (lut_buffer[i] & 0xff);
	}

	msgs.addr = slave_addr;
	msgs.flags = 0;
	msgs.buf = iris3_payload;
	msgs.len = msg_len;

	if (i2c_dongle_break && gDongleType != 2) {
		pr_err("[iris3] station does not exit, abort i2c burst write process, i2c_dongle_break=%d, gDongleType=%d\n", i2c_dongle_break, gDongleType);
		ret = -EIO;
	} else {
        i = 0;
		while (i++ < 5) {
			if (NULL == client->adapter) {
				ret = -1;
				break;
			}

			if(gDongleType == 2) {
				ret = i2c_transfer(client->adapter, &msgs, 1);
				if(ret == 1) {
					ret = 0;
				} else {
					ret = ret < 0 ? ret : -EIO;
					pr_err("[iris3] %s: i2c_transfer failed, ret=%d\n", __func__, ret);
				}
				i2c_dongle_break = false;
				break;
			} else {
				pr_err("[iris3] POGO != STATION, retry iris i2c burst write\n");
				msleep(100);
				ret = -EIO;
				i2c_dongle_break = true;
			}
		}
	}

	kfree(iris3_payload);
	return ret;
}

int iris3_i2c_read_cmd(u32 reg_addr, u32 *reg_val)
{
	struct addr_val readPacket;

	readPacket.addr = reg_addr & 0xffffffff;

	if(iris3_i2c_cmd_four_read(&readPacket) < 0)
		return -1;

	*reg_val = readPacket.data;

	return 0;
}

int iris3_i2c_write_cmd(u32 reg_addr, u32 reg_val)
{
	struct addr_val writePacket;

	writePacket.addr = reg_addr & 0xffffffff;
	writePacket.data = reg_val & 0xffffffff;

	if(iris3_i2c_cmd_four_write(&writePacket) < 0)
		return -1;

	return 0;
}

int iris3_i2c_send_cmd_list(int *cmdlist, int len)
{
	int i = 0;
    int retry = 0;
    int ret = -1;
	bool cmd_delay = false;
    struct addr_val writePacket;

    //pr_err("[iris3] %s: lens of cmdlist = %d\n", __func__, len);

	for(i = 0; i < len; i++) {
		if((i % 2) == 0) {
			if(cmdlist[i] == 0xffffffff)
			    cmd_delay = true;
			else
		        writePacket.addr = cmdlist[i] & 0xffffffff;
	    }else{
			if(cmd_delay) {
				pr_debug("[iris3] %s: command delay = %d\n", __func__, cmdlist[i]);
			    msleep(cmdlist[i]);
			    cmd_delay = false;
			} else {
		        writePacket.data = cmdlist[i] & 0xffffffff;
				//pr_err("[iris3] %s: addr=0x%08x data=0x%08x\n", __func__,writePacket.addr , writePacket.data);

                //return if station detached
                if(!check_EC_status())
                    return -1;

                retry = 0;
		        while((ret = iris3_i2c_cmd_four_write(&writePacket)) != 0 && retry < 3) {
                    pr_err("[iris3] %s: failed to send cmdlist[%d], ret=%d, len=%d, retry=%d\n",
                            __func__, i, ret, len, retry);
                    msleep(10);
                    retry++;
                }

                if (ret < 0) {
					pr_err("[iris3] i2c error, abort i2c process\n");
					break;
				}
		    }
	    }
	}

	return ret;
}

int iris3_i2c_send_cmd_hbm(struct iris3_i2c_data *data, int enable)
{
    int lenList;
    int rc = 0;

    mutex_lock(&data->access_mutex);

    if(enable) {
        lenList = sizeof(panel_hbm_on_cmd)/sizeof(panel_hbm_on_cmd[0]);
        rc = iris3_i2c_send_cmd_list(panel_hbm_on_cmd, lenList);
    } else {
        lenList = sizeof(panel_hbm_off_cmd)/sizeof(panel_hbm_off_cmd[0]);
        rc = iris3_i2c_send_cmd_list(panel_hbm_off_cmd, lenList);
    }

    if(rc == 0) {
        pr_err("[iris3] %s: set HBM %s\n", __func__, enable?"on":"off");
        last_hbm_setting = enable;
    } else
        pr_err("[iris3] %s: set HBM %s failed(%d)\n", __func__, enable?"on":"off", rc);

    mutex_unlock(&data->access_mutex);
    return rc;
}

int iris3_i2c_send_cmd_update_bl(struct iris3_i2c_data *data, int level)
{
    int lenList;
    int bl_upper_val, bl_lower_val;
    int rc = 0;

    bl_upper_val = (level & 0x000000ff) << 16;
    bl_lower_val = level & 0x0000ff00;
    set_bl_cmd[3] = 0x00000051 | bl_upper_val | bl_lower_val;

    mutex_lock(&data->access_mutex);

    lenList = sizeof(set_bl_cmd)/sizeof(set_bl_cmd[0]);
    if((rc = iris3_i2c_send_cmd_list(set_bl_cmd, lenList)) != 0)
        pr_err("[iris3] %s: set bl failed(%d)\n", __func__, level);
    else
        pr_err("[iris3] %s: set bl=%d\n", __func__, level);

    mutex_unlock(&data->access_mutex);
    return rc;
}

int iris3_i2c_send_cmd_peaking2d(struct iris3_i2c_data *data, int level)
{
    int lenList;
    int rc = 0;

    mutex_lock(&data->access_mutex);
    pr_err("[iris3] %s: level = %d\n", __func__, level);

    lenList = sizeof(peaking2d_table[level])/sizeof(peaking2d_table[level][0]);
    if((rc = iris3_i2c_send_cmd_list(peaking2d_table[level], lenList)) != 0)
        goto unlock;

    lenList = sizeof(peaking2d_update)/sizeof(peaking2d_update[0]);
    if((rc = iris3_i2c_send_cmd_list(peaking2d_update, lenList)) != 0)
        goto unlock;

unlock:
    mutex_unlock(&data->access_mutex);
    return rc;
}

int iris3_i2c_send_cmd_lce(struct iris3_i2c_data *data, int mode, int level)
{
    int lenList;
    int rc = 0;

    mutex_lock(&data->access_mutex);
    pr_err("[iris3] %s: %s mode, level: %d\n", __func__,
            mode?"video":"graphic", level);

    if(mode == 0) {
        lenList = sizeof(lce_table[level])/sizeof(lce_table[level][0]);
        if((rc = iris3_i2c_send_cmd_list(lce_table[level], lenList)) != 0)
            goto unlock;

        lenList = sizeof(lce_update)/sizeof(lce_update[0]);
        if((rc = iris3_i2c_send_cmd_list(lce_update, lenList)) !=0)
            goto unlock;
    } else {
        lenList = sizeof(lce_table[level+6])/sizeof(lce_table[level+6][0]);
        if((rc = iris3_i2c_send_cmd_list(lce_table[level+6], lenList)) !=0)
            goto unlock;

        lenList = sizeof(lce_update)/sizeof(lce_update[0]);
        if((rc = iris3_i2c_send_cmd_list(lce_update, lenList)) !=0)
            goto unlock;
    }

unlock:
    mutex_unlock(&data->access_mutex);
    return rc;
}

int iris3_i2c_send_cmd_abypass(struct iris3_i2c_data *data)
{
    int lenList;
    int rc = 0;

    pr_err("[iris3] %s\n", __func__);
    mutex_lock(&data->access_mutex);

    lenList = sizeof(cmd_abypass1)/sizeof(cmd_abypass1[0]);
    if((rc = iris3_i2c_send_cmd_list(cmd_abypass1, lenList)) != 0)
        goto unlock;

    msleep(100);

    //init panel
    lenList = sizeof(panel_init_cmd)/sizeof(panel_init_cmd[0]);
    if((rc = iris3_i2c_send_cmd_list(panel_init_cmd, lenList)) != 0)
        goto unlock;

    msleep(100);

    lenList = sizeof(cmd_abypass2)/sizeof(cmd_abypass2[0]);
    if((rc = iris3_i2c_send_cmd_list(cmd_abypass2, lenList)) != 0)
        goto unlock;

unlock:
    mutex_unlock(&data->access_mutex);
    return rc;
}

int iris3_i2c_send_cmd_poweroff(struct iris3_i2c_data *data)
{
    int lenList;
    int rc = 0;

    mutex_lock(&data->access_mutex);
    pr_err("[iris3] %s ++\n", __func__);

    lenList = sizeof(panel_off_cmd)/sizeof(panel_off_cmd[0]);
    if((rc = iris3_i2c_send_cmd_list(panel_off_cmd, lenList)) != 0)
        goto unlock;

    pr_err("[iris3] %s --\n", __func__);

unlock:
    mutex_unlock(&data->access_mutex);
    return rc;
}

int iris3_i2c_send_cmd_lightup(struct iris3_i2c_data *data)
{
	int lenList;
    int rc = 0;

    mutex_lock(&data->access_mutex);
    pr_err("[iris3] %s ++\n", __func__);

    //sys: a1: init 0
    lenList = sizeof(sys_a1_init_0)/sizeof(sys_a1_init_0[0]);
	if((rc = iris3_i2c_send_cmd_list(sys_a1_init_0, lenList)) != 0)
        goto unlock;

    //sys: a1: init 1
    lenList = sizeof(sys_a1_init_1)/sizeof(sys_a1_init_1[0]);
    if((rc = iris3_i2c_send_cmd_list(sys_a1_init_1, lenList)) != 0)
        goto unlock;

	msleep(10);

    //sys: a1: init 2
    lenList = sizeof(sys_a1_init_2)/sizeof(sys_a1_init_2[0]);
    if((rc = iris3_i2c_send_cmd_list(sys_a1_init_2, lenList)) != 0)
        goto unlock;

	msleep(10);

    //sys: a1: init 3
    lenList = sizeof(sys_a1_init_3)/sizeof(sys_a1_init_3[0]);
    if((rc = iris3_i2c_send_cmd_list(sys_a1_init_3, lenList)) != 0)
        goto unlock;

    //sys: a1: init 4
    lenList = sizeof(sys_a1_init_4)/sizeof(sys_a1_init_4[0]);
    if((rc = iris3_i2c_send_cmd_list(sys_a1_init_4, lenList)) != 0)
        goto unlock;

    //sys: a1: init 5
    lenList = sizeof(sys_a1_init_5)/sizeof(sys_a1_init_5[0]);
    if((rc = iris3_i2c_send_cmd_list(sys_a1_init_5, lenList)) != 0)
        goto unlock;

    //sys: a1: init 6
    lenList = sizeof(sys_a1_init_6)/sizeof(sys_a1_init_6[0]);
    if((rc = iris3_i2c_send_cmd_list(sys_a1_init_6, lenList)) != 0)
        goto unlock;

    //sys: lp: sys_misc
    lenList = sizeof(sys_misc)/sizeof(sys_misc[0]);
    if((rc = iris3_i2c_send_cmd_list(sys_misc, lenList)) != 0)
        goto unlock;

    //sys: lp: evs_dly
    lenList = sizeof(sys_lp_evs_dly)/sizeof(sys_lp_evs_dly[0]);
    if((rc = iris3_i2c_send_cmd_list(sys_lp_evs_dly, lenList)) != 0)
        goto unlock;

    //mipi_rx_ctrl
    lenList = sizeof(mipi_rx_ctrl)/sizeof(mipi_rx_ctrl[0]);
    if((rc = iris3_i2c_send_cmd_list(mipi_rx_ctrl, lenList)) != 0)
        goto unlock;

    //mipi_rx_dphy
    lenList = sizeof(mipi_rx_dphy)/sizeof(mipi_rx_dphy[0]);
    if((rc = iris3_i2c_send_cmd_list(mipi_rx_dphy, lenList)) != 0)
        goto unlock;

    //mipi_tx_dphy
    lenList = sizeof(mipi_tx_dphy)/sizeof(mipi_tx_dphy[0]);
    if((rc = iris3_i2c_send_cmd_list(mipi_tx_dphy, lenList)) != 0)
        goto unlock;

    //mipi_tx_init
    lenList = sizeof(mipi_tx_init)/sizeof(mipi_tx_init[0]);
    if((rc = iris3_i2c_send_cmd_list(mipi_tx_init, lenList)) != 0)
        goto unlock;

    //mipi_tx_lp_evs_dly
    lenList = sizeof(mipi_tx_lp_evs_dly)/sizeof(mipi_tx_lp_evs_dly[0]);
    if((rc = iris3_i2c_send_cmd_list(mipi_tx_lp_evs_dly, lenList)) != 0)
        goto unlock;

    //mipi_rx_lpcd
    lenList = sizeof(mipi_rx_lpcd)/sizeof(mipi_rx_lpcd[0]);
    if((rc = iris3_i2c_send_cmd_list(mipi_rx_lpcd, lenList)) != 0)
        goto unlock;

    //dport_init
    lenList = sizeof(dport_init)/sizeof(dport_init[0]);
    if((rc = iris3_i2c_send_cmd_list(dport_init, lenList)) != 0)
        goto unlock;

    //dport_update
    lenList = sizeof(dport_update)/sizeof(dport_update[0]);
    if((rc = iris3_i2c_send_cmd_list(dport_update, lenList)) != 0)
        goto unlock;

    //peaking2d_init
    lenList = sizeof(peaking2d_init)/sizeof(peaking2d_init[0]);
    if((rc = iris3_i2c_send_cmd_list(peaking2d_init, lenList)) != 0)
        goto unlock;

    //peaking2d_update
    lenList = sizeof(peaking2d_update)/sizeof(peaking2d_update[0]);
    if((rc = iris3_i2c_send_cmd_list(peaking2d_update, lenList)) != 0)
        goto unlock;

    //dpp_init
    lenList = sizeof(dpp_init)/sizeof(dpp_init[0]);
    if((rc = iris3_i2c_send_cmd_list(dpp_init, lenList)) != 0)
        goto unlock;

    //dpp_update
    lenList = sizeof(dpp_update)/sizeof(dpp_update[0]);
    if((rc = iris3_i2c_send_cmd_list(dpp_update, lenList)) != 0)
        goto unlock;

    //cm_init
    lenList = sizeof(cm_init)/sizeof(cm_init[0]);
    if((rc = iris3_i2c_send_cmd_list(cm_init, lenList)) != 0)
        goto unlock;

    // cm_init_csc
    lenList = sizeof(cm_init_csc)/sizeof(cm_init_csc[0]);
    if((rc = iris3_i2c_send_cmd_list(cm_init_csc, lenList)) != 0)
        goto unlock;

    //lce_gra_det_0
    lenList = sizeof(lce_gra_det_0)/sizeof(lce_gra_det_0[0]);
    if((rc = iris3_i2c_send_cmd_list(lce_gra_det_0, lenList)) != 0)
        goto unlock;

    //lce_init
    lenList = sizeof(lce_init)/sizeof(lce_init[0]);
    if((rc = iris3_i2c_send_cmd_list(lce_init, lenList)) != 0)
        goto unlock;

    //lce_update
    lenList = sizeof(lce_update)/sizeof(lce_update[0]);
    if((rc = iris3_i2c_send_cmd_list(lce_update, lenList)) != 0)
        goto unlock;

    //dtg_init
    lenList = sizeof(dtg_init)/sizeof(dtg_init[0]);
    if((rc = iris3_i2c_send_cmd_list(dtg_init, lenList)) != 0)
        goto unlock;

    //dtg_lp_evs_dly
    lenList = sizeof(dtg_lp_evs_dly)/sizeof(dtg_lp_evs_dly[0]);
    if((rc =iris3_i2c_send_cmd_list(dtg_lp_evs_dly, lenList)) != 0)
        goto unlock;

    msleep(20);

    //init panel
    lenList = sizeof(panel_init_cmd)/sizeof(panel_init_cmd[0]);
    if((rc = iris3_i2c_send_cmd_list(panel_init_cmd, lenList)) != 0)
        goto unlock;

    msleep(20);

    //init pwil_ctrl_0
    lenList = sizeof(pwil_ctrl_0)/sizeof(pwil_ctrl_0[0]);
    if((rc = iris3_i2c_send_cmd_list(pwil_ctrl_0, lenList)) != 0)
        goto unlock;

    //init pwil_timeout
    lenList = sizeof(pwil_timeout)/sizeof(pwil_timeout[0]);
    if((rc = iris3_i2c_send_cmd_list(pwil_timeout, lenList)) != 0)
        goto unlock;

    //init pwil_ctrl_3_csc
    lenList = sizeof(pwil_ctrl_3_csc)/sizeof(pwil_ctrl_3_csc[0]);
    if((rc = iris3_i2c_send_cmd_list(pwil_ctrl_3_csc, lenList)) != 0)
        goto unlock;

    //pwil_update
    lenList = sizeof(pwil_update)/sizeof(pwil_update[0]);
    if((rc = iris3_i2c_send_cmd_list(pwil_update, lenList)) != 0)
        goto unlock;

    pr_err("[iris3] %s --\n", __func__);

unlock:
    mutex_unlock(&data->access_mutex);
    return rc;
}

static int iris3_i2c_read_chip_id(struct iris3_i2c_data *data)
{
    struct addr_val readChipID;

    readChipID.addr = 0xf001fff8;
    iris3_i2c_cmd_four_read(&readChipID);

    pr_err("[iris3] chipid 0x%x: 0x%x\n", readChipID.addr, readChipID.data);
    data->chipid = readChipID.data;

    return 0;
}

extern int asus_wait4hid(void);
static int wait4hid(void)
{
    int ret = 0;
    if (asus_wait4hid() < 0) {
        pr_err("[iris3] wait for HID timeout\n");
        ret = -1;
    }
    if(gDongleType != 2) {
        pr_err("[iris3] gDongleType=%d, abort power on sequence\n", gDongleType);
        ret = -2;
    }

    return ret;
}

static int power_on_gpio(void)
{
    int retryCount = 3;
    int resetmipiCount = 5, test = 5;
    int ret = 0;

    pr_err("[iris3] ++power_on_gpio\n");

    while ((retryCount--) > 0) {
        //wait for HID ready
        ret = wait4hid();
        if(ret == -1)
            continue;
        else if(ret == -2)
            break;

        pr_err("[iris3] Disable mipi, put mipi into LP11 state\n");
        ret = hid_to_enable_mipi(0);
        if(ret < 0) {
            pr_err("[iris3] set hid_to_enable_mipi disable failed\n");
            continue;
        }
        msleep(10);

        //sync with EC
        hid_to_get_reset_mipi_status(&test);
        if (test != 0) {
            while ((resetmipiCount--) > 0) {
                pr_err("[iris3] disable mipi AGAIN, put mipi into LP11 state (Count:%d)\n", resetmipiCount);
                ret = hid_to_enable_mipi(0);                
                msleep(10);
                hid_to_get_reset_mipi_status(&test);
                if (!test) {
                    pr_err("[iris3] mipi in LP11 state (Count: %d)\n", resetmipiCount);
                    break;
                } else {
                    pr_err("[iris3] mipi NOT in lp11 state (Count: %d)\n", resetmipiCount);
                }
            }
        } else {
            pr_err("[iris3] Put mipi to LP11 state successfully at 1st time\n");
        }
        //Do it twice to make sure mipi signal is stop
        //ret = hid_to_enable_mipi(0);
        //if(ret < 0) {
        //    pr_err("[iris3] set hid_to_enable_mipi disable failed\n");
        //    continue;
        //}

        pr_err("[iris3] reset LCD\n");
        ret = hid_to_gpio_set(LCD_RESET_N, 1);
        if(ret < 0) {
            pr_err("[iris3] assert LCD_RESET_N high1 failed\n");
            continue;
        }
        msleep(10);
        ret = hid_to_gpio_set(LCD_RESET_N, 0);
        if(ret < 0) {
            pr_err("[iris3] assert LCD_RESET_N low failed\n");
            continue;
        }
        msleep(10);
        ret = hid_to_gpio_set(LCD_RESET_N, 1);
        if(ret < 0) {
            pr_err("[iris3] assert LCD_RESET_N high2 failed\n");
            continue;
        }
        ret = hid_to_gpio_set(PX_RST_N_SHIFT, 1);
        if(ret < 0) {
            pr_err("[iris3] assert PX_RST_N_SHIFT high1 failed\n");
            continue;
        }
        msleep(10);
        ret = hid_to_gpio_set(PX_RST_N_SHIFT, 0);
        if(ret < 0) {
            pr_err("[iris3] assert PX_RST_N_SHIFT low failed\n");
            continue;
        }
        msleep(10);
        ret = hid_to_gpio_set(PX_RST_N_SHIFT, 1);
        if(ret < 0) {
            pr_err("[iris3] assert PX_RST_N_SHIFT high2 failed\n");
            continue;
        }
        break;
    }

    return ret;
}

static int power_off_gpio(void)
{
    int retryCount = 1;
    int ret = 0;

    while ((retryCount--) > 0) {
        //wait for HID ready
        ret = wait4hid();
        if(ret == -1)
            continue;
        else if(ret == -2)
            break;

        ret = hid_to_gpio_set(PX_RST_N_SHIFT, 0);
        if(ret < 0){
            pr_err("[iris3] assert PX_RST_N_SHIFT low failed\n");
            continue;
        }
        ret = hid_to_gpio_set(LCD_RESET_N, 0);
        if(ret < 0){
            pr_err("[iris3] assert LCD_RESET_N low failed\n");
            continue;
        }
        ret = hid_to_enable_mipi(0);
        if(ret < 0){
            pr_err("[iris3] set hid_to_enable_mipi disable failed\n");
            continue;
        }
        break;
    }
    return ret;
}

static ssize_t update_fw_store(struct device *dev,
                    struct device_attribute *attr,
                    const char *buf, size_t count)
{
    unsigned int val;
    int ret;

    if(check_EC_status()) {
        if(kstrtouint(buf, 10, &val))
            return -EFAULT;

        if(val != 1)
            return -EFAULT;

        ret = iris3_update_gamestation_fw(1);
        if(ret < 0) {
            pr_err("[iris3] %s: update gs firmware failed\n", __func__);
            return -EFAULT;
        }
    }

    return count;
}

static ssize_t hbm_cmd_store(struct device *dev,
                    struct device_attribute *attr,
                    const char *buf, size_t count)
{
    unsigned int val;
    struct iris3_i2c_data *data = dev_get_drvdata(dev);

    if(check_EC_status()) {
        if(kstrtouint(buf, 10, &val))
            return -EFAULT;

        if(val < 0 || val > 1)
            return -EFAULT;

        iris3_i2c_send_cmd_hbm(data, val);
    }

    return count;
}

static ssize_t peaking_cmd_store(struct device *dev,
                    struct device_attribute *attr,
                    const char *buf, size_t count)
{
    unsigned int level;
    int ret = 0;
    struct iris3_i2c_data *data = dev_get_drvdata(dev);

    if(check_EC_status()) {
        if(!iris3_init_finish) {
            pr_err("[iris3] %s: iris not ready.\n", __func__);
            return -EFAULT;
        }

        if(kstrtouint(buf, 10, &level))
            return -EFAULT;
        if(level < 0 || level > 4)
            return -EFAULT;

        ret = iris3_i2c_send_cmd_peaking2d(data, level);
        if(ret < 0) {
            pr_err("[iris3] %s: set peaking failed\n", __func__);
            return count;
        }

        data->peaking_level = level;
    }

    return count;
}

static ssize_t peaking_cmd_show(struct device *dev,
                   struct device_attribute *attr, char *buf)
{
    struct iris3_i2c_data *data = dev_get_drvdata(dev);
    return scnprintf(buf, PAGE_SIZE, "%d\n", data->peaking_level);
}

static ssize_t lce_cmd_store(struct device *dev,
                    struct device_attribute *attr,
                    const char *buf, size_t count)
{
    unsigned int mode, level;
    int ret = 0;
    struct iris3_i2c_data *data = dev_get_drvdata(dev);

    if(check_EC_status()) {
        if(!iris3_init_finish) {
            pr_err("[iris3] %s: iris not ready.\n", __func__);
            return -EFAULT;
        }

        sscanf(buf, "%u,%u", &mode, &level);

        if(mode > 1)
            return -EFAULT;
        else if(level < 0 || level > 5)
            return -EFAULT;

        ret = iris3_i2c_send_cmd_lce(data, mode, level);
        if(ret < 0) {
            pr_err("[iris3] %s: set lce failed\n", __func__);
            return count;
        }

        data->lce_mode = mode;
        data->lce_level = level;
    }

    return count;
}

static ssize_t lce_cmd_show(struct device *dev,
                   struct device_attribute *attr, char *buf)
{
    struct iris3_i2c_data *data = dev_get_drvdata(dev);
    return scnprintf(buf, PAGE_SIZE, "%d,%d\n", data->lce_mode, data->lce_level);
}

static ssize_t lightup_cmd_store(struct device *dev,
                    struct device_attribute *attr,
                    const char *buf, size_t count)
{
    unsigned int val;
    int ret = 0;
    struct iris3_i2c_data *data = dev_get_drvdata(dev);
    int i = 0;

    if(kstrtouint(buf, 10, &val))
        return -EFAULT;

    switch(val) {
        case 0:
            pr_err("[iris3] station plug-out+++\n");
            mutex_lock(&data->access_mutex);
            iris3_init_finish = 0;
            iris3_init_bl_finish = 0;
            iris3_stn_update_fw_complete = 0;
            iris3_plugin_init_finish = 0;
            iris3_stat = IRIS3_SUSPEND;
            station_attached = 0;
         //   dsi_display_cb_register(NULL, NULL, NULL, NULL, NULL);
            mutex_unlock(&data->access_mutex);
            stn_cancel_resume = 1;
            cancel_work_sync(&data->suspend_work);
            cancel_work_sync(&data->resume_work);
            stn_cancel_resume = 0;
            pr_err("[iris3] station plug-out---\n");
            break;
        case 1:
            pr_err("[iris3] station plug-in+++\n");
            dp_display_commit_cnt = 10;	// clear the commit count when plug-in
            station_attached = 1;
            for(i = 0; i < 150; i++) {
                ret = wait4dp();
                if(gDongleType != 2) {
                    pr_err("[iris3] gDongleType=%d, abort plug-in sequence\n", gDongleType);
                    return -EFAULT;
                }
                if(ret < 0) {
                    //pr_err("[iris3] wait4dp timeout\n");
                    continue;
                } else {
                    pr_err("[iris3] dp complete\n");
                    break;
                }
            }

            if(power_on_gpio() < 0){
                pr_err("[iris3] power on gpio HID failed, abort light up Iris3\n");
                return -EFAULT;
            }

            ret = iris3_i2c_send_cmd_lightup(data);
            if(ret < 0) {
                pr_err("[iris3] %s: lightup failed\n", __func__);
                return -EFAULT;
            }

            // read panel raw data for calibration
            if (iris3_i2c_read_panel_data(0xC7, 128) < 0) {
                pr_err("[iris3] %s: game station read panel data fails\n", __func__);
            } else {
                pr_err("[iris3] %s: 0xC7 panel data read\n", __func__);
            }
            if (iris3_i2c_read_panel_data(0xC8, 126) < 0) {
                pr_err("[iris3] %s: game station read panel data fails\n", __func__);
            } else {
                pr_err("[iris3] %s: 0xC8 panel data read\n", __func__);
            }

            msleep(10);
            for(i = 0; i < 5; i++) {
                ret = wait4hid();
                if(ret == -1)
                    continue;
                else if(ret == -2)
                    return -EFAULT;

                ret = hid_to_enable_mipi(1);
                if(ret >= 0)
                    break;
                pr_debug("[iris3] set hid_to_enable_mipi high failed, retry=%d\n",i);
            }

            hid_to_enable_mipi(1);//do it twice

            if(ret < 0){
                pr_err("[iris3] enable mipi HID failed, abort light up Iris3\n");
                return -EFAULT;
            }

            iris3_init_finish = 1;
            iris3_plugin_init_finish = 1;
            iris3_stat = IRIS3_RESUME;

            if(get_dp_status()) {
                ret = iris3_i2c_send_cmd_update_bl(data, bl_level);
                if(ret < 0) {
                    pr_err("[iris3] %s: set backlight failed\n", __func__);
                    return -EFAULT;
                }
                iris3_init_bl_finish = 1;
            }
            pr_err("[iris3] station plug-in---\n");
            break;
        case 2:
            msleep(50);
            if(power_on_gpio() < 0){
                pr_err("[iris3] power on gpio HID failed, abort light up Iris3\n");
                return -EFAULT;
            }

            ret = iris3_i2c_send_cmd_abypass(data);
            if(ret < 0) {
                pr_err("[iris3] %s: abypass failed\n", __func__);
                return -EFAULT;
            }

            msleep(10);
            for(i = 0; i < 5; i++) {
                ret = wait4hid();
                if(ret == -1)
                    continue;
                else if(ret == -2)
                    return -EFAULT;

                ret = hid_to_enable_mipi(1);
                if(ret >= 0)
                    break;
                pr_debug("[iris3] set hid_to_enable_mipi high failed, retry=%d\n",i);
            }
            hid_to_enable_mipi(1);//do it twice

            if(ret < 0){
                pr_err("[iris3] enable mipi HID failed, abort light up Iris3\n");
                return -EFAULT;
            }

            iris3_init_finish = 1;
            iris3_plugin_init_finish = 1;
            iris3_stat = IRIS3_RESUME;

            if(get_dp_status()) {
                ret = iris3_i2c_send_cmd_update_bl(data, bl_level);
                if(ret < 0) {
                    pr_err("[iris3] %s: set backlight failed\n", __func__);
                    return -EFAULT;
                }
                iris3_init_bl_finish = 1;
            }
            break;
        case 7:
            hid_to_get_reset_mipi_status(&ret);
            break;
        default:
            pr_err("[iris3] %s: unknow val %d\n", __func__, val);
            break;
        }

    return count;
}

static ssize_t update_bl_store(struct device *dev,
                    struct device_attribute *attr,
                    const char *buf, size_t count)
{
    unsigned int val;
    int bl_val;
    struct iris3_i2c_data *data = dev_get_drvdata(dev);

    if(check_EC_status()) {
        if(kstrtouint(buf, 10, &val))
            return -EFAULT;

        if(val > 255)
            val = 255;
        else if(val < 0)
            val = 0;

        bl_val = remap_bl_value(val);
        iris3_i2c_send_cmd_update_bl(data, bl_val);
    }

    return count;
}

static ssize_t update_bl_show(struct device *dev,
                   struct device_attribute *attr, char *buf)
{
    if(check_EC_status())
        return scnprintf(buf, PAGE_SIZE, "%d\n", bl_level);

    return scnprintf(buf, PAGE_SIZE, "Not in station\n");
}

static ssize_t dbg_read_store(struct device *dev,
                    struct device_attribute *attr,
                    const char *buf, size_t count)
{
    unsigned int val;

    if(check_EC_status()) {
        if(kstrtouint(buf, 16, &val))
            return -EFAULT;

        printk("[iris3] iris3_i2c_dbg_read_addr:%x\n", val);
        dbg_readback.addr = val  & 0xffffffff;
        printk("[iris3] addr:%x\n", dbg_readback.addr);
    }

    return count;
}

static ssize_t dbg_read_show(struct device *dev,
                   struct device_attribute *attr, char *buf)
{
    if(check_EC_status()) {
        iris3_i2c_cmd_four_read(&dbg_readback);
        return scnprintf(buf, PAGE_SIZE, "0x%x: 0x%x\n",
                        dbg_readback.addr, dbg_readback.data);
    }

    return scnprintf(buf, PAGE_SIZE, "Not in station\n");
}

static ssize_t dbg_write_store(struct device *dev,
                    struct device_attribute *attr,
                    const char *buf, size_t count)
{
    unsigned long val;
    struct addr_val writePacket;

    if(check_EC_status()) {
        if(kstrtoul(buf, 16, &val))
            return -EFAULT;

        printk("[iris3] iris3_i2c_dbg_write_data:%lx\n", val);
        writePacket.addr = (val >> 32) & 0xffffffff;
        writePacket.data = val & 0xffffffff;
        printk("[iris3] addr:%x, data:%x\n", writePacket.addr, writePacket.data);
        iris3_i2c_cmd_four_write(&writePacket);
    }

    return count;
}

static ssize_t init_comp_show(struct device *dev,
                   struct device_attribute *attr, char *buf)
{
    return scnprintf(buf, PAGE_SIZE, "%d", iris3_stn_update_fw_complete);
}

static ssize_t chip_id_show(struct device *dev,
                   struct device_attribute *attr, char *buf)
{
    struct iris3_i2c_data *data = dev_get_drvdata(dev);

    iris3_i2c_read_chip_id(data);
    return scnprintf(buf, PAGE_SIZE, "0x%08x\n", data->chipid);
}

static DEVICE_ATTR(chip_id,          0444, chip_id_show,      NULL);
static DEVICE_ATTR(dbg_write,        0664, NULL,              dbg_write_store);
static DEVICE_ATTR(dbg_read,         0664, dbg_read_show,     dbg_read_store);
static DEVICE_ATTR(lightup,          0664, NULL,              lightup_cmd_store);
static DEVICE_ATTR(lce,              0664, lce_cmd_show,      lce_cmd_store);
static DEVICE_ATTR(peaking,          0664, peaking_cmd_show,  peaking_cmd_store);
static DEVICE_ATTR(brightness,       0664, update_bl_show,    update_bl_store);
static DEVICE_ATTR(hbm,              0664, NULL,              hbm_cmd_store);
static DEVICE_ATTR(update_fw,        0664, NULL,              update_fw_store);
static DEVICE_ATTR(init_complete,    0444, init_comp_show,    NULL);

static struct attribute *iris3_i2c_attrs[] = {
    &dev_attr_chip_id.attr,
    &dev_attr_dbg_write.attr,
    &dev_attr_dbg_read.attr,
    &dev_attr_lightup.attr,
    &dev_attr_lce.attr,
    &dev_attr_peaking.attr,
    &dev_attr_brightness.attr,
    &dev_attr_hbm.attr,
    &dev_attr_update_fw.attr,
    &dev_attr_init_complete.attr,
    NULL
};

static const struct attribute_group iris3_i2c_attr_group = {
    .attrs = iris3_i2c_attrs,
};

void iris3_i2c_set_hbm(int enable)
{
    struct iris3_i2c_data *data = i2c_get_clientdata(iris3_i2c_handle);
    if(iris3_init_bl_finish) {
        if(enable != last_hbm_setting)
            iris3_i2c_send_cmd_hbm(data, enable);
        else
            pr_err("[iris3] %s: HBM mode same, skip switch mode\n", __func__);
    } else {
        pr_err("[iris3] %s: iris not ready.\n", __func__);
    }
}

void iris3_i2c_set_init_bl(void)
{
    struct iris3_i2c_data *data = i2c_get_clientdata(iris3_i2c_handle);
    bl_level = get_last_backlight_value();

    if(iris3_init_finish) {
        pr_err("[iris3] %s: iris ready.\n", __func__);
        iris3_i2c_send_cmd_update_bl(data, bl_level);
        iris3_init_bl_finish = 1;
    } else {
        pr_err("[iris3] %s: iris not ready.\n", __func__);
    }
}

void iris3_i2c_sync_bl(int level)
{
    struct iris3_i2c_data *data = i2c_get_clientdata(iris3_i2c_handle);
    bl_level = level;

    if(!iris3_init_bl_finish  || g_enter_AOD) {
        pr_err("[iris3] %s: skip set stn bl\n", __func__);
        return;
    }

    iris3_i2c_send_cmd_update_bl(data, level);
}

void iris3_i2c_suspend_work(struct work_struct *work)
{
    struct iris3_i2c_data *data = i2c_get_clientdata(iris3_i2c_handle);
    int rc = 0;

    if(iris3_stat == IRIS3_SUSPEND) {
        pr_err("[iris3] %s: skip suspend\n", __func__);
        return;
    }

    mutex_lock(&data->onoff_mutex);

    pr_err("[iris3] suspend work+++\n");

    iris3_stat = IRIS3_SUSPEND;
    iris3_init_finish = 0;
    iris3_init_bl_finish = 0;
    iris3_stn_update_fw_complete = 0;

    rc = iris3_i2c_send_cmd_poweroff(data);
    if(rc < 0) {
        pr_err("[iris3] suspend work---\n");
        mutex_unlock(&data->onoff_mutex);
        return;
    }

    msleep(80);
    if(power_off_gpio() < 0)
        pr_err("[iris3] power off gpio HID failed");

    hid_suspend_vote(data->hid_suspend_id);
    pr_err("[iris3] suspend work---\n");
    mutex_unlock(&data->onoff_mutex);
}

void iris3_i2c_resume_work(struct work_struct *work)
{
    struct iris3_i2c_data *data = i2c_get_clientdata(iris3_i2c_handle);
    int i = 0;
    int ret = 0;

    if(iris3_stat == IRIS3_RESUME) {
        pr_err("[iris3] %s: skip resume\n", __func__);
        return;
    }

    mutex_lock(&data->onoff_mutex);

    pr_err("[iris3] resume work+++\n");

    iris3_stat = IRIS3_RESUME;

    for(i = 0; i < 150; i++) {
        ret = wait4dp();
        if (!station_attached || stn_cancel_resume)
            goto unlock;
        if(ret < 0) {
            //pr_err("[iris3] wait4dp timeout\n");
            continue;
        } else {
            pr_err("[iris3] dp complete\n");
            break;
        }
    }

    if(power_on_gpio() < 0){
        pr_err("[iris3] power on gpio HID failed, abort resume work\n");
        goto unlock;
    }

    ret = iris3_i2c_send_cmd_lightup(data);
    if(ret < 0) {
        pr_err("[iris3] %s: lightup failed\n", __func__);
        goto unlock;;
    }

    ret = iris3_update_gamestation_fw(0);
    if(ret < 0) {
        pr_err("[iris3] %s: update gs firmware failed\n", __func__);
    }

    msleep(10);
    for(i = 0; i < 5; i++) {
        ret = hid_to_enable_mipi(1);
        if(ret >= 0)
            break;
    }

    if(ret < 0){
        pr_err("[iris3] enable mipi HID failed, abort resume work\n");
        goto unlock;;
    }

    iris3_init_finish = 1;

    if(get_dp_status()) {
        iris3_i2c_send_cmd_update_bl(data, bl_level);
        iris3_init_bl_finish = 1;
    }

unlock:
    pr_err("[iris3] resume work---\n");
    mutex_unlock(&data->onoff_mutex);
    return;
}

void iris3_i2c_suspend(void)
{
    struct iris3_i2c_data *data = i2c_get_clientdata(iris3_i2c_handle);

    pr_err("[iris3] %s ++\n", __func__);

    stn_cancel_resume = 1;
    cancel_work_sync(&data->suspend_work);
    cancel_work_sync(&data->resume_work);
    stn_cancel_resume = 0;
    queue_work(data->resume_wq, &data->suspend_work);
}

void iris3_i2c_resume(void)
{
    struct iris3_i2c_data *data = i2c_get_clientdata(iris3_i2c_handle);

    if(!iris3_plugin_init_finish)
        return;

    pr_err("[iris3] %s ++\n", __func__);
    queue_work(data->resume_wq, &data->resume_work);
}


void iris3_i2c_power_off(void)
{
    struct iris3_i2c_data *data = i2c_get_clientdata(iris3_i2c_handle);

    if(!iris3_plugin_init_finish)
        return;

    pr_err("[iris3] %s ++\n", __func__);
    iris3_i2c_send_cmd_poweroff(data);
    msleep(80);
    if(power_off_gpio() < 0)
        pr_err("[iris3] power off gpio HID failed");
}

static int iris3_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct iris3_i2c_data *data;
    int error;

	iris3_i2c_handle = client;

    if(!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
        return -ENODEV;
    else
        printk("[iris3] client->addr : 0x%x\n", client->addr);

	data = devm_kzalloc(&client->dev, sizeof(struct iris3_i2c_data), GFP_KERNEL);
	if(!data) {
		dev_err(&client->dev, "Failed to allocate memory\n");
		return -ENOMEM;
	}


    mutex_init(&data->access_mutex);
    mutex_init(&data->onoff_mutex);
    data->resume_wq = create_singlethread_workqueue("iris3_i2c_resume_wq");
    INIT_WORK(&data->resume_work, iris3_i2c_resume_work);
    INIT_WORK(&data->suspend_work, iris3_i2c_suspend_work);

    data->hid_suspend_id = hid_vote_register("IRIS3");

    i2c_set_clientdata(client, data);

    error = sysfs_create_group(&client->dev.kobj, &iris3_i2c_attr_group);
    if(error) {
        dev_err(&client->dev, "Failure %d creating sysfs group\n", error);
        return error;
    }

    bl_level = get_last_backlight_value();
    dsi_display_cb_register(iris3_i2c_sync_bl, iris3_i2c_set_init_bl,
                            iris3_i2c_suspend, iris3_i2c_resume,
                            iris3_i2c_set_hbm,iris3_i2c_power_off);

    iris3_i2c_cb_register(iris3_i2c_read_cmd, iris3_i2c_write_cmd, iris3_i2c_burst_write);

    return 0;
}

static int iris3_i2c_remove(struct i2c_client *client)
{
    struct iris3_i2c_data *data;

    data = i2c_get_clientdata(client);
    if(data->resume_wq)
        destroy_workqueue(data->resume_wq);
    iris3_init_finish = 0;
    iris3_init_bl_finish = 0;
    iris3_plugin_init_finish = 0;
    iris3_stat = IRIS3_SUSPEND;
    dsi_display_cb_register(NULL, NULL, NULL, NULL, NULL,NULL);
    sysfs_remove_group(&client->dev.kobj, &iris3_i2c_attr_group);
    iris3_i2c_cb_register(NULL, NULL, NULL);

    hid_vote_unregister(data->hid_suspend_id, "IRIS3");

    return 0;
}

static const struct i2c_device_id iris3_i2c_id[] = {
    { "iris3_i2c", 0},
    { },
};

#ifdef CONFIG_OF
static const struct of_device_id iris3_match_table[] = {
    { .compatible = "pixelworks,iris3",},
    { },
};
#else
#define iris3_match_table NULL
#endif

static struct i2c_driver iris3_i2c_driver = {
    .driver		= {
        .name		= "iris3",
        .owner = THIS_MODULE,
        .of_match_table	= iris3_match_table,
    },
    .probe		= iris3_i2c_probe,
    .remove		= iris3_i2c_remove,
    .id_table 	= iris3_i2c_id,
};

static int __init iris3_i2c_bus_init(void)
{
    i2c_add_driver(&iris3_i2c_driver);

    return 0;
}
module_init(iris3_i2c_bus_init);

static void __exit iris3_i2c_bus_exit(void)
{
    i2c_del_driver(&iris3_i2c_driver);
}
module_exit(iris3_i2c_bus_exit);

MODULE_LICENSE("GPL");
