/*****************************************************************************
* File: track-report.h
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
#ifndef TRACKREPORT_H
#define TRACKREPORT_H

/*==========================================================================*/
/* DEFINES                                                                  */
/*==========================================================================*/

/*==========================================================================*/
/* STRUCTURES                                                               */
/*==========================================================================*/


// Piezo bar Track Report

struct __attribute__((__packed__)) track_report {
    uint8_t  trk_id : 5;    // track_id
    uint8_t  bar_id : 3;    // bar_id
    uint8_t  force_lvl;     // force of touch
    uint16_t  center;       // center position, pos0
    uint16_t  bottom;       // bottom position, pos1
    uint16_t  top;          // top position, pos2
};

// Strain Gauge Track Report

#define STG_ELMT_MAX                7

// Strain Gauge bar_id = {6, 7}
#define STG_START_BAR_ID            6
#define IS_STG_TRACK_REPORT(barid)  ((int)((barid)>= STG_START_BAR_ID))

struct __attribute__((__packed__)) stg_track_report {
    uint8_t  rsvd   : 5;
    uint8_t  bar_id : 3;                    // bar_id
    uint8_t  force_lvl[STG_ELMT_MAX];       // force of touch for i'th gauge
};


#define TR_DIAG_MAX_MPATH                 10    // num in PH_0006_LP_Pixie

typedef struct tr_diag_vers_001_s
{
    uint8_t           start_code;       // always 0x00
    uint8_t           vers;             // 0x01 in this case
    uint16_t          mpa[TR_DIAG_MAX_MPATH];     // D1
    uint8_t           d_mp[TR_DIAG_MAX_MPATH];    // D1
    uint8_t           ntm;                        // D1
    uint8_t           gpio;                       // C2
    uint8_t           atc;                        // C2
    uint8_t           rsvd[5];

} tr_diag_vers_001_t, *p_tr_diag_vers_001_t;

typedef struct tr_diag_vers_002_s
{
    uint8_t           start_code;       // always 0x00
    uint8_t           vers;             // 0x02 in this case
    uint16_t          mpa[TR_DIAG_MAX_MPATH];     // D1
    uint8_t           d_mp[TR_DIAG_MAX_MPATH];    // D1
    uint8_t           ntm;                        // D1
    uint8_t           gpio;                       // C2
    uint8_t           atc;                        // C2
    uint8_t           frame_rate;                 // C2
    uint8_t           trig;                       // C2
    uint8_t           rsvd[3];

} tr_diag_vers_002_t, *p_tr_diag_vers_002_t;

/*****************************************************************************
* Gesture Reports may be added to struct bar1d_host_tr_s using the following
* format:
* 
* 1. gesture records will conform to the 8 byte size.
* 2. First gesture record will have first two bytes of 0x00 0x80
* 3. All subsequent gesture records will have first byte >0x80
*
* Gesture Header Record:
*
*    7  6  5  4  3  2  1  0
*  +-----------------------+
*  |          0x00         |
*  +-----------------------+
*  |          0x80         |
*  +-----------------------+
*  |    swipe0_velocity    |
*  +-----------------------+
*  |    swipe1_velocity    |
*  +-----------------------+
*  |       reserved        |
*  |                       |
*  +--+--+--+--+-----------+
*  |Lo|Sh|Ca|St|EN           |
*  +--+--+--+--+-----------+
*
* L - long squeeze indicator
* S - short squeeze indicator
* Ca - squeeze cancelled indicator
* St - squeeze start indicator
* En - squeeze end indicator
*/
#define GS_MASK(size, type)  ((type) ((1 << (size)) - 1))
#define GS_PMASK(mask, pos)  ((mask) << (pos))
#define GS_GET_FIELD(val, pos, mask) (((val) >> (pos)) & (mask))

#define GS_SLIDER_REC_MAX     4

#define GS_HDR_SQUEEZE_END_POS        3
#define GS_HDR_SQUEEZE_END_SIZE       1
#define GS_HDR_SQUEEZE_END_MASK       GS_MASK(GS_HDR_SQUEEZE_END_SIZE, uint8_t)
#define GS_HDR_SQUEEZE_END_PMASK      GS_PMASK(GS_HDR_SQUEEZE_END_MASK, GS_HDR_SQUEEZE_END_POS)
#define GS_GET_SQUEEZE_END(s)         (GS_GET_FIELD(s, GS_HDR_SQUEEZE_END_POS, GS_HDR_SQUEEZE_END_MASK))

#define GS_HDR_SQUEEZE_START_POS      4
#define GS_HDR_SQUEEZE_START_SIZE     1
#define GS_HDR_SQUEEZE_START_MASK     GS_MASK(GS_HDR_SQUEEZE_START_SIZE, uint8_t)
#define GS_HDR_SQUEEZE_START_PMASK    GS_PMASK(GS_HDR_SQUEEZE_START_MASK, GS_HDR_SQUEEZE_START_POS)
#define GS_GET_SQUEEZE_START(s)       (GS_GET_FIELD(s, GS_HDR_SQUEEZE_START_POS, GS_HDR_SQUEEZE_START_MASK))

#define GS_HDR_SQUEEZE_CANCEL_POS      5
#define GS_HDR_SQUEEZE_CANCEL_SIZE     1
#define GS_HDR_SQUEEZE_CANCEL_MASK     GS_MASK(GS_HDR_SQUEEZE_CANCEL_SIZE, uint8_t)
#define GS_HDR_SQUEEZE_CANCEL_PMASK    GS_PMASK(GS_HDR_SQUEEZE_CANCEL_MASK, GS_HDR_SQUEEZE_CANCEL_POS)
#define GS_GET_SQUEEZE_CANCEL(s)       (GS_GET_FIELD(s, GS_HDR_SQUEEZE_CANCEL_POS, GS_HDR_SQUEEZE_CANCEL_MASK))

#define GS_HDR_SQUEEZE_SHORT_POS      6
#define GS_HDR_SQUEEZE_SHORT_SIZE     1
#define GS_HDR_SQUEEZE_SHORT_MASK     GS_MASK(GS_HDR_SQUEEZE_SHORT_SIZE, uint8_t)
#define GS_HDR_SQUEEZE_SHORT_PMASK    GS_PMASK(GS_HDR_SQUEEZE_SHORT_MASK, GS_HDR_SQUEEZE_SHORT_POS)
#define GS_GET_SQUEEZE_SHORT(s)       (GS_GET_FIELD(s, GS_HDR_SQUEEZE_SHORT_POS, GS_HDR_SQUEEZE_SHORT_MASK))

#define GS_HDR_SQUEEZE_LONG_POS      7
#define GS_HDR_SQUEEZE_LONG_SIZE     1
#define GS_HDR_SQUEEZE_LONG_MASK     GS_MASK(GS_HDR_SQUEEZE_LONG_SIZE, uint8_t)
#define GS_HDR_SQUEEZE_LONG_PMASK    GS_PMASK(GS_HDR_SQUEEZE_LONG_MASK, GS_HDR_SQUEEZE_LONG_POS)
#define GS_GET_SQUEEZE_LONG(s)       (GS_GET_FIELD(s, GS_HDR_SQUEEZE_LONG_POS, GS_HDR_SQUEEZE_LONG_MASK))

typedef struct __attribute__((__packed__)) gs_hdr_rec_s 
{
	uint8_t		escape0;
	uint8_t		escape1;
	int8_t		swipe0_velocity;
	int8_t		swipe1_velocity;
	int8_t		swipe2_velocity;
	uint8_t		reserved[2];
	uint8_t		squeeze;
} gs_hdr_rec_t, *p_gs_hdr_rec_t;

/*****************************************************************************
*
* Gesture Slider Records:
*
*    7  6  5  4  3  2  1  0
*  +-----------------------+
*  |          0x81         |
*  +-----------------------+
*  |fid1 | id1 |fid0 | id0 |
*  +-----------------------+
*  |     slider_force0     |
*  +-----------------------+
*  |     slider_force1     |
*  +-----------------------+
*  |      slider_pos0      |
*  |                       |
*  +-----------------------+
*  |      slider_pos1      |
*  |                       |
*  +-----------------------+
*
*/
#define GS_SLIDER_ID0_POS           0
#define GS_SLIDER_ID0_SIZE          2
#define GS_SLIDER_ID0_MASK          GS_MASK(GS_SLIDER_ID0_SIZE, uint8_t)
#define GS_SLIDER_ID0_PMASK         GS_PMASK(GS_SLIDER_ID0_MASK, GS_SLIDER_ID0_POS)
#define GS_GET_SLIDER_ID0(s)        (GS_GET_FIELD(s, GS_SLIDER_ID0_POS, GS_SLIDER_ID0_MASK))

#define GS_SLIDER_FID0_POS          2
#define GS_SLIDER_FID0_SIZE         2
#define GS_SLIDER_FID0_MASK         GS_MASK(GS_SLIDER_FID0_SIZE, uint8_t)
#define GS_SLIDER_FID0_PMASK        GS_PMASK(GS_SLIDER_FID0_MASK, GS_SLIDER_FID0_POS)
#define GS_GET_SLIDER_FID0(s)       (GS_GET_FIELD(s, GS_SLIDER_FID0_POS, GS_SLIDER_FID0_MASK))

#define GS_SLIDER_ID1_POS           4
#define GS_SLIDER_ID1_SIZE          2
#define GS_SLIDER_ID1_MASK          GS_MASK(GS_SLIDER_ID1_SIZE, uint8_t)
#define GS_SLIDER_ID1_PMASK         GS_PMASK(GS_SLIDER_ID1_MASK, GS_SLIDER_ID1_POS)
#define GS_GET_SLIDER_ID1(s)        (GS_GET_FIELD(s, GS_SLIDER_ID1_POS, GS_SLIDER_ID1_MASK))

#define GS_SLIDER_FID1_POS          6
#define GS_SLIDER_FID1_SIZE         2
#define GS_SLIDER_FID1_MASK         GS_MASK(GS_SLIDER_FID1_SIZE, uint8_t)
#define GS_SLIDER_FID1_PMASK        GS_PMASK(GS_SLIDER_FID1_MASK, GS_SLIDER_FID1_POS)
#define GS_GET_SLIDER_FID1(s)       (GS_GET_FIELD(s, GS_SLIDER_FID1_POS, GS_SLIDER_FID1_MASK))

typedef struct __attribute__((__packed__)) gs_slider_rec_s 
{
	uint8_t		escape0;
	uint8_t		slider_finger_id;
	uint8_t		slider_force0;
	uint8_t		slider_force1;
	uint16_t	slider_pos0;
	uint16_t	slider_pos1;
} gs_slider_rec_t, *p_gs_slider_rec_t;

typedef struct __attribute__((__packed__)) gs_rpt_s 
{
  uint16_t        length;
  uint16_t        fr_nr;    // not used
	gs_hdr_rec_t	  hdr;
	gs_slider_rec_t	rec[GS_SLIDER_REC_MAX];

} gs_rpt_t, *p_gs_rpt_t;

#define TR_DIAG_REC_VERS_MAX              0x7f

/*==========================================================================*/
/* PROTOTYPES                                                               */
/*==========================================================================*/

#endif // TRACKREPORT_H
