#ifndef ASUS_ACTUATOR_I2C_H
#define ASUS_ACTUATOR_I2C_H
#include <linux/types.h>
#include "cam_actuator_dev.h"

int actuator_read_byte(struct cam_actuator_ctrl_t * ctrl,uint32_t reg_addr, uint32_t* reg_data);
int actuator_read_word(struct cam_actuator_ctrl_t * ctrl,uint32_t reg_addr, uint32_t* reg_data);
int actuator_read_dword(struct cam_actuator_ctrl_t * ctrl,uint32_t reg_addr, uint32_t* reg_data);
int actuator_read_seq_bytes(struct cam_actuator_ctrl_t * ctrl,uint32_t reg_addr, uint8_t* reg_data, uint32_t size);

int actuator_poll_byte(struct cam_actuator_ctrl_t * ctrl,uint32_t reg_addr, uint16_t reg_data, uint32_t delay_ms);
int actuator_poll_word(struct cam_actuator_ctrl_t * ctrl,uint32_t reg_addr, uint16_t reg_data, uint32_t delay_ms);

int actuator_write_byte(struct cam_actuator_ctrl_t * ctrl,uint32_t reg_addr, uint32_t reg_data);
int actuator_write_word(struct cam_actuator_ctrl_t * ctrl,uint32_t reg_addr, uint32_t reg_data);
int actuator_write_dword(struct cam_actuator_ctrl_t * ctrl,uint32_t reg_addr, uint32_t reg_data);
int actuator_write_seq_bytes(struct cam_actuator_ctrl_t * ctrl,uint32_t reg_addr, uint8_t* reg_data,uint32_t size);
#endif
