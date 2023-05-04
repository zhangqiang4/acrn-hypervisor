/*-
* Copyright (c) 2023 Intel Corporation.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions
* are met:
* 1. Redistributions of source code must retain the above copyright
*    notice, this list of conditions and the following disclaimer.
* 2. Redistributions in binary form must reproduce the above copyright
*    notice, this list of conditions and the following disclaimer in the
*    documentation and/or other materials provided with the distribution.
*
* THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
* ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
* ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
* OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
* HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
* LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
* OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
* SUCH DAMAGE.
*
* $FreeBSD$
*/

#ifndef TELLTALE_H_
#define TELLTALE_H_

#include <types.h>

/*
 * we can do telltale on every display pipe with regional CRC support. There
 * may be multiple pipes on a GPU and there may be multiple GPUs on a system.
 */
#define MAX_TELLTALE_INSTANCES 	8
#define MAX_REGION_PER_PIPE 	8
#define MAX_RECORDS_PER_PIPE 	32 	/* must be the power of 2 */

struct display_region {
	uint16_t x, y;
	uint16_t width, height;
};

struct telltale_enable_crc_data {
	uint16_t bdf;
	uint8_t pipe;
	uint8_t region_cnt;
	uint32_t _rsvd0;
	struct display_region regions[];
};

struct telltale_disable_crc_data {
	uint16_t bdf;
	uint8_t pipe;
};

struct telltale_crc_regs {
	uint32_t ctrl;
	uint32_t pos;
	uint32_t size;
	uint32_t val;
};

struct telltale_get_crc_data {
	uint16_t bdf;
	uint8_t pipe;
	uint8_t frames;
	uint32_t _rsvd0;
	struct telltale_crc_regs records[];
};

/* Pipe A CRC regs */

#define PIPE_CRC_BASE(x) 	(0x60000 + 0x1000 * x)

#define PIPE_CRC_REGIONAL_SIZE 	0x48
#define   PIPE_CRC_REGIONAL_SIZE_Y_SHIFT 		0
#define   PIPE_CRC_REGIONAL_SIZE_Y_MASK 		0x00003FFF
#define   PIPE_CRC_REGIONAL_SIZE_X_SHIFT    	16
#define   PIPE_CRC_REGIONAL_SIZE_X_MASK     	0x3FFF0000

#define PIPE_CRC_REGIONAL_POS 	0x4C
#define   PIPE_CRC_REGIONAL_POS_Y_SHIFT 		0
#define   PIPE_CRC_REGIONAL_POS_Y_MASK  		0x00003FFF
#define   PIPE_CRC_REGIONAL_POS_X_SHIFT 		16
#define   PIPE_CRC_REGIONAL_POS_X_MASK  		0x1FFF0000

#define PIPE_CRC_CTL 			0x50
#define   PIPE_CRC_ACCUM_START_FRAME_SHIFT 		0
#define   PIPE_CRC_ACCUM_START_FRAME_MASK 		0xF
#define   PIPE_CRC_ACCUM_END_FRAME_SHIFT 		4
#define   PIPE_CRC_ACCUM_END_FRAME_MASK 		0xF0
#define   PIPE_CRC_ACCUM_ENABLE 				BIT(8)
#define   PIPE_CRC_CHANNEL_MASK_SHIFT 			16
#define   PIPE_CRC_CHANNEL_MASK_MASK 			0x70000
#define   PIPE_CRC_FIELD_EYE 					BIT(23)
#define   PIPE_CRC_DONE 						BIT(24)
#define   PIPE_CRC_CHANGE 						BIT(25)
#define   PIPE_CRC_SOURCE_SHIFT  				28
#define   PIPE_CRC_SOURCE_MASK  				0x70000000
#define     PIPE_CRC_SOURCE_PLANE_1				0
#define     PIPE_CRC_SOURCE_PLANE_2				2
#define     PIPE_CRC_SOURCE_PLANE_3				6
#define     PIPE_CRC_SOURCE_PLANE_4				7
#define     PIPE_CRC_SOURCE_PLANE_5				5
#define     PIPE_CRC_SOURCE_PLANE_6				3
#define     PIPE_CRC_SOURCE_PLANE_7				1
#define     PIPE_CRC_SOURCE_DMUX				4
#define   PIPE_CRC_ENABLE       				BIT(31)

#define PIPE_CRC_EXPECT 		0x54

#define PIPE_CRC_ACCUM_CTL 		0x58
#define   PIPE_CRC_ACCUM_FRAME_COUNT_SHIFT 		0
#define   PIPE_CRC_ACCUM_FRAME_COUNT_MASK 		0xFFFF
#define   PIPE_CRC_ACCUM_DONE 					BIT(29)
#define   PIPE_CRC_AACUM_START_ON_MATCH 		BIT(30)
#define   PIPE_CRC_ACCUM_ENABLE_ENHANCED        BIT(31)

#define PIPE_CRC_ACCUM_MATCH 	0x5C

#define PIPE_CRC_RES 			0x64

#define PIPE_CRC_ACCUM_RES 		0x6C

extern int32_t telltale_enable_crc(void *data);
extern int32_t telltale_disable_crc(void *data);
extern int32_t telltale_get_crc(void *data);

#endif /* TELLTALE_H_ */
