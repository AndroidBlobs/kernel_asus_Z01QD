#ifndef ASUS_OIS_H
#define ASUS_OIS_H

#include "cam_ois_dev.h"

extern uint8_t g_ois_status;//ATD
extern char g_module_vendor[8];//ATD
extern uint32_t g_fw_version;
extern uint8_t g_ois_mode;//ATD

extern uint8_t g_ois_power_state;
extern uint8_t g_ois_camera_open;

extern void asus_ois_init(struct cam_ois_ctrl_t * ctrl);
extern void ois_probe_check(void);
extern uint8_t get_ois_probe_status(void);
extern void set_ois_module_vendor_from_eeprom(uint8_t vendor_id);
extern void set_ois_module_sn_from_eeprom(uint32_t sn);

extern void track_mode_change_from_i2c_write(struct cam_sensor_i2c_reg_setting * setting);

extern void ois_lock(void);
extern void ois_unlock(void);
extern void ois_wait_process(void);

extern int ois_busy_job_trylock(void);
extern void ois_busy_job_unlock(void);

extern uint8_t ois_allow_vcm_move(void);

extern void asus_ois_init_config(void);
extern void asus_ois_deinit_config(void);

extern void set_ois_afc_data_from_eeprom(uint32_t dac_10cm, uint32_t dac_50cm);
extern void set_ois_dit_afc_data_from_eeprom(uint32_t dac_10cm, uint32_t dac_50cm);

#endif
