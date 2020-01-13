/* Copyright (c) 2017, The Linux Foundation. All rights reserved.
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

#include <linux/of.h>
#include <linux/of_gpio.h>
#include <cam_sensor_cmn_header.h>
#include <cam_sensor_util.h>
#include <cam_sensor_io.h>
#include <cam_req_mgr_util.h>
#include "cam_actuator_soc.h"
#include "cam_soc_util.h"

static int cam_actuator_get_dt_i2c_info(struct cam_actuator_ctrl_t *a_ctrl)
{
	int                             rc = 0;
	struct cam_hw_soc_info         *soc_info = &a_ctrl->soc_info;
	struct cam_actuator_soc_private     *soc_private = (struct cam_actuator_soc_private *)soc_info->soc_private;
	struct cam_actuator_i2c_info_t      *i2c_info = &soc_private->i2c_info;
	struct device_node             *of_node = NULL;
	uint32_t id_info[3];

	if (!soc_info->dev) {
		CAM_ERR(CAM_ACTUATOR, "soc_info is not initialized");
		return -EINVAL;
	}

	of_node = soc_info->dev->of_node;
	if (!of_node) {
		CAM_ERR(CAM_ACTUATOR, "dev.of_node NULL");
		return -EINVAL;
	}

	rc = of_property_read_u32_array(of_node, "qcom,slave-id",
		id_info, 3);
	if (rc < 0) {
		CAM_ERR(CAM_ACTUATOR, "read slave id failed, rc %d",rc);
		return rc;
	}
	rc = of_property_read_u8(of_node, "qcom,i2c-freq-mode",&i2c_info->i2c_freq_mode);
	if (rc < 0) {
		CAM_ERR(CAM_ACTUATOR,"i2c-freq-mode read fail, rc %d. Setting to 0\n",rc);
		i2c_info->i2c_freq_mode = 0;
	}
	i2c_info->slave_addr = id_info[0];
	i2c_info->id_register = id_info[1];
	i2c_info->chip_id = id_info[2];
	CAM_INFO(CAM_ACTUATOR,"Get from DT, slave addr 0x%x, id_reg 0x%x, chip_id 0x%x",
					 i2c_info->slave_addr,
					 i2c_info->id_register,
					 i2c_info->chip_id
					);
	return rc;
}

int32_t cam_actuator_parse_dt(struct cam_actuator_ctrl_t *a_ctrl,
	struct device *dev)
{
	int32_t                         rc = 0;
	int                             i;
	struct cam_hw_soc_info          *soc_info = &a_ctrl->soc_info;
	struct cam_actuator_soc_private *soc_private =
		(struct cam_actuator_soc_private *)a_ctrl->soc_info.soc_private;
	struct cam_sensor_power_ctrl_t  *power_info = &soc_private->power_info;
	struct device_node              *of_node = NULL;

	/* Initialize mutex */
	mutex_init(&(a_ctrl->actuator_mutex));

	rc = cam_soc_util_get_dt_properties(soc_info);
	if (rc < 0) {
		CAM_ERR(CAM_ACTUATOR, "parsing common soc dt(rc %d)", rc);
		return rc;
	}

	of_node = soc_info->dev->of_node;

	//ASUS_BSP +++ Zhengwei "read id register when probe"
	power_info->dev = a_ctrl->soc_info.dev;
	/* Initialize default parameters */
	for (i = 0; i < soc_info->num_clk; i++) {
		soc_info->clk[i] = devm_clk_get(soc_info->dev,
					soc_info->clk_name[i]);
		if (!soc_info->clk[i]) {
			CAM_ERR(CAM_ACTUATOR, "get failed for %s",
				 soc_info->clk_name[i]);
			rc = -ENOENT;
			return rc;
		}
	}

	rc = cam_actuator_get_dt_i2c_info(a_ctrl);
	if(rc < 0)
	{
		CAM_ERR(CAM_ACTUATOR, "Get i2c info failed!");
		return -EINVAL;
	}
#if 0
	rc = cam_get_dt_power_setting_data(of_node,soc_info,power_info);
	if(rc < 0 || power_info->power_setting_size <= 0)
	{
		CAM_ERR(CAM_ACTUATOR, "Get power setting failed!");
		return -EINVAL;
	}
	rc = msm_camera_fill_vreg_params(
		soc_info,
		power_info->power_setting,
		power_info->power_setting_size);
	if (rc) {
		CAM_ERR(CAM_ACTUATOR,
			"failed to fill vreg params for power up rc:%d", rc);
		return rc;
	}
	rc = msm_camera_fill_vreg_params(
		soc_info,
		power_info->power_down_setting,
		power_info->power_down_setting_size);
	if (rc) {
		CAM_ERR(CAM_ACTUATOR,
			"failed to fill vreg params power down rc:%d", rc);
		return rc;
	}
	//ASUS_BSP --- Zhengwei "read id register when probe"
#endif
	if (a_ctrl->io_master_info.master_type == CCI_MASTER) {
		rc = of_property_read_u32(of_node, "cci-master",
			&(a_ctrl->cci_i2c_master));
		CAM_DBG(CAM_ACTUATOR, "cci-master %d, rc %d",
			a_ctrl->cci_i2c_master, rc);
		if ((rc < 0) || (a_ctrl->cci_i2c_master >= MASTER_MAX)) {
			CAM_ERR(CAM_ACTUATOR,
				"Wrong info: rc: %d, dt CCI master:%d",
				rc, a_ctrl->cci_i2c_master);
			rc = -EFAULT;
			return rc;
		}
	}

	if (!soc_info->gpio_data) {
		CAM_INFO(CAM_ACTUATOR, "No GPIO found");
		rc = 0;
		return rc;
	}

	if (!soc_info->gpio_data->cam_gpio_common_tbl_size) {
		CAM_INFO(CAM_ACTUATOR, "No GPIO found");
		return -EINVAL;
	}

	rc = cam_sensor_util_init_gpio_pin_tbl(soc_info,
		&power_info->gpio_num_info);
	if ((rc < 0) || (!power_info->gpio_num_info)) {
		CAM_ERR(CAM_ACTUATOR, "No/Error Actuator GPIOs");
		return -EINVAL;
	}
	return rc;
}
