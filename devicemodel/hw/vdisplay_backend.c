/*
 * Copyright (C) 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Virtual Display for VMs
 *
 */
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include "log.h"
#include "vdisplay.h"
#include "atomic.h"
#include "timer.h"

#define transto_10bits(color) (uint16_t)(color * 1024 + 0.5)
#define EDID_BASIC_BLOCK_SIZE 128
#define EDID_CEA861_EXT_BLOCK_SIZE 128

struct state {
	bool is_ui_realized;
	bool is_active;
	bool is_wayland;
	bool is_x11;
	bool is_fullscreen;
	uint64_t updates;
	int n_connect;
};

struct timer_vblank {
	void *virtio_data;
	void (*vblank_inject)(void *data, unsigned int frame, int i);
	struct acrn_timer vblank_timer;
	int vblank_id;
	int refresh_rate;
	uint32_t flip_sequence;
};

struct screen {
	char *name;
	void *backend;
	struct screen_backend_ops *vscreen_ops;
	struct timespec last_time;
	bool is_timer_vblank;
	struct timer_vblank sw_vblank;
	int vrefresh;
};

static struct display {
	struct state s;
	struct screen *scrs;
	int scrs_num;
	int pipe_num;
	int vfid;

	int backlight_num;
	char *backlight[MAX_BACKLIGHT_DEVICE];
	pthread_t tid;
	/* Add one UI_timer(33ms) to render the buffers from guest_vm */
	struct acrn_timer ui_timer;
	struct vdpy_display_bh ui_timer_bh;
	// protect the request_list
	pthread_mutex_t vdisplay_mutex;
	// receive the signal that request is submitted
	pthread_cond_t  vdisplay_signal;
	TAILQ_HEAD(display_list, vdpy_display_bh) request_list;


} vdpy = {
	.s.is_ui_realized = false,
	.s.is_active = false,
	.s.is_wayland = false,
	.s.is_x11 = false,
	.s.n_connect = 0
};


typedef enum {
	ESTT = 1, // Established Timings I & II
	STDT,    // Standard Timings
	ESTT3,   // Established Timings III
	CEA861,	// CEA-861 Timings
} TIMING_MODE;

static const struct timing_entry {
	uint32_t hpixel;// Horizontal pixels
	uint32_t vpixel;// Vertical pixels
	uint32_t byte;  // byte idx in the Established Timings I & II
	uint32_t byte_t3;// byte idx in the Established Timings III Descriptor
	uint32_t bit;   // bit idx
	uint8_t hz;     // frequency
	bool	is_std; // the flag of standard mode
	bool	is_cea861; // CEA-861 timings
	uint8_t vic;	// Video Indentification Code
} timings[] = {
	/* Established Timings I & II (all @ 60Hz) */
	{ .hpixel = 1280, .vpixel = 1024, .byte  = 36, .bit = 0, .hz = 75},
	{ .hpixel = 1024, .vpixel =  768, .byte  = 36, .bit = 1, .hz = 75},
	{ .hpixel = 1024, .vpixel =  768, .byte  = 36, .bit = 3, .hz = 60},
	{ .hpixel =  800, .vpixel =  600, .byte  = 35, .bit = 0, .hz = 60 },
	{ .hpixel =  640, .vpixel =  480, .byte  = 35, .bit = 5, .hz = 60 },

	/* Standard Timings */
	{ .hpixel = 1920, .vpixel = 1080, .hz = 60,  .is_std = true },
	{ .hpixel = 1680, .vpixel = 1050, .hz = 60,  .is_std = true },
	{ .hpixel = 1600, .vpixel = 1200, .hz = 60,  .is_std = true },
	{ .hpixel = 1600, .vpixel =  900, .hz = 60,  .is_std = true },
	{ .hpixel = 1440, .vpixel =  900, .hz = 60,  .is_std = true },
};

static const struct timing_entry timings_cea[] = {
	/* CEA-861 Timings */
	{ .hpixel = 3840, .vpixel = 2160, .hz = 60,  .is_cea861 = true, .vic = 97 },
	{ .hpixel = 3840, .vpixel = 2160, .hz = 100,  .is_cea861 = true, .vic = 117 },
	{ .hpixel = 3840, .vpixel = 2160, .hz = 120,  .is_cea861 = true, .vic = 118 },
};

typedef struct frame_param{
	uint32_t hav_pixel;     // Horizontal Addressable Video in pixels
	uint32_t hb_pixel;      // Horizontal Blanking in pixels
	uint32_t hfp_pixel;     // Horizontal Front Porch in pixels
	uint32_t hsp_pixel;     // Horizontal Sync Pulse Width in pixels
	uint32_t lhb_pixel;     // Left Horizontal Border or Right Horizontal
	                        // Border in pixels

	uint32_t vav_line;      // Vertical Addressable Video in lines
	uint32_t vb_line;       // Vertical Blanking in lines
	uint32_t vfp_line;      // Vertical Front Porch in Lines
	uint32_t vsp_line;      // Vertical Sync Pulse Width in Lines
	uint32_t tvb_line;      // Top Vertical Border or Bottom Vertical
	                        // Border in Lines

	uint64_t pixel_clock;   // Hz
	uint32_t width;         // mm
	uint32_t height;        // mm
	bool high_resolution;
}frame_param;

typedef struct base_param{
	uint32_t h_pixel;       // pixels
	uint32_t v_pixel;       // lines
	uint32_t rate;          // Hz
	uint32_t width;         // mm
	uint32_t height;        // mm

	const char *id_manuf;   // ID Manufacturer Name, ISA 3-character ID Code
	uint16_t id_product;    // ID Product Code
	uint32_t id_sn;         // ID Serial Number and it is a number only.

	const char *sn;         // Serial number.
	const char *product_name;// Product name.
}base_param;

static void
vdpy_edid_set_baseparam(base_param *b_param, uint32_t width, uint32_t height, uint32_t refresh_rate)
{
	b_param->h_pixel = width;
	b_param->v_pixel = height;
	b_param->rate = refresh_rate;
	if (b_param->rate <= 0)
		b_param->rate = VDPY_DEFAULT_VREFRESH;
	b_param->width = width;
	b_param->height = height;

	b_param->id_manuf = "ACRN";
	b_param->id_product = 4321;
	b_param->id_sn = 12345678;

	b_param->sn = "A0123456789";
	b_param->product_name = "ACRN_Monitor";
}

static void
vdpy_edid_set_frame(frame_param *frame, const base_param *b_param)
{
	frame->hav_pixel = b_param->h_pixel;
	frame->hb_pixel = b_param->h_pixel * 35 / 100;
	frame->hfp_pixel = b_param->h_pixel * 25 / 100;
	frame->hsp_pixel = b_param->h_pixel * 3 / 100;
	frame->lhb_pixel = 0;
	frame->vav_line = b_param->v_pixel;
	frame->vb_line = b_param->v_pixel * 35 / 1000;
	frame->vfp_line = b_param->v_pixel * 5 / 1000;
	frame->vsp_line = b_param->v_pixel * 5 / 1000;
	frame->tvb_line = 0;
	frame->pixel_clock = b_param->rate *
			(frame->hav_pixel + frame->hb_pixel + frame->lhb_pixel * 2) *
			(frame->vav_line + frame->vb_line + frame->tvb_line * 2);
	frame->width = b_param->width;
	frame->height = b_param->height;
	frame->high_resolution = false;
	if (frame->hav_pixel >= 4096 || frame->vav_line >= 4096 ||
				(frame->pixel_clock / 10000) > 65535) {
		frame->high_resolution = true;
	}
}

static void
vdpy_edid_set_color(uint8_t *edid, float red_x, float red_y,
				   float green_x, float green_y,
				   float blue_x, float blue_y,
				   float white_x, float white_y)
{
	uint8_t *color;
	uint16_t rx, ry, gx, gy, bx, by, wx, wy;

	rx = transto_10bits(red_x);
	ry = transto_10bits(red_y);
	gx = transto_10bits(green_x);
	gy = transto_10bits(green_y);
	bx = transto_10bits(blue_x);
	by = transto_10bits(blue_y);
	wx = transto_10bits(white_x);
	wy = transto_10bits(white_y);

	color = edid + 25;
	color[0] = ((rx & 0x03) << 6) |
		   ((ry & 0x03) << 4) |
		   ((gx & 0x03) << 2) |
		    (gy & 0x03);
	color[1] = ((bx & 0x03) << 6) |
		   ((by & 0x03) << 4) |
		   ((wx & 0x03) << 2) |
		    (wy & 0x03);
	color[2] = rx >> 2;
	color[3] = ry >> 2;
	color[4] = gx >> 2;
	color[5] = gy >> 2;
	color[6] = bx >> 2;
	color[7] = by >> 2;
	color[8] = wx >> 2;
	color[9] = wy >> 2;
}

static uint8_t
vdpy_edid_set_timing(const struct timing_entry *timings, int size, uint8_t *addr, TIMING_MODE mode)
{
	static uint16_t idx;
	const struct timing_entry *timing;
	uint8_t stdcnt;
	uint16_t hpixel;
	int16_t AR;
	uint8_t num_timings;

	stdcnt = 0;
	num_timings = 0;

	if(mode == STDT) {
		addr += 38;
	}

	idx = 0;
	for(; idx < size; idx++){
		timing = timings + idx;

		switch(mode){
		case ESTT: // Established Timings I & II
			if(timing->byte) {
				addr[timing->byte] |= (1 << timing->bit);
				break;
			} else {
				continue;
			}
		case ESTT3: // Established Timings III
			if(timing->byte_t3){
				addr[timing->byte_t3] |= (1 << timing->bit);
				break;
			} else {
				continue;
			}
		case STDT: // Standard Timings
			if(stdcnt < 8 && timing->is_std) {
				hpixel = (timing->hpixel >> 3) - 31;
				if (timing->hpixel == 0 ||
				    timing->vpixel == 0) {
					AR = -1;
				} else if (hpixel & 0xff00) {
					AR = -2;
				} else if (timing->hpixel * 10 ==
				    timing->vpixel * 16) {
					AR = 0;
				} else if (timing->hpixel * 3 ==
				    timing->vpixel * 4) {
					AR = 1;
				} else if (timing->hpixel * 4 ==
				    timing->vpixel * 5) {
					AR = 2;
				} else if (timing->hpixel * 9 ==
				    timing->vpixel * 16) {
					AR = 3;
				} else {
					AR = -2;
				}
				if (AR >= 0) {
					addr[0] = hpixel & 0xff;
					addr[1] = (AR << 6) | ((timing->hz - 60) & 0x3f);
					addr += 2;
					stdcnt++;
				} else if (AR == -1){
					addr[0] = 0x01;
					addr[1] = 0x01;
					addr += 2;
					stdcnt++;
				}
				break;
			} else {
				continue;
			}
			break;
		case CEA861: // CEA-861 Timings
			if (timing->is_cea861) {
				addr[0] = timing->vic;
				addr += 1;
				num_timings++;
			}
			break;
		default:
			continue;
		}
	}
	while(mode == STDT && stdcnt < 8){
		addr[0] = 0x01;
		addr[1] = 0x01;
		addr += 2;
		stdcnt++;
	}

	return num_timings;
}

static void
vdpy_edid_set_dtd(uint8_t *dtd, const frame_param *frame)
{
	uint16_t pixel_clk;

	// Range: 10 kHz to 655.35 MHz in 10 kHz steps
	pixel_clk = frame->pixel_clock / 10000;
	memcpy(dtd, &pixel_clk, sizeof(pixel_clk));
	dtd[2] = frame->hav_pixel & 0xff;
	dtd[3] = frame->hb_pixel & 0xff;
	dtd[4] = ((frame->hav_pixel & 0xf00) >> 4) |
		 ((frame->hb_pixel & 0xf00) >> 8);
	dtd[5] = frame->vav_line & 0xff;
	dtd[6] = frame->vb_line & 0xff;
	dtd[7] = ((frame->vav_line & 0xf00) >> 4) |
		 ((frame->vb_line & 0xf00) >> 8);
	dtd[8] = frame->hfp_pixel & 0xff;
	dtd[9] = frame->hsp_pixel & 0xff;
	dtd[10] = ((frame->vfp_line & 0xf) << 4) |
		   (frame->vsp_line & 0xf);
	dtd[11] = ((frame->hfp_pixel & 0x300) >> 2) |
		  ((frame->hsp_pixel & 0x300) >> 4) |
		  ((frame->vfp_line & 0x030) >> 6) |
		  ((frame->vsp_line & 0x030) >> 8);
	dtd[12] = frame->width & 0xff;
	dtd[13] = frame->height & 0xff;
	dtd[14] = ((frame->width & 0xf00) >> 4) |
		  ((frame->height & 0xf00) >> 8);
	dtd[15] = frame->lhb_pixel & 0xff;
	dtd[16] = frame->tvb_line & 0xff;
	dtd[17] = 0x18;
}

static void
vdpy_edid_set_descripter(uint8_t *desc, uint8_t is_dtd, uint8_t tag,
			const base_param *b_param, const frame_param *frame)
{
	const char* text;
	uint16_t len;

	if (is_dtd) {
		if (frame->high_resolution) {
			/*
			 * Large screen. The pixel_clock won't fit in two bytes.
			 * We fill in a dummy DTD here and OS will pick up PTM
			 * from extension block.
			 */
			desc[3] = 0x10; /* Tag 0x10: Dummy descriptor */
		} else {
			vdpy_edid_set_dtd(desc, frame);
		}
		return;
	}
	desc[3] = tag;
	text = NULL;
	switch(tag){
	// Established Timings III Descriptor (tag #F7h)
	case 0xf7:
		desc[5] = 0x0a; // Revision Number
		vdpy_edid_set_timing(timings, sizeof(timings)/sizeof(timings[0]), desc, ESTT3);
		break;
	// Display Range Limits & Additional Timing Descriptor (tag #FDh)
	case 0xfd:
		desc[5] =  50; // Minimum Vertical Rate. (50 -> 125 Hz)
		desc[6] = 125; // Maximum Vertical Rate.
		desc[7] =  30; // Minimum Horizontal Rate.(30 -> 160 kHz)
		desc[8] = 160; // Maximum Horizontal Rate.
		desc[9] = 2550 / 10; // Max Pixel Clock. (2550 MHz)
		desc[10] = 0x01; // no extended timing information
		desc[11] = '\n'; // padding
		break;
	// Display Product Name (ASCII) String Descriptor (tag #FCh)
	case 0xfc:
	// Display Product Serial Number Descriptor (tag #FFh)
	case 0xff:
		text = (tag == 0xff) ? b_param->sn : b_param->product_name;
		memset(desc + 5, ' ', 13);
		if (text == NULL)
			break;
		len = strlen(text);
		if (len > 12)
			len = 12;
		memcpy(desc + 5, text, len);
		desc[len + 5] = '\n';
		break;
	// Dummy Descriptor (Tag #10h)
	case 0x10:
	default:
		break;
	}
}

static uint8_t
vdpy_edid_get_checksum(uint8_t *edid)
{
	uint8_t sum;
	int i;

	sum = 0;
	for (i = 0; i < 127; i++) {
		sum += edid[i];
	}

	return 0x100 - sum;
}

static void
vdpy_edid_generate(uint8_t *edid, size_t size, struct edid_info *info)
{
	uint16_t id_manuf;
	uint16_t id_product;
	uint16_t did_value;
	uint32_t serial;
	uint8_t *desc;
	uint8_t *edid_did;
	int index;
	base_param b_param;
	uint8_t num_cea_timings = 0;
	frame_param frame;

	vdpy_edid_set_baseparam(&b_param, info->prefx, info->prefy, info->refresh_rate);
	vdpy_edid_set_frame(&frame, &b_param);

	memset(edid, 0, size);
	/* edid[7:0], fixed header information, (00 FF FF FF FF FF FF 00)h */
	memset(edid + 1, 0xff, 6);

	/* edid[17:8], Vendor & Product Identification */
	// Manufacturer ID is a big-endian 16-bit value.
	id_manuf = ((((b_param.id_manuf[0] - '@') & 0x1f) << 10) |
		    (((b_param.id_manuf[1] - '@') & 0x1f) << 5) |
		    (((b_param.id_manuf[2] - '@') & 0x1f) << 0));
	edid[8] = id_manuf >> 8;
	edid[9] = id_manuf & 0xff;

	// Manufacturer product code is a little-endian 16-bit number.
	id_product = b_param.id_product;
	memcpy(edid+10, &id_product, sizeof(id_product));

	// Serial number is a little-endian 32-bit value.
	serial = b_param.id_sn;
	memcpy(edid+12, &serial, sizeof(serial));

	edid[16] = 0;           // Week of Manufacture
	edid[17] = 2018 - 1990; // Year of Manufacture or Model Year.
				// Acrn is released in 2018.

	edid[18] = 1;   // Version Number
	edid[19] = 4;   // Revision Number

	/* edid[24:20], Basic Display Parameters & Features */
	// Video Input Definition: 1 Byte
	edid[20] = 0xa5; // Digital input;
			 // 8 Bits per Primary Color;
			 // DisplayPort is supported

	// Horizontal and Vertical Screen Size or Aspect Ratio: 2 Bytes
	// screen size, in centimetres
	edid[21] = info->prefx / 10;
	edid[22] = info->prefy / 10;

	// Display Transfer Characteristics (GAMMA): 1 Byte
	// Stored Value = (GAMMA x 100) - 100
	edid[23] = 120; // display gamma: 2.2

	// Feature Support: 1 Byte
	edid[24] = 0x06; // sRGB Standard is the default color space;
			 // Preferred Timing Mode includes the native
			 // pixel format and preferred.

	/* edid[34:25], Display x, y Chromaticity Coordinates */
	vdpy_edid_set_color(edid, 0.6400, 0.3300,
				  0.3000, 0.6000,
				  0.1500, 0.0600,
				  0.3127, 0.3290);

	/* edid[37:35], Established Timings */
	vdpy_edid_set_timing(timings, sizeof(timings)/sizeof(timings[0]), edid, ESTT);

	/* edid[53:38], Standard Timings: Identification 1 -> 8 */
	vdpy_edid_set_timing(timings, sizeof(timings)/sizeof(timings[0]), edid, STDT);

	/* edid[125:54], Detailed Timing Descriptor - 18 bytes x 4 */
	// Preferred Timing Mode
	desc = edid + 54;
	vdpy_edid_set_descripter(desc, 0x1, 0, &b_param, &frame);
	// Display Range Limits & Additional Timing Descriptor (tag #FDh)
	desc += 18;
	vdpy_edid_set_descripter(desc, 0, 0xfd, &b_param, &frame);
	// Display Product Name (ASCII) String Descriptor (tag #FCh)
	desc += 18;
	vdpy_edid_set_descripter(desc, 0, 0xfc, &b_param, &frame);
	// Display Product Serial Number Descriptor (tag #FFh)
	desc += 18;
	vdpy_edid_set_descripter(desc, 0, 0xff, &b_param, &frame);

	/* EDID[126], Extension Block Count */
	edid[126] = 0;  // no Extension Block

	/* Checksum */
	edid[127] = vdpy_edid_get_checksum(edid);

	if (size >= (EDID_BASIC_BLOCK_SIZE + EDID_CEA861_EXT_BLOCK_SIZE)) {
		edid[126] = 1;
		edid[127] = vdpy_edid_get_checksum(edid);

		// CEA EDID Extension
		edid[EDID_BASIC_BLOCK_SIZE + 0] = 0x02;
		// Revision Number
		edid[EDID_BASIC_BLOCK_SIZE + 1] = 0x03;
		// SVDs
		edid[EDID_BASIC_BLOCK_SIZE + 4] |= 0x02 << 5;
		desc = edid + EDID_BASIC_BLOCK_SIZE + 5;

		for (index = 0; index < sizeof(timings_cea)/sizeof(timings_cea[0]); index++) {
			if (b_param.h_pixel == timings_cea[index].hpixel &&
					b_param.v_pixel == timings_cea[index].vpixel &&
					b_param.rate == timings_cea[index].hz) {
				break;
			}

		}
		if (index < sizeof(timings_cea)/sizeof(timings_cea[0]))
			num_cea_timings += vdpy_edid_set_timing(&timings_cea[index], 1, desc, CEA861);

		edid[EDID_BASIC_BLOCK_SIZE + 4] |= num_cea_timings;
		edid[EDID_BASIC_BLOCK_SIZE + 2] |= 5 + num_cea_timings;

		desc = edid + EDID_BASIC_BLOCK_SIZE;
		edid[EDID_BASIC_BLOCK_SIZE + 127] = vdpy_edid_get_checksum(desc);
	}

	if (frame.high_resolution && size >= 384) {
		edid[126]++;
		edid[127] = vdpy_edid_get_checksum(edid);
		edid_did = edid + 256;
		// DISPLAY ID Extension
		edid_did[0] = 0x70;
		// Version
		edid_did[1] = 0x13;
		// len
		edid_did[2] = 23;
		// product type
		edid_did[3] = 0x03;
		// DTD
		edid_did[5] = 0x03;
		// Revision
		edid_did[6] = 0x00;
		// Block len
		edid_did[7] = 0x14;
		edid_did[8]  = (frame.pixel_clock / 10000) & 0xff;
		edid_did[9]  = ((frame.pixel_clock / 10000) & 0xff00) >> 8;
		edid_did[10] = ((frame.pixel_clock /10000) & 0xff0000) >> 16;
		edid_did[11] = 0x88;
		did_value = (frame.hav_pixel - 1) & 0xffff;
		memcpy(&edid_did[12],  &did_value, 2);
		did_value = (frame.hb_pixel - 1) & 0xffff;
		memcpy(&edid_did[14],  &did_value, 2);
		did_value = (frame.hfp_pixel - 1) & 0xffff;
		memcpy(&edid_did[16],  &did_value, 2);
		did_value = (frame.hsp_pixel - 1) & 0xffff;
		memcpy(&edid_did[18],  &did_value, 2);
		did_value = (frame.vav_line - 1) & 0xffff;
		memcpy(&edid_did[20],  &did_value, 2);
		did_value = (frame.vb_line - 1) & 0xffff;
		memcpy(&edid_did[22],  &did_value, 2);
		did_value = (frame.vfp_line - 1) & 0xffff;
		memcpy(&edid_did[24],  &did_value, 2);
		did_value = (frame.vsp_line - 1) & 0xffff;
		memcpy(&edid_did[26],  &did_value, 2);
		edid_did[28] = vdpy_edid_get_checksum(edid_did + 1);
		edid_did[127] = vdpy_edid_get_checksum(edid_did);
	}
}

static void
query_display_info(int scanout_id, struct display_info *info)
{
	struct screen *vscr;
	if (!info || scanout_id >= vdpy.scrs_num)
		return;
	vscr = vdpy.scrs + scanout_id;
	vscr->vscreen_ops->vdpy_display_info(vscr->backend, info);
	/* override by user parameter */
	if (vscr->vrefresh > 0)
		info->vrefresh = vscr->vrefresh;
	if (info->vrefresh <= 0)
		info->vrefresh = VDPY_DEFAULT_VREFRESH;
}

void
vdpy_get_edid(int handle, int scanout_id, uint8_t *edid, size_t size)
{
	struct edid_info edid_info;
	struct display_info display;

	if (scanout_id >= vdpy.scrs_num)
		return;

	query_display_info(scanout_id, &display);

	if (handle == vdpy.s.n_connect) {
		edid_info.prefx = display.width;
		edid_info.prefy = display.height;
		edid_info.maxx = VDPY_MAX_WIDTH;
		edid_info.maxy = VDPY_MAX_HEIGHT;
		edid_info.refresh_rate = display.vrefresh;
	} else {
		edid_info.prefx = VDPY_DEFAULT_WIDTH;
		edid_info.prefy = VDPY_DEFAULT_HEIGHT;
		edid_info.maxx = VDPY_MAX_WIDTH;
		edid_info.maxy = VDPY_MAX_HEIGHT;
		edid_info.refresh_rate = VDPY_DEFAULT_VREFRESH;
	}
	edid_info.vendor = NULL;
	edid_info.name = NULL;
	edid_info.sn = NULL;

	vdpy_edid_generate(edid, size, &edid_info);
}

void
vdpy_get_display_info(int handle, int scanout_id, struct display_info *info)
{
	struct display_info display;
	if (scanout_id >= vdpy.scrs_num)
		return;

	query_display_info(scanout_id, &display);

	if (handle == vdpy.s.n_connect) {
		info->xoff = display.xoff;
		info->yoff = display.yoff;
		info->width = display.width;
		info->height = display.height;
	} else {
		info->xoff = 0;
		info->yoff = 0;
		info->width = 0;
		info->height = 0;
	}
}

void vdpy_set_rotation(int handle,int scanout_id, int plane_id, uint64_t rotation)
{
	struct screen *vscr;
	vscr = vdpy.scrs + scanout_id;
	if(vscr->vscreen_ops->vdpy_set_rotation)
		vscr->vscreen_ops->vdpy_set_rotation(vscr->backend, plane_id, rotation);
}

void vdpy_set_pixel_blend_mode(int handle,int scanout_id, int plane_id, uint32_t mode, uint16_t alpha)
{
	struct screen *vscr;
	vscr = vdpy.scrs + scanout_id;
	if(vscr->vscreen_ops->vdpy_set_pixel_blend_mode)
		vscr->vscreen_ops->vdpy_set_pixel_blend_mode(vscr->backend, plane_id, mode, alpha);
}

void vdpy_set_planar(int handle,int scanout_id, int plane_id, uint32_t size, uint32_t *dmabuf)
{
	struct screen *vscr;
	vscr = vdpy.scrs + scanout_id;
	if(vscr->vscreen_ops->vdpy_set_planar)
		vscr->vscreen_ops->vdpy_set_planar(vscr->backend, plane_id, size, dmabuf);
}

void
vdpy_get_plane_rotation(int handle, int scanout_id, int plane_id, uint64_t *rotation, uint32_t *count)
{
	struct screen *vscr;
	vscr = vdpy.scrs + scanout_id;
	if(vscr->vscreen_ops->vdpy_get_plane_rotation) {
		vscr->vscreen_ops->vdpy_get_plane_rotation(vscr->backend, plane_id, rotation, count);
	} else {
		*rotation= 0;
	}
}

void vdpy_sprite_flush_sync(int handle, int scanout_id)
{
	struct screen *vscr;
	vscr = vdpy.scrs + scanout_id;
	if(vscr->vscreen_ops->vdpy_sprite_flush_sync)
		vscr->vscreen_ops->vdpy_sprite_flush_sync(vscr->backend);
}

void vdpy_update_sprite(int handle, int scanout_id, int plane_id, struct surface *surf)
{
	struct screen *vscr;
	vscr = vdpy.scrs + scanout_id;
	if(vscr->vscreen_ops->vdpy_update_sprite)
		vscr->vscreen_ops->vdpy_update_sprite(vscr->backend, plane_id, surf);

}

void
vdpy_get_plane_info(int handle, int scanout_id, uint32_t *size, uint32_t *num, uint32_t *info)
{
	struct screen *vscr;
	vscr = vdpy.scrs + scanout_id;
	if(vscr->vscreen_ops->vdpy_get_plane_info)
		vscr->vscreen_ops->vdpy_get_plane_info(vscr->backend, size, num, info);
	else {
		*size = 0;
		*num = 0;
	}
}

int
vdpy_backlight_update_status(int handle, uint32_t backlight_id, struct backlight_properties *props)
{
	int ret = 0;

	if (handle != vdpy.s.n_connect)
		return -1;
	if (backlight_id >= vdpy.backlight_num)
		return -1;

	ret = set_backlight_brightness(vdpy.backlight[backlight_id], props->brightness);
	ret = set_backlight_power(vdpy.backlight[backlight_id], props->power);
	return ret;
}

int
vdpy_get_backlight(int handle, uint32_t backlight_id, int32_t *brightness)
{
	int ret = 0;

	if (handle != vdpy.s.n_connect)
		return -1;
	if (backlight_id >= vdpy.backlight_num)
		return -1;

	ret = get_backlight_brightness(vdpy.backlight[backlight_id], brightness);
	return ret;
}

int
vdpy_get_backlight_info(int handle, uint32_t backlight_id, struct backlight_info *info)
{
	int ret = 0;

	if (handle != vdpy.s.n_connect)
		return -1;
	if (backlight_id >= vdpy.backlight_num)
		return -1;

	ret = get_backlight_brightness_info(vdpy.backlight[backlight_id], info);
	return ret;
}

static void
vdpy_sdl_ui_timer(void *data, uint64_t nexp)
{
	struct display *ui_vdpy;
	struct vdpy_display_bh *bh_task;

	ui_vdpy = (struct display *)data;

	/* Don't submit the display_request if another func already
	 * acquires the mutex.
	 * This is to optimize the mevent thread otherwise it needs
	 * to wait for some time.
	 */
	if (pthread_mutex_trylock(&ui_vdpy->vdisplay_mutex))
		return;

	bh_task = &ui_vdpy->ui_timer_bh;
	if ((bh_task->bh_flag & ACRN_BH_PENDING) == 0) {
		bh_task->bh_flag |= ACRN_BH_PENDING;
		TAILQ_INSERT_TAIL(&ui_vdpy->request_list, bh_task, link);
	}
	pthread_cond_signal(&ui_vdpy->vdisplay_signal);
	pthread_mutex_unlock(&ui_vdpy->vdisplay_mutex);
}

static void
vdpy_refresh(void *data)
{
	struct display *ui_vdpy;
	struct timespec cur_time;
	uint64_t elapsed_time;
	struct screen *vscr;
	int i;

	ui_vdpy = (struct display *)data;
	for (i = 0; i < vdpy.scrs_num; i++) {
		vscr = ui_vdpy->scrs + i;

		clock_gettime(CLOCK_MONOTONIC, &cur_time);

		elapsed_time = (cur_time.tv_sec - vscr->last_time.tv_sec) * 1000000000 +
				cur_time.tv_nsec - vscr->last_time.tv_nsec;

		/* the time interval is less than 10ms. Skip it */
		if (elapsed_time < 10000000)
			return;

		if(vscr->vscreen_ops->vdpy_cursor_refresh)
			vscr->vscreen_ops->vdpy_cursor_refresh(vscr->backend);
		}
}

static int vdpy_init_thread()
{
	int ret = 0;
	struct vdpy_backend **pdpp, *pdp;
	SET_FOREACH(pdpp, vdpy_backend_set) {
		pdp = *pdpp;
		if (pdp->init_thread)
			ret |= pdp->init_thread();
	}
	return ret;
}

static void vdpy_deinit_thread()
{
	struct vdpy_backend **pdpp, *pdp;
	SET_FOREACH(pdpp, vdpy_backend_set) {
		pdp = *pdpp;
		if(pdp->deinit_thread) {
			pdp->deinit_thread();
		}
	}
}

static void *
vdpy_display_thread(void *data)
{
	struct vdpy_display_bh *bh;
	struct itimerspec ui_timer_spec;

	struct screen *vscr;
	int i, ret = 0;

	ret = vdpy_init_thread();
	if(ret)
		goto sdl_fail;

	for (i = 0; i < vdpy.scrs_num; i++) {
		vscr = vdpy.scrs + i;
		clock_gettime(CLOCK_MONOTONIC, &vscr->last_time);
	}
	pthread_mutex_init(&vdpy.vdisplay_mutex, NULL);
	pthread_cond_init(&vdpy.vdisplay_signal, NULL);
	TAILQ_INIT(&vdpy.request_list);
	vdpy.s.is_active = 1;

	vdpy.ui_timer_bh.task_cb = vdpy_refresh;
	vdpy.ui_timer_bh.data = &vdpy;
	vdpy.ui_timer.clockid = CLOCK_MONOTONIC;
	acrn_timer_init(&vdpy.ui_timer, vdpy_sdl_ui_timer, &vdpy);
	ui_timer_spec.it_interval.tv_sec = 0;
	ui_timer_spec.it_interval.tv_nsec = 33000000;
	/* Wait for 5s to start the timer */
	ui_timer_spec.it_value.tv_sec = 5;
	ui_timer_spec.it_value.tv_nsec = 0;
	/* Start one periodic timer to refresh UI based on 30fps */
	acrn_timer_settime(&vdpy.ui_timer, &ui_timer_spec);

	pr_info("vdisplay thread is created\n");
	/* Begin to process the display_cmd after initialization */
	do {
		if (!vdpy.s.is_active) {
			pr_info("display is exiting\n");
			break;
		}
		pthread_mutex_lock(&vdpy.vdisplay_mutex);

		if (TAILQ_EMPTY(&vdpy.request_list))
			pthread_cond_wait(&vdpy.vdisplay_signal,
					  &vdpy.vdisplay_mutex);

		/* the bh_task is handled in vdisplay_mutex lock */
		while (!TAILQ_EMPTY(&vdpy.request_list)) {
			bh = TAILQ_FIRST(&vdpy.request_list);

			TAILQ_REMOVE(&vdpy.request_list, bh, link);

			bh->task_cb(bh->data);

			if (atomic_load(&bh->bh_flag) & ACRN_BH_FREE) {
				free(bh);
				bh = NULL;
			} else {
				/* free is owned by the submitter */
				atomic_store(&bh->bh_flag, ACRN_BH_DONE);
			}
		}

		pthread_mutex_unlock(&vdpy.vdisplay_mutex);
	} while (1);

	acrn_timer_deinit(&vdpy.ui_timer);
	/* SDL display_thread will exit because of DM request */
	pthread_mutex_destroy(&vdpy.vdisplay_mutex);
	pthread_cond_destroy(&vdpy.vdisplay_signal);

sdl_fail:
	vdpy_deinit_thread();
	return NULL;
}

bool vdpy_submit_bh(int handle, struct vdpy_display_bh *bh_task)
{
	bool bh_ok = false;

	if (handle != vdpy.s.n_connect) {
		return bh_ok;
	}

	if (!vdpy.s.is_active)
		return bh_ok;

	pthread_mutex_lock(&vdpy.vdisplay_mutex);

	if ((bh_task->bh_flag & ACRN_BH_PENDING) == 0) {
		bh_task->bh_flag |= ACRN_BH_PENDING;
		TAILQ_INSERT_TAIL(&vdpy.request_list, bh_task, link);
		bh_ok = true;
	}
	pthread_cond_signal(&vdpy.vdisplay_signal);
	pthread_mutex_unlock(&vdpy.vdisplay_mutex);

	return bh_ok;
}

void vblank_timer_handler(void *arg, uint64_t n)
{
	struct timer_vblank *tvbl = (struct timer_vblank *)arg;
	tvbl->vblank_inject(tvbl->virtio_data, ++tvbl->flip_sequence, tvbl->vblank_id);
}

static void timer_vblank_init(struct timer_vblank *tvbl, void (*func)
		(void *data,unsigned int frame,int i), void *data)
{
	int rc;
	tvbl->vblank_timer.clockid = CLOCK_MONOTONIC;
	tvbl->vblank_inject = func;
	tvbl->virtio_data = data;
	rc = acrn_timer_init(&tvbl->vblank_timer, vblank_timer_handler,
			tvbl);
	if (rc < 0) {
		pr_err("initializa vblank timer failed \r\n");
		return;
	}
}

static void timer_vblank_enable(struct timer_vblank *tvbl)
{
	struct itimerspec ts;
	/* setting the interval time */
	ts.it_interval.tv_sec = 0;
	ts.it_interval.tv_nsec = 1000000000 / tvbl->refresh_rate;
	/* set the delay time it will be started when timer_setting */
	ts.it_value.tv_sec = 0;
	ts.it_value.tv_nsec = 1;
	if (acrn_timer_settime(&tvbl->vblank_timer, &ts) != 0) {
		pr_err("acrn timer set time failed\n");
		return;
	}
}

void
vdpy_vblank_init(int scanout_id, void (*func)
		(void *data,unsigned int frame,int i), void *data)
{
	struct screen *vscr;
	struct display_info display;
	vscr = vdpy.scrs + scanout_id;
	if(vscr->is_timer_vblank) {
		query_display_info(scanout_id, &display);
		vscr->sw_vblank.refresh_rate = display.vrefresh;
		timer_vblank_init(&vscr->sw_vblank, func, data);
		return;
	}

	if(vscr->vscreen_ops->vdpy_vblank_init)
		vscr->vscreen_ops->vdpy_vblank_init(vscr->backend, func, data);
}

int
vdpy_init(struct vdpy_if *vdpy_if, vblank_inject_func func, void *data)
{
	int err, count;

	if (vdpy.s.n_connect)
		return 0;

	/* start one vdpy_display_thread to handle the 3D request
	 * in this dedicated thread. Otherwise the libSDL + 3D doesn't
	 * work.
	 */
	err = pthread_create(&vdpy.tid, NULL, vdpy_display_thread, &vdpy);
	if (err) {
		pr_err("Failed to create the vdpy_display_thread.\n");
		return 0;
	}
	pthread_setname_np(vdpy.tid, "acrn_vdisplay");
	count = 0;
	/* Wait up to 200ms so that the vdpy_display_thread is ready to
	 * handle the 3D request
	 */
	while (!vdpy.s.is_active && count < 50) {
		usleep(10000);
		count++;
	}

	if (!vdpy.s.is_active) {
		pr_err("display_thread is not ready.\n");
		return vdpy.s.n_connect;
	}
	vdpy.s.n_connect++;

	if (vdpy_if) {
		vdpy_if->scanout_num = vdpy.scrs_num;
		vdpy_if->pipe_num = vdpy.pipe_num;
		vdpy_if->backlight_num = vdpy.backlight_num;
		vdpy_if->vfid = vdpy.vfid;
		for(count=0; count< vdpy.scrs_num; count++) {
			vdpy_vblank_init(count,func,data);
		}
	}

	return vdpy.s.n_connect;
}

int
vdpy_deinit(int handle)
{
	int i;
	if (handle != vdpy.s.n_connect) {
		return -1;
	}

	vdpy.s.n_connect--;

	if (!vdpy.s.is_active) {
		return -1;
	}

	pthread_mutex_lock(&vdpy.vdisplay_mutex);
	vdpy.s.is_active = 0;
	/* Wakeup the vdpy_display_thread if it is waiting for signal */
	pthread_cond_signal(&vdpy.vdisplay_signal);
	pthread_mutex_unlock(&vdpy.vdisplay_mutex);

	pthread_join(vdpy.tid, NULL);

	for(i=0; i<vdpy.scrs_num; i++) {
		if(vdpy.scrs[i].is_timer_vblank)
			acrn_timer_deinit(&vdpy.scrs[i].sw_vblank.vblank_timer);
	}

	pr_info("Exit SDL display thread\n");

	return 0;
}

void
vdpy_enable_vblank(int scanout_id)
{
	struct screen *vscr;
	vscr = vdpy.scrs + scanout_id;
	if(vscr->is_timer_vblank) {
		timer_vblank_enable(&vscr->sw_vblank);
		return;
	}

	if(vscr->vscreen_ops->vdpy_enable_vblank)
		vscr->vscreen_ops->vdpy_enable_vblank(vscr->backend);
}

void
vdpy_create_res(int dmabuf_fd)
{
	struct vdpy_backend **pdpp, *pdp;
	SET_FOREACH(pdpp, vdpy_backend_set) {
		pdp = *pdpp;
		if(pdp->create_res)
			pdp->create_res(dmabuf_fd);
	}

}
void
vdpy_destroy_res(int dmabuf_fd)
{
	struct vdpy_backend **pdpp, *pdp;
	SET_FOREACH(pdpp, vdpy_backend_set) {
		pdp = *pdpp;
		if(pdp->destroy_res)
			pdp->destroy_res(dmabuf_fd);
	}
}
void
vdpy_surface_set(int handle, int scanout_id, struct surface *surf)
{
	struct screen *vscr;
	vscr = vdpy.scrs + scanout_id;

	if (handle != vdpy.s.n_connect) {
		return;
	}
	if (vdpy.tid != pthread_self()) {
		pr_err("%s: unexpected code path as unsafe 3D ops in multi-threads env.\n",
			__func__);
		return;
	}
	if (scanout_id >= vdpy.scrs_num) {
		return;
	}

	vscr->vscreen_ops->vdpy_surface_set(vscr->backend, surf);
}

void vdpy_surface_update(int handle, int scanout_id, struct surface *surf)
{
	struct screen *vscr;
	vscr = vdpy.scrs + scanout_id;
	if (handle != vdpy.s.n_connect) {
		return;
	}

	if (vdpy.tid != pthread_self()) {
		pr_err("%s: unexpected code path as unsafe 3D ops in multi-threads env.\n",
			__func__);
		return;
	}

	if (!surf) {
		pr_err("Incorrect order of submitting Virtio-GPU cmd.\n");
	        return;
	}

	if (scanout_id >= vdpy.scrs_num) {
		return;
	}

	vscr->vscreen_ops->vdpy_surface_update(vscr->backend, surf);

	/* update the rendering time */
	clock_gettime(CLOCK_MONOTONIC, &vscr->last_time);
}

void vdpy_cursor_define(int handle, int scanout_id, struct cursor *cur)
{
	struct screen *vscr;
	vscr = vdpy.scrs + scanout_id;
	if (handle != vdpy.s.n_connect) {
		return;
	}

	if (vdpy.tid != pthread_self()) {
		pr_err("%s: unexpected code path as unsafe 3D ops in multi-threads env.\n",
			__func__);
		return;
	}

	if (scanout_id >= vdpy.scrs_num) {
		return;
	}
	if(vscr->vscreen_ops->vdpy_cursor_define)
		vscr->vscreen_ops->vdpy_cursor_define(vscr->backend, cur);
}

void vdpy_cursor_move(int handle, int scanout_id, uint32_t x, uint32_t y)
{
	struct screen *vscr;
	vscr = vdpy.scrs + scanout_id;
	if (handle != vdpy.s.n_connect) {
		return;
	}

	if (scanout_id >= vdpy.scrs_num) {
		return;
	}

	if(vscr->vscreen_ops->vdpy_cursor_move)
		vscr->vscreen_ops->vdpy_cursor_move(vscr->backend, x, y);
}

void vdpy_surface_set_vga(int handle, int scanout_id, struct surface *surf)
{
	struct screen *vscr;
	vscr = vdpy.scrs + scanout_id;

	if (handle != vdpy.s.n_connect) {
		return;
	}
	if (scanout_id >= vdpy.scrs_num) {
		return;
	}

	if(vscr->vscreen_ops->vdpy_surface_set_vga)
		vscr->vscreen_ops->vdpy_surface_set_vga(vscr->backend, surf);
}

void vdpy_surface_update_vga(int handle, int scanout_id, struct surface *surf)
{
	struct screen *vscr;

	if (handle != vdpy.s.n_connect) {
		return;
	}
	if (scanout_id >= vdpy.scrs_num) {
		return;
	}
	vscr = vdpy.scrs + scanout_id;

	if(vscr->vscreen_ops->vdpy_surface_update_vga)
		vscr->vscreen_ops->vdpy_surface_update_vga(vscr->backend, surf);
}

void vdpy_set_modifier(int handle, uint64_t modifier, int scanout_id)
{
	struct screen *vscr;
	vscr = vdpy.scrs + scanout_id;
	vscr->vscreen_ops->vdpy_set_modifier(vscr->backend, modifier);
}

void vdpy_set_scaling(int handle,int scanout_id, int plane_id, int x1, int y1, int x2, int y2)
{
	struct screen *vscr;
	vscr = vdpy.scrs + scanout_id;
	vscr->vscreen_ops->vdpy_set_scaling(vscr->backend, plane_id, x1, y1, x2, y2);
}

static struct vdpy_backend *vdpy_find_backend(char *name)
{
	struct vdpy_backend **pdpp, *pdp;

	SET_FOREACH(pdpp, vdpy_backend_set) {
		pdp = *pdpp;
		if (!strcmp(pdp->name, name))
			return pdp;
	}

	return NULL;
}

static void vdpy_init_screen(char *name, void **backend, struct screen_backend_ops **screen_ops)
{
	struct vdpy_backend *ops;
	ops = vdpy_find_backend(name);
	if (!ops) {
		pr_err("failed to find backend %s", name);
		return;
	}
	if (ops->init_screen)
		ops->init_screen(backend, screen_ops);
}

static int init_backends()
{
	int ret = 0;
	struct vdpy_backend **pdpp, *pdp;
	SET_FOREACH(pdpp, vdpy_backend_set) {
		pdp = *pdpp;
		if (pdp->init)
			ret |= pdp->init();
	}
	return ret;
}

static void deinit_backends()
{
	struct vdpy_backend **pdpp, *pdp;
	SET_FOREACH(pdpp, vdpy_backend_set) {
		pdp = *pdpp;
		if(pdp->deinit) {
			pdp->deinit();
		}
	}
}

bool vdpy_mplane_check()
{
	struct vdpy_backend **pdpp, *pdp;
	int i = 0;
	bool active_backend = false;
	bool mplane_support = false;
	SET_FOREACH(pdpp, vdpy_backend_set) {
		pdp = *pdpp;
		active_backend = false;
		for (i = 0; i < vdpy.scrs_num; i++) {
			if (strcmp(vdpy.scrs[i].name, pdp->name) == 0) {
				active_backend = true;
				break;
			}
		}
		if(active_backend) {
			if (!pdp->mplane_check) {
				mplane_support = false;
			} else {
				mplane_support = pdp->mplane_check();
			}
			if (!mplane_support)
				return false;
		}
	}
	if (mplane_support)
		return true;
	else
		return false;
}

void vdpy_mplane_fallback()
{
	struct vdpy_backend **pdpp, *pdp;
	SET_FOREACH(pdpp, vdpy_backend_set) {
		pdp = *pdpp;
		if(pdp->mplane_fallback)
			pdp->mplane_fallback();
	}
}

int
gfx_ui_init()
{
	int ret,i;
	ret = init_backends();
	if(!ret) {
		for(i=0; i<vdpy.scrs_num; i++) {
			vdpy_init_screen(vdpy.scrs[i].name, &vdpy.scrs[i].backend,
					&vdpy.scrs[i].vscreen_ops);
		}
		vdpy.s.is_ui_realized = true;
	}
	return ret;
}

void
gfx_ui_deinit()
{
	if (!vdpy.s.is_ui_realized) {
		return;
	}
	deinit_backends();
	free(vdpy.scrs);
}

static int vdpy_set_para(char *name, char *tmp)
{
	struct vdpy_backend *ops;
	ops = vdpy_find_backend(name);
	if (!ops) {
		pr_err("failed to find backend %s", name);
		return -1;
	}
	if (ops->parse_cmd)
		return ops->parse_cmd(tmp);
	else
		return 0;
}

int vdpy_parse_cmd_option(const char *opts)
{
	char *str, *stropts, *tmp, *subtmp;
	int error, snum, vfid = 0;
	int hz = -1;
	struct screen *scr;

	error = 0;
	scr = calloc(VSCREEN_MAX_NUM, sizeof(struct screen));
	if (!scr) {
		pr_err("%s, memory allocation for scrs failed.", __func__);
		return -1;
	}

	vdpy.scrs = scr;
	vdpy.scrs_num = 0;
	vdpy.pipe_num = 0;
	vdpy.backlight_num = 0;
	vdpy.vfid = 0;
	stropts = strdup(opts);
	while ((str = strsep(&stropts, ",")) != NULL) {
		if (strcasestr(str, "backlight=")) {
			if((strncmp(str, "backlight", 9) == 0)) {
				strsep(&str, "=");
				if (vdpy.backlight_num <= MAX_BACKLIGHT_DEVICE &&
						strlen(str) > 0 && check_backlist_device(str) >= 0) {
					vdpy.backlight[vdpy.backlight_num] = strdup(str);
					pr_info("backlight dev:%s\n", str);
					vdpy.backlight_num++;
				}
			}
			continue;
		}
		if (strcasestr(str, "dgpu-vfid=")) {
			snum = sscanf(str, "dgpu-vfid=%d", &vfid);
			if (snum == 1)
				vdpy.vfid = vfid;
			else
				pr_err("invalid value for vfid: %s\n", str);
			continue;
		}

		/* pipe paramter */
		scr = vdpy.scrs + vdpy.scrs_num;
		subtmp = strcasestr(str, "timer-vblank");
		if (subtmp) {
			scr->is_timer_vblank = true;
			scr->sw_vblank.vblank_id = 2 + vdpy.pipe_num;
			vdpy.pipe_num++;
		}
		scr->vrefresh = 0;
		subtmp = strcasestr(str, "hz=");
		if (subtmp) {
			snum = sscanf(subtmp, "hz=%d", &hz);
			if (snum != 1) {
				pr_err("incorrect hz option");
			}
			scr->vrefresh = hz;
		}

		tmp = strcasestr(str, "=");
		if (tmp && strcasestr(str, "geometry=")) {
			scr->name = "sdl";
			vdpy.scrs_num++;
                } else if (tmp && strcasestr(str, "lease=")) {
                       scr->name = "lease";
                       vdpy.scrs_num++;
                       vdpy.pipe_num++;
		} else if (tmp && strcasestr(str, "projection=")) {
			scr->name = "projection";
			vdpy.scrs_num++;
			vdpy.pipe_num++;
		} else if (tmp && strcasestr(str, "plane-display=")) {
			scr->name = "plane-display";
			vdpy.scrs_num++;
			vdpy.pipe_num++;
		} else {
			pr_err("couldn't find backend for parameter %s\n", str);
			error = -1;
			goto out;
		}

		if (scr->name)
			error = vdpy_set_para(scr->name,str);

		if (vdpy.scrs_num > VSCREEN_MAX_NUM) {
			pr_err("%d virtual displays are too many that acrn-dm can't support!\n");
			break;
		}
	}
out:
	free(stropts);

	return error;
}
