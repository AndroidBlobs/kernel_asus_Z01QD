/*****************************************************************************
* File: trdd.c
*
* (c) 2018 Sentons Inc. - All Rights Reserved.
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

/*****************************************************************************
 * INCLUDE FILES
 ****************************************************************************/
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <getopt.h>

/*****************************************************************************
 * MACROS AND DATA STRUCTURES
 ****************************************************************************/
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
    uint8_t           vers;             // 0x01 in this case
    uint16_t          mpa[TR_DIAG_MAX_MPATH];     // D1
    uint8_t           d_mp[TR_DIAG_MAX_MPATH];    // D1
    uint8_t           ntm;                        // D1
    uint8_t           gpio;                       // C2
    uint8_t           atc;                        // C2
    uint8_t           frame_rate;                 // C2
    uint8_t           trig;                       // C2
    uint8_t           rsvd[3];

} tr_diag_vers_002_t, *p_tr_diag_vers_002_t;

#define TR_DIAG_NUM_REC_001     (sizeof(tr_diag_vers_001_t)/8)
#define TR_DIAG_NUM_REC_002     (sizeof(tr_diag_vers_002_t)/8)

typedef struct input_tr_rec_s
{
    uint32_t ts;
    uint32_t fn;
    uint32_t bar_id;
    uint32_t a1;
    uint32_t a2;
    uint32_t a3;
    uint32_t a4;
    uint32_t a5;
    uint32_t a6;
    uint32_t a7;

} input_tr_rec_t, *p_input_tr_rec_t;

#define MAX_TRACK_REPORT_RECORDS         30

typedef struct track_report_rec_s
{
    uint8_t     bar_track_id;
    uint8_t     force;
    uint16_t    pos0;
    uint16_t    pos1;
    uint16_t    pos2;

} track_report_rec_t, *p_track_report_rec_t;

#define NUM_STRAIN_FORCE                7

typedef struct strain_track_report_s
{
    uint8_t     bar_track_id;
    uint8_t     force[NUM_STRAIN_FORCE];

} strain_track_report_t, *p_strain_track_report_t;

typedef struct track_report_s 
{
    uint16_t length;
    uint16_t frame;
    track_report_rec_t  tr_rec[MAX_TRACK_REPORT_RECORDS];

} track_report_t, *p_track_report_t;

#define MAX_TRACK_REPORT_SIZE   (sizeof(track_report_t))

typedef struct event_log_s 
{
    uint8_t     sub_sys;
    uint8_t     evt_id;
    uint16_t    parm1;
    uint16_t    parm2;
    uint16_t    parm3;

} event_log_t, *p_event_log_t;

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
*  +---+---+---------------+
*  |Lo|Sh|Ca|St|EN           |
*  +---+---+---------------+
*
* L - long squeeze indicator
* S - short squeeze indicator
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


#define STATE_TR            0
#define STATE_TR_DIAG       1

#define MIN_STRAIN_BAR_ID   6
#define MAX_STRAIN_BAR_ID   7
#define IS_STRAIN_BAR(id)   ((id) <= MAX_STRAIN_BAR_ID && (id) >= MIN_STRAIN_BAR_ID)

#define FTYPE_TR_DIAG_TXT   0
#define FTYPE_DEEP_TRACE    1
#define FTYPE_TR_DIAG_BIN   2
#define FTYPE_EVENT_LOG_BIN 3


/*****************************************************************************
 * GLOBAL VARIABLES
 ****************************************************************************/

char *in_fname = NULL;
char *out_fname = NULL;
FILE *fin = NULL;
FILE *fout = NULL;
FILE *ferr = NULL;
int state = STATE_TR;
int tr_cur_rec;
int tr_max_rec;
uint8_t *p_tr_diag_buf;
uint8_t *p_tr_diag_base;
tr_diag_vers_001_t tr_diag_rec_001;
tr_diag_vers_002_t tr_diag_rec_002;
int ftype = FTYPE_TR_DIAG_TXT;

uint32_t cur_ts = 0;


/*****************************************************************************
 * FUNCTION DECLARATIONS
 ****************************************************************************/


void process_report_rec(p_input_tr_rec_t p_in);
void start_tr_diag(p_input_tr_rec_t p_in);
void process_tr_diag(p_input_tr_rec_t p_in);
void print_strain_tr(p_input_tr_rec_t p_in);
void print_normal_tr(p_input_tr_rec_t p_in);
void print_tr_diag(uint8_t *p_buf);
void print_tr_diag_001(p_tr_diag_vers_001_t p_buf);
void print_tr_diag_002(p_tr_diag_vers_002_t p_buf);
void process_tr_diag_txt_file(void);
void process_tr_diag_bin_file(void);
void process_deep_trace_file(void);
void process_event_log_bin_file(void);


/*****************************************************************************
 * SOURCE CODE
 ****************************************************************************/

/** 
 */
void print_usage() {
    printf("Usage: trdd [-erdh] [-o outfile] [infile]\n");
    printf("       -d: print deep_trace.bin (binary) file\n");
    printf("       -e: print event_log.bin (binary) file\n");
    printf("       -h: print this help text\n");
    printf("       -r: print track_report.bin (binary) file\n");
    printf("       default: print track_report.log (text) file\n");
}


FILE *open_in_file(char *fname, int ftype)
{
    FILE *fret = NULL;
    if (fname == NULL) {
        fret = stdin;
    } else {
        switch (ftype) {
            case FTYPE_TR_DIAG_TXT: fret = fopen(fname, "r"); break;
            case FTYPE_TR_DIAG_BIN:
            case FTYPE_EVENT_LOG_BIN:
            case FTYPE_DEEP_TRACE : fret = fopen(fname, "rb"); break;
            default: fprintf(ferr,"Unknown ftype (%d)\n", ftype); break;
        }
    }
    return fret;
}

FILE *open_out_file(char *fname, int ftype)
{
    FILE *fret = NULL;

    if (fname == NULL) {
        fret = stdout;
    } else {
        fret = fopen(fname, "w");
    }
    return fret;
}

/*****************************************************************************
 * main()
 *
 * usage: trdd [track_report_file]
 *
 * If track_report_file not supplied, assumes stdin.
 *
 * input is track report logging file from snt8100fsr reference linux driver:
 *
 * echo 1 >/sys/snt8100fsr/log_track_reports
 * echo 0 >/sys/snt8100fsr/log_track_reports
 *
 * This program will parse a track_report log file and reformat the track
 * report diagnostic section.
 *
 * To turn on track report diagnostic logging:
 *
 * echo 0x40 1 >/sys/snt8100fsr/set_reg
 *
 * To Make:
 *
 * cc -o trdd trdd.c
 *
 ****************************************************************************/
int main(int argc, char *argv[])
{
    int option = 0;

    ferr = stderr;
  
     //Specifying the expected options
    while ((option = getopt(argc, argv,"edho:r")) != -1) {
        switch (option) {
            case 'e': ftype=FTYPE_EVENT_LOG_BIN; break;
            case 'd': ftype=FTYPE_DEEP_TRACE; break;
            case 'o': out_fname = optarg; break;
            case 'r': ftype=FTYPE_TR_DIAG_BIN; break;
            default : print_usage(); exit(1); break;
        }
    }

    // Open input and output files
    if (optind < argc) {
        in_fname = argv[optind];
    } 
    fin = open_in_file(in_fname, ftype);
    if (fin==NULL) {
        fprintf(ferr, "ERROR! Couldn't open %s\n", in_fname);
        exit(1);
    }
    fout = open_out_file(out_fname, ftype);
    if (fout==NULL) {
        fprintf(ferr, "ERROR! Couldn't open %s\n", out_fname);
        exit(1);
    }
    switch (ftype) {
        case FTYPE_EVENT_LOG_BIN:   process_event_log_bin_file();   break;
        case FTYPE_TR_DIAG_TXT:     process_tr_diag_txt_file();     break;
        case FTYPE_TR_DIAG_BIN:     process_tr_diag_bin_file();     break;
        case FTYPE_DEEP_TRACE :     process_deep_trace_file();      break;
        default: fprintf(ferr,"Unknown ftype (%d)\n", ftype); break;
    }
}

void process_tr_diag_txt_file(void)
{
    int num_token;
    char line[256];
    char *p;
    input_tr_rec_t tr_in;
    int lineno=1;
  
    p = fgets(line, 256, fin);
    while (p) {

        /* snt8100fsr driver adds a \0 after first line. take it out */
        if (line[0]==0) p = line+1; else p = line;

        num_token = sscanf(p,"%u, %u, %u, %u, %u, %u, %u, %u, %u, %u",
                            &tr_in.ts,
                            &tr_in.fn,
                            &tr_in.bar_id,
                            &tr_in.a1,
                            &tr_in.a2,
                            &tr_in.a3,
                            &tr_in.a4,
                            &tr_in.a5,
                            &tr_in.a6,
                            &tr_in.a7);

        if (num_token == 8 || num_token == 10) // piezo or strain report
            process_report_rec(&tr_in);
        else
            fprintf(fout,"%s",line);

        p = fgets(line, 256, fin);
        lineno++;
    }
}


void process_report_rec(p_input_tr_rec_t p_in)
{
    /* add extra \n on new record  */
    if (p_in->ts != cur_ts) {
        fprintf(fout, "\n");
    }

    switch (state) {
        case STATE_TR: {
            if (p_in->bar_id == 0 && p_in->a1 == 0) {
                start_tr_diag(p_in);
            } else if (IS_STRAIN_BAR(p_in->bar_id)) {
                print_strain_tr(p_in);
            } else {
                print_normal_tr(p_in);
            }

        }
        break;

        case STATE_TR_DIAG: {
            process_tr_diag(p_in);
        }
        break;

        default: fprintf(ferr, "ERROR: unknown state %d\n", state); exit(1);
    }

    /* update new record indicator */
    if (p_in->ts != cur_ts)
        cur_ts = p_in->ts;
}

/*****************************************************************************
 * start_tr_diag()
 *
 * Initialize state transition to processing tr_diag section of track report.
 * Key opeation is taking track reports and refactoring their data into the
 * tr_diag structure.
 *
 * conversion of normal tr to bytes in tr_diag:
 *
 * bar_id<<3 | a1&0x3 = byte0
 * a2 = byte1
 * a3 = byte2 | byte3
 * a4 = byte4 | byte5
 * a5 = byte6 | byte7
 *
 * conversion of strain tr to bytes in tr_diag:
 *
 * bar_id << 3 = byte0 (WARNING! MISSING 3lsb0:2)
 * a1 = byte1
 * a2 = byte2
 * a3 = byte3
 * a4 = byte4
 * a5 = byte5
 * a6 = byte6
 * a7 = byte7
 *
 ****************************************************************************/

void start_tr_diag(p_input_tr_rec_t p_in)
{
    if (p_in->ts != cur_ts) {
        fprintf(fout, "%u, %u\n", p_in->ts, p_in->fn);
    }
    state = STATE_TR_DIAG;
    tr_cur_rec = 1;

    // currently 2 versions of tr_diag structure defined.
    if (p_in->a2 == 1) {
        p_tr_diag_base = (uint8_t*) &tr_diag_rec_001;
        p_tr_diag_buf = p_tr_diag_base;
        tr_max_rec = TR_DIAG_NUM_REC_001;
    } else if (p_in->a2 == 2) {
        p_tr_diag_base = (uint8_t*) &tr_diag_rec_002;
        p_tr_diag_buf = p_tr_diag_base;
        tr_max_rec = TR_DIAG_NUM_REC_002;
    } else {
        fprintf(ferr, "ERROR Unkown tr_diag version %d\n", (int)p_in->a2);
        exit(1);
    }
    *p_tr_diag_buf++ = 0; // barid|a1 is 0 by def
    *p_tr_diag_buf++ = p_in->a2;
    *p_tr_diag_buf++ = p_in->a3&0xff;
    *p_tr_diag_buf++ = (p_in->a3>>8)&0xff;
    *p_tr_diag_buf++ = p_in->a4&0xff;
    *p_tr_diag_buf++ = (p_in->a4>>8)&0xff;
    *p_tr_diag_buf++ = p_in->a5&0xff;
    *p_tr_diag_buf++ = (p_in->a5>>8)&0xff;

    if (tr_cur_rec == tr_max_rec) {
        print_tr_diag(p_tr_diag_base);
        state = STATE_TR;
    }
}


/*****************************************************************************
 * process_tr_diag()
 *
 * continue converting track report records into the tr_diag structure.
 *
 ****************************************************************************/
void process_tr_diag(p_input_tr_rec_t p_in)
{
    if (IS_STRAIN_BAR(p_in->bar_id)) {
        fprintf(ferr, "WARNING! Possible loss of data in tr_diag frame=%u\n",p_in->fn);
        *p_tr_diag_buf++ = p_in->bar_id<<3;
        *p_tr_diag_buf++ = p_in->a1;
        *p_tr_diag_buf++ = p_in->a2;
        *p_tr_diag_buf++ = p_in->a3;
        *p_tr_diag_buf++ = p_in->a4;
        *p_tr_diag_buf++ = p_in->a5;
        *p_tr_diag_buf++ = p_in->a6;
        *p_tr_diag_buf++ = p_in->a7;

    } else {
        *p_tr_diag_buf++ = p_in->bar_id<<3 | p_in->a1&0x3;
        *p_tr_diag_buf++ = p_in->a2;
        *p_tr_diag_buf++ = p_in->a3&0xff;
        *p_tr_diag_buf++ = (p_in->a3>>8)&0xff;
        *p_tr_diag_buf++ = p_in->a4&0xff;
        *p_tr_diag_buf++ = (p_in->a4>>8)&0xff;
        *p_tr_diag_buf++ = p_in->a5&0xff;
        *p_tr_diag_buf++ = (p_in->a5>>8)&0xff;
    }
    tr_cur_rec++;
    if (tr_cur_rec == tr_max_rec) {
        print_tr_diag(p_tr_diag_base);
        state = STATE_TR;
    }
}


/*****************************************************************************
 * print_tr_diag()
 ****************************************************************************/
void print_tr_diag(uint8_t* p_buf)
{
    if (p_buf) {
        switch (p_buf[1]) {
            case 1: print_tr_diag_001((p_tr_diag_vers_001_t) p_buf); break;
            case 2: print_tr_diag_002((p_tr_diag_vers_002_t) p_buf); break;
            default: fprintf(stderr, "ERROR! print: Unknown version %u\n", p_buf[1]);
                     exit(1);
        }
    }
}


/*****************************************************************************
 * print_tr_diag_001()
 *
 * formats and prints version 1 tr_diag.
 ****************************************************************************/
void print_tr_diag_001(p_tr_diag_vers_001_t p_diag)
{
  int i;
  fprintf(fout, "    vers=%d, gpio=0x%02x, atc=%d, ntm.stress=%d, ntm.nt=%d\n",
            p_diag->vers,
            p_diag->gpio,
            p_diag->atc,
            p_diag->ntm>>4,
            p_diag->ntm&0xf);
  fprintf(fout, "    mpa : ");
  for (i=0; i < TR_DIAG_MAX_MPATH; i++) {
    fprintf(fout, "0x%02x ",p_diag->mpa[i]);
  }
  fprintf(fout, "\n    d_mp: ");
  for (i=0; i < TR_DIAG_MAX_MPATH; i++) {
    fprintf(fout, "0x%04x ",p_diag->d_mp[i]);
  }
  fprintf(fout,"\n");
}


/*****************************************************************************
 * print_tr_diag_002()
 *
 * formats and prints version 2 tr_diag.
 ****************************************************************************/
void print_tr_diag_002(p_tr_diag_vers_002_t p_diag)
{
  int i;
  fprintf(fout, "    vers=%d, fr=%u, trig=0x%02x, gpio=0x%02x, atc=%d, ntm.stress=%d, ntm.nt=%d\n",
            p_diag->vers,
            p_diag->frame_rate,
            p_diag->trig,
            p_diag->gpio,
            p_diag->atc,
            p_diag->ntm>>4,
            p_diag->ntm&0xf);
  fprintf(fout, "    mpa : ");
  for (i=0; i < TR_DIAG_MAX_MPATH; i++) {
    fprintf(fout, "0x%04x ",p_diag->mpa[i]&0xffff);
  }
  fprintf(fout, "\n    d_mp: ");
  for (i=0; i < TR_DIAG_MAX_MPATH; i++) {
    fprintf(fout, "0x%02x ",p_diag->d_mp[i]&0xff);
  }
  fprintf(fout,"\n");
}


/*****************************************************************************
 * print_strain_tr()
 *
 ****************************************************************************/
void print_strain_tr(p_input_tr_rec_t p_in)
{
    fprintf(fout, "%u, %u, %u, %u, %u, %u, %u, %u, %u, %u\n",
            p_in->ts,
            p_in->fn,
            p_in->bar_id,
            p_in->a1,
            p_in->a2,
            p_in->a3,
            p_in->a4,
            p_in->a5,
            p_in->a6,
            p_in->a7);
}



/*****************************************************************************
 * print_normal_tr()
 *
 ****************************************************************************/
void print_normal_tr(p_input_tr_rec_t p_in)
{
    fprintf(fout, "%u, %u, %u, %u, %u, %u, %u, %u\n",
            p_in->ts,
            p_in->fn,
            p_in->bar_id,
            p_in->a1,
            p_in->a2,
            p_in->a3,
            p_in->a4,
            p_in->a5);
}

void print_track_rec_bin(int frame, p_track_report_rec_t p_tr_rec, uint32_t ts)
{   
    if (p_tr_rec==NULL) return;

    int bar_id = p_tr_rec->bar_track_id >> 5 & 0x7;
    int track_id = p_tr_rec->bar_track_id & 0x1f;
    if (ts)
        fprintf(fout, "%d(%u): ", frame, ts);
    else
        fprintf(fout, "%d: ", frame);
    if (IS_STRAIN_BAR(bar_id)) {
        p_strain_track_report_t p_sr_rec = (p_strain_track_report_t) p_tr_rec;
        fprintf(fout,"bar=%u, %u %u %u %u %u %u %u\n",
                bar_id,
                p_sr_rec->force[0],
                p_sr_rec->force[1],
                p_sr_rec->force[2],
                p_sr_rec->force[3],
                p_sr_rec->force[4],
                p_sr_rec->force[5],
                p_sr_rec->force[6]
                );
    } else {
        fprintf(fout,"bar=%u trk=%u frc=%u p0=%u p1=%u p2=%u\n",
                 bar_id, 
                 track_id, 
                 p_tr_rec->force, 
                 p_tr_rec->pos0,
                 p_tr_rec->pos1,
                 p_tr_rec->pos2);
    }
}

void print_tr_bin(p_track_report_t p_tr, uint32_t ts)
{
    if (p_tr == NULL) {fprintf(ferr,"NULL tr record\n"); exit(1);}

    fprintf(fout,"\n");
    int num_rec = (p_tr->length - sizeof(uint16_t))/sizeof(track_report_rec_t); // sub out frame field

    int rec_idx = 0;
    int gs_rec_found = 0;

    while (rec_idx < num_rec) {

        // tr_diag section
        if (p_tr->tr_rec[rec_idx].bar_track_id == 0 && p_tr->tr_rec[rec_idx].force < 0x80) {
            if (rec_idx == 0) {
                if (ts == 0)
                    fprintf(fout, "%d:\n", p_tr->frame);
                else
                    fprintf(fout, "%d(%u):\n", p_tr->frame, ts);
            }
            print_tr_diag((uint8_t*)&p_tr->tr_rec[rec_idx]);

            break;  // end of records
        } 

        if (gs_rec_found || (p_tr->tr_rec[rec_idx].bar_track_id == 0 && p_tr->tr_rec[rec_idx].force >= 0x80)) {
            // gesture section
            if (gs_rec_found == 0) {
                p_gs_hdr_rec_t p = (p_gs_hdr_rec_t) &p_tr->tr_rec[rec_idx];
                if (rec_idx == 0) {
                    if (ts == 0)
                        fprintf(fout, "%d:\n", p_tr->frame);
                    else
                        fprintf(fout, "%d(%u):\n", p_tr->frame, ts);
                }
                fprintf(fout, "    Gesture: sw0=%u sw1=%u sw2=%u, sq_s=%u, sq_l=%u\n",
                                p->swipe0_velocity, p->swipe1_velocity, p->swipe1_velocity,
                                GS_GET_SQUEEZE_SHORT(p->squeeze), 
                                GS_GET_SQUEEZE_LONG(p->squeeze));
                gs_rec_found = 1;
            } else {
                p_gs_slider_rec_t p = (p_gs_slider_rec_t) &p_tr->tr_rec[rec_idx];
                uint8_t id0 = GS_GET_SLIDER_ID0(p->slider_finger_id);
                uint8_t id1 = GS_GET_SLIDER_ID1(p->slider_finger_id);
                uint8_t fid0 = GS_GET_SLIDER_FID0(p->slider_finger_id);
                uint8_t fid1 = GS_GET_SLIDER_FID1(p->slider_finger_id);
                if (fid0) {
                    fprintf(fout, "    Slider[%u,%u]: frc=%u, pos=%u\n", id0, fid0,
                                    p->slider_force0, p->slider_pos0);
                }
                if (fid1) {
                    fprintf(fout, "    Slider[%u,%u]: frc=%u, pos=%u\n", id1, fid1,
                                    p->slider_force1, p->slider_pos1);
                }
            }

        } else {
            // track reports section
            print_track_rec_bin(p_tr->frame, &p_tr->tr_rec[rec_idx], ts);
        }
        rec_idx++;
    }
}



void process_tr_diag_bin_file(void)
{
    uint16_t rec_len;
    uint32_t ts;
    track_report_t tr;
    int ret;

    while (42) {
        // read log record length
        ret = fread((void*)&rec_len, sizeof(uint16_t), 1, fin);
        if (ret != 1) {
            if (!feof(fin)) 
                fprintf(ferr, "Failed to read bin track report log rec size %d - EOF\n", ret);
            return;
        }

        // read timestamp
        ret = fread((void*)&ts, sizeof(uint32_t), 1, fin);
        if (ret != 1) {
            fprintf(ferr, "Failed to read bin track report log rec timestamp %d\n", ret);
            return;
        }

        // read tr record length
        ret = fread((void*)&tr.length, sizeof(uint16_t), 1, fin);
        if (ret != 1) {
            fprintf(ferr, "Failed to read bin track report rec size %d - EOF\n", ret);
            return;
        }

        // read frame+tr reports
        ret = fread((void*)&tr.frame, sizeof(uint8_t), tr.length, fin);
        if (ret != tr.length) {
            fprintf(ferr, "Failed to read bin track report rec size %d - EOF\n", ret);
            return;
        }

        // print reports
        print_tr_bin(&tr, ts);
    }
}

void process_event_log_bin_file(void)
{
    uint32_t ev[2];
    int ret;
    uint32_t status;

    ret = fread((void*)&status, sizeof(uint8_t), sizeof(status), fin);
    if (ret != sizeof(status)) {
        if (!feof(fin)) 
            fprintf(ferr, "Failed to read event log status size %d\n", ret);
        return;
    }
    fprintf(fout,"EventLog Status = %d\n", status);

    while (42) {
        // read log record 
        ret = fread((void*)&ev, sizeof(uint8_t), sizeof(ev), fin);
        if (ret != sizeof(ev)) {
            if (!feof(fin)) 
                fprintf(ferr, "Failed to read event log rec size %d\n", ret);
            return;
        }
        fprintf(fout,"0x%02x  0x%02x  0x%04x  0x%04x  0x%04x\n",
                ev[0] >> 24 & 0xff,
                ev[0] >> 16 & 0xff,
                ev[0] & 0xffff,
                ev[1] >> 16 & 0xffff,
                ev[1] & 0xffff);
    }

}




void process_deep_trace_file(void)
{
    int i;
    uint16_t trace_len;
    uint16_t r_idx;
    uint16_t w_idx;
    int ret = fread((void*)&trace_len, sizeof(uint16_t), 1, fin);
    if (ret != 1) {
        fprintf(ferr, "Failed to read deep trace size %d\n", ret);
        return;
    }
    ret = fread((void*)&r_idx, sizeof(uint16_t), 1, fin);
    if (ret != 1) {
        fprintf(ferr, "Failed to read deep trace r_idx size\n");
        return;
    }
    ret = fread((void*)&w_idx, sizeof(uint16_t), 1, fin);
    if (ret != 1) {
        fprintf(ferr, "Failed to read deep trace w_idx size\n");
        return;
    }
    trace_len -= 2*sizeof(uint16_t);

    uint8_t *p_buf = malloc(trace_len);
    if (p_buf==NULL) {
        fprintf(ferr,"Could not malloc deep trace buffer %d\n", trace_len);
        return;
    }

    ret = fread((void*)p_buf, sizeof(uint8_t), trace_len, fin);
    if (ret != trace_len) {
        fprintf(ferr, "Could not read full deep trace file (%d!=%d)\n", ret, trace_len);
        return;
    }

    while (r_idx != w_idx) {
        uint8_t rec_len = p_buf[r_idx] - sizeof(uint8_t);     // sub out len field
        r_idx = (r_idx+1) % trace_len;
        track_report_t tr;
        uint8_t *p_tr_buf = (uint8_t*)&tr;
        for (i=0; i < rec_len; i++) {
            p_tr_buf[i] = p_buf[r_idx];
            r_idx = (r_idx+1) % trace_len;
        }
        print_tr_bin(&tr, 0 /*no ts*/);
    }
    
}


/* EOF */


