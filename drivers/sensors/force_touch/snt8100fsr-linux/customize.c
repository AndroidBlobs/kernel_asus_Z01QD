/*****************************************************************************
* File: customize.c
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
#include <linux/input.h>
#include "config.h"
#include <linux/input/mt.h>
#include "track_report.h"
#include "input_device.h"
#include "debug.h"
#include "device.h"
#include "file_control.h"
#include <linux/unistd.h>
#include <linux/delay.h>

#define USE_SHIM_DEMO
#ifdef  USE_SHIM_DEMO
#define MAX_TRACK_REPORTS 12
#define MT_MAX_BARID       2
#define MT_MAX_TOUCHBAR    5
#define MT_MAX_SLOTS      (MT_MAX_BARID*MT_MAX_TOUCHBAR)
#define PANEL_SIZE 2559
/*
 *  State machine of parsing inbound Track Report:
 *  <frame_no trackId barId force_level center bottom top>
 */

#define SNT_SYSFS_TR_STATE_FRAME        0
#define SNT_SYSFS_TR_STATE_BAR          1
#define SNT_SYSFS_TR_STATE_TRACK        2
#define SNT_SYSFS_TR_STATE_FORCE        3
#define SNT_SYSFS_TR_STATE_POS          4
#define SNT_SYSFS_TR_STATE_MINPOS       5
#define SNT_SYSFS_TR_STATE_MAXPOS       6
#define SNT_SYSFS_TR_STATE_MAX          7

/*==========================================================================*/
/* register_input_events()                                                  */
/* Customize to register which input events we'll be sending                */
/*==========================================================================*/
void register_input_events(struct input_dev *input_dev) {
    //Clay: simulate sensor hal
    	/* Set Light Sensor input device */
	input_dev->id.bustype = BUS_I2C;
	set_bit(EV_ABS, input_dev->evbit);
	
	//Gesture Type
	//input_set_capability(input_dev, EV_ABS, ABS_MT_DISTANCE);
	//__set_bit(ABS_MT_DISTANCE, input_dev->absbit);
	//input_set_abs_params(input_dev, ABS_MT_DISTANCE, 0, 12, 0, 0);
	
	//length
	//input_set_capability(input_dev, EV_ABS, ABS_MT_ORIENTATION);
	//__set_bit(ABS_MT_ORIENTATION, input_dev->absbit);
	//input_set_abs_params(input_dev, ABS_MT_ORIENTATION, 0, 5, 0, 0);
	
	//Trk_id
	input_set_capability(input_dev, EV_ABS, ABS_MT_TRACKING_ID);
	__set_bit(ABS_MT_TRACKING_ID, input_dev->absbit);
	input_set_abs_params(input_dev, ABS_MT_TRACKING_ID, 0, 8, 0, 0);

	//Bar_id
	input_set_capability(input_dev, EV_ABS, ABS_MT_WIDTH_MAJOR);
	__set_bit(ABS_MT_WIDTH_MAJOR, input_dev->absbit);
	input_set_abs_params(input_dev, ABS_MT_WIDTH_MAJOR, -2, -1, 0, 0);

	//PRESSUR
	input_set_capability(input_dev, EV_ABS, ABS_MT_WIDTH_MINOR);
	__set_bit(ABS_MT_WIDTH_MINOR, input_dev->absbit);
	input_set_abs_params(input_dev, ABS_MT_WIDTH_MINOR, 0, 255, 0, 0);

	//Fr_nr
	input_set_capability(input_dev, EV_ABS, ABS_MT_BLOB_ID);
	__set_bit(ABS_MT_BLOB_ID, input_dev->absbit);
	input_set_abs_params(input_dev, ABS_MT_BLOB_ID, 0, 65535, 0, 0);

	//Top
	input_set_capability(input_dev, EV_ABS, ABS_MT_TOOL_X);
	__set_bit(ABS_MT_TOOL_X, input_dev->absbit);
	input_set_abs_params(input_dev, ABS_MT_TOOL_X, 0, PANEL_SIZE, 0, 0);
	
	//Bot
	input_set_capability(input_dev, EV_ABS, ABS_MT_TOOL_Y);
	__set_bit(ABS_MT_TOOL_Y, input_dev->absbit);
	input_set_abs_params(input_dev, ABS_MT_TOOL_Y, 0, PANEL_SIZE, 0, 0);

	//Center
	input_set_capability(input_dev, EV_ABS, ABS_MT_TOOL_TYPE);
	__set_bit(ABS_MT_TOOL_TYPE, input_dev->absbit);
	input_set_abs_params(input_dev, ABS_MT_TOOL_TYPE, 0, PANEL_SIZE, 0, 0);
	
    PRINT_DEBUG("done");
}
void input_event_report(int g_id, int len, int trk_id, int bar_id, int force, int fr_nr, int center){
	
	//Gesture Type
	input_report_abs(get_input_device(), ABS_MT_TOOL_X, g_id);
	//Length
	input_report_abs(get_input_device(), ABS_MT_TOOL_Y, len);
	//Trk_id
	input_report_abs(get_input_device(), ABS_MT_TRACKING_ID, trk_id);
	//Bar_id
	input_report_abs(get_input_device(), ABS_MT_WIDTH_MAJOR, bar_id);
	//PRESSUR
	input_report_abs(get_input_device(), ABS_MT_WIDTH_MINOR, force);
	//Fr_nr
	input_report_abs(get_input_device(), ABS_MT_BLOB_ID, fr_nr);
	//center
	input_report_abs(get_input_device(), ABS_MT_TOOL_TYPE, center);
	input_event(get_input_device(), EV_SYN, SYN_REPORT, 7);
	input_sync(get_input_device());
}
/*==========================================================================*/
/* process_track_report()                                                   */
/* Customize to process track reports from the device                       */
/*==========================================================================*/
void process_track_reports(uint16_t frame,
                           struct track_report *tr,
                           size_t count) 
{
    int i;
    int gs_found = 0;
    int slot_id;
    int gesture_id = 0;
    //float trans = 0;
    long trans_result = 0;

    //Grip type
    if(snt8100fsr_g->en_sensor_evt){
	for(i = 0; i < count; i++) {
		// diagnostic section 
		if (tr[i].bar_id==0 && tr[i].trk_id==0 && tr[i].force_lvl <= TR_DIAG_REC_VERS_MAX) {
			break;
	        }

		slot_id = (tr[i].bar_id*MT_MAX_TOUCHBAR) + tr[i].trk_id%MT_MAX_TOUCHBAR;
	        // gesture section
	        if (gs_found |(tr[i].bar_id==0 && tr[i].trk_id==0 && tr[i].force_lvl > TR_DIAG_REC_VERS_MAX)) {
	            if (gs_found == 0) {
	                // process gesture header data here
	                p_gs_hdr_rec_t p = (p_gs_hdr_rec_t) &tr[i];
	                // ...
			if(p->swipe0_velocity == 1){
				gesture_id = 4;
			} else if (p->swipe1_velocity == 1){
				gesture_id = 5;
			} else if (p->swipe2_velocity == 1){
				gesture_id = 6;
			} else if (GS_GET_SQUEEZE_SHORT(p->squeeze) == 1){
				gesture_id = 7;
			} else if (GS_GET_SQUEEZE_LONG(p->squeeze) == 1){
				gesture_id = 8;
			} else if (GS_GET_SQUEEZE_START(p->squeeze) == 1){
				gesture_id = 9;
			} else if (GS_GET_SQUEEZE_CANCEL(p->squeeze) == 1){
				gesture_id = 10;
			} else if (GS_GET_SQUEEZE_END(p->squeeze) == 1){
			  	gesture_id = 11;
			}

			/* Work around for short squeeze */
			if(gesture_id == 8){
				if(grip_status_g->G_SQUEEZE_LONG == 0){
					gesture_id = 10;
				}
			}
			/*
			PRINT_DEBUG("Gesture: SW[%03x], SQ[st%u,cn%u,sh%u,lg%u,end%u]", 
					p->swipe0_velocity<<8|p->swipe1_velocity<<4|p->swipe2_velocity,
					GS_GET_SQUEEZE_START(p->squeeze), 
					GS_GET_SQUEEZE_CANCEL(p->squeeze), 
					GS_GET_SQUEEZE_SHORT(p->squeeze), 
					GS_GET_SQUEEZE_LONG(p->squeeze),
					GS_GET_SQUEEZE_END(p->squeeze));

	                */
	                PRINT_INFO("Gesture: SQ[st%u,cn%u,sh%u,lg%u, end%u], g_id=%d", 
                                	GS_GET_SQUEEZE_START(p->squeeze), 
                                	GS_GET_SQUEEZE_CANCEL(p->squeeze), 
                                        GS_GET_SQUEEZE_SHORT(p->squeeze), 
	                                GS_GET_SQUEEZE_LONG(p->squeeze),
					GS_GET_SQUEEZE_END(p->squeeze),
	                                gesture_id);
			PRINT_INFO("Squeeze status: S_ON=%d, L_ON = %d, SL_ON = %d", 
			grip_status_g->G_SQUEEZE_SHORT ,grip_status_g->G_SQUEEZE_LONG, grip_status_g->G_SQUEEZE_EN);
	                gs_found = 1;
			input_event_report(gesture_id, count, tr[i].trk_id, tr[i].bar_id, tr[i].force_lvl, frame, tr[i].center);
		   } else {
	                // process slider records here
	                p_gs_slider_rec_t p = (p_gs_slider_rec_t) &tr[i];
			uint8_t id0 = GS_GET_SLIDER_ID0(p->slider_finger_id);
                	uint8_t id1 = GS_GET_SLIDER_ID1(p->slider_finger_id);
                	uint8_t fid0 = GS_GET_SLIDER_FID0(p->slider_finger_id);
                	uint8_t fid1 = GS_GET_SLIDER_FID1(p->slider_finger_id);

	                if (fid0) {                    
				PRINT_DEBUG("Slider[%u,%u]: F%u, P%u", id0, fid0,
	                                    p->slider_force0, p->slider_pos0);
	                }
	                if (fid1) {
	                        PRINT_DEBUG("Slider[%u,%u]: F%u, P%u", id1, fid1,
	                                    p->slider_force1, p->slider_pos1);
	                }
	            }
	        } else {
	        	if(grip_status_g->G_TAP1_EN == 0 && grip_status_g->G_TAP2_EN == 
	        	0 && grip_status_g->G_TAP3_EN == 0){
		        	//Squeeze Case
		        	if(tr[i].bar_id == 1){
					//trans = (float)B1_F_value / 256;
					//trans_result = (int)(trans * tr[i].force_lvl);
					trans_result = (256 * tr[i].force_lvl) /B1_F_value;
					if(trans_result > 255) trans_result = 255;
		        		input_event_report(0, count, tr[i].trk_id, tr[i].bar_id, trans_result, frame, tr[i].center);
		        	}else if(tr[i].bar_id == 2){
					//trans = (float)B2_F_value / 256;
					//trans_result = (int)(trans * tr[i].force_lvl);
					trans_result = (256 * tr[i].force_lvl) /B2_F_value;
					if(trans_result > 255) trans_result = 255;
		        		input_event_report(0, count, tr[i].trk_id, tr[i].bar_id, trans_result, frame, tr[i].center);
		        	}else{
					trans_result = (256 * tr[i].force_lvl) /B0_F_value;
					if(trans_result > 255) trans_result = 255;
			        	input_event_report(0, count, tr[i].trk_id, tr[i].bar_id, trans_result, frame, tr[i].center);
		        	}
	        	}else{
	        		//Normal Data
		        	input_event_report(0, count, tr[i].trk_id, tr[i].bar_id, tr[i].force_lvl, frame, tr[i].center);
	        	}
			/*
			    PRINT_DEBUG("Track Report (slot: %u) ->", slot_id);
			    PRINT_DEBUG("   Frame Nr: (%u)->", frame);
			    PRINT_DEBUG("     Bar ID: %u", tr[i].bar_id);
			    PRINT_DEBUG("   Track ID: %u", tr[i].trk_id);
			    PRINT_DEBUG("  Force Lvl: %u", tr[i].force_lvl);
			    PRINT_DEBUG("        Top: %u", tr[i].bottom);
			    PRINT_DEBUG("     Center: %u", tr[i].center);
			    PRINT_DEBUG("     Bottom: %u", tr[i].top);
			*/
		}
	}
    }
    PRINT_DEBUG("done");
}
#else
#define MAX_TRACK_REPORTS 12
#define MT_MAX_BARID       2
#define MT_MAX_TOUCHBAR    5
#define MT_MAX_SLOTS      (MT_MAX_BARID*MT_MAX_TOUCHBAR)
#define PANEL_SIZE 2559
#define UINT32_MAX 0xffffffff
/*==========================================================================*/
/* register_input_events()                                                  */
/* Customize to register which input events we'll be sending                */
/*==========================================================================*/
void register_input_events(struct input_dev *input_dev) {
    PRINT_FUNC();
	input_dev->name = "snt8100fsr";
	input_dev->id.bustype = BUS_I2C;


	//Trk_id
	/*
	input_set_capability(input_dev, EV_ABS, ABS_X);
	__set_bit(ABS_X, input_dev->absbit);
    input_set_abs_params(input_dev, ABS_X, 0, UINT32_MAX, 0, 0);
	input_set_capability(input_dev, EV_ABS, ABS_Y);
	__set_bit(ABS_Y, input_dev->absbit);
    input_set_abs_params(input_dev, ABS_Y, 0, UINT32_MAX, 0, 0);
	input_set_capability(input_dev, EV_ABS, ABS_Z);
	__set_bit(ABS_Z, input_dev->absbit);
    input_set_abs_params(input_dev, ABS_Z, 0, UINT32_MAX, 0, 0);
	input_set_capability(input_dev, EV_ABS, ABS_RX);
	__set_bit(ABS_RX, input_dev->absbit);
    input_set_abs_params(input_dev, ABS_RX, 0, UINT32_MAX, 0, 0);
	input_set_capability(input_dev, EV_ABS, ABS_RY);
	__set_bit(ABS_RY, input_dev->absbit);
    input_set_abs_params(input_dev, ABS_RY, 0, UINT32_MAX, 0, 0);
	input_set_capability(input_dev, EV_ABS, ABS_RZ);
	__set_bit(ABS_RZ, input_dev->absbit);
    input_set_abs_params(input_dev, ABS_RZ, 0, UINT32_MAX, 0, 0);
	input_set_capability(input_dev, EV_ABS, ABS_HAT0X);
	__set_bit(ABS_HAT0X, input_dev->absbit);
    input_set_abs_params(input_dev, ABS_HAT0X, 0, UINT32_MAX, 0, 0);
	input_set_capability(input_dev, EV_ABS, ABS_HAT0Y);
	__set_bit(ABS_HAT0Y, input_dev->absbit);
    input_set_abs_params(input_dev, ABS_HAT0Y, 0, UINT32_MAX, 0, 0);
	input_set_capability(input_dev, EV_ABS, ABS_HAT1X);
	__set_bit(ABS_HAT1X, input_dev->absbit);
    input_set_abs_params(input_dev, ABS_HAT1X, 0, UINT32_MAX, 0, 0);
	input_set_capability(input_dev, EV_ABS, ABS_HAT1Y);
	__set_bit(ABS_HAT1Y, input_dev->absbit);
    input_set_abs_params(input_dev, ABS_HAT1Y, 0, UINT32_MAX, 0, 0);
	input_set_capability(input_dev, EV_ABS, ABS_HAT2X);
	__set_bit(ABS_HAT2X, input_dev->absbit);
    input_set_abs_params(input_dev, ABS_HAT2X, 0, UINT32_MAX, 0, 0);
	input_set_capability(input_dev, EV_ABS, ABS_HAT2Y);
	__set_bit(ABS_HAT2Y, input_dev->absbit);
    input_set_abs_params(input_dev, ABS_HAT2Y, 0, UINT32_MAX, 0, 0);
	input_set_capability(input_dev, EV_ABS, ABS_HAT3X);
	__set_bit(ABS_HAT3X, input_dev->absbit);
    input_set_abs_params(input_dev, ABS_HAT3X, 0, UINT32_MAX, 0, 0);
	input_set_capability(input_dev, EV_ABS, ABS_HAT3Y);
	__set_bit(ABS_HAT3Y, input_dev->absbit);
    input_set_abs_params(input_dev, ABS_HAT3Y, 0, UINT32_MAX, 0, 0);
	input_set_capability(input_dev, EV_ABS, ABS_THROTTLE);
	__set_bit(ABS_THROTTLE, input_dev->absbit);
    input_set_abs_params(input_dev, ABS_THROTTLE, 0, UINT32_MAX, 0, 0);
	input_set_capability(input_dev, EV_ABS, ABS_RUDDER);
	__set_bit(ABS_RUDDER, input_dev->absbit);
    input_set_abs_params(input_dev, ABS_RUDDER, 0, UINT32_MAX, 0, 0);
	*/

/*

    input_set_abs_params(input_dev, ABS_MT_WIDTH_MAJOR, 0, UINT32_MAX, 0, 0);
    input_set_abs_params(input_dev, ABS_MT_WIDTH_MAJOR, 0, UINT32_MAX, 0, 0);
    input_set_abs_params(input_dev, ABS_MT_PRESSURE, 0, UINT32_MAX, 0, 0);
    input_set_abs_params(input_dev, ABS_MT_BLOB_ID, 0, UINT32_MAX, 0, 0);
    input_set_abs_params(input_dev, ABS_MT_TOOL_X, 0, UINT32_MAX, 0, 0);
    input_set_abs_params(input_dev, ABS_MT_TOOL_Y, 0, UINT32_MAX, 0, 0);
    input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR, 0, UINT32_MAX, 0, 0);
    input_set_abs_params(input_dev, ABS_HAT0Y, 0, UINT32_MAX, 0, 0);
    input_set_abs_params(input_dev, ABS_HAT1X, 0, UINT32_MAX, 0, 0);
    input_set_abs_params(input_dev, ABS_HAT1Y, 0, UINT32_MAX, 0, 0);
    input_set_abs_params(input_dev, ABS_HAT2X, 0, UINT32_MAX, 0, 0);
    input_set_abs_params(input_dev, ABS_HAT2Y, 0, UINT32_MAX, 0, 0);
    input_set_abs_params(input_dev, ABS_HAT3X, 0, UINT32_MAX, 0, 0);
    input_set_abs_params(input_dev, ABS_HAT3Y, 0, UINT32_MAX, 0, 0);
    input_set_abs_params(input_dev, ABS_THROTTLE, 0, UINT32_MAX, 0, 0);
    input_set_abs_params(input_dev, ABS_RUDDER, 0, UINT32_MAX, 0, 0);

    PRINT_DEBUG("done");
}

#define TR_TOT_IN_PACKET(idx)     (((idx) == 2) ?  2 : 7)
#define TR_TOT_REMAIN(count, idx) ((count)-(idx)*7)
#define MIN(a, b) ((a<b) ? a : b)
uint8_t INPUT_ID[16] = {
    ABS_MT_WIDTH_MAJOR, ABS_MT_WIDTH_MAJOR, ABS_MT_PRESSURE,
    ABS_MT_BLOB_ID, ABS_MT_TOOL_X, ABS_MT_TOOL_Y,
    ABS_MT_TOUCH_MAJOR, ABS_HAT0Y, ABS_HAT1X,
    ABS_HAT1Y, ABS_HAT2X, ABS_HAT2Y,
    ABS_HAT3X, ABS_HAT3Y, ABS_THROTTLE, ABS_RUDDER,
};
*/
    //PART 1
    set_bit(EV_KEY, input_dev->evbit);
    set_bit(EV_SYN, input_dev->evbit);
    set_bit(EV_ABS, input_dev->evbit);

    input_set_abs_params(input_dev, ABS_MT_POSITION_X, -2, -1, 0, 0);
    input_set_abs_params(input_dev, ABS_MT_POSITION_Y, 0, 2559, 0, 0);
    input_set_abs_params(input_dev, ABS_MT_PRESSURE, 0, 255, 0, 0);
    input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
    input_set_abs_params(input_dev, ABS_MT_TOUCH_MINOR, 0, 255, 0, 0);
	
    input_mt_destroy_slots(input_dev);
    input_mt_init_slots(input_dev, MT_MAX_SLOTS, 0);
	
    //PART 2
    //set_bit(EV_SYN, input_dev->evbit);
    //set_bit(EV_ABS, input_dev->evbit);
	input_set_capability(input_dev, EV_ABS, ABS_X);
	__set_bit(ABS_X, input_dev->absbit);
    input_set_abs_params(input_dev, ABS_X, 0, UINT32_MAX, 0, 0);
	input_set_capability(input_dev, EV_ABS, ABS_Y);
	__set_bit(ABS_Y, input_dev->absbit);
    input_set_abs_params(input_dev, ABS_Y, 0, UINT32_MAX, 0, 0);
	input_set_capability(input_dev, EV_ABS, ABS_Z);
	__set_bit(ABS_Z, input_dev->absbit);
    input_set_abs_params(input_dev, ABS_Z, 0, UINT32_MAX, 0, 0);
	input_set_capability(input_dev, EV_ABS, ABS_RX);
	__set_bit(ABS_RX, input_dev->absbit);
    input_set_abs_params(input_dev, ABS_RX, 0, UINT32_MAX, 0, 0);
	input_set_capability(input_dev, EV_ABS, ABS_RY);
	__set_bit(ABS_RY, input_dev->absbit);
    input_set_abs_params(input_dev, ABS_RY, 0, UINT32_MAX, 0, 0);
	input_set_capability(input_dev, EV_ABS, ABS_RZ);
	__set_bit(ABS_RZ, input_dev->absbit);
    input_set_abs_params(input_dev, ABS_RZ, 0, UINT32_MAX, 0, 0);
	input_set_capability(input_dev, EV_ABS, ABS_HAT0X);
	__set_bit(ABS_HAT0X, input_dev->absbit);
    input_set_abs_params(input_dev, ABS_HAT0X, 0, UINT32_MAX, 0, 0);
	input_set_capability(input_dev, EV_ABS, ABS_HAT0Y);
	__set_bit(ABS_HAT0Y, input_dev->absbit);
    input_set_abs_params(input_dev, ABS_HAT0Y, 0, UINT32_MAX, 0, 0);
	input_set_capability(input_dev, EV_ABS, ABS_HAT1X);
	__set_bit(ABS_HAT1X, input_dev->absbit);
    input_set_abs_params(input_dev, ABS_HAT1X, 0, UINT32_MAX, 0, 0);
	input_set_capability(input_dev, EV_ABS, ABS_HAT1Y);
	__set_bit(ABS_HAT1Y, input_dev->absbit);
    input_set_abs_params(input_dev, ABS_HAT1Y, 0, UINT32_MAX, 0, 0);
	input_set_capability(input_dev, EV_ABS, ABS_HAT2X);
	__set_bit(ABS_HAT2X, input_dev->absbit);
    input_set_abs_params(input_dev, ABS_HAT2X, 0, UINT32_MAX, 0, 0);
	input_set_capability(input_dev, EV_ABS, ABS_HAT2Y);
	__set_bit(ABS_HAT2Y, input_dev->absbit);
    input_set_abs_params(input_dev, ABS_HAT2Y, 0, UINT32_MAX, 0, 0);
	input_set_capability(input_dev, EV_ABS, ABS_HAT3X);
	__set_bit(ABS_HAT3X, input_dev->absbit);
    input_set_abs_params(input_dev, ABS_HAT3X, 0, UINT32_MAX, 0, 0);
	input_set_capability(input_dev, EV_ABS, ABS_HAT3Y);
	__set_bit(ABS_HAT3Y, input_dev->absbit);
    input_set_abs_params(input_dev, ABS_HAT3Y, 0, UINT32_MAX, 0, 0);
	input_set_capability(input_dev, EV_ABS, ABS_THROTTLE);
	__set_bit(ABS_THROTTLE, input_dev->absbit);
    input_set_abs_params(input_dev, ABS_THROTTLE, 0, UINT32_MAX, 0, 0);
	input_set_capability(input_dev, EV_ABS, ABS_RUDDER);
	__set_bit(ABS_RUDDER, input_dev->absbit);
    input_set_abs_params(input_dev, ABS_RUDDER, 0, UINT32_MAX, 0, 0);

    PRINT_DEBUG("done");
}

/*==========================================================================*/
/* process_track_report()                                                   */
/* Customize to process track reports from the device                       */
/*==========================================================================*/
#define TR_TOT_IN_PACKET(idx)     (((idx) == 2) ?  2 : 7)
#define TR_TOT_REMAIN(count, idx) ((count)-(idx)*7)
#define MIN(a, b) ((a<b) ? a : b)
uint8_t INPUT_ID[16] = {
    ABS_X, ABS_Y, ABS_Z,
    ABS_RX, ABS_RY, ABS_RZ,
    ABS_HAT0X, ABS_HAT0Y, ABS_HAT1X,
    ABS_HAT1Y, ABS_HAT2X, ABS_HAT2Y,
    ABS_HAT3X, ABS_HAT3Y, ABS_THROTTLE, ABS_RUDDER,
};
void process_track_reports(uint16_t frame,
                           struct track_report *tr,
                           size_t count) {
    //Part 1
    int i;
    int remap_center = 0;
    int R_side_cont = 80;
    int L_side_cont = 77;

    //int i;//, j;
    int32_t trbuf[16];
    uint16_t packet_header = 0x5A96;
    uint8_t  packet_tot = (count / 7) + 1;
    uint8_t  packet_idx, tr_idx, cnt;
    PRINT_FUNC();

    /* process a track report */
    /* ...code... */

    /*
    PRINT_INFO("[austin][KERN] count: %zu, packet_tot: %d", count, packet_tot);
    for(i = 0; i < count; i++) {
        PRINT_INFO("   Frame Nr: (%u)->", frame);
        PRINT_INFO("     Bar ID: %u", tr[i].bar_id);
        PRINT_INFO("   Track ID: %u", tr[i].trk_id);
        PRINT_INFO("  Force Lvl: %u", tr[i].force_lvl);
        PRINT_INFO("        Top: %u", tr[i].top);
        PRINT_INFO("     Center: %u", tr[i].center);
        PRINT_INFO("     Bottom: %u", tr[i].bottom);
    }
    */
    //Part 1
    if(snt8100fsr_g->en_demo) {
	    for(i = 0; i < count; i++) {
		    int slot_id = (tr[i].bar_id*MT_MAX_TOUCHBAR) + tr[i].trk_id%MT_MAX_TOUCHBAR;

		    input_mt_slot(get_input_device(), slot_id);

		    if (tr[i].force_lvl > 0) {

		    //input_report_key(get_input_device(), BTN_TOOL_FINGER, true);
		    /* ===Remap Touch Event===
		    * R-side: 80~1696 to 80~2559  => 0~1616 to 0~2479
		    * L-side: 77~233 to 2185~2559 => 0~156 to 2108~2482
		    */
			if(tr[i].bar_id == 0){
				remap_center = R_side_cont + ((tr[i].center-R_side_cont)*1534)/1000;
    			}else if (tr[i].bar_id == 1){
			  	remap_center = L_side_cont + ((tr[i].center-L_side_cont)*241)/100;
	    		}
			input_mt_report_slot_state(get_input_device(), MT_TOOL_FINGER, true);
			//input_report_key(get_input_device(), BTN_TOUCH, 1);
			//input_report_key(get_input_device(), BTN_TOOL_FINGER, true);
			input_report_abs(get_input_device(), ABS_MT_POSITION_X, -1 - tr[i].bar_id);
			//input_report_abs(get_input_device(), ABS_MT_POSITION_Y, 2559-tr[i].center);
			input_report_abs(get_input_device(), ABS_MT_POSITION_Y, 2559 - remap_center);
			input_report_abs(get_input_device(), ABS_MT_PRESSURE, tr[i].force_lvl);
		    } else {
			    input_mt_report_slot_state(get_input_device(), MT_TOOL_FINGER, false);
			    //input_report_key(get_input_device(), BTN_TOUCH, 0);
			    //input_report_key(get_input_device(), BTN_TOOL_FINGER, 0);
			    input_report_abs(get_input_device(), ABS_MT_PRESSURE, tr[i].force_lvl);
		    }
	    }
	    input_mt_sync_frame(get_input_device());
	    input_sync(get_input_device());
    }else{
	    for (packet_idx = 0; packet_idx < packet_tot; packet_idx++) {

	        memset(trbuf, 0, sizeof(trbuf));
	        PRINT_DEBUG("[austin][KERN] %d(%d) out of %d", packet_idx, TR_TOT_IN_PACKET(packet_idx), packet_tot);

	        trbuf[0]  = (packet_header << 16) | (packet_tot << 8) | (packet_idx);
	        trbuf[15] = 0;

	        PRINT_DEBUG("[austin][KERN] cnt: %d (min(%d, %d))",
	                MIN(TR_TOT_IN_PACKET(packet_idx), (int)TR_TOT_REMAIN(count, packet_idx)),
	                TR_TOT_IN_PACKET(packet_idx), (int)TR_TOT_REMAIN(count, packet_idx));

	        input_report_abs(get_input_device(), INPUT_ID[0], trbuf[0]);
	        PRINT_DEBUG("[austin][KERN] trbuf[0]: %08X", trbuf[0]);

	        //for (cnt = 0; cnt < TR_TOT_IN_PACKET(packet_idx); cnt++) {
		for (cnt = 0; cnt < MIN(TR_TOT_IN_PACKET(packet_idx),
					TR_TOT_REMAIN(count, packet_idx)); cnt++) {

	            tr_idx = packet_idx * 7 + cnt;
	            trbuf[cnt*2+1] = (int32_t)((tr[tr_idx].center << 16) | (tr[tr_idx].force_lvl << 8) |
	                                        (tr[tr_idx].bar_id << 5)  | (tr[tr_idx].trk_id));
	            trbuf[cnt*2+2] = (int32_t)((tr[tr_idx].bottom << 16) | tr[tr_idx].top);
	            input_report_abs(get_input_device(), INPUT_ID[cnt*2+1], trbuf[cnt*2+1]);
	            input_report_abs(get_input_device(), INPUT_ID[cnt*2+2], trbuf[cnt*2+2]);
	            PRINT_DEBUG("[austin][KERN] trbuf[%d](%d): %08X", cnt*2+1, INPUT_ID[cnt*2+1], trbuf[cnt*2+1]);
	            PRINT_DEBUG("[austin][KERN] trbuf[%d](%d): %08X", cnt*2+2, INPUT_ID[cnt*2+2], trbuf[cnt*2+2]);
	        }

	        input_sync(get_input_device());

	        input_report_abs(get_input_device(), INPUT_ID[0], -1);
	        input_sync(get_input_device());
	    }
    }
    PRINT_INFO("done");
}
#endif

#if USE_TRIG_IRQ
/*==========================================================================*/
/* process_trigger()                                                        */
/* Customize to process trigger interrupts from the device                  */
/* trig_id values: 0, 1, 2 correlate to TRIG0, TRIG1, TRIG2 in config.h     */
/*==========================================================================*/
void process_trigger(int trig_id, int gpio_value)
{
    PRINT_FUNC();
    if(trig_id == 1){
	if(gpio_value == 1){
      		PRINT_INFO("Gesture: Tap%d, finger %s!", trig_id, "down");
		input_event_report(trig_id, 0, 0, 0 ,0, 0, 0);
	}else{
      		PRINT_INFO("Gesture: Tap%d, finger %s!", trig_id, "up");
		input_event_report(trig_id, 0, 1, 0 ,0, 0, 0);
	}
    }else if (trig_id == 2){
		if(gpio_value == 1){
      			PRINT_INFO("Gesture: Tap%d, finger %s!", trig_id, "down");
			input_event_report(trig_id, 0, 0, 0 ,0, 0, 0);
		}else{
      			PRINT_INFO("Gesture: Tap%d, finger %s!", trig_id, "up");
			input_event_report(trig_id, 0, 1, 0 ,0, 0, 0);
		}
    }else{
		if(gpio_value == 1){
      			PRINT_INFO("Gesture: Tap%d, finger %s!", trig_id, "down");
			input_event_report(trig_id, 0, 0, 0 ,0, 0, 0);
		}else{
      			PRINT_INFO("Gesture: Tap%d, finger %s!", trig_id, "up");
			input_event_report(trig_id, 0, 1, 0 ,0, 0, 0);
		}
    }
}
#endif
