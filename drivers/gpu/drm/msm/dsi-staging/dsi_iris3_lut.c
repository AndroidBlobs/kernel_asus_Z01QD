#include <linux/firmware.h>
#include <drm/drm_mipi_dsi.h>
#include "dsi_iris3_api.h"
#include "dsi_iris3_lightup.h"
#include "dsi_iris3_lightup_ocp.h"
#include "dsi_iris3_pq.h"


enum {
	IRIS_SEND_LUT_BY_CMDLIST,
	IRIS_SEND_LUT_BY_DMA,
};

static u8 payload_size;

static u8 *iris_dbc_lut_buf = NULL;
static u8 *iris_cm_lut_buf = NULL;
static u8 *iris_sdr2hdr_lut_buf = NULL;
static u8 *iris_scaler1d_lut_buf = NULL;
static u8 *iris_gamma_lut_buf = NULL;

static struct dsi_cmd_desc *cm_lut_send_cmd = NULL;
static struct dsi_cmd_desc *scaler1d_lut_send_cmd = NULL;
static struct dsi_cmd_desc *sdr2hdr_lut_send_cmd = NULL;
static struct dsi_cmd_desc *sdr2hdr_inv_uv_send_cmd = NULL;/*write the PI*/



static struct dsi_cmd_desc *dbc_lut_income_send_cmd = NULL;
static struct dsi_cmd_desc *dbc_lut_compk_send_cmd = NULL;
static struct dsi_cmd_desc *dbc_lut_cabc_dlv_send_cmd = NULL;

static struct dsi_cmd_desc *gamma_lut_send_cmd = NULL;


static struct dsi_panel_cmd_set panel_lut_cmds;

struct msmfb_iris_ambient_info iris_ambient_lut;
static uint32_t lut_lut2[LUT_LEN] = {};
static uint32_t LUT2_fw[LUT_LEN+LUT_LEN+LUT_LEN] = {};

static u8 *iris_ambient_lut_buf = NULL;
/* SDR2HDR_UVYGAIN_BLOCK_CNT > SDR2HDR_LUT2_BLOCK_CNT */
static struct dsi_cmd_desc *dynamic_lut_send_cmd = NULL;

struct msmfb_iris_maxcll_info iris_maxcll_lut;
static uint32_t lut_luty[LUT_LEN] = {};
static uint32_t lut_lutuv[LUT_LEN] = {};
static uint32_t LUTUVY_fw[LUT_LEN+LUT_LEN+LUT_LEN+LUT_LEN+LUT_LEN+LUT_LEN] = {};

static u8 *iris_maxcll_lut_buf = NULL;
static struct dsi_cmd_desc *dynamic_lutuvy_send_cmd = NULL;

// for iris setting via i2c
uint32_t CM_LUT_GS[CM_LUT_NUMBER][CM_LUT_BLOCK_NUMBER][729];
uint32_t GAMMA_LUT_GS[8][GAMMA_LUT_SIZE/4];
uint32_t ENABLE_LUT[8] = {
	0xf056000c, 0x0000005e,
	0xf056000c, 0x0000005f,
	0xf0560140, 0x00000100
};

// parse gs fw once, only reboot will reset this flag
static int m_gs_lut_parsed = false;

extern struct iris_setting_info iris_setting;
extern int iris3_stn_update_fw_complete; //ASUS_BSP

struct lut_node {
	u32 dbc_income_pkt_cnt;
	u32 dbc_compk_pkt_cnt;
	u32 dbc_dlv_pkt_cnt;

	u32 cm_one_lut_pkt_cnt;

	u32 hdr_one_lut_pkt_cnt;
	u32 uv_one_lut_pkt_cnt;

	u32 scaler_one_lut_pkt_cnt;

	u32 gamma_one_lut_pkt_cnt;

	u32 lut_cmd_cnts_max;

	u32 hdr_lut2_pkt_cnt;

	u32 hdr_lutuvy_pkt_cnt;
};

static struct lut_node iris_lut_param;
static void __lut_update_work_handler(struct work_struct *work);

static void iris_ambient_lut_init(void)
{
	iris_ambient_lut.ambient_lux = 0;
	iris_ambient_lut.ambient_bl_ratio = 0;
	iris_ambient_lut.lut_lut2_payload = &lut_lut2;

	kfree(iris_ambient_lut_buf);
	iris_ambient_lut_buf = NULL;
	kfree(dynamic_lut_send_cmd);
	dynamic_lut_send_cmd = NULL;
}

static void iris_maxcll_lut_init(void)
{
	iris_maxcll_lut.mMAXCLL= 2200;
	iris_maxcll_lut.lut_luty_payload = &lut_luty;
	iris_maxcll_lut.lut_lutuv_payload = &lut_lutuv;

	kfree(iris_maxcll_lut_buf);
	iris_maxcll_lut_buf = NULL;
	kfree(dynamic_lutuvy_send_cmd);
	dynamic_lutuvy_send_cmd = NULL;
}

static void iris_lut_out_cmds_init(void)
{
	/* sent out cmds
	iris_out_cmds_buf_reset();*/

	memset(&panel_lut_cmds, 0x00, sizeof(panel_lut_cmds));
	panel_lut_cmds.state = DSI_CMD_SET_STATE_HS;
}

static void iris_lut_buf_init(void)
{

	struct iris_cfg *pcfg = NULL;

	pcfg = iris_get_cfg();
	payload_size = pcfg->split_pkt_size;

	kfree(iris_dbc_lut_buf);
	iris_dbc_lut_buf = NULL;
	kfree(iris_cm_lut_buf);
	iris_cm_lut_buf = NULL;
	kfree(iris_sdr2hdr_lut_buf);
	iris_sdr2hdr_lut_buf = NULL;
	kfree(iris_scaler1d_lut_buf);
	iris_scaler1d_lut_buf = NULL;
	kfree(iris_gamma_lut_buf);
	iris_gamma_lut_buf = NULL;

	kfree(dbc_lut_income_send_cmd);
	dbc_lut_income_send_cmd = NULL;
	kfree(dbc_lut_compk_send_cmd);
	dbc_lut_compk_send_cmd = NULL;
	kfree(dbc_lut_cabc_dlv_send_cmd);
	dbc_lut_cabc_dlv_send_cmd = NULL;
	kfree(cm_lut_send_cmd);
	cm_lut_send_cmd = NULL;
	kfree(sdr2hdr_lut_send_cmd);
	sdr2hdr_lut_send_cmd = NULL;
	kfree(sdr2hdr_inv_uv_send_cmd);
	sdr2hdr_inv_uv_send_cmd = NULL;
	kfree(scaler1d_lut_send_cmd);
	scaler1d_lut_send_cmd = NULL;
	kfree(gamma_lut_send_cmd);
	gamma_lut_send_cmd = NULL;

	memset(&iris_lut_param, 0x00, sizeof(iris_lut_param));

	iris_lut_out_cmds_init();

	/* for HDR ambient light */
	iris_ambient_lut_init();

	/* for HDR maxcll */
	iris_maxcll_lut_init();
}

static int iris_dbc_lut_read(const u8 *fw_data)
{
	u32 level_index = 0, block_index = 0, lut_pkt_cmd_index = 0;

	u32 iris_lut_buf_index = 0, cmd_payload_len=0;

	u32 ucmds_cnt, ab_table_index=0, temp_index = 0, temp_index2 = 0;
	u32 addr_offset = 0;

	u32 dbc_payload_size = payload_size;
	u32 dbc_pkt_size = dbc_payload_size + DIRECT_BUS_HEADER_SIZE;
	u32 dbc_income_pkt_cnt = (DBC_INCOME_BL_SIZE + dbc_payload_size -1)/dbc_payload_size;

	u32 dbc_compk_block_pkt_cnt = (DBC_COMPK_BLOCK_SIZE + dbc_payload_size -1)/dbc_payload_size;
	u32 dbc_compk_pkt_cnt = dbc_compk_block_pkt_cnt * DBC_COMPK_BLOCK_NUM;

	u32 dbc_dlv_pkt_cnt = (DBC_CABC_DLV_SIZE + dbc_payload_size -1)/dbc_payload_size;

	struct ocp_header ocp_burst_header;

	memset(&ocp_burst_header, 0, sizeof(ocp_burst_header));
	ocp_burst_header.header = 0x0020000C;
	ocp_burst_header.address = INCOME_BL_A;


	if (dbc_lut_income_send_cmd == NULL) {
		dbc_lut_income_send_cmd = kzalloc(sizeof(struct dsi_cmd_desc)*AB_TABLE*dbc_income_pkt_cnt, GFP_KERNEL);
		iris_lut_param.lut_cmd_cnts_max += AB_TABLE*dbc_income_pkt_cnt;
		iris_lut_param.dbc_income_pkt_cnt = dbc_income_pkt_cnt;

		pr_err("%s, iris_lut_param.lut_cmd_cnts_max = %d \n", __func__, iris_lut_param.lut_cmd_cnts_max);
	}
	if (dbc_lut_compk_send_cmd == NULL) {
		dbc_lut_compk_send_cmd = kzalloc(sizeof(struct dsi_cmd_desc)*AB_TABLE*dbc_compk_pkt_cnt*DBC_LEVEL_CNT, GFP_KERNEL);
		iris_lut_param.lut_cmd_cnts_max += AB_TABLE*dbc_compk_pkt_cnt;
		iris_lut_param.dbc_compk_pkt_cnt = dbc_compk_pkt_cnt;
	}
	if (dbc_lut_cabc_dlv_send_cmd == NULL) {
		dbc_lut_cabc_dlv_send_cmd = kzalloc(sizeof(struct dsi_cmd_desc)*dbc_dlv_pkt_cnt*DBC_LEVEL_CNT, GFP_KERNEL);
		iris_lut_param.lut_cmd_cnts_max += dbc_dlv_pkt_cnt;
		iris_lut_param.dbc_dlv_pkt_cnt = dbc_dlv_pkt_cnt;
	}

	ucmds_cnt = AB_TABLE*dbc_income_pkt_cnt + AB_TABLE*DBC_LEVEL_CNT*dbc_compk_pkt_cnt +
				DBC_LEVEL_CNT * dbc_dlv_pkt_cnt;

	if (iris_dbc_lut_buf == NULL)
		iris_dbc_lut_buf = kzalloc(dbc_pkt_size*ucmds_cnt, GFP_KERNEL);

	if (!dbc_lut_income_send_cmd || !dbc_lut_compk_send_cmd || !dbc_lut_cabc_dlv_send_cmd || !iris_dbc_lut_buf) {
		pr_err("%s: failed to alloc mem: lut_cmd: %p, :%p, :%p buf: %p \n", __func__,
				dbc_lut_income_send_cmd, dbc_lut_compk_send_cmd, dbc_lut_cabc_dlv_send_cmd, iris_dbc_lut_buf);
		return IRIS_FAILED;
	}

	//parse income bl

	addr_offset = 0;
	for(lut_pkt_cmd_index = 0; lut_pkt_cmd_index < dbc_income_pkt_cnt; lut_pkt_cmd_index++) {


		if(lut_pkt_cmd_index == dbc_income_pkt_cnt-1)
			cmd_payload_len = DBC_INCOME_BL_SIZE - (dbc_income_pkt_cnt-1) * dbc_payload_size;
		else
			cmd_payload_len = dbc_payload_size;

		for(ab_table_index =0; ab_table_index< AB_TABLE; ab_table_index++) {
			dbc_lut_income_send_cmd[ab_table_index*dbc_income_pkt_cnt +lut_pkt_cmd_index].msg.type = 0x29;
			dbc_lut_income_send_cmd[ab_table_index*dbc_income_pkt_cnt +lut_pkt_cmd_index].msg.tx_len = cmd_payload_len + DIRECT_BUS_HEADER_SIZE;
			dbc_lut_income_send_cmd[ab_table_index*dbc_income_pkt_cnt +lut_pkt_cmd_index].post_wait_ms = LUT_CMD_WAIT_MS;
			dbc_lut_income_send_cmd[ab_table_index*dbc_income_pkt_cnt +lut_pkt_cmd_index].msg.tx_buf = iris_dbc_lut_buf + iris_lut_buf_index;

			ocp_burst_header.address = (ab_table_index==0) ? INCOME_BL_A : INCOME_BL_B;
			ocp_burst_header.address += addr_offset;
			memcpy(&iris_dbc_lut_buf[iris_lut_buf_index], &ocp_burst_header, DIRECT_BUS_HEADER_SIZE);
			iris_lut_buf_index += DIRECT_BUS_HEADER_SIZE;
			memcpy(&iris_dbc_lut_buf[iris_lut_buf_index], fw_data, cmd_payload_len);
			iris_lut_buf_index += cmd_payload_len;
		}
		addr_offset +=cmd_payload_len;
		fw_data += cmd_payload_len;

	}


	//parse compk
	for(level_index = 0; level_index < DBC_LEVEL_CNT; level_index++){

		ocp_burst_header.header = 0x0020000C;

		for(block_index = 0; block_index < DBC_COMPK_BLOCK_NUM; block_index++){

			addr_offset = 0;
			for(lut_pkt_cmd_index = 0; lut_pkt_cmd_index < dbc_compk_block_pkt_cnt; lut_pkt_cmd_index++) {

				if(lut_pkt_cmd_index == dbc_compk_block_pkt_cnt-1)
					cmd_payload_len = DBC_COMPK_BLOCK_SIZE - (dbc_compk_block_pkt_cnt-1) * dbc_payload_size;
				else
					cmd_payload_len = dbc_payload_size;

				for(ab_table_index =0; ab_table_index< AB_TABLE; ab_table_index++) {

					temp_index = (level_index*AB_TABLE + ab_table_index)*dbc_compk_pkt_cnt;
					temp_index2 = block_index * dbc_compk_block_pkt_cnt + lut_pkt_cmd_index;
					dbc_lut_compk_send_cmd[temp_index+temp_index2].msg.type = 0x29;
					dbc_lut_compk_send_cmd[temp_index+temp_index2].msg.tx_len = cmd_payload_len + DIRECT_BUS_HEADER_SIZE;
					dbc_lut_compk_send_cmd[temp_index+temp_index2].msg.tx_buf = iris_dbc_lut_buf + iris_lut_buf_index;

					if (block_index ==0)
						ocp_burst_header.address = (ab_table_index==0) ? COMPK_DLV_A_0 : COMPK_DLV_B_0;
					else
						ocp_burst_header.address = (ab_table_index==0) ? COMPK_DLV_A_1 : COMPK_DLV_B_1;

					ocp_burst_header.address += addr_offset;

					memcpy(&iris_dbc_lut_buf[iris_lut_buf_index], &ocp_burst_header, DIRECT_BUS_HEADER_SIZE);
					iris_lut_buf_index += DIRECT_BUS_HEADER_SIZE;
					memcpy(&iris_dbc_lut_buf[iris_lut_buf_index], fw_data, cmd_payload_len);
					iris_lut_buf_index += dbc_payload_size;

				}
				addr_offset +=cmd_payload_len;
				fw_data += cmd_payload_len;

			}
		}

		//use ocp burst to write IP
		memset(&ocp_burst_header, 0, sizeof(ocp_burst_header));
		ocp_burst_header.header = 0x00000000;
		ocp_burst_header.address = CABC_DLV_0;

		//parse CABC_DLV
		{
			for(lut_pkt_cmd_index = 0; lut_pkt_cmd_index < dbc_dlv_pkt_cnt; lut_pkt_cmd_index++) {

				if(lut_pkt_cmd_index == dbc_dlv_pkt_cnt-1)
					cmd_payload_len = DBC_CABC_DLV_SIZE - (dbc_dlv_pkt_cnt-1) * dbc_payload_size;
				else
					cmd_payload_len = dbc_payload_size;

				dbc_lut_cabc_dlv_send_cmd[level_index*dbc_dlv_pkt_cnt + lut_pkt_cmd_index].msg.type = 0x29;
				dbc_lut_cabc_dlv_send_cmd[level_index*dbc_dlv_pkt_cnt + lut_pkt_cmd_index].msg.tx_len = cmd_payload_len + DIRECT_BUS_HEADER_SIZE;
				dbc_lut_cabc_dlv_send_cmd[level_index*dbc_dlv_pkt_cnt + lut_pkt_cmd_index].post_wait_ms = LUT_CMD_WAIT_MS;
				dbc_lut_cabc_dlv_send_cmd[level_index*dbc_dlv_pkt_cnt + lut_pkt_cmd_index].msg.tx_buf = iris_dbc_lut_buf + iris_lut_buf_index;

				memcpy(&iris_dbc_lut_buf[iris_lut_buf_index], &ocp_burst_header, DIRECT_BUS_HEADER_SIZE);
				iris_lut_buf_index += DIRECT_BUS_HEADER_SIZE;
				memcpy(&iris_dbc_lut_buf[iris_lut_buf_index], fw_data, cmd_payload_len);
				iris_lut_buf_index += dbc_payload_size;

				fw_data += cmd_payload_len;
			}
		}

	}

	return IRIS_SUCCESS;
}


int iris_cm_lut_read(u32 lut_table_index, const u8 *fw_data)
{
	u32 lut_pkt_cmd_index = 0;

	u32 iris_lut_buf_index, cmd_payload_len=0;

	u32 cm_pkt_size = payload_size + DIRECT_BUS_HEADER_SIZE;
	u32 cm_block_pkt_cnt = (CM_LUT_BLOCK_SIZE + payload_size - 1)/payload_size;
	u32 cm_one_lut_pkt_cnt = cm_block_pkt_cnt * CM_LUT_BLOCK_NUMBER;

	struct ocp_header ocp_burst_header;

	memset(&ocp_burst_header, 0, sizeof(ocp_burst_header));
	ocp_burst_header.header = 0x0008000C;
	ocp_burst_header.address = CM_LUT_ADDRESS;

	if (iris_cm_lut_buf == NULL)
		iris_cm_lut_buf = kzalloc(cm_pkt_size*cm_one_lut_pkt_cnt*CM_LUT_NUMBER, GFP_KERNEL);

	if(cm_lut_send_cmd == NULL) {
		cm_lut_send_cmd = kzalloc(sizeof(struct dsi_cmd_desc)*cm_one_lut_pkt_cnt*CM_LUT_NUMBER, GFP_KERNEL);
		iris_lut_param.lut_cmd_cnts_max += cm_one_lut_pkt_cnt*CM_LUT_GROUP;
		iris_lut_param.cm_one_lut_pkt_cnt = cm_one_lut_pkt_cnt;

		pr_err("%s, iris_lut_param.lut_cmd_cnts_max = %d \n", __func__, iris_lut_param.lut_cmd_cnts_max);
	}

	if (!cm_lut_send_cmd || !iris_cm_lut_buf) {
		pr_err("%s: failed to alloc mem: lut_cmd: %p, buf: %p \n", __func__,
				cm_lut_send_cmd, iris_scaler1d_lut_buf);
		return IRIS_FAILED;
	}


	pr_debug("%s:cm_block_pkt_cnt=%d cm_one_lut_pkt_cnt=%d lut_table_index=%d\n",__func__, cm_block_pkt_cnt, cm_one_lut_pkt_cnt, lut_table_index);

	{
		ocp_burst_header.address = CM_LUT_ADDRESS + (lut_table_index%CM_LUT_GROUP) * CM_LUT_BLOCK_SIZE;
		for(lut_pkt_cmd_index = 0; lut_pkt_cmd_index < cm_one_lut_pkt_cnt; lut_pkt_cmd_index++) {

			iris_lut_buf_index = lut_table_index*cm_pkt_size*cm_one_lut_pkt_cnt + lut_pkt_cmd_index*cm_pkt_size;

			// need update ocp_burst_header.address here
			if ((lut_pkt_cmd_index%cm_block_pkt_cnt)==0)
				ocp_burst_header.address = CM_LUT_ADDRESS +
								(lut_pkt_cmd_index / cm_block_pkt_cnt) * CM_LUT_BLOCK_ADDRESS_INC +
								(lut_table_index%CM_LUT_GROUP) * CM_LUT_BLOCK_SIZE;

			if (CM_LUT_BLOCK_SIZE >= ((lut_pkt_cmd_index%cm_block_pkt_cnt + 1) * payload_size))
				cmd_payload_len = payload_size;
			else
				cmd_payload_len = CM_LUT_BLOCK_SIZE - (cm_block_pkt_cnt-1) * payload_size; // last pkt may less than payload_size

			cm_lut_send_cmd[lut_pkt_cmd_index+ cm_one_lut_pkt_cnt*lut_table_index].msg.type = 0x29;
			cm_lut_send_cmd[lut_pkt_cmd_index+ cm_one_lut_pkt_cnt*lut_table_index].msg.tx_len = cmd_payload_len + DIRECT_BUS_HEADER_SIZE;
			cm_lut_send_cmd[lut_pkt_cmd_index+ cm_one_lut_pkt_cnt*lut_table_index].post_wait_ms = LUT_CMD_WAIT_MS;
			cm_lut_send_cmd[lut_pkt_cmd_index+ cm_one_lut_pkt_cnt*lut_table_index].msg.tx_buf = iris_cm_lut_buf + iris_lut_buf_index;

			pr_debug("%s: lut_table_index=%d, lut_pkt_cmd_index=%d,ocp_burst_header address=%x header=%x\n",__func__, lut_table_index, lut_pkt_cmd_index, ocp_burst_header.address, ocp_burst_header.header);
			memcpy(&iris_cm_lut_buf[iris_lut_buf_index], &ocp_burst_header, DIRECT_BUS_HEADER_SIZE);
			iris_lut_buf_index += DIRECT_BUS_HEADER_SIZE;
			memcpy(&iris_cm_lut_buf[iris_lut_buf_index], fw_data, cmd_payload_len);

			fw_data += cmd_payload_len;
			ocp_burst_header.address +=cmd_payload_len;


		}
	}

	return IRIS_SUCCESS;
}

static void iris_cm_lut_check(const u8 *fw_data)
{

	u32 lut_table_index = 0;

	for(lut_table_index = 0; lut_table_index < CM_LUT_NUMBER; lut_table_index++)
		iris_cm_lut_read(lut_table_index, (fw_data + CM_FW_START_ADDR + lut_table_index * CM_LUT_SIZE));
}

static int iris_sdr2hdr_lut_read(u32 lut_group_index, const u8 *fw_data)
{
	u32 lut_block_index = 0, lut_pkt_cmd_index = 0;
	u32 temp_index = 0;
	u32 iris_lut_buf_index, cmd_payload_len=0;

	u32 hdr_payload_size = payload_size;
	u32 hdr_pkt_size = hdr_payload_size + DIRECT_BUS_HEADER_SIZE;
	u32 hdr_block_pkt_cnt = (SDR2HDR_LUT_BLOCK_SIZE + hdr_payload_size - 1)/hdr_payload_size;
	u32 hdr_one_lut_pkt_cnt = hdr_block_pkt_cnt * SDR2HDR_LUT_BLOCK_NUMBER;

	u32 uv_block_pkt_cnt = (SDR2HDR_INV_UV_SIZE + hdr_payload_size - 1)/hdr_payload_size;
	u32 uv_one_lut_pkt_cnt = uv_block_pkt_cnt * SDR2HDR_INV_UV_NUMBER;

	u32 one_group_size = hdr_pkt_size*(hdr_one_lut_pkt_cnt+uv_one_lut_pkt_cnt);

	struct ocp_header ocp_dbus_header;
	struct ocp_header ocp_burst_header;

	memset(&ocp_dbus_header, 0, sizeof(ocp_dbus_header));
	ocp_dbus_header.header = 0x0004000C;
	ocp_dbus_header.address = SDR2HDR_LUT_ADDRESS;


	if (sdr2hdr_lut_send_cmd == NULL) {
		sdr2hdr_lut_send_cmd = kzalloc(sizeof(struct dsi_cmd_desc)*hdr_one_lut_pkt_cnt*SDR2HDR_LUT_GROUP_CNT , GFP_KERNEL);
		iris_lut_param.lut_cmd_cnts_max+= hdr_one_lut_pkt_cnt;
		iris_lut_param.hdr_one_lut_pkt_cnt = hdr_one_lut_pkt_cnt;

		pr_err("%s, iris_lut_param.lut_cmd_cnts_max = %d \n", __func__, iris_lut_param.lut_cmd_cnts_max);
	}

	if (sdr2hdr_inv_uv_send_cmd == NULL) {
		sdr2hdr_inv_uv_send_cmd = kzalloc(sizeof(struct dsi_cmd_desc)*uv_one_lut_pkt_cnt*SDR2HDR_LUT_GROUP_CNT , GFP_KERNEL);
		iris_lut_param.lut_cmd_cnts_max += uv_one_lut_pkt_cnt;
		iris_lut_param.uv_one_lut_pkt_cnt = uv_one_lut_pkt_cnt;
	}

	if (iris_sdr2hdr_lut_buf == NULL) {
		iris_sdr2hdr_lut_buf = kzalloc(one_group_size*SDR2HDR_LUT_GROUP_CNT, GFP_KERNEL);
	}

	if (!iris_sdr2hdr_lut_buf || !sdr2hdr_lut_send_cmd || !sdr2hdr_inv_uv_send_cmd) {
		pr_err("%s: failed to alloc mem: lut_cmd: %p, uv_cmd: %p, buf: %p \n", __func__,
				sdr2hdr_lut_send_cmd, sdr2hdr_inv_uv_send_cmd, iris_sdr2hdr_lut_buf);
		return IRIS_FAILED;
	}

	{
		//use DB to write
		ocp_dbus_header.header = 0x0004000C;
		ocp_dbus_header.address = SDR2HDR_LUT_ADDRESS;
		//parse LUT0,1,2,3 and UVY0,1,2
		for(lut_block_index = 0; lut_block_index < SDR2HDR_LUT_BLOCK_NUMBER; lut_block_index++){

			ocp_dbus_header.address = lut_block_index*SDR2HDR_LUT_BLOCK_ADDRESS_INC;
			for(lut_pkt_cmd_index = 0; lut_pkt_cmd_index < hdr_block_pkt_cnt; lut_pkt_cmd_index++) {

				iris_lut_buf_index = lut_group_index*one_group_size;
				iris_lut_buf_index += lut_block_index*hdr_pkt_size*hdr_block_pkt_cnt + lut_pkt_cmd_index*hdr_pkt_size;

				if(lut_pkt_cmd_index == hdr_block_pkt_cnt-1)
					cmd_payload_len = SDR2HDR_LUT_BLOCK_SIZE - (hdr_block_pkt_cnt-1) * hdr_payload_size;
				else
					cmd_payload_len = hdr_payload_size;

				temp_index = lut_pkt_cmd_index+ hdr_block_pkt_cnt*lut_block_index;
				sdr2hdr_lut_send_cmd[lut_group_index*hdr_one_lut_pkt_cnt + temp_index].msg.type = 0x29;
				sdr2hdr_lut_send_cmd[lut_group_index*hdr_one_lut_pkt_cnt + temp_index].msg.tx_len = cmd_payload_len + DIRECT_BUS_HEADER_SIZE;
				sdr2hdr_lut_send_cmd[lut_group_index*hdr_one_lut_pkt_cnt + temp_index].post_wait_ms = 0;
				sdr2hdr_lut_send_cmd[lut_group_index*hdr_one_lut_pkt_cnt + temp_index].msg.tx_buf = iris_sdr2hdr_lut_buf + iris_lut_buf_index;

				pr_debug("%s: lut_table_index=%d, iris_lut_buf_index=%d, temp_index =%d, ocp_dbus_header address=%x header=%x\n",__func__, lut_block_index, iris_lut_buf_index, temp_index, ocp_dbus_header.address, ocp_dbus_header.header);
				memcpy(&iris_sdr2hdr_lut_buf[iris_lut_buf_index], &ocp_dbus_header, DIRECT_BUS_HEADER_SIZE);
				iris_lut_buf_index += DIRECT_BUS_HEADER_SIZE;
				memcpy(&iris_sdr2hdr_lut_buf[iris_lut_buf_index], fw_data, cmd_payload_len);

				fw_data += cmd_payload_len;
				ocp_dbus_header.address += cmd_payload_len;

			}
		}
		//sdr2hdr_lut_send_cmd[lut_group_index][SDR2HDR_LUT_CMD_CNT_TOTAL/2 -1].dchdr.last = 1;// to fix AP(8096 1080p*15fps) underflow issue
		//sdr2hdr_lut_send_cmd[lut_group_index][SDR2HDR_LUT_CMD_CNT_TOTAL -1].dchdr.last = 1;

		//use ocp burst to write IP
		memset(&ocp_burst_header, 0, sizeof(ocp_burst_header));
		ocp_burst_header.header = 0x00000000;
		ocp_burst_header.address = SDR2HDR_INV_UV_ADDRESS;

		//parse INV_UV0,1
		for(lut_block_index = 0; lut_block_index < SDR2HDR_INV_UV_NUMBER; lut_block_index++){

			for(lut_pkt_cmd_index = 0; lut_pkt_cmd_index < uv_block_pkt_cnt; lut_pkt_cmd_index++) {

				iris_lut_buf_index = lut_group_index*one_group_size;
				iris_lut_buf_index += hdr_one_lut_pkt_cnt*hdr_pkt_size;
				iris_lut_buf_index += lut_pkt_cmd_index*hdr_pkt_size + lut_block_index*hdr_pkt_size*uv_block_pkt_cnt;

				if(lut_pkt_cmd_index == uv_block_pkt_cnt-1)
					cmd_payload_len = SDR2HDR_INV_UV_SIZE - (uv_block_pkt_cnt-1) * hdr_payload_size;
				else
					cmd_payload_len = hdr_payload_size;

				temp_index = lut_pkt_cmd_index+ uv_block_pkt_cnt*lut_block_index;
				sdr2hdr_inv_uv_send_cmd[lut_group_index*uv_one_lut_pkt_cnt + temp_index].msg.type = 0x29;
				sdr2hdr_inv_uv_send_cmd[lut_group_index*uv_one_lut_pkt_cnt + temp_index].msg.tx_len = cmd_payload_len + DIRECT_BUS_HEADER_SIZE;
				sdr2hdr_inv_uv_send_cmd[lut_group_index*uv_one_lut_pkt_cnt + temp_index].msg.tx_buf = iris_sdr2hdr_lut_buf + iris_lut_buf_index;

				pr_debug("%s: lut_table_index=%d, iris_lut_buf_index=%d, temp_index=%d, ocp_burst_header address=%x header=%x\n",__func__, lut_block_index, iris_lut_buf_index, temp_index, ocp_burst_header.address, ocp_burst_header.header);
				memcpy(&iris_sdr2hdr_lut_buf[iris_lut_buf_index], &ocp_burst_header, DIRECT_BUS_HEADER_SIZE);
				iris_lut_buf_index += DIRECT_BUS_HEADER_SIZE;
				memcpy(&iris_sdr2hdr_lut_buf[iris_lut_buf_index], fw_data, cmd_payload_len);

				fw_data += cmd_payload_len;
				ocp_burst_header.address += cmd_payload_len;

				//iris_dump_packet(&iris_sdr2hdr_lut_buf[iris_lut_buf_index], LUT_ONE_PKT_SIZE);

			}
		}
	}
	return IRIS_SUCCESS;

}

static void iris_sdr2hdr_lut_check(const u8 *fw_data)
{

	u32 lut_table_index = 0;

	for(lut_table_index = 0; lut_table_index < SDR2HDR_LUT_GROUP_CNT; lut_table_index++)
		iris_sdr2hdr_lut_read(lut_table_index, (fw_data + SDR2HDR_FW_START_ADDR + lut_table_index * SDR2HDR_GROUP_SIZE));
}


static void iris_scaler1d_lut_address_update(struct ocp_header *ocp_burst_header, u32 lut_pkt_cmd_index)
{
	u32 address_shift = 0;
	switch (lut_pkt_cmd_index) {
	case 0: // Y_H
		address_shift = SCALER1D_H_Y_HIGH;
		break;
	case 1: //UV_H
		address_shift = SCALER1D_H_UV_HIGH;
		break;
	case 2: // Y_L
		address_shift = SCALER1D_H_Y_LOW;
		break;
	case 3: //UV_L
		address_shift = SCALER1D_H_UV_LOW;
		break;
	case 4: // Y
		address_shift = SCALER1D_V_Y;
		break;
	case 5: //UV
		address_shift = SCALER1D_V_UV;
		break;
	}


	ocp_burst_header->address = SCALER1D_LUT_ADDRESS + address_shift;
}

static int iris_scaler1d_lut_read(u32 lut_table_index, const u8 *fw_data)
{
	u32 block_index = 0, lut_pkt_cmd_index = 0;

	u32 iris_lut_buf_index, cmd_payload_len=0;

	u32 scaler_payload_size = (payload_size >= SCALER1D_LUT_BLOCK_SIZE) ? SCALER1D_LUT_BLOCK_SIZE: payload_size;

	u32 scaler_pkt_size = scaler_payload_size + DIRECT_BUS_HEADER_SIZE;
	u32 scaler_block_pkt_cnt = (SCALER1D_LUT_BLOCK_SIZE + scaler_payload_size - 1)/scaler_payload_size;
	u32 scaler_one_lut_pkt_cnt = scaler_block_pkt_cnt*SCALER_LUT_BLOCK_NUMBER;

	const u8 *fw_data_start = fw_data;

	struct ocp_header ocp_burst_header;
	memset(&ocp_burst_header, 0, sizeof(ocp_burst_header));
	ocp_burst_header.header = 0x0010000C;
	ocp_burst_header.address = SCALER1D_LUT_ADDRESS;

	if (scaler1d_lut_send_cmd == NULL) {
		scaler1d_lut_send_cmd = kzalloc(sizeof(struct dsi_cmd_desc)*scaler_one_lut_pkt_cnt*SCALER1D_LUT_NUMBER, GFP_KERNEL);
		iris_lut_param.lut_cmd_cnts_max+= scaler_one_lut_pkt_cnt;
		iris_lut_param.scaler_one_lut_pkt_cnt = scaler_one_lut_pkt_cnt;

		pr_err("%s, iris_lut_param.lut_cmd_cnts_max = %d scaler_block_pkt_cnt =%d\n", __func__, iris_lut_param.lut_cmd_cnts_max, scaler_block_pkt_cnt);
	}

	if (iris_scaler1d_lut_buf == NULL)
		iris_scaler1d_lut_buf = kzalloc(scaler_pkt_size*scaler_one_lut_pkt_cnt*SCALER1D_LUT_NUMBER, GFP_KERNEL);

	if (!scaler1d_lut_send_cmd || !iris_scaler1d_lut_buf) {
		pr_err("%s: failed to alloc mem: lut_cmd: %p, buf: %p \n", __func__,
				scaler1d_lut_send_cmd, iris_scaler1d_lut_buf);
		return IRIS_FAILED;
	}

	{
		for(lut_pkt_cmd_index = 0; lut_pkt_cmd_index < scaler_one_lut_pkt_cnt; lut_pkt_cmd_index++) {

			iris_lut_buf_index = lut_table_index*scaler_pkt_size*scaler_one_lut_pkt_cnt + lut_pkt_cmd_index*scaler_pkt_size;

			block_index = lut_pkt_cmd_index / scaler_block_pkt_cnt;
			if (lut_pkt_cmd_index % scaler_block_pkt_cnt == 0) { //update address at new block
				iris_scaler1d_lut_address_update(&ocp_burst_header, block_index);

				fw_data = fw_data_start + block_index/2 * SCALER1D_LUT_BLOCK_SIZE;
			}

			cmd_payload_len = scaler_payload_size;

			if (SCALER1D_LUT_BLOCK_SIZE >= ((lut_pkt_cmd_index%scaler_block_pkt_cnt + 1) * scaler_payload_size))
				cmd_payload_len = scaler_payload_size;
			else
				cmd_payload_len = SCALER1D_LUT_BLOCK_SIZE - (scaler_block_pkt_cnt-1) * scaler_payload_size; // last pkt may less than payload_size


			scaler1d_lut_send_cmd[lut_pkt_cmd_index + scaler_one_lut_pkt_cnt*lut_table_index].msg.type = 0x29;
			scaler1d_lut_send_cmd[lut_pkt_cmd_index + scaler_one_lut_pkt_cnt*lut_table_index].msg.tx_len = cmd_payload_len + DIRECT_BUS_HEADER_SIZE;
			scaler1d_lut_send_cmd[lut_pkt_cmd_index + scaler_one_lut_pkt_cnt*lut_table_index].post_wait_ms = LUT_CMD_WAIT_MS;
			//scaler1d_lut_send_cmd[lut_pkt_cmd_index + scaler_one_lut_pkt_cnt*lut_table_index].last_command = (lut_pkt_cmd_index == SCALER1D_ONE_LUT_CMD_CNT-1);
			scaler1d_lut_send_cmd[lut_pkt_cmd_index + scaler_one_lut_pkt_cnt*lut_table_index].msg.tx_buf = iris_scaler1d_lut_buf + iris_lut_buf_index;

			pr_debug("%s: lut_table_index=%d, lut_pkt_cmd_index=%d,ocp_burst_header address=%x header=%x\n",__func__, lut_table_index, lut_pkt_cmd_index, ocp_burst_header.address, ocp_burst_header.header);
			memcpy(&iris_scaler1d_lut_buf[iris_lut_buf_index], &ocp_burst_header, DIRECT_BUS_HEADER_SIZE);
			iris_lut_buf_index += DIRECT_BUS_HEADER_SIZE;
			memcpy(&iris_scaler1d_lut_buf[iris_lut_buf_index], fw_data, cmd_payload_len);

			fw_data += cmd_payload_len;
			ocp_burst_header.address += cmd_payload_len;

		}
	}
	return IRIS_SUCCESS;
}
static void iris_scaler1d_lut_check(const u8 *fw_data) {
	u32 lut_table_index = 0;

	for (lut_table_index = 0; lut_table_index < SCALER1D_LUT_NUMBER; lut_table_index ++)
		iris_scaler1d_lut_read(lut_table_index, (fw_data + SCALER1D_FW_START_ADDR + lut_table_index * SCALER1D_LUT_SIZE));
}


/* only need payload, header will be overwritten by dtsi*/
static int iris_gamma_lut_read(u32 lut_table_index, const u8 *fw_data)
{
	u32 lut_pkt_cmd_index = 0;
	u32 iris_lut_buf_index, cmd_payload_len=0;

	u32 gamma_payload_size = payload_size;
	u32 gamma_pkt_size = gamma_payload_size + DIRECT_BUS_HEADER_SIZE;
	u32 gamma_one_lut_pkt_cnt = (GAMMA_LUT_SIZE + gamma_payload_size -1)/gamma_payload_size;

	struct ocp_header ocp_burst_header;

	memset(&ocp_burst_header, 0, sizeof(ocp_burst_header));
	ocp_burst_header.header = 0x00000000;
	ocp_burst_header.address = GAMMA_REG_ADDRESS; // address will be overwritten

	if (gamma_lut_send_cmd == NULL) {
		gamma_lut_send_cmd = kzalloc(sizeof(struct dsi_cmd_desc)*gamma_one_lut_pkt_cnt*GAMMA_LUT_NUMBER, GFP_KERNEL);
		iris_lut_param.lut_cmd_cnts_max += gamma_one_lut_pkt_cnt;
		iris_lut_param.gamma_one_lut_pkt_cnt = gamma_one_lut_pkt_cnt;

		pr_err("%s, iris_lut_param.lut_cmd_cnts_max = %d \n", __func__, iris_lut_param.lut_cmd_cnts_max);
	}

	if (iris_gamma_lut_buf == NULL)
		iris_gamma_lut_buf = kzalloc(gamma_pkt_size*gamma_one_lut_pkt_cnt*GAMMA_LUT_NUMBER, GFP_KERNEL);

	if (!iris_gamma_lut_buf || !gamma_lut_send_cmd) {
		pr_err("%s: failed to alloc mem: gamma_lut_send_cmd: %p, buf: %p \n", __func__,
				gamma_lut_send_cmd, iris_gamma_lut_buf);
		return IRIS_FAILED;
	}

	{

		ocp_burst_header.address = GAMMA_REG_ADDRESS;
		for(lut_pkt_cmd_index = 0; lut_pkt_cmd_index < gamma_one_lut_pkt_cnt; lut_pkt_cmd_index++) {

			iris_lut_buf_index = lut_table_index*gamma_pkt_size*gamma_one_lut_pkt_cnt + lut_pkt_cmd_index*gamma_pkt_size;

			cmd_payload_len = gamma_payload_size;
			if(lut_pkt_cmd_index == gamma_one_lut_pkt_cnt - 1)
				cmd_payload_len = GAMMA_LUT_SIZE - gamma_payload_size * lut_pkt_cmd_index;

			gamma_lut_send_cmd[lut_table_index*gamma_one_lut_pkt_cnt+lut_pkt_cmd_index].msg.type = 0x29;
			gamma_lut_send_cmd[lut_table_index*gamma_one_lut_pkt_cnt+lut_pkt_cmd_index].msg.tx_len = cmd_payload_len + DIRECT_BUS_HEADER_SIZE;
			gamma_lut_send_cmd[lut_table_index*gamma_one_lut_pkt_cnt+lut_pkt_cmd_index].post_wait_ms = LUT_CMD_WAIT_MS;
			gamma_lut_send_cmd[lut_table_index*gamma_one_lut_pkt_cnt+lut_pkt_cmd_index].msg.tx_buf = iris_gamma_lut_buf + iris_lut_buf_index;

			pr_debug("%s: lut_table_index=%d, lut_pkt_cmd_index=%d,ocp_burst_header address=%x header=%x\n",__func__, lut_table_index, lut_pkt_cmd_index, ocp_burst_header.address, ocp_burst_header.header);
			memcpy(&iris_gamma_lut_buf[iris_lut_buf_index], &ocp_burst_header, DIRECT_BUS_HEADER_SIZE);
			iris_lut_buf_index += DIRECT_BUS_HEADER_SIZE;
			memcpy(&iris_gamma_lut_buf[iris_lut_buf_index], fw_data, cmd_payload_len);

			fw_data += cmd_payload_len;
			ocp_burst_header.address += cmd_payload_len;

		}
	}
	return IRIS_SUCCESS;
}
static void iris_gamma_lut_check(const u8 *fw_data)
{
	u32 lut_table_index;
	for (lut_table_index = 0; lut_table_index < GAMMA_LUT_NUMBER; lut_table_index ++)
		iris_gamma_lut_read(lut_table_index, (fw_data + GAMMA_FW_START_ADDR + lut_table_index * GAMMA_LUT_SIZE));
}

static void iris_panel_nits_check(const u8 *fw_data)
{
	struct iris_cfg *pcfg = iris_get_cfg();
	pcfg->panel_nits = *(fw_data + PANEL_NITS_FW_START_ADDR + 1);
	pcfg->panel_nits = (pcfg->panel_nits << 8)
		+ *(fw_data + PANEL_NITS_FW_START_ADDR);
}

static void iris_cm_lut_gs_parse(const u8 *fw_data)
{
	uint32_t start_index = 0;
	int i, j;

	start_index = CM_FW_START_ADDR;

	for (i = 0; i < CM_LUT_NUMBER; i++) {
		pr_debug("start_index=0x%x\n", start_index);
		for (j = 0; j < CM_LUT_BLOCK_NUMBER; j++) {
			memcpy(&CM_LUT_GS[i][j][0], fw_data + start_index + j * CM_LUT_BLOCK_SIZE, CM_LUT_BLOCK_SIZE);
		}
		start_index += CM_LUT_SIZE;
	}

}

static void iris_gamma_lut_gs_parse(const u8 *fw_data)
{
	int i;

	for(i = 0; i < 8; i++) {
		memcpy(&GAMMA_LUT_GS[i][0], fw_data + GAMMA_FW_START_ADDR + i * GAMMA_LUT_SIZE, GAMMA_LUT_SIZE);
	}
}

static int iris_lut_data_read(const u8 *fw_data, size_t fw_size, int for_i2c)
{
	struct iris_cfg *pcfg = NULL;

	pcfg = iris_get_cfg();

	if(fw_size != IRIS_FW_SIZE)
		pr_err("%s: fw_size is wrong should =%d bytes\n", __func__, (IRIS_FW_SIZE));
	else {
		if (for_i2c == 0) {
			iris_dbc_lut_read(fw_data);
			iris_cm_lut_check(fw_data);
			iris_sdr2hdr_lut_check(fw_data);
			iris_scaler1d_lut_check(fw_data);
			iris_gamma_lut_check(fw_data);
			iris_panel_nits_check(fw_data);

			iris_set_lut_cnt(iris_lut_param.lut_cmd_cnts_max);
		} else {
			iris_cm_lut_gs_parse(fw_data);
			iris_gamma_lut_gs_parse(fw_data);
			if (false == m_gs_lut_parsed) {
				INIT_WORK(&pcfg->lut_update_work, __lut_update_work_handler);
				m_gs_lut_parsed = true;
			}
		}
	}

	return IRIS_SUCCESS;
}

int iris3_parse_lut_cmds(struct device *device, const char *name)
{
	const struct firmware *fw = NULL;
	int ret = 0, result = IRIS_SUCCESS;
	int for_i2c;

	//init buf before parse. parce once is ok.
	for_i2c = (strcmp(name, IRIS3_FIRMWARE_GS_NAME) == 0) ? 1 : 0;
	if (!for_i2c) {
		iris_lut_buf_init();
	}

	if (name) {
		/* Firmware file must be in /vendor/firmware/ */
		ret = request_firmware(&fw, name, device);
		if (ret) {
			pr_err("%s: failed to request firmware: %s, ret = %d\n",
					__func__, name, ret);
			result = IRIS_FAILED;
		} else {
			pr_info("%s: request firmware: name = %s, size = %zu bytes\n",
						__func__, name, fw->size);
			iris_lut_data_read(fw->data, fw->size, for_i2c);
			release_firmware(fw);
		}
	} else {
		pr_err("%s: firmware is null\n", __func__);
		result = IRIS_FAILED;
	}

	return result;
}

/******************************
**** for DBC, lut_table_index means DBC_LEVEL;lut_abtable_index means AB_table.
****: EX: lut_table_index=DBC_INIT, lut_abtable_index=0; means send income_BL A table to iris.
****
**** bSendFlag means send commands out or not;
**** bSendFlag = true, means send out directly.
**** bSendFlag = false, means just pack it and send it together with next command
********************************/

int iris_lut_send(u8 lut_type, u8 lut_table_index, u32 lut_abtable_index, bool bSendFlag)
{
	// used to tranfser IP domin luts.
	static struct dsi_panel_cmd_set panel_cmds;
	struct iris_cfg * pcfg;
	u8 ip = 0xff;
	/*fixed for Lut table update in lightup sequence*/
	u8 opt_id = 0xfe;
	u8 type = IRIS_SEND_LUT_BY_DMA;
	u32 cmds_cnt_temp;
	pcfg = iris_get_cfg();

	cmds_cnt_temp = 0;

	switch(lut_type){
		case DBC_LUT:

			if (!iris_dbc_lut_buf || !dbc_lut_income_send_cmd || !dbc_lut_compk_send_cmd)
				break;
			if(lut_table_index > DBC_HIGH)
				break;

			//lut_abtable_index will be used at AB_table index here.
			if(lut_abtable_index > 0)
				lut_abtable_index = 1;

			if (lut_table_index == DBC_INIT) {

				memcpy(&pcfg->iris_cmds.iris_cmds_buf[pcfg->iris_cmds.cmds_index],
					&dbc_lut_income_send_cmd[lut_abtable_index * iris_lut_param.dbc_income_pkt_cnt], sizeof(struct dsi_cmd_desc)*iris_lut_param.dbc_income_pkt_cnt);
				pcfg->iris_cmds.cmds_index += iris_lut_param.dbc_income_pkt_cnt;
			} else {

				memcpy(&pcfg->iris_cmds.iris_cmds_buf[pcfg->iris_cmds.cmds_index],
					&dbc_lut_compk_send_cmd[(lut_abtable_index + (lut_table_index-1)*AB_TABLE) * iris_lut_param.dbc_compk_pkt_cnt], sizeof(struct dsi_cmd_desc)*iris_lut_param.dbc_compk_pkt_cnt);
				pcfg->iris_cmds.cmds_index += iris_lut_param.dbc_compk_pkt_cnt;
			}
			pr_debug("%s: call DBC_LUT\n", __func__);
		break;
		case CM_LUT:
			if (!iris_cm_lut_buf || !cm_lut_send_cmd)
				break;
			if (lut_table_index >= CM_LUT_NUMBER)
				break;

			memcpy(&pcfg->iris_cmds.iris_cmds_buf[pcfg->iris_cmds.cmds_index],
				&cm_lut_send_cmd[iris_lut_param.cm_one_lut_pkt_cnt*lut_table_index], sizeof(struct dsi_cmd_desc)*iris_lut_param.cm_one_lut_pkt_cnt);
			pcfg->iris_cmds.cmds_index += iris_lut_param.cm_one_lut_pkt_cnt;

		break;
		case SDR2HDR_LUT:
			if (!iris_sdr2hdr_lut_buf || !sdr2hdr_lut_send_cmd)
				break;
			if (lut_table_index >= SDR2HDR_LUT_GROUP_CNT)
				break;

			memcpy(&pcfg->iris_cmds.iris_cmds_buf[pcfg->iris_cmds.cmds_index],
				&sdr2hdr_lut_send_cmd[lut_table_index * iris_lut_param.hdr_one_lut_pkt_cnt], sizeof(struct dsi_cmd_desc)*iris_lut_param.hdr_one_lut_pkt_cnt);
			pcfg->iris_cmds.cmds_index += iris_lut_param.hdr_one_lut_pkt_cnt;

		break;
		case SCALER1D_LUT:
			if (!iris_scaler1d_lut_buf || !scaler1d_lut_send_cmd)
				break;
			if (lut_table_index >= SCALER1D_LUT_NUMBER)
				break;

			memcpy(&pcfg->iris_cmds.iris_cmds_buf[pcfg->iris_cmds.cmds_index],
				&scaler1d_lut_send_cmd[iris_lut_param.scaler_one_lut_pkt_cnt*lut_table_index], sizeof(struct dsi_cmd_desc)*iris_lut_param.scaler_one_lut_pkt_cnt);
			pcfg->iris_cmds.cmds_index += iris_lut_param.scaler_one_lut_pkt_cnt;

		break;
		case AMBINET_HDR_GAIN:
			if (!iris_maxcll_lut_buf || !dynamic_lutuvy_send_cmd)
				break;

			memcpy(&pcfg->iris_cmds.iris_cmds_buf[pcfg->iris_cmds.cmds_index], \
					&dynamic_lutuvy_send_cmd[0], \
					sizeof(struct dsi_cmd_desc)*iris_lut_param.hdr_lutuvy_pkt_cnt);

			pcfg->iris_cmds.cmds_index += iris_lut_param.hdr_lutuvy_pkt_cnt;
			pr_err("%s\n", __func__);
		break;

		case AMBINET_SDR2HDR_LUT:
			if (!iris_ambient_lut_buf || !dynamic_lut_send_cmd)
				break;

			memcpy(&pcfg->iris_cmds.iris_cmds_buf[pcfg->iris_cmds.cmds_index], \
					&dynamic_lut_send_cmd[0], \
					sizeof(struct dsi_cmd_desc)*iris_lut_param.hdr_lut2_pkt_cnt);

			pcfg->iris_cmds.cmds_index += iris_lut_param.hdr_lut2_pkt_cnt;

		break;

		default:
			pr_debug("%s:type of %d have no cmd \n", __func__, lut_type);
		break;

	}

	pr_debug("%s: called lut_type=%d lut_table_index=%d lut_abtable_index=%d cmd_cnt %d, max_cnt =%d bSendFlag=%d\n", __func__,
					lut_type, lut_table_index, lut_abtable_index,
					pcfg->iris_cmds.cmds_index, iris_lut_param.lut_cmd_cnts_max, bSendFlag);
#if 0
	if(pcfg->iris_cmds.cmds_index != 0 && bSendFlag == true) {
		panel_lut_cmds.cmds = pcfg->iris_cmds.iris_cmds_buf;
		panel_lut_cmds.count = pcfg->iris_cmds.cmds_index;
		panel_lut_cmds.cmds[pcfg->iris_cmds.cmds_index-1].last_command = true;
		iris3_dsi_cmds_send(pcfg->panel, panel_lut_cmds.cmds, panel_lut_cmds.count, panel_lut_cmds.state);
		//reset mem
		iris_lut_out_cmds_init();
	}
#endif
	panel_cmds.count = 0;
	switch(lut_type){
		case DBC_LUT:

			if (!iris_dbc_lut_buf || !dbc_lut_cabc_dlv_send_cmd)
				break;

			ip = IRIS_IP_DBC;
			if (lut_table_index >= DBC_OFF && lut_table_index <= DBC_HIGH) {
				panel_cmds.cmds = &dbc_lut_cabc_dlv_send_cmd[(lut_table_index-1)*iris_lut_param.dbc_dlv_pkt_cnt];
				panel_cmds.count = iris_lut_param.dbc_dlv_pkt_cnt;
			}

		break;
		case SDR2HDR_LUT:
			if (!iris_sdr2hdr_lut_buf || !sdr2hdr_inv_uv_send_cmd)
				break;
			// for sdr2hdr's case 709->709, 709->p3, 709->2020, should write PI.
			if( lut_table_index < 3)
				break;

			ip = IRIS_IP_SDR2HDR;
			panel_cmds.cmds = &sdr2hdr_inv_uv_send_cmd[lut_table_index * iris_lut_param.uv_one_lut_pkt_cnt];
			panel_cmds.count = iris_lut_param.uv_one_lut_pkt_cnt;

		break;

		case GAMMA_LUT:
			if (!iris_gamma_lut_buf || !gamma_lut_send_cmd)
				break;

			ip = IRIS_IP_DPP;
			panel_cmds.cmds = &gamma_lut_send_cmd[lut_table_index * iris_lut_param.gamma_one_lut_pkt_cnt];
			panel_cmds.count = iris_lut_param.gamma_one_lut_pkt_cnt;

		break;
		default:
			pr_debug("%s:type of %d have no register level cmd \n", __func__, lut_type);
		break;

	}

	pr_debug("%s: cmd_cnt %d\n", __func__, panel_cmds.count);

	if(panel_cmds.count != 0) {
		if (type == IRIS_SEND_LUT_BY_CMDLIST) {
			panel_cmds.state = DSI_CMD_SET_STATE_HS;
			iris3_dsi_cmds_send(pcfg->panel, panel_cmds.cmds, panel_cmds.count, panel_cmds.state);
		} else if (type == IRIS_SEND_LUT_BY_DMA) {
			int32_t rc = 0;
			if (ip != 0xff)
				rc = iris_update_lut_payload(ip, opt_id, &panel_cmds);
			else
				rc = -1;
			if (rc != 0) {
				pr_err("update lut payload is error\n");
				return -EINVAL;
			}
		}
	}

	return IRIS_SUCCESS;
}

void iris_ambient_lut_update(enum LUT_TYPE lutType)
{

	u32 hdr_payload_size = payload_size;
	u32 hdr_pkt_size = hdr_payload_size + DIRECT_BUS_HEADER_SIZE;
	u32 hdr_block_pkt_cnt = (SDR2HDR_LUT_BLOCK_SIZE + hdr_payload_size - 1)/hdr_payload_size;

	u32 iris_lut_buf_index, lut_block_index, lut_block_cnt,lut_pkt_cmd_index;
	u32 temp_index, index_i;
	u32 dbus_addr_start;

	u32 lut_fw_index;
	u32 cmd_payload_len;

	struct ocp_header ocp_dbus_header;

	memset(&ocp_dbus_header, 0, sizeof(ocp_dbus_header));
	ocp_dbus_header.header = 0x0004000C;
	ocp_dbus_header.address = SDR2HDR_LUT2_ADDRESS;

	if (lutType == AMBINET_SDR2HDR_LUT) {
		dbus_addr_start = SDR2HDR_LUT2_ADDRESS;
		lut_block_cnt = SDR2HDR_LUT2_BLOCK_CNT;

		// copy lut2 to the firmware format.
		// lut2 is EVEN+ODD, LUT2_fw is  EVEN ODD EVEN ODD EVEN ODD
		for(index_i = 0; index_i < LUT_LEN; index_i++) {

			if(index_i%2 == 0) // EVEN
				lut_fw_index = index_i/2;
			else // ODD
				lut_fw_index = index_i/2 + LUT_LEN/2;

			LUT2_fw[lut_fw_index] = lut_lut2[index_i];
			LUT2_fw[lut_fw_index + LUT_LEN] = lut_lut2[index_i];
			LUT2_fw[lut_fw_index + LUT_LEN + LUT_LEN] = lut_lut2[index_i];
		}
	} else {
		pr_err("%s input lutType error %d\n", __func__, lutType);
		return;
	}

	if (dynamic_lut_send_cmd == NULL) {
		dynamic_lut_send_cmd = kzalloc(sizeof(struct dsi_cmd_desc)*hdr_pkt_size*hdr_block_pkt_cnt*SDR2HDR_LUT2_BLOCK_NUMBER, GFP_KERNEL);
		iris_lut_param.lut_cmd_cnts_max+= hdr_block_pkt_cnt*SDR2HDR_LUT2_BLOCK_NUMBER;
		iris_lut_param.hdr_lut2_pkt_cnt = hdr_block_pkt_cnt*SDR2HDR_LUT2_BLOCK_NUMBER;
	}

	if (iris_ambient_lut_buf)
		memset(iris_ambient_lut_buf, 0, hdr_pkt_size*iris_lut_param.hdr_lut2_pkt_cnt);

	if (!iris_ambient_lut_buf)
		iris_ambient_lut_buf = kzalloc(hdr_pkt_size*iris_lut_param.hdr_lut2_pkt_cnt, GFP_KERNEL);

	if (!iris_ambient_lut_buf) {
		pr_err("%s: failed to alloc mem\n", __func__);
		return;
	}

	lut_fw_index = 0;
	//parse LUT2
	for(lut_block_index = 0; lut_block_index < lut_block_cnt; lut_block_index++){

		ocp_dbus_header.address = dbus_addr_start + lut_block_index*SDR2HDR_LUT_BLOCK_ADDRESS_INC;
		for(lut_pkt_cmd_index = 0; lut_pkt_cmd_index < hdr_block_pkt_cnt; lut_pkt_cmd_index++) {

			iris_lut_buf_index = lut_block_index*hdr_pkt_size*hdr_block_pkt_cnt + lut_pkt_cmd_index*hdr_pkt_size;

			if(lut_pkt_cmd_index == hdr_block_pkt_cnt-1)
				cmd_payload_len = SDR2HDR_LUT_BLOCK_SIZE - (hdr_block_pkt_cnt-1) * hdr_payload_size;
			else
				cmd_payload_len = hdr_payload_size;

			temp_index = lut_pkt_cmd_index+ hdr_block_pkt_cnt*lut_block_index;
			dynamic_lut_send_cmd[temp_index].msg.type = 0x29;
			dynamic_lut_send_cmd[temp_index].msg.tx_len = cmd_payload_len + DIRECT_BUS_HEADER_SIZE;
			dynamic_lut_send_cmd[temp_index].post_wait_ms = 0;
			dynamic_lut_send_cmd[temp_index].msg.tx_buf = iris_ambient_lut_buf + iris_lut_buf_index;

			memcpy(&iris_ambient_lut_buf[iris_lut_buf_index], &ocp_dbus_header, DIRECT_BUS_HEADER_SIZE);
			iris_lut_buf_index += DIRECT_BUS_HEADER_SIZE;

			memcpy(&iris_ambient_lut_buf[iris_lut_buf_index], &LUT2_fw[lut_fw_index], cmd_payload_len);

			lut_fw_index += cmd_payload_len/4;
			ocp_dbus_header.address += cmd_payload_len;


		}
	}
}

void iris_maxcll_lut_update(enum LUT_TYPE lutType)
{
#if 0
	int i;
	lutType = lutType;
	for(i=0; i<LUT_LEN; i++) {
		pr_err("luty[%d] = %d\n", i, lut_luty[i]);
	}
	for(i=0; i<LUT_LEN; i++) {
		pr_err("lutuv[%d] = %d\n", i, lut_lutuv[i]);
	}
#endif
	u32 hdr_payload_size = payload_size;
	u32 hdr_pkt_size = hdr_payload_size + DIRECT_BUS_HEADER_SIZE;
	u32 hdr_block_pkt_cnt = (SDR2HDR_LUT_BLOCK_SIZE + hdr_payload_size - 1)/hdr_payload_size;

	u32 iris_lut_buf_index, lut_block_index, lut_block_cnt,lut_pkt_cmd_index;
	u32 temp_index, index_i;
	u32 dbus_addr_start;

	u32 lut_fw_index;
	u32 cmd_payload_len;
	u32 dynamic_lutuvy_size;
	u32 maxcll_lut_size;

	struct ocp_header ocp_dbus_header;

	memset(&ocp_dbus_header, 0, sizeof(ocp_dbus_header));
	ocp_dbus_header.header = 0x0004000C;
	ocp_dbus_header.address = SDR2HDR_LUTUVY_ADDRESS;

	if (lutType == AMBINET_HDR_GAIN) {
		dbus_addr_start = SDR2HDR_LUTUVY_ADDRESS;
		lut_block_cnt = SDR2HDR_LUTUVY_BLOCK_CNT;

		// copy lutuvy to the firmware format.
		// lutuvy is EVEN+ODD, LUT2_fw is  EVEN ODD EVEN ODD EVEN ODD
		for(index_i = 0; index_i < LUT_LEN; index_i++) {

			if(index_i%2 == 0) // EVEN
				lut_fw_index = index_i/2;
			else // ODD
				lut_fw_index = index_i/2 + LUT_LEN/2;

			LUTUVY_fw[lut_fw_index] = lut_lutuv[index_i];
			LUTUVY_fw[lut_fw_index + LUT_LEN] = lut_lutuv[index_i];
			LUTUVY_fw[lut_fw_index + LUT_LEN + LUT_LEN] = lut_lutuv[index_i];
			LUTUVY_fw[lut_fw_index + LUT_LEN + LUT_LEN+ LUT_LEN] = lut_lutuv[index_i];
			LUTUVY_fw[lut_fw_index + LUT_LEN + LUT_LEN+ LUT_LEN+ LUT_LEN] = lut_luty[index_i];
			LUTUVY_fw[lut_fw_index + LUT_LEN + LUT_LEN+ LUT_LEN+ LUT_LEN+ LUT_LEN] = lut_luty[index_i];
		}
	} else {
		pr_err("%s input lutType error %d\n", __func__, lutType);
		return;
	}

	if (dynamic_lutuvy_send_cmd == NULL) {
		dynamic_lutuvy_size = sizeof(struct dsi_cmd_desc)*hdr_pkt_size*hdr_block_pkt_cnt*SDR2HDR_LUTUVY_BLOCK_NUMBER;
		dynamic_lutuvy_send_cmd = kzalloc(dynamic_lutuvy_size, GFP_KERNEL);
		iris_lut_param.lut_cmd_cnts_max+= hdr_block_pkt_cnt*SDR2HDR_LUTUVY_BLOCK_NUMBER;
		iris_lut_param.hdr_lutuvy_pkt_cnt = hdr_block_pkt_cnt*SDR2HDR_LUTUVY_BLOCK_NUMBER;

		if (dynamic_lutuvy_send_cmd == NULL) {
			pr_err("%s: failed to alloc lutuvy mem, size:%u\n", __func__, dynamic_lutuvy_size);
			return;
		}
	}

	maxcll_lut_size = hdr_pkt_size*iris_lut_param.hdr_lutuvy_pkt_cnt;
	if (iris_maxcll_lut_buf != NULL)
		memset(iris_maxcll_lut_buf, 0, maxcll_lut_size);
	else
		iris_maxcll_lut_buf = kzalloc(maxcll_lut_size, GFP_KERNEL);

	if (iris_maxcll_lut_buf == NULL) {
		pr_err("%s: failed to alloc maxcll lut mem, size:%u\n", __func__, maxcll_lut_size);
		return;
	}

	lut_fw_index = 0;
	//parse LUTUVY
	for(lut_block_index = 0; lut_block_index < lut_block_cnt; lut_block_index++){

		ocp_dbus_header.address = dbus_addr_start + lut_block_index*SDR2HDR_LUT_BLOCK_ADDRESS_INC;
		for(lut_pkt_cmd_index = 0; lut_pkt_cmd_index < hdr_block_pkt_cnt; lut_pkt_cmd_index++) {

			iris_lut_buf_index = lut_block_index*hdr_pkt_size*hdr_block_pkt_cnt + lut_pkt_cmd_index*hdr_pkt_size;

			if(lut_pkt_cmd_index == hdr_block_pkt_cnt-1)
				cmd_payload_len = SDR2HDR_LUT_BLOCK_SIZE - (hdr_block_pkt_cnt-1) * hdr_payload_size;
			else
				cmd_payload_len = hdr_payload_size;

			temp_index = lut_pkt_cmd_index+ hdr_block_pkt_cnt*lut_block_index;
			dynamic_lutuvy_send_cmd[temp_index].msg.type = 0x29;
			dynamic_lutuvy_send_cmd[temp_index].msg.tx_len = cmd_payload_len + DIRECT_BUS_HEADER_SIZE;
			dynamic_lutuvy_send_cmd[temp_index].post_wait_ms = 0;
			dynamic_lutuvy_send_cmd[temp_index].msg.tx_buf = iris_maxcll_lut_buf + iris_lut_buf_index;

			memcpy(&iris_maxcll_lut_buf[iris_lut_buf_index], &ocp_dbus_header, DIRECT_BUS_HEADER_SIZE);
			iris_lut_buf_index += DIRECT_BUS_HEADER_SIZE;

			memcpy(&iris_maxcll_lut_buf[iris_lut_buf_index], &LUTUVY_fw[lut_fw_index], cmd_payload_len);

			lut_fw_index += cmd_payload_len/4;
			ocp_dbus_header.address += cmd_payload_len;


		}
	}
}



int iris_gamma_lut_update(u32 lut_table_index, const u32 *fw_data)
{
	u16 *gammaformat = NULL;
	int i;

	pr_info("%s, index = %d\n", __func__,lut_table_index);
	gammaformat = kmalloc(sizeof(u16) * GAMMA_LUT_LENGTH, GFP_KERNEL);

	memset(gammaformat, 0, GAMMA_LUT_LENGTH * sizeof(u16));

	for(i=0; i<GAMMA_LUT_LENGTH-3; i++) {
		if (i<66) {
			gammaformat[i] = (u16)fw_data[i];
		} else if(i>=66 && i<130) {
			gammaformat[i+1] = (u16)fw_data[i];
		} else {
			gammaformat[i+2] = (u16)fw_data[i];
		}
	}
	iris_gamma_lut_read(lut_table_index, (u8 *)gammaformat);
	kfree(gammaformat);
	return IRIS_SUCCESS;
}

int iris_cm_lut_gs_update(struct iris_cfg *pcfg)
{
	u32 start_addr;
	u32 cm_lut_addr;
	int gamut_mode;
	int start_lut_index;
	int end_lut_index;
	int i;

	struct quality_setting *pqlt_cur_setting = & iris_setting.quality_gs;

	cm_lut_addr = CM_LUT_BASE_PI_ADDR;
	gamut_mode = pqlt_cur_setting->pq_setting.cmcolorgamut;
	start_lut_index = gamut_mode * CM_LUT_GROUP;
	end_lut_index = start_lut_index + CM_LUT_GROUP;

	pr_info("%s, station gamut mode=%d\n", __func__, gamut_mode);

	for (; start_lut_index < end_lut_index; start_lut_index++) {

		for (i=0; i < CM_LUT_BLOCK_NUMBER; i++) {
			start_addr = cm_lut_addr + start_lut_index%CM_LUT_GROUP * CM_LUT_BLOCK_SIZE + i * CM_LUT_BLOCK_ADDRESS_INC;
			pr_debug("CM%d_%d start_addr=0x%08x\n", start_lut_index, i, start_addr);
			if (pcfg->iris3_i2c_burst_write(start_addr, &CM_LUT_GS[start_lut_index][i][0], 729) < 0) {
				pr_err("[iris3] %s: i2c burst write cm LUT fails\n",  __func__);
				return -1;
			}
		}
	}

	return 0;
}

int iris_gamma_lut_gs_update(struct iris_cfg *pcfg)
{
	int gamut_mode;
	struct quality_setting *pqlt_cur_setting = & iris_setting.quality_gs;

	gamut_mode = pqlt_cur_setting->pq_setting.cmcolorgamut;

	if (pcfg->iris3_i2c_burst_write(GAMMA_REG_ADDRESS, &GAMMA_LUT_GS[gamut_mode+1][0], GAMMA_LUT_SIZE/4) < 0) {
		pr_err("[iris3] %s: i2c burst write gamma LUT fails\n", __func__);
		return -1;
	}

	return 0;
}

static void __lut_update_work_handler(struct work_struct *work)
{
	struct iris_cfg *pcfg = NULL;
	int i;

	pcfg = iris_get_cfg();

	mutex_lock(&pcfg->gs_mutex);
	if (pcfg->iris3_i2c_write) {

		pr_err("[iris3] %s: start to update FW\n", __func__);
		// update cm lut
		if (iris_cm_lut_gs_update(pcfg) < 0) {
			pr_err("[iris3] %s: update game station iris3 cm LUT fails\n", __func__);
		} else {
			pr_err("[iris3] %s: cm LUT updated\n", __func__);

			// update gamma lut
			if (iris_gamma_lut_gs_update(pcfg) < 0) {
				pr_err("[iris3] %s: update game station iris3 gamma LUT fails\n", __func__);
			} else {
				pr_err("[iris3] %s: gamma LUT updated\n", __func__);

				// all luts updated, enable lut
				for (i = 0; i < sizeof(ENABLE_LUT)/sizeof(ENABLE_LUT[0]); i+=2) {
					if (pcfg->iris3_i2c_write(ENABLE_LUT[i], ENABLE_LUT[i+1]) < 0) {
						pr_err("[iris3] %s: i2c set reg fails, reg=0x%08x, val=0x%08x\n", __func__, ENABLE_LUT[i], ENABLE_LUT[i+1]);
						mutex_unlock(&pcfg->gs_mutex);
						return;
					}
				}
				iris3_stn_update_fw_complete = 1; //ASUS_BSP
			}
		}
	} else {
		pr_err("[iris3] %s: Game Station is not connected\n", __func__);
	}
	mutex_unlock(&pcfg->gs_mutex);
}

int iris3_update_gamestation_fw(int force_parse)
{
	struct iris_cfg *pcfg = NULL;

	pcfg = iris_get_cfg();

	if (!m_gs_lut_parsed || force_parse) {
		if (iris3_parse_lut_cmds(&pcfg->display->pdev->dev, IRIS3_FIRMWARE_GS_NAME) < 0) {
			pr_err("[iris3] Load game station iris3 firmware fails\n");
			return -1;
		}
	}

	if (pcfg->iris3_i2c_write) {
		schedule_work(&pcfg->lut_update_work);
	}

	return 0;
}
EXPORT_SYMBOL(iris3_update_gamestation_fw);
