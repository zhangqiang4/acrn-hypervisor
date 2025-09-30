/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2016 Alex Teaca <iateaca@FreeBSD.org>
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/param.h>
#include <time.h>
#include <sys/timerfd.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/soundcard.h>
#include <alsa/asoundlib.h>
#include <pthread.h>
#include <math.h>
#include "dm.h"
#include "pci_core.h"
#include "pci_hda.h"
#include "hdac_reg.h"
#include "timer.h"

#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif

#define MAX_PCM_DEV_NAME_LEN 64

static int hda_debug = 0;
#define DPRINTF(format, arg...)                                                                                        \
	do {                                                                                                           \
		if (hda_debug) {                                                                                       \
			printf("[%s](%d) ", __func__, __LINE__);                                                       \
			printf(format, ##arg);                                                                         \
		}                                                                                                      \
	} while (0)

#define T_DEFAULT_POLLING_NS 2000000
#define AUDIO_PLAY_BUFFED_PERIODS 2

/*
 * Audio Player module function definitions
 */
/* audio defines */

struct audio_params {
	int channels;
	snd_pcm_format_t format;
	int rate;
};

struct audio {
	snd_pcm_t *pcm;
	uint8_t dir;
	uint8_t inited;
	char dev_name[64];
	struct audio_params params;
	snd_pcm_uframes_t period_size;
	bool play_started;
	uint8_t *temp_buf;
	size_t temp_buf_size;
	size_t temp_buf_wpos;
};

/*
 * HDA Codec defines
 */
#define INTEL_VENDORID 0x8086

#define HDA_CODEC_SUBSYSTEM_ID ((INTEL_VENDORID << 16) | 0x01)
#define HDA_CODEC_ROOT_NID 0x00
#define HDA_CODEC_FG_NID 0x01
#define HDA_CODEC_AUDIO_OUTPUT_NID 0x02
#define HDA_CODEC_PIN_OUTPUT_NID 0x03
#define HDA_CODEC_AUDIO_INPUT_NID 0x04
#define HDA_CODEC_PIN_INPUT_NID 0x05

#define HDA_CODEC_STREAMS_COUNT 0x02
#define HDA_CODEC_STREAM_OUTPUT 0x00
#define HDA_CODEC_STREAM_INPUT 0x01

#define HDA_CODEC_PARAMS_COUNT 0x14
#define HDA_CODEC_CONN_LIST_COUNT 0x01
#define HDA_CODEC_RESPONSE_EX_UNSOL 0x10
#define HDA_CODEC_RESPONSE_EX_SOL 0x00
#define HDA_CODEC_AMP_NUMSTEPS 0x4a

#define HDA_CODEC_SUPP_STREAM_FORMATS_PCM (1 << HDA_PARAM_SUPP_STREAM_FORMATS_PCM_SHIFT)

#define HDA_CODEC_FMT_BASE_MASK (0x01 << 14)

#define HDA_CODEC_FMT_MULT_MASK (0x07 << 11)
#define HDA_CODEC_FMT_MULT_2 (0x01 << 11)
#define HDA_CODEC_FMT_MULT_3 (0x02 << 11)
#define HDA_CODEC_FMT_MULT_4 (0x03 << 11)

#define HDA_CODEC_FMT_DIV_MASK 0x07
#define HDA_CODEC_FMT_DIV_SHIFT 8

#define HDA_CODEC_FMT_BITS_MASK (0x07 << 4)
#define HDA_CODEC_FMT_BITS_8 (0x00 << 4)
#define HDA_CODEC_FMT_BITS_16 (0x01 << 4)
#define HDA_CODEC_FMT_BITS_24 (0x03 << 4)
#define HDA_CODEC_FMT_BITS_32 (0x04 << 4)

#define HDA_CODEC_FMT_CHAN_MASK (0x0f << 0)

#define HDA_CODEC_AUDIO_WCAP_OUTPUT (0x00 << HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_SHIFT)
#define HDA_CODEC_AUDIO_WCAP_INPUT (0x01 << HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_SHIFT)
#define HDA_CODEC_AUDIO_WCAP_PIN (0x04 << HDA_PARAM_AUDIO_WIDGET_CAP_TYPE_SHIFT)
#define HDA_CODEC_AUDIO_WCAP_CONN_LIST (1 << HDA_PARAM_AUDIO_WIDGET_CAP_CONN_LIST_SHIFT)
#define HDA_CODEC_AUDIO_WCAP_FORMAT_OVR (1 << HDA_PARAM_AUDIO_WIDGET_CAP_FORMAT_OVR_SHIFT)
#define HDA_CODEC_AUDIO_WCAP_AMP_OVR (1 << HDA_PARAM_AUDIO_WIDGET_CAP_AMP_OVR_SHIFT)
#define HDA_CODEC_AUDIO_WCAP_OUT_AMP (1 << HDA_PARAM_AUDIO_WIDGET_CAP_OUT_AMP_SHIFT)
#define HDA_CODEC_AUDIO_WCAP_IN_AMP (1 << HDA_PARAM_AUDIO_WIDGET_CAP_IN_AMP_SHIFT)
#define HDA_CODEC_AUDIO_WCAP_STEREO (1 << HDA_PARAM_AUDIO_WIDGET_CAP_STEREO_SHIFT)

#define HDA_CODEC_PIN_CAP_OUTPUT (1 << HDA_PARAM_PIN_CAP_OUTPUT_CAP_SHIFT)
#define HDA_CODEC_PIN_CAP_INPUT (1 << HDA_PARAM_PIN_CAP_INPUT_CAP_SHIFT)
#define HDA_CODEC_PIN_CAP_PRESENCE_DETECT (1 << HDA_PARAM_PIN_CAP_PRESENCE_DETECT_CAP_SHIFT)

#define HDA_CODEC_OUTPUT_AMP_CAP_MUTE_CAP (1 << HDA_PARAM_OUTPUT_AMP_CAP_MUTE_CAP_SHIFT)
#define HDA_CODEC_OUTPUT_AMP_CAP_STEPSIZE (0x03 << HDA_PARAM_OUTPUT_AMP_CAP_STEPSIZE_SHIFT)
#define HDA_CODEC_OUTPUT_AMP_CAP_NUMSTEPS (HDA_CODEC_AMP_NUMSTEPS << HDA_PARAM_OUTPUT_AMP_CAP_NUMSTEPS_SHIFT)
#define HDA_CODEC_OUTPUT_AMP_CAP_OFFSET (HDA_CODEC_AMP_NUMSTEPS << HDA_PARAM_OUTPUT_AMP_CAP_OFFSET_SHIFT)

#define HDA_CODEC_SET_AMP_GAIN_MUTE_MUTE 0x80
#define HDA_CODEC_SET_AMP_GAIN_MUTE_GAIN_MASK 0x7f

#define HDA_CODEC_PIN_SENSE_PRESENCE_PLUGGED (1 << 31)
#define HDA_CODEC_PIN_WIDGET_CTRL_OUT_ENABLE (1 << HDA_CMD_GET_PIN_WIDGET_CTRL_OUT_ENABLE_SHIFT)
#define HDA_CODEC_PIN_WIDGET_CTRL_IN_ENABLE (1 << HDA_CMD_GET_PIN_WIDGET_CTRL_IN_ENABLE_SHIFT)

#define HDA_CONFIG_DEFAULTCONF_COLOR_BLACK (0x01 << HDA_CONFIG_DEFAULTCONF_COLOR_SHIFT)
#define HDA_CONFIG_DEFAULTCONF_COLOR_RED (0x05 << HDA_CONFIG_DEFAULTCONF_COLOR_SHIFT)

#define HDA_CODEC_BUF_SIZE (4096)

#define HDA_DMA_ACCESS_LEN (sizeof(uint32_t))

#define MAX_GAIN_VALUE 0x4a

/*
 * HDA Audio Context data structures
 */

typedef void (*transfer_func_t)(void *arg);
typedef int (*setup_func_t)(void *arg);

struct hda_audio_ctxt {
	char name[64];
	uint8_t run;
	uint8_t started;
	void *priv;
	struct acrn_timer timer;
	uint64_t sample_interval_ns; /* Timer interval in nanoseconds */
	setup_func_t do_setup;
	transfer_func_t do_transfer;
};

/*
 * HDA Audio Context module function declarations
 */

static void hda_audio_ctxt_timer_callback(void *arg, uint64_t nexp);
static int hda_audio_ctxt_init(
	struct hda_audio_ctxt *actx, const char *tname, transfer_func_t do_transfer, setup_func_t do_setup, void *priv);
static int hda_audio_ctxt_start(struct hda_audio_ctxt *actx);
static int hda_audio_ctxt_stop(struct hda_audio_ctxt *actx);

/*
 * HDA Codec data structures
 */

struct hda_codec_vdev;

typedef uint32_t (*verb_func_t)(struct hda_codec_vdev *sc, uint16_t verb, uint16_t payload);

struct hda_codec_stream {
	uint8_t buf[HDA_CODEC_BUF_SIZE];
	uint8_t channel;
	uint16_t fmt;
	uint8_t stream;

	uint8_t left_gain;
	uint8_t right_gain;
	uint8_t left_mute;
	uint8_t right_mute;

	struct audio *aud;
	struct hda_audio_ctxt actx;
	uint64_t tlast;
	snd_pcm_sframes_t adjustment;
};

struct hda_codec_vdev {
	uint32_t no_nodes;
	uint32_t subsystem_id;
	const uint32_t (*get_parameters)[HDA_CODEC_PARAMS_COUNT];
	const uint8_t (*conn_list)[HDA_CODEC_CONN_LIST_COUNT];
	const uint32_t *conf_default;
	const uint8_t *pin_ctrl_default;
	const verb_func_t *verb_handlers;

	struct hda_codec_inst *hci;
	struct hda_codec_stream streams[HDA_CODEC_STREAMS_COUNT];
	char *ctl;
};

/*
 * HDA Codec module function declarations
 */
static int hda_codec_init(struct hda_codec_inst *hci, const char *play, const char *rec, const char *ctl);
static int hda_codec_reset(struct hda_codec_inst *hci);
static int hda_codec_command(struct hda_codec_inst *hci, uint32_t cmd_data);
static int hda_codec_notify(struct hda_codec_inst *hci, uint8_t run, uint8_t stream, uint8_t dir);

static int hda_codec_parse_format(uint16_t fmt, struct audio_params *params);

static uint32_t hda_codec_audio_output_nid(struct hda_codec_vdev *sc, uint16_t verb, uint16_t payload);
static void hda_codec_audio_output_do_transfer(void *arg);
static int hda_codec_audio_output_do_setup(void *arg);
static uint32_t hda_codec_audio_input_nid(struct hda_codec_vdev *sc, uint16_t verb, uint16_t payload);
static void hda_codec_audio_input_do_transfer(void *arg);
static int hda_codec_audio_input_do_setup(void *arg);

static uint32_t hda_codec_audio_inout_nid(struct hda_codec_stream *st, uint16_t verb, uint16_t payload);

/*
 * HDA Codec global data
 */

#define HDA_CODEC_ROOT_DESC                                                                                            \
	[HDA_CODEC_ROOT_NID] = {                                                                                       \
		[HDA_PARAM_VENDOR_ID] = INTEL_VENDORID,                                                                \
		[HDA_PARAM_REVISION_ID] = 0xffff, /* 1 Subnode, StartNid = 1 */                                        \
		[HDA_PARAM_SUB_NODE_COUNT] = 0x00010001,                                                               \
	},

#define HDA_CODEC_FG_COMMON_DESC                                                                                       \
	[HDA_PARAM_FCT_GRP_TYPE] = HDA_PARAM_FCT_GRP_TYPE_NODE_TYPE_AUDIO, /* B8 - B32, 8.0 - 192.0kHz */              \
		[HDA_PARAM_SUPP_PCM_SIZE_RATE] = (0x1f << 16) | 0x7ff,                                                 \
	[HDA_PARAM_SUPP_STREAM_FORMATS] = HDA_CODEC_SUPP_STREAM_FORMATS_PCM, [HDA_PARAM_INPUT_AMP_CAP] = 0x00, /* None \
														*/     \
		[HDA_PARAM_OUTPUT_AMP_CAP] = 0x00, /* None */                                                          \
		[HDA_PARAM_GPIO_COUNT] = 0x00,

#define HDA_CODEC_FG_OUTPUT_DESC                                                                                       \
	[HDA_CODEC_FG_NID] = {/* 2 Subnodes, StartNid = 2 */                                                           \
		[HDA_PARAM_SUB_NODE_COUNT] = 0x00020002,                                                               \
		HDA_CODEC_FG_COMMON_DESC},

#define HDA_CODEC_FG_INPUT_DESC                                                                                        \
	[HDA_CODEC_FG_NID] = {/* 2 Subnodes, StartNid = 4 */                                                           \
		[HDA_PARAM_SUB_NODE_COUNT] = 0x00040002,                                                               \
		HDA_CODEC_FG_COMMON_DESC},

#define HDA_CODEC_FG_DUPLEX_DESC                                                                                       \
	[HDA_CODEC_FG_NID] = {/* 4 Subnodes, StartNid = 2 */                                                           \
		[HDA_PARAM_SUB_NODE_COUNT] = 0x00020004,                                                               \
		HDA_CODEC_FG_COMMON_DESC},

#define HDA_CODEC_OUTPUT_DESC                                                                                          \
	[HDA_CODEC_AUDIO_OUTPUT_NID] =                                                                                 \
		{                                                                                                      \
			[HDA_PARAM_AUDIO_WIDGET_CAP] = HDA_CODEC_AUDIO_WCAP_OUTPUT | HDA_CODEC_AUDIO_WCAP_FORMAT_OVR | \
				HDA_CODEC_AUDIO_WCAP_AMP_OVR | HDA_CODEC_AUDIO_WCAP_OUT_AMP |                          \
				HDA_CODEC_AUDIO_WCAP_STEREO, /* B16, 16.0 - 192.0kHz */                                \
			[HDA_PARAM_SUPP_PCM_SIZE_RATE] = (0x02 << 16) | 0x7fc,                                         \
			[HDA_PARAM_SUPP_STREAM_FORMATS] = HDA_CODEC_SUPP_STREAM_FORMATS_PCM,                           \
			[HDA_PARAM_INPUT_AMP_CAP] = 0x00, /* None */                                                   \
			[HDA_PARAM_CONN_LIST_LENGTH] = 0x00,                                                           \
			[HDA_PARAM_OUTPUT_AMP_CAP] = HDA_CODEC_OUTPUT_AMP_CAP_MUTE_CAP |                               \
				HDA_CODEC_OUTPUT_AMP_CAP_STEPSIZE | HDA_CODEC_OUTPUT_AMP_CAP_NUMSTEPS |                \
				HDA_CODEC_OUTPUT_AMP_CAP_OFFSET,                                                       \
	},                                                                                                             \
	[HDA_CODEC_PIN_OUTPUT_NID] = {                                                                                 \
		[HDA_PARAM_AUDIO_WIDGET_CAP] =                                                                         \
			HDA_CODEC_AUDIO_WCAP_PIN | HDA_CODEC_AUDIO_WCAP_CONN_LIST | HDA_CODEC_AUDIO_WCAP_STEREO,       \
		[HDA_PARAM_PIN_CAP] = HDA_CODEC_PIN_CAP_OUTPUT | HDA_CODEC_PIN_CAP_PRESENCE_DETECT,                    \
		[HDA_PARAM_INPUT_AMP_CAP] = 0x00, /* None */                                                           \
		[HDA_PARAM_CONN_LIST_LENGTH] = 0x01,                                                                   \
		[HDA_PARAM_OUTPUT_AMP_CAP] = 0x00, /* None */                                                          \
	},

#define HDA_CODEC_INPUT_DESC                                                                                           \
	[HDA_CODEC_AUDIO_INPUT_NID] =                                                                                  \
		{                                                                                                      \
			[HDA_PARAM_AUDIO_WIDGET_CAP] = HDA_CODEC_AUDIO_WCAP_INPUT | HDA_CODEC_AUDIO_WCAP_CONN_LIST |   \
				HDA_CODEC_AUDIO_WCAP_FORMAT_OVR | HDA_CODEC_AUDIO_WCAP_AMP_OVR |                       \
				HDA_CODEC_AUDIO_WCAP_IN_AMP | HDA_CODEC_AUDIO_WCAP_STEREO, /* B16, 16.0 - 192.0kHz */  \
			[HDA_PARAM_SUPP_PCM_SIZE_RATE] = (0x02 << 16) | 0x7fc,                                         \
			[HDA_PARAM_SUPP_STREAM_FORMATS] = HDA_CODEC_SUPP_STREAM_FORMATS_PCM,                           \
			[HDA_PARAM_OUTPUT_AMP_CAP] = 0x00, /* None */                                                  \
			[HDA_PARAM_CONN_LIST_LENGTH] = 0x01,                                                           \
			[HDA_PARAM_INPUT_AMP_CAP] = HDA_CODEC_OUTPUT_AMP_CAP_MUTE_CAP |                                \
				HDA_CODEC_OUTPUT_AMP_CAP_STEPSIZE | HDA_CODEC_OUTPUT_AMP_CAP_NUMSTEPS |                \
				HDA_CODEC_OUTPUT_AMP_CAP_OFFSET,                                                       \
	},                                                                                                             \
	[HDA_CODEC_PIN_INPUT_NID] = {                                                                                  \
		[HDA_PARAM_AUDIO_WIDGET_CAP] = HDA_CODEC_AUDIO_WCAP_PIN | HDA_CODEC_AUDIO_WCAP_STEREO,                 \
		[HDA_PARAM_PIN_CAP] = HDA_CODEC_PIN_CAP_INPUT | HDA_CODEC_PIN_CAP_PRESENCE_DETECT,                     \
		[HDA_PARAM_INPUT_AMP_CAP] = 0x00, /* None */                                                           \
		[HDA_PARAM_OUTPUT_AMP_CAP] = 0x00, /* None */                                                          \
	},

static const uint32_t hda_codec_output_parameters[][HDA_CODEC_PARAMS_COUNT] = {
	HDA_CODEC_ROOT_DESC HDA_CODEC_FG_OUTPUT_DESC HDA_CODEC_OUTPUT_DESC};

static const uint32_t hda_codec_input_parameters[][HDA_CODEC_PARAMS_COUNT] = {
	HDA_CODEC_ROOT_DESC HDA_CODEC_FG_INPUT_DESC HDA_CODEC_INPUT_DESC};

static const uint32_t hda_codec_duplex_parameters[][HDA_CODEC_PARAMS_COUNT] = {
	HDA_CODEC_ROOT_DESC HDA_CODEC_FG_DUPLEX_DESC HDA_CODEC_OUTPUT_DESC HDA_CODEC_INPUT_DESC};

#define HDA_CODEC_NODES_COUNT (ARRAY_SIZE(hda_codec_duplex_parameters))

static const uint8_t hda_codec_conn_list[HDA_CODEC_NODES_COUNT][HDA_CODEC_CONN_LIST_COUNT] = {
	[HDA_CODEC_PIN_OUTPUT_NID] = {HDA_CODEC_AUDIO_OUTPUT_NID},
	[HDA_CODEC_AUDIO_INPUT_NID] = {HDA_CODEC_PIN_INPUT_NID},
};

static const uint32_t hda_codec_conf_default[HDA_CODEC_NODES_COUNT] = {
	[HDA_CODEC_PIN_OUTPUT_NID] = HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_JACK | HDA_CONFIG_DEFAULTCONF_DEVICE_LINE_OUT |
		HDA_CONFIG_DEFAULTCONF_COLOR_BLACK | (0x01 << HDA_CONFIG_DEFAULTCONF_ASSOCIATION_SHIFT),
	[HDA_CODEC_PIN_INPUT_NID] = HDA_CONFIG_DEFAULTCONF_CONNECTIVITY_JACK | HDA_CONFIG_DEFAULTCONF_DEVICE_LINE_IN |
		HDA_CONFIG_DEFAULTCONF_COLOR_RED | (0x02 << HDA_CONFIG_DEFAULTCONF_ASSOCIATION_SHIFT),
};

static const uint8_t hda_codec_pin_ctrl_default[HDA_CODEC_NODES_COUNT] = {
	[HDA_CODEC_PIN_OUTPUT_NID] = HDA_CODEC_PIN_WIDGET_CTRL_OUT_ENABLE,
	[HDA_CODEC_PIN_INPUT_NID] = HDA_CODEC_PIN_WIDGET_CTRL_IN_ENABLE,
};

static const verb_func_t hda_codec_verb_handlers[HDA_CODEC_NODES_COUNT] = {
	[HDA_CODEC_AUDIO_OUTPUT_NID] = hda_codec_audio_output_nid,
	[HDA_CODEC_AUDIO_INPUT_NID] = hda_codec_audio_input_nid,
};

/*
 * HDA defines
 */
#define PCIR_HDCTL 0x40
#define INTEL_VENDORID 0x8086
#define HDA_INTEL_82801G 0x27d8

#define HDA_IOSS_NO 0x08
#define HDA_OSS_NO 0x04
#define HDA_ISS_NO 0x04
#define HDA_CODEC_MAX 0x0f
#define HDA_LAST_OFFSET (0x2084 + ((HDA_ISS_NO) * 0x20) + ((HDA_OSS_NO) * 0x20))
#define HDA_CORB_ENTRY_LEN 0x04
#define HDA_RIRB_ENTRY_LEN 0x08
#define HDA_BDL_ENTRY_LEN 0x10
#define HDA_DMA_PIB_ENTRY_LEN 0x08
#define HDA_STREAM_TAGS_CNT 0x10
#define HDA_STREAM_REGS_BASE 0x80
#define HDA_STREAM_REGS_LEN 0x20

#define HDA_BDL_MAX_LEN 0x0100

#define HDAC_SDSTS_FIFORDY (1 << 5)

#define HDA_RIRBSTS_IRQ_MASK (HDAC_RIRBSTS_RINTFL | HDAC_RIRBSTS_RIRBOIS)
#define HDA_STATESTS_IRQ_MASK ((1 << HDA_CODEC_MAX) - 1)
#define HDA_SDSTS_IRQ_MASK (HDAC_SDSTS_DESE | HDAC_SDSTS_FIFOE | HDAC_SDSTS_BCIS)

/*
 * HDA data structures
 */

struct pci_hda_vdev;

typedef void (*hda_set_reg_handler)(struct pci_hda_vdev *sc, uint32_t offset, uint32_t old);

struct hda_bdle {
	uint32_t addrl;
	uint32_t addrh;
	uint32_t len;
	uint32_t ioc;
} __packed;

struct hda_bdle_desc {
	void *addr;
	uint8_t ioc;
	uint32_t len;
};

struct hda_codec_cmd_ctl {
	const char *name;
	void *dma_vaddr;
	uint8_t run;
	uint16_t rp;
	uint16_t size;
	uint16_t wp;
};

struct hda_stream_desc {
	uint8_t dir;
	uint8_t run;
	uint8_t stream;

	/* bp is the no. of bytes transferred in the current bdle */
	uint32_t bp;
	/* be is the no. of bdles transferred in the bdl */
	uint32_t be;

	uint32_t bdl_cnt;
	struct hda_bdle_desc bdl[HDA_BDL_MAX_LEN];
};

struct pci_hda_vdev {
	struct pci_vdev *pci_dev;
	uint32_t regs[HDA_LAST_OFFSET];

	uint8_t lintr;
	uint8_t rirb_cnt;
	uint64_t wall_clock_start;

	struct hda_codec_cmd_ctl corb;
	struct hda_codec_cmd_ctl rirb;

	uint8_t codecs_no;
	struct hda_codec_inst *codecs[HDA_CODEC_MAX];

	/* Base Address of the DMA Position Buffer */
	void *dma_pib_vaddr;

	struct hda_stream_desc streams[HDA_IOSS_NO];
	/* 2 tables for output and input */
	uint8_t stream_map[2][HDA_STREAM_TAGS_CNT];
};

/*
 * HDA module function declarations
 */
static inline void hda_set_reg_by_offset(struct pci_hda_vdev *sc, uint32_t offset, uint32_t value);
static inline uint32_t hda_get_reg_by_offset(struct pci_hda_vdev *sc, uint32_t offset);
static inline void hda_set_field_by_offset(struct pci_hda_vdev *sc, uint32_t offset, uint32_t mask, uint32_t value);

static struct pci_hda_vdev *hda_init(char *play, char *rec, char *ctl);
static void hda_update_intr(struct pci_hda_vdev *sc);
static void hda_response_interrupt(struct pci_hda_vdev *sc);
static int hda_codec_constructor(
	struct pci_hda_vdev *sc, struct hda_codec_class *codec, const char *play, const char *rec, const char *ctl);
// static struct hda_codec_class *hda_find_codec_class(const char *name);

static int hda_send_command(struct pci_hda_vdev *sc, uint32_t verb);
static int hda_notify_codecs(struct pci_hda_vdev *sc, uint8_t run, uint8_t stream, uint8_t dir);
static void hda_reset(struct pci_hda_vdev *sc);
static void hda_reset_regs(struct pci_hda_vdev *sc);
static void hda_stream_reset(struct pci_hda_vdev *sc, uint8_t stream_ind);
static int hda_stream_start(struct pci_hda_vdev *sc, uint8_t stream_ind);
static int hda_stream_stop(struct pci_hda_vdev *sc, uint8_t stream_ind);
static uint32_t hda_read(struct pci_hda_vdev *sc, uint32_t offset);
static int hda_write(struct pci_hda_vdev *sc, uint32_t offset, uint8_t size, uint32_t value);

static inline void hda_print_cmd_ctl_data(struct hda_codec_cmd_ctl *p);
static int hda_corb_start(struct pci_hda_vdev *sc);
static int hda_corb_run(struct pci_hda_vdev *sc);
static int hda_rirb_start(struct pci_hda_vdev *sc);

static void *hda_dma_get_vaddr(struct pci_hda_vdev *sc, uint64_t dma_paddr, size_t len);
static void hda_dma_st_dword(void *dma_vaddr, uint32_t data);
static uint32_t hda_dma_ld_dword(void *dma_vaddr);

static inline uint8_t hda_get_stream_by_offsets(uint32_t offset, uint8_t reg_offset);
static inline uint32_t hda_get_offset_stream(uint8_t stream_ind);

static void hda_set_gctl(struct pci_hda_vdev *sc, uint32_t offset, uint32_t old);
static void hda_set_statests(struct pci_hda_vdev *sc, uint32_t offset, uint32_t old);
static void hda_set_corbwp(struct pci_hda_vdev *sc, uint32_t offset, uint32_t old);
static void hda_set_corbctl(struct pci_hda_vdev *sc, uint32_t offset, uint32_t old);
static void hda_set_rirbctl(struct pci_hda_vdev *sc, uint32_t offset, uint32_t old);
static void hda_set_rirbsts(struct pci_hda_vdev *sc, uint32_t offset, uint32_t old);
static void hda_set_dpiblbase(struct pci_hda_vdev *sc, uint32_t offset, uint32_t old);
static void hda_set_sdctl(struct pci_hda_vdev *sc, uint32_t offset, uint32_t old);
static void hda_set_sdctl2(struct pci_hda_vdev *sc, uint32_t offset, uint32_t old);
static void hda_set_sdsts(struct pci_hda_vdev *sc, uint32_t offset, uint32_t old);

static int hda_signal_state_change(struct hda_codec_inst *hci);
static int hda_response(struct hda_codec_inst *hci, uint32_t response, uint8_t unsol);
static int hda_transfer(struct hda_codec_inst *hci, uint8_t stream, uint8_t dir, uint8_t *buf, size_t count);

static void hda_set_pib(struct pci_hda_vdev *sc, uint8_t stream_ind, uint32_t pib);
static uint64_t hda_get_clock_ns(void);

/*
 * PCI HDA function declarations
 */
static int pci_hda_init(struct vmctx *ctx, struct pci_vdev *dev, char *opts);
static void pci_hda_write(
	struct vmctx *ctx, int vcpu, struct pci_vdev *dev, int baridx, uint64_t offset, int size, uint64_t value);
static uint64_t pci_hda_read(struct vmctx *ctx, int vcpu, struct pci_vdev *dev, int baridx, uint64_t offset, int size);
/*
 * HDA global data
 */

static const hda_set_reg_handler hda_set_reg_table[] = {[HDAC_GCTL] = hda_set_gctl,
	[HDAC_STATESTS] = hda_set_statests,
	[HDAC_CORBWP] = hda_set_corbwp,
	[HDAC_CORBCTL] = hda_set_corbctl,
	[HDAC_RIRBCTL] = hda_set_rirbctl,
	[HDAC_RIRBSTS] = hda_set_rirbsts,
	[HDAC_DPIBLBASE] = hda_set_dpiblbase,

#define HDAC_ISTREAM(n, iss, oss)                                                                                      \
	[_HDAC_ISDCTL(n, iss, oss)] = hda_set_sdctl, [_HDAC_ISDCTL(n, iss, oss) + 2] = hda_set_sdctl2,                 \
			      [_HDAC_ISDSTS(n, iss, oss)] = hda_set_sdsts,

#define HDAC_OSTREAM(n, iss, oss)                                                                                      \
	[_HDAC_OSDCTL(n, iss, oss)] = hda_set_sdctl, [_HDAC_OSDCTL(n, iss, oss) + 2] = hda_set_sdctl2,                 \
			      [_HDAC_OSDSTS(n, iss, oss)] = hda_set_sdsts,

	HDAC_ISTREAM(0, HDA_ISS_NO, HDA_OSS_NO) HDAC_ISTREAM(1, HDA_ISS_NO, HDA_OSS_NO)
		HDAC_ISTREAM(2, HDA_ISS_NO, HDA_OSS_NO) HDAC_ISTREAM(3, HDA_ISS_NO, HDA_OSS_NO)

			HDAC_OSTREAM(0, HDA_ISS_NO, HDA_OSS_NO) HDAC_OSTREAM(1, HDA_ISS_NO, HDA_OSS_NO)
				HDAC_OSTREAM(2, HDA_ISS_NO, HDA_OSS_NO) HDAC_OSTREAM(3, HDA_ISS_NO, HDA_OSS_NO)};

static const uint16_t hda_corb_sizes[] = {
	[HDAC_CORBSIZE_CORBSIZE_2] = 2,
	[HDAC_CORBSIZE_CORBSIZE_16] = 16,
	[HDAC_CORBSIZE_CORBSIZE_256] = 256,
	[HDAC_CORBSIZE_CORBSIZE_MASK] = 0,
};

static const uint16_t hda_rirb_sizes[] = {
	[HDAC_RIRBSIZE_RIRBSIZE_2] = 2,
	[HDAC_RIRBSIZE_RIRBSIZE_16] = 16,
	[HDAC_RIRBSIZE_RIRBSIZE_256] = 256,
	[HDAC_RIRBSIZE_RIRBSIZE_MASK] = 0,
};

static const struct hda_ops hops = {
	.signal = hda_signal_state_change,
	.response = hda_response,
	.transfer = hda_transfer,
};

/*
 * audio_init - initialize an instance of audio player
 * @dev_name - the backend sound device used to play / capture
 * @dir - dir = 1 for write mode, dir = 0 for read mode
 */
struct audio *audio_init(const char *dev_name, uint8_t dir) {
	struct audio *aud = NULL;
	int err;
	size_t nlen;

	aud = calloc(1, sizeof(*aud));
	if (!aud)
		return NULL;

	nlen = strlen(dev_name);
	if (nlen < sizeof(aud->dev_name))
		memcpy(aud->dev_name, dev_name, nlen + 1);
	else {
		DPRINTF("dev_name too big");
		free(aud);
		return NULL;
	}

	aud->dir = dir;

	/* Initialize ALSA PCM device */
	err = snd_pcm_open(
		&aud->pcm, aud->dev_name, dir ? SND_PCM_STREAM_PLAYBACK : SND_PCM_STREAM_CAPTURE, SND_PCM_NONBLOCK);
	if (err < 0) {
		pr_err("Failed to open ALSA device: %s, error: %s\n", aud->dev_name, snd_strerror(err));
		free(aud);
		return NULL;
	}

	return aud;
}

/*
 * audio_set_params - reset the sound device and set the audio params
 * @aud - the audio player to be configured
 * @params - the audio parameters to be set
 */
int audio_set_params(struct audio *aud, struct audio_params *params) {
	snd_pcm_hw_params_t *hw_params = NULL;
	int err;

	/* Stop any running operations */
	snd_pcm_drop(aud->pcm);

	/* Allocate hardware parameters object */
	err = snd_pcm_hw_params_malloc(&hw_params);
	if (err < 0) {
		DPRINTF("Failed to allocate hw_params: %s\n", snd_strerror(err));
		return -1;
	}

	/* Initialize hardware parameters */
	err = snd_pcm_hw_params_any(aud->pcm, hw_params);
	if (err < 0) {
		DPRINTF("Failed to initialize hw_params: %s\n", snd_strerror(err));
		snd_pcm_hw_params_free(hw_params);
		return -1;
	}

	/* Set access type */
	err = snd_pcm_hw_params_set_access(aud->pcm, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
	if (err < 0) {
		DPRINTF("Failed to set access type: %s\n", snd_strerror(err));
		snd_pcm_hw_params_free(hw_params);
		return -1;
	}

	/* Set format - params->format is already snd_pcm_format_t */
	err = snd_pcm_hw_params_set_format(aud->pcm, hw_params, params->format);
	if (err < 0) {
		DPRINTF("Failed to set format: %s\n", snd_strerror(err));
		snd_pcm_hw_params_free(hw_params);
		return -1;
	}

	/* Set channels */
	err = snd_pcm_hw_params_set_channels(aud->pcm, hw_params, params->channels);
	if (err < 0) {
		DPRINTF("Failed to set channels: %s\n", snd_strerror(err));
		snd_pcm_hw_params_free(hw_params);
		return -1;
	}

	/* Set sample rate */
	unsigned int rate = params->rate;
	err = snd_pcm_hw_params_set_rate_near(aud->pcm, hw_params, &rate, 0);
	if (err < 0) {
		DPRINTF("Failed to set rate: %s\n", snd_strerror(err));
		snd_pcm_hw_params_free(hw_params);
		return -1;
	}

	if (rate != (unsigned int)params->rate) {
		DPRINTF("Rate adjusted: requested %d, got %u\n", params->rate, rate);
	}

	snd_pcm_uframes_t period_size = 512;
	int dir = 0;
	err = snd_pcm_hw_params_set_period_size_near(aud->pcm, hw_params, &period_size, &dir);
	if (err < 0) {
		DPRINTF("Failed to set period size: %s\n", snd_strerror(err));
		snd_pcm_hw_params_free(hw_params);
		return -1;
	}

	/* Apply hardware parameters */
	err = snd_pcm_hw_params(aud->pcm, hw_params);
	if (err < 0) {
		DPRINTF("Failed to apply hw_params: %s\n", snd_strerror(err));
		snd_pcm_hw_params_free(hw_params);
		return -1;
	}

	err = snd_pcm_hw_params_get_period_size(hw_params, &period_size, NULL);
	if (err < 0) {
		DPRINTF("Failed to get period size: %s\n", snd_strerror(err));
		period_size = 0;
	}
	aud->period_size = period_size;
	DPRINTF("Actual period size: %ld\n", aud->period_size);

	/* Prepare PCM for use */
	err = snd_pcm_prepare(aud->pcm);
	if (err < 0) {
		DPRINTF("Failed to prepare PCM: %s\n", snd_strerror(err));
		snd_pcm_hw_params_free(hw_params);
		return -1;
	}

	/* Store parameters */
	aud->params = *params;
	aud->inited = 1;

	/* Clean up */
	snd_pcm_hw_params_free(hw_params);

	DPRINTF("ALSA audio configured: rate=%d, channels=%d, format=%d\n", params->rate, params->channels,
		params->format);

	return 0;
}
static uint64_t hda_get_clock_ns(void);

/*
 * audio_playback - plays samples to the sound device using non-blocking operations
 * @aud - the audio player used to play the samples
 * @buf - the buffer containing the samples
 * @count - the number of bytes in buffer
 * @adjustment - adjustment indicator for the caller to adjust the data rate
 *
 */
int audio_playback(struct audio *aud, const uint8_t *buf, size_t count, snd_pcm_sframes_t *adjustment,
	uint32_t left_gain, uint32_t right_gain) {
	size_t frame_size;
	const void *current_buf = buf;
	void *target_buffer;
	size_t remaining = count, play_cnt = count;
	int i;
	uint32_t max_gain = MAX_GAIN_VALUE;
	float l_multifier, r_multifier;
	int err;

	if (!aud->inited) {
		DPRINTF("Audio device not initialized\n");
		return -1;
	}

	frame_size = aud->params.channels * snd_pcm_format_width(aud->params.format) / 8;
	if (frame_size == 0) {
		DPRINTF("Invalid frame size\n");
		return -1;
	}

	l_multifier = (powf(10.f, ((float)left_gain / max_gain)) - 1.f) / 9.f;
	r_multifier = (powf(10.f, ((float)right_gain / max_gain)) - 1.f) / 9.f;
	size_t threshold = aud->period_size * frame_size * AUDIO_PLAY_BUFFED_PERIODS;
	if (aud->play_started) {
		snd_pcm_sframes_t delayp = 0;
		if (snd_pcm_delay(aud->pcm, &delayp)) {
			DPRINTF("error getting delay\n");
		}

		snd_pcm_sframes_t target_delay = aud->period_size * AUDIO_PLAY_BUFFED_PERIODS;
		if (target_delay - delayp > (target_delay / 10)) {
			*adjustment = 1;
		} else if (delayp - target_delay > (target_delay / 10)) {
			*adjustment = -1;
		} else {
			*adjustment = 0;
		}
		DPRINTF("delay: %ld/%ld #%ld\n", delayp, target_delay, *adjustment);
		target_buffer = aud->temp_buf;
		/* copy to temp_buf and apply gain */
		while (remaining > 0) {
			for (i = 0; i < aud->params.channels; i++) {
				float multifier = (i % 2 == 0) ? l_multifier : r_multifier;
				int width = snd_pcm_format_width(aud->params.format);

				if (width == 8) {
					*(int8_t *)target_buffer = (int8_t)(*(int8_t *)current_buf * multifier);
				} else if (width == 16) {
					*(int16_t *)target_buffer = (int16_t)(*(int16_t *)current_buf * multifier);
				} else if (width == 32) {
					*(int32_t *)target_buffer = (int32_t)(*(int32_t *)current_buf * multifier);
				} else {
					pr_err("Unsupported sample width for gain adjustment %f\n", multifier);
					return -1;
				}
				current_buf += (width / 8);
				target_buffer += (width / 8);
				remaining -= (width / 8);
			}
		}
	} else {
		*adjustment = 0;
		if (aud->temp_buf_wpos + remaining > aud->temp_buf_size) {
			DPRINTF("Temporary buffer overflow\n");
			return -1;
		}

		target_buffer = aud->temp_buf + aud->temp_buf_wpos;
		aud->temp_buf_wpos += remaining;

		while (remaining > 0) {
			/* copy to temp_buf and apply gain */
			for (i = 0; i < aud->params.channels; i++) {
				float multifier = (i % 2 == 0) ? l_multifier : r_multifier;
				int width = snd_pcm_format_width(aud->params.format);

				if (width == 8) {
					*(int8_t *)target_buffer = (int8_t)(*(int8_t *)current_buf * multifier);
				} else if (width == 16) {
					*(int16_t *)target_buffer = (int16_t)(*(int16_t *)current_buf * multifier);
				} else if (width == 32) {
					*(int32_t *)target_buffer = (int32_t)(*(int32_t *)current_buf * multifier);
				} else {
					pr_err("Unsupported sample width for gain adjustment %f\n", multifier);
					return -1;
				}
				current_buf += (width / 8);
				target_buffer += (width / 8);
				remaining -= (width / 8);
			}
		}
		/* start playing after buffer reaches threshold. after that play incoming buf instantly*/
		if (aud->temp_buf_wpos < threshold)
			return 0;
		else {
			snd_pcm_start(aud->pcm);
			aud->play_started = true;
			play_cnt = aud->temp_buf_wpos;
			DPRINTF("Playback started\n");
		}
	}

	snd_pcm_sframes_t frames_to_write = play_cnt / frame_size;
	snd_pcm_uframes_t frames_written = snd_pcm_writei(aud->pcm, aud->temp_buf, frames_to_write);

	if (frames_written < frames_to_write)
		DPRINTF("error: short write %ld of %ld frames\n", frames_written, frames_to_write);

	if (frames_written == -EAGAIN) {
		DPRINTF("Write would block, try again later\n");
		/* Resource temporarily unavailable, try again */
	} else if (frames_written == -EPIPE) {
		/* Buffer underrun, recover */
		DPRINTF("Buffer underrun occurred, recovering\n");
		err = snd_pcm_recover(aud->pcm, frames_written, 0);
		if (err < 0) {
			DPRINTF("Failed to recover from underrun: %s\n", snd_strerror(err));
			return -1;
		}
	} else if (frames_written < 0) {
		DPRINTF("Failed to write audio data: %s\n", snd_strerror(frames_written));
		return -1;
	}

	return 0;
}

void audio_record_start(struct audio *aud) {
	if (!aud || aud->dir || !aud->inited || !aud->pcm)
		return;

	DPRINTF("Starting audio recording\n");
	snd_pcm_start(aud->pcm);
}

/*
 * audio_record - records samples from the sound device using
 * non-blocking operations.
 * @aud - the audio player used to capture the samples
 * @buf - the buffer to receive the samples
 * @count - the number of bytes to capture in buffer
 * Returns -1 on error and 0 on success
 */
int audio_record(struct audio *aud, uint8_t *buf, size_t count, size_t *act_read, snd_pcm_sframes_t *adjustment) {
	snd_pcm_sframes_t frames_read, frames_to_read, frames_available;
	size_t frame_size;
	int err;
	uint8_t *current_buf = buf;
	size_t remaining = count;

	if (!aud->inited) {
		DPRINTF("Audio device not initialized\n");
		return -1;
	}

	/* Calculate frame size using ALSA format */
	frame_size = aud->params.channels * snd_pcm_format_width(aud->params.format) / 8;
	if (frame_size == 0) {
		DPRINTF("Invalid frame size\n");
		return -1;
	}

	frames_to_read = remaining / frame_size;
	if (frames_to_read == 0) {
		DPRINTF("Invalid frame size\n");
		return -1;
	}

	frames_available = snd_pcm_avail(aud->pcm);
	if (frames_available < 0){
		DPRINTF("error getting delay\n");
	}
	DPRINTF("frames available %ld\n", frames_available);
	if (frames_available < aud->period_size) {
		*act_read = 0;
		*adjustment = 0;
		return 0;
	} else if (frames_available - frames_to_read > aud->period_size) {
		*adjustment = 1;
	} else {
		*adjustment = -1;
	}

	frames_read = snd_pcm_readi(aud->pcm, current_buf, frames_to_read);

	if (frames_read == -EAGAIN) {
		/* Resource temporarily unavailable, try again */
		DPRINTF("unavailable, try again later\n");
		*act_read = 0;
		*adjustment = 0;
		return 0;
	} else if (frames_read == -EPIPE) {
		/* Buffer overrun, recover */
		DPRINTF("Buffer overrun occurred, recovering\n");
		err = snd_pcm_recover(aud->pcm, frames_read, 0);
		if (err < 0) {
			DPRINTF("Failed to recover from overrun: %s\n", snd_strerror(err));
			return -1;
		}
	} else if (frames_read < 0) {
		DPRINTF("Failed to read audio data: %s\n", snd_strerror(frames_read));
		return -1;
	} else if (frames_read != frames_to_read) {
		DPRINTF("Short read: %ld of %ld frames\n", frames_read, frames_to_read);
	}

	*act_read = frames_read * frame_size;
	DPRINTF("audio_record: requested %ld avail %ld, got %ld\n", count, frames_available, *act_read);
	return 0;
}

/*
 * audio_cleanup - cleanup audio resources
 * @aud - the audio player to cleanup
 */
void audio_cleanup(struct audio *aud) {
	if (!aud)
		return;

	if (aud->pcm) {
		snd_pcm_drop(aud->pcm);
		snd_pcm_close(aud->pcm);
		aud->pcm = NULL;
	}

	free(aud);
}
/* end of audio player */

/*
 * HDA Codec module function definitions
 */

static int hda_codec_init(struct hda_codec_inst *hci, const char *play, const char *rec, const char *ctl) {
	struct hda_codec_vdev *sc = NULL;
	struct hda_codec_stream *st = NULL;
	int err;

	if (!(play || rec))
		return -1;

	sc = calloc(1, sizeof(*sc));
	if (!sc)
		return -1;

	if (play && rec)
		sc->get_parameters = hda_codec_duplex_parameters;
	else {
		if (play)
			sc->get_parameters = hda_codec_output_parameters;
		else
			sc->get_parameters = hda_codec_input_parameters;
	}
	sc->subsystem_id = HDA_CODEC_SUBSYSTEM_ID;
	sc->no_nodes = HDA_CODEC_NODES_COUNT;
	sc->conn_list = hda_codec_conn_list;
	sc->conf_default = hda_codec_conf_default;
	sc->pin_ctrl_default = hda_codec_pin_ctrl_default;
	sc->verb_handlers = hda_codec_verb_handlers;
	DPRINTF("HDA Codec nodes: %d\n", sc->no_nodes);

	/*
	 * Initialize the Audio Output stream
	 */
	if (play) {
		st = &sc->streams[HDA_CODEC_STREAM_OUTPUT];

		err = hda_audio_ctxt_init(&st->actx, "hda-audio-output", hda_codec_audio_output_do_transfer,
			hda_codec_audio_output_do_setup, sc);

		if (err) {
			pr_err("Failed to init audio output context\n");
			return -1;
		}

		DPRINTF("audio init play\n");
		st->aud = audio_init(play, 1);
		if (!st->aud) {
			pr_err("Fail to init the output audio player\n");
			return -1;
		}
	}

	/*
	 * Initialize the Audio Input stream
	 */
	if (rec) {
		st = &sc->streams[HDA_CODEC_STREAM_INPUT];

		err = hda_audio_ctxt_init(&st->actx, "hda-audio-input", hda_codec_audio_input_do_transfer,
			hda_codec_audio_input_do_setup, sc);

		if (err) {
			pr_err("Failed to init audio input context\n");
			return -1;
		}

		DPRINTF("audio init rec\n");
		st->aud = audio_init(rec, 0);
		if (!st->aud) {
			DPRINTF("Fail to init the input audio player\n");
			return -1;
		}
	}

	if (strlen(ctl) != 0) {
		/* TODO: add mixer volume ctl */
		sc->ctl = NULL;
	} else {
		sc->ctl = NULL;
	}

	sc->hci = hci;
	hci->priv = sc;

	return (0);
}

static int hda_codec_reset(struct hda_codec_inst *hci) {
	const struct hda_ops *hops = NULL;
	struct hda_codec_vdev *sc = NULL;
	struct hda_codec_stream *st = NULL;
	int i;

	hops = hci->hops;

	sc = (struct hda_codec_vdev *)hci->priv;

	for (i = 0; i < HDA_CODEC_STREAMS_COUNT; i++) {
		st = &sc->streams[i];
		st->left_gain = HDA_CODEC_AMP_NUMSTEPS;
		st->right_gain = HDA_CODEC_AMP_NUMSTEPS;
		st->left_mute = HDA_CODEC_SET_AMP_GAIN_MUTE_MUTE;
		st->right_mute = HDA_CODEC_SET_AMP_GAIN_MUTE_MUTE;
	}

	DPRINTF("cad: 0x%x\n", hci->cad);

	if (!hops->signal) {
		DPRINTF("The controller ops does not implement \
			 the signal function\n");
		return -1;
	}

	return (hops->signal(hci));
}

static int hda_codec_command(struct hda_codec_inst *hci, uint32_t cmd_data) {
	const struct hda_ops *hops = NULL;
	struct hda_codec_vdev *sc = NULL;
	uint8_t cad = 0, nid = 0;
	uint16_t verb = 0, payload = 0;
	uint32_t res = 0;

	/* 4 bits */
	cad = (cmd_data >> HDA_CMD_CAD_SHIFT) & 0x0f;
	/* 8 bits */
	nid = (cmd_data >> HDA_CMD_NID_SHIFT) & 0xff;

	if ((cmd_data & 0x70000) == 0x70000) {
		/* 12 bits */
		verb = (cmd_data >> HDA_CMD_VERB_12BIT_SHIFT) & 0x0fff;
		/* 8 bits */
		payload = cmd_data & 0xff;
	} else {
		/* 4 bits */
		verb = (cmd_data >> HDA_CMD_VERB_4BIT_SHIFT) & 0x0f;
		/* 16 bits */
		payload = cmd_data & 0xffff;
	}

	hops = hci->hops;

	sc = (struct hda_codec_vdev *)hci->priv;

	if (cad != hci->cad || nid >= sc->no_nodes) {
		DPRINTF("Invalid command data\n");
		return -1;
	}

	if (!hops->response) {
		DPRINTF("The controller ops does not implement \
			 the response function\n");
		return -1;
	}

	switch (verb) {
	case HDA_CMD_VERB_GET_PARAMETER:
		if (payload < HDA_CODEC_PARAMS_COUNT)
			res = sc->get_parameters[nid][payload];
		break;
	case HDA_CMD_VERB_GET_CONN_LIST_ENTRY:
		res = sc->conn_list[nid][0];
		break;
	case HDA_CMD_VERB_GET_PIN_WIDGET_CTRL:
		res = sc->pin_ctrl_default[nid];
		break;
	case HDA_CMD_VERB_GET_PIN_SENSE:
		res = HDA_CODEC_PIN_SENSE_PRESENCE_PLUGGED;
		break;
	case HDA_CMD_VERB_GET_CONFIGURATION_DEFAULT:
		res = sc->conf_default[nid];
		break;
	case HDA_CMD_VERB_GET_SUBSYSTEM_ID:
		res = sc->subsystem_id;
		break;
	default:
		assert(sc->verb_handlers);
		if (sc->verb_handlers[nid])
			res = sc->verb_handlers[nid](sc, verb, payload);
		else
			DPRINTF("Unknown VERB: 0x%x\n", verb);
		break;
	}

	DPRINTF("cad: 0x%x nid: 0x%x verb: 0x%x payload: 0x%x response: 0x%x\n", cad, nid, verb, payload, res);

	return (hops->response(hci, res, HDA_CODEC_RESPONSE_EX_SOL));
}

static int hda_codec_notify(struct hda_codec_inst *hci, uint8_t run, uint8_t stream, uint8_t dir) {
	struct hda_codec_vdev *sc = NULL;
	struct hda_codec_stream *st = NULL;
	struct hda_audio_ctxt *actx = NULL;
	int i;
	int err;

	sc = (struct hda_codec_vdev *)hci->priv;

	i = dir ? HDA_CODEC_STREAM_OUTPUT : HDA_CODEC_STREAM_INPUT;
	st = &sc->streams[i];

	DPRINTF("run: %d, stream: 0x%x, st->stream: 0x%x dir: %d\n", run, stream, st->stream, dir);

	if (stream != st->stream) {
		DPRINTF("Stream not found\n");
		return (0);
	}

	actx = &st->actx;

	if (run)
		err = hda_audio_ctxt_start(actx);
	else
		err = hda_audio_ctxt_stop(actx);

	return (err);
}

static int hda_codec_parse_format(uint16_t fmt, struct audio_params *params) {
	uint8_t div = 0;

	/* Compute the Sample Rate */
	params->rate = (fmt & HDA_CODEC_FMT_BASE_MASK) ? 44100 : 48000;

	switch (fmt & HDA_CODEC_FMT_MULT_MASK) {
	case HDA_CODEC_FMT_MULT_2:
		params->rate *= 2;
		break;
	case HDA_CODEC_FMT_MULT_3:
		params->rate *= 3;
		break;
	case HDA_CODEC_FMT_MULT_4:
		params->rate *= 4;
		break;
	}

	div = (fmt >> HDA_CODEC_FMT_DIV_SHIFT) & HDA_CODEC_FMT_DIV_MASK;
	params->rate /= (div + 1);

	/* Compute the Bits per Sample - convert to snd_pcm_format_t */
	switch (fmt & HDA_CODEC_FMT_BITS_MASK) {
	case HDA_CODEC_FMT_BITS_8:
		params->format = SND_PCM_FORMAT_U8;
		break;
	case HDA_CODEC_FMT_BITS_16:
		params->format = SND_PCM_FORMAT_S16_LE;
		break;
	case HDA_CODEC_FMT_BITS_24:
		params->format = SND_PCM_FORMAT_S24_LE;
		break;
	case HDA_CODEC_FMT_BITS_32:
		params->format = SND_PCM_FORMAT_S32_LE;
		break;
	default:
		DPRINTF("Unknown format bits: 0x%x\n", fmt & HDA_CODEC_FMT_BITS_MASK);
		return -1;
	}

	/* Compute the Number of Channels */
	params->channels = (fmt & HDA_CODEC_FMT_CHAN_MASK) + 1;

	return (0);
}

static uint32_t hda_codec_audio_output_nid(struct hda_codec_vdev *sc, uint16_t verb, uint16_t payload) {
	struct hda_codec_stream *st = &sc->streams[HDA_CODEC_STREAM_OUTPUT];
	int res;

	res = hda_codec_audio_inout_nid(st, verb, payload);

	return (res);
}

static void hda_codec_audio_output_do_transfer(void *arg) {
	const struct hda_ops *hops = NULL;
	struct hda_codec_vdev *sc = (struct hda_codec_vdev *)arg;
	struct hda_codec_inst *hci = NULL;
	struct hda_codec_stream *st = NULL;
	struct audio *aud = NULL;
	uint64_t tdelta = 0, tnow;
	uint64_t n_bytes_tx;
	snd_pcm_sframes_t n_frames;
	uint32_t left_gain, right_gain;
	int err;

	hci = sc->hci;

	hops = hci->hops;

	st = &sc->streams[HDA_CODEC_STREAM_OUTPUT];
	aud = st->aud;

	tnow = hda_get_clock_ns();
	if (st->tlast == 0) {
		tdelta = T_DEFAULT_POLLING_NS;
	} else {
		tdelta = tnow - st->tlast;
	}

	st->tlast = tnow;

	n_frames = (aud->params.rate * tdelta / NS_PER_SEC) + st->adjustment;
	if (n_frames < 0)
		n_frames = 0;

	size_t frame_size = aud->params.channels * (snd_pcm_format_width(aud->params.format) / 8);
	n_bytes_tx = frame_size * n_frames;
	n_bytes_tx &= ~(HDA_DMA_ACCESS_LEN - 1);

	if (n_bytes_tx > sizeof(st->buf)) {
		n_bytes_tx = sizeof(st->buf);
	}

	if (n_bytes_tx > 0) {
		err = hops->transfer(hci, st->stream, 1, st->buf, n_bytes_tx);
		if (err)
			return;

		if (sc->ctl == NULL) {
			/* No mixer control, use soft amp */
			left_gain = (st->left_mute == HDA_CODEC_SET_AMP_GAIN_MUTE_MUTE) ? 0 : st->left_gain;
			right_gain = (st->right_mute == HDA_CODEC_SET_AMP_GAIN_MUTE_MUTE) ? 0 : st->right_gain;
		} else {
			left_gain = MAX_GAIN_VALUE;
			right_gain = MAX_GAIN_VALUE;
		}

		err = audio_playback(aud, st->buf, n_bytes_tx, &st->adjustment, left_gain, right_gain);
		if (err < 0) {
			DPRINTF("audio_playback failed\n");
			return;
		}
	}
}

static int hda_codec_audio_output_do_setup(void *arg) {
	struct hda_codec_vdev *sc = (struct hda_codec_vdev *)arg;
	struct hda_codec_stream *st = NULL;
	struct audio *aud = NULL;
	struct audio_params params;
	int err;

	st = &sc->streams[HDA_CODEC_STREAM_OUTPUT];
	aud = st->aud;

	err = hda_codec_parse_format(st->fmt, &params);
	if (err)
		return -1;

	DPRINTF("rate: %d, channels: %d, format: 0x%x\n", params.rate, params.channels, params.format);

	st->tlast = 0;

	err = audio_set_params(aud, &params);
	if (err)
		return err;

	size_t frame_size = aud->params.channels * snd_pcm_format_width(aud->params.format) / 8;
	aud->temp_buf_size = aud->period_size * frame_size * 8;
	aud->temp_buf = malloc(aud->temp_buf_size);
	if (!aud->temp_buf) {
		DPRINTF("Failed to allocate temporary buffer\n");
		return -1;
	}
	aud->temp_buf_wpos = 0;
	aud->play_started = false;

	return 0;
}

static uint32_t hda_codec_audio_input_nid(struct hda_codec_vdev *sc, uint16_t verb, uint16_t payload) {
	struct hda_codec_stream *st = &sc->streams[HDA_CODEC_STREAM_INPUT];
	int res;

	res = hda_codec_audio_inout_nid(st, verb, payload);

	return (res);
}

static void hda_codec_audio_input_do_transfer(void *arg) {
	const struct hda_ops *hops = NULL;
	struct hda_codec_vdev *sc = (struct hda_codec_vdev *)arg;
	struct hda_codec_inst *hci = NULL;
	struct hda_codec_stream *st = NULL;
	struct audio *aud = NULL;
	size_t bytes_read;
	uint64_t tdelta = 0, tnow;
	uint64_t n_bytes_rx;
	snd_pcm_sframes_t n_frames;
	int err;

	hci = sc->hci;

	hops = hci->hops;

	st = &sc->streams[HDA_CODEC_STREAM_INPUT];
	aud = st->aud;

	tnow = hda_get_clock_ns();
	if (st->tlast == 0) {
		tdelta = T_DEFAULT_POLLING_NS;
	} else {
		tdelta = tnow - st->tlast;
	}

	st->tlast = tnow;

	n_frames = (aud->params.rate * tdelta / NS_PER_SEC) + st->adjustment;

	size_t frame_size = aud->params.channels * (snd_pcm_format_width(aud->params.format) / 8);
	n_bytes_rx = frame_size * n_frames;
	/* dma alignment */
	n_bytes_rx &= ~(HDA_DMA_ACCESS_LEN - 1);

	if (n_bytes_rx > sizeof(st->buf)) {
		n_bytes_rx = sizeof(st->buf);
	}

	err = audio_record(aud, st->buf, n_bytes_rx, &bytes_read, &st->adjustment);
	if (!err && bytes_read > 0) {
		hops->transfer(hci, st->stream, 0, st->buf, bytes_read);
	}
}

static int hda_codec_audio_input_do_setup(void *arg) {
	struct hda_codec_vdev *sc = (struct hda_codec_vdev *)arg;
	struct hda_codec_stream *st = NULL;
	struct audio *aud = NULL;
	struct audio_params params;
	int err;

	st = &sc->streams[HDA_CODEC_STREAM_INPUT];
	aud = st->aud;

	err = hda_codec_parse_format(st->fmt, &params);
	if (err)
		return -1;

	DPRINTF("rate: %d, channels: %d, format: 0x%x\n", params.rate, params.channels, params.format);

	err = audio_set_params(aud, &params);

	if (!err) {
		audio_record_start(aud);
	}

	return err;
}

static uint32_t hda_codec_audio_inout_nid(struct hda_codec_stream *st, uint16_t verb, uint16_t payload) {
	uint32_t res = 0;
	uint8_t mute = 0;
	uint8_t gain = 0;

	DPRINTF("%s verb: 0x%x, payload, 0x%x\n", st->actx.name, verb, payload);

	switch (verb) {
	case HDA_CMD_VERB_GET_CONV_FMT:
		res = st->fmt;
		break;
	case HDA_CMD_VERB_SET_CONV_FMT:
		st->fmt = payload;
		break;
	case HDA_CMD_VERB_GET_AMP_GAIN_MUTE:
		if (payload & HDA_CMD_GET_AMP_GAIN_MUTE_LEFT) {
			res = st->left_gain | st->left_mute;
			DPRINTF("GET_AMP_GAIN_MUTE_LEFT: 0x%x\n", res);
		} else {
			res = st->right_gain | st->right_mute;
			DPRINTF("GET_AMP_GAIN_MUTE_RIGHT: 0x%x\n", res);
		}
		break;
	case HDA_CMD_VERB_SET_AMP_GAIN_MUTE:
		mute = payload & HDA_CODEC_SET_AMP_GAIN_MUTE_MUTE;
		gain = payload & HDA_CODEC_SET_AMP_GAIN_MUTE_GAIN_MASK;

		if (payload & HDA_CMD_SET_AMP_GAIN_MUTE_LEFT) {
			st->left_mute = mute;
			st->left_gain = gain;
			DPRINTF("SET_AMP_GAIN_MUTE_LEFT: \
			    mute: 0x%x gain: 0x%x\n",
				mute, gain);
		}

		if (payload & HDA_CMD_SET_AMP_GAIN_MUTE_RIGHT) {
			st->right_mute = mute;
			st->right_gain = gain;
			DPRINTF("SET_AMP_GAIN_MUTE_RIGHT: \
			    mute: 0x%x gain: 0x%x\n",
				mute, gain);
		}
		break;
	case HDA_CMD_VERB_GET_CONV_STREAM_CHAN:
		res = (st->stream << 4) | st->channel;
		break;
	case HDA_CMD_VERB_SET_CONV_STREAM_CHAN:
		st->channel = payload & 0x0f;
		st->stream = (payload >> 4) & 0x0f;
		DPRINTF("st->channel: 0x%x st->stream: 0x%x\n", st->channel, st->stream);
		if (!st->stream)
			hda_audio_ctxt_stop(&st->actx);
		break;
	default:
		DPRINTF("Unknown VERB: 0x%x\n", verb);
		break;
	}

	return (res);
}

static struct hda_codec_class hda_codec = {
	.name = "hda_codec",
	.init = hda_codec_init,
	.reset = hda_codec_reset,
	.command = hda_codec_command,
	.notify = hda_codec_notify,
};

static void hda_audio_ctxt_timer_callback(void *arg, uint64_t nexp) {
	struct hda_audio_ctxt *actx = arg;

	if (!actx || !actx->run) {
		return;
	}

	/* Call the transfer function for each timer expiration */
	for (uint64_t i = 0; i < nexp; i++) {
		if (actx->run) {
			actx->do_transfer(actx->priv);
		}
	}
}

static int hda_audio_ctxt_init(struct hda_audio_ctxt *actx, const char *tname, transfer_func_t do_transfer,
	setup_func_t do_setup, void *priv) {
	int err;

	memset(actx, 0, sizeof(*actx));

	actx->run = 0;
	actx->do_transfer = do_transfer;
	actx->do_setup = do_setup;
	actx->priv = priv;
	actx->sample_interval_ns = T_DEFAULT_POLLING_NS;

	if (strlen(tname) < sizeof(actx->name))
		memcpy(actx->name, tname, strlen(tname) + 1);
	else
		strcpy(actx->name, "unknown");

	/* Initialize timer with CLOCK_MONOTONIC for absolute timing */
	actx->timer.clockid = CLOCK_MONOTONIC;
	err = acrn_timer_init(&actx->timer, hda_audio_ctxt_timer_callback, actx);
	if (err < 0) {
		pr_err("Failed to initialize timer for %s\n", actx->name);
		return err;
	}

	actx->started = 1;

	return (0);
}

static int hda_audio_ctxt_start(struct hda_audio_ctxt *actx) {
	int err = 0;
	struct itimerspec timer_spec;
	struct timespec current_time;

	/* The stream is supposed to be stopped */
	if (actx->run)
		return -1;

	err = (*actx->do_setup)(actx->priv);
	if (err) {
		DPRINTF("Failed to setup audio context %s\n", actx->name);
		return err;
	}

	/* Get current absolute time */
	if (clock_gettime(CLOCK_MONOTONIC, &current_time) < 0) {
		DPRINTF("Failed to get current time for %s\n", actx->name);
		return -1;
	}

	/* Set timer for periodic execution.
	 * Start after a small initial delay to accumulate buffer margin */
	timer_spec.it_value.tv_sec = 0;
	timer_spec.it_value.tv_nsec = actx->sample_interval_ns * 4;

	/* Set periodic interval */
	timer_spec.it_interval.tv_sec = 0;
	timer_spec.it_interval.tv_nsec = actx->sample_interval_ns;

	actx->run = 1;

	err = acrn_timer_settime(&actx->timer, &timer_spec);
	if (err < 0) {
		DPRINTF("Failed to start timer for %s\n", actx->name);
		actx->run = 0;
		return err;
	}

	DPRINTF("Started audio context %s with timer interval %lu ns\n", actx->name, actx->sample_interval_ns);

	return (0);
}

static int hda_audio_ctxt_stop(struct hda_audio_ctxt *actx) {
	int err = 0;
	struct itimerspec timer_spec;

	if (!actx->run)
		return (0);

	/* Stop the timer by setting it_value to 0 */
	memset(&timer_spec, 0, sizeof(timer_spec));

	/* Setting both it_value and it_interval to 0 disables the timer */
	err = acrn_timer_settime(&actx->timer, &timer_spec);
	if (err < 0) {
		DPRINTF("Failed to stop timer for %s\n", actx->name);
		return err;
	}

	actx->run = 0;

	DPRINTF("Stopped audio context %s\n", actx->name);

	struct hda_codec_vdev *sc = (struct hda_codec_vdev *)actx->priv;
	if (sc) {
		struct hda_codec_stream *st = &sc->streams[HDA_CODEC_STREAM_OUTPUT];
		if (st && st->aud && st->aud->temp_buf) {
			free(st->aud->temp_buf);
			st->aud->temp_buf = NULL;
		}
	}

	return (0);
}

static inline void hda_set_reg_by_offset(struct pci_hda_vdev *sc, uint32_t offset, uint32_t value) {
	sc->regs[offset] = value;
}

static inline uint32_t hda_get_reg_by_offset(struct pci_hda_vdev *sc, uint32_t offset) {
	return sc->regs[offset];
}

static inline void hda_set_field_by_offset(struct pci_hda_vdev *sc, uint32_t offset, uint32_t mask, uint32_t value) {
	uint32_t reg_value = 0;

	reg_value = hda_get_reg_by_offset(sc, offset);

	reg_value &= ~mask;
	reg_value |= (value & mask);

	hda_set_reg_by_offset(sc, offset, reg_value);
}

static int hda_parse_opts(char *play, char *rec, char *ctl, char *opts) {
	char *str, *type, *cpy, *c;
	int ret = 0;

	/*
	 * hda sound command line should be:
	 * '-s n hda-sound,...'.
	 * Playback substreams can be added by
	 * 	'pcmp=pcm_name_str
	 * Capture substreams can be added by
	 * 	'pcmc=pcm_name_str
	 * Kcontrol element can be added by
	 * 	'ctl=kctl_identifer
	 * The 'kctl_identifer' should be got by
	 * 	'amixer controls' such as
	 * 	'numid=99,iface=MIXER,name='PCM Playback Volume'.
	 * Substreams and kcontrols should be seperated by '&', as
	 * 	'-s n hda-sound,pcmp=...&pcmc=...&ctl=...'.
	 */
	ctl[0] = '\0';
	c = cpy = strdup(opts);
	while ((str = strsep(&cpy, "&")) != NULL) {
		type = strsep(&str, "=");
		if (strstr("pcmp", type)) {
			pr_info("%s: init pcmp stream %s\n", __func__, str);
			strncpy(play, str, MAX_PCM_DEV_NAME_LEN - 1);
		} else if (strstr("pcmc", type)) {
			pr_info("%s: init pcmc stream %s\n", __func__, str);
			strncpy(rec, str, MAX_PCM_DEV_NAME_LEN - 1);
		} else if (strstr("ctl", type)) {
			pr_info("%s: init ctl %s\n", __func__, str);
			strncpy(ctl, str, MAX_PCM_DEV_NAME_LEN - 1);
		} else {
			pr_err("%s: unknow type %s!\n", __func__, type);
			ret = -1;
		}
	}
	free(c);
	return ret;
}

static struct pci_hda_vdev *hda_init(char *play, char *rec, char *ctl) {
	struct pci_hda_vdev *sc = NULL;
	struct hda_codec_class *codec = NULL;
	int err;

	sc = calloc(1, sizeof(*sc));
	if (!sc)
		return (NULL);

	hda_reset_regs(sc);

	codec = &hda_codec;
	if (codec) {
		pr_info("hda_init play: %s rec: %s ctl: %s\n", play, rec, ctl);
		if (play != NULL || rec != NULL) {
			err = hda_codec_constructor(sc, codec, play, rec, ctl);
			if (err) {
				pr_err("Failed to construct codec %s\n", codec->name);
				free(sc);
				sc = NULL;
			}
		}
	}
	return (sc);
}

static void hda_update_intr(struct pci_hda_vdev *sc) {
	struct pci_vdev *dev = sc->pci_dev;
	uint32_t intctl = hda_get_reg_by_offset(sc, HDAC_INTCTL);
	uint32_t intsts = 0;
	uint32_t sdsts = 0;
	uint32_t rirbsts = 0;
	uint32_t wakeen = 0;
	uint32_t statests = 0;
	uint32_t off = 0;
	int i;

	/* update the CIS bits */
	rirbsts = hda_get_reg_by_offset(sc, HDAC_RIRBSTS);
	if (rirbsts & (HDAC_RIRBSTS_RINTFL | HDAC_RIRBSTS_RIRBOIS))
		intsts |= HDAC_INTSTS_CIS;

	wakeen = hda_get_reg_by_offset(sc, HDAC_WAKEEN);
	statests = hda_get_reg_by_offset(sc, HDAC_STATESTS);
	if (statests & wakeen)
		intsts |= HDAC_INTSTS_CIS;

	/* update the SIS bits */
	for (i = 0; i < HDA_IOSS_NO; i++) {
		off = hda_get_offset_stream(i);
		sdsts = hda_get_reg_by_offset(sc, off + HDAC_SDSTS);
		if (sdsts & HDAC_SDSTS_BCIS)
			intsts |= (1 << i);
	}

	/* update the GIS bit */
	if (intsts)
		intsts |= HDAC_INTSTS_GIS;

	hda_set_reg_by_offset(sc, HDAC_INTSTS, intsts);

	if ((intctl & HDAC_INTCTL_GIE) && ((intsts & ~HDAC_INTSTS_GIS) & intctl)) {
		if (!sc->lintr) {
			pci_lintr_assert(dev);
			sc->lintr = 1;
		}
	} else {
		if (sc->lintr) {
			pci_lintr_deassert(dev);
			sc->lintr = 0;
		}
	}
}

static void hda_response_interrupt(struct pci_hda_vdev *sc) {
	uint8_t rirbctl = hda_get_reg_by_offset(sc, HDAC_RIRBCTL);

	if ((rirbctl & HDAC_RIRBCTL_RINTCTL) && sc->rirb_cnt) {
		sc->rirb_cnt = 0;
		hda_set_field_by_offset(sc, HDAC_RIRBSTS, HDAC_RIRBSTS_RINTFL, HDAC_RIRBSTS_RINTFL);
		hda_update_intr(sc);
	}
}

static int hda_codec_constructor(
	struct pci_hda_vdev *sc, struct hda_codec_class *codec, const char *play, const char *rec, const char *ctl) {
	struct hda_codec_inst *hci = NULL;

	if (sc->codecs_no >= HDA_CODEC_MAX)
		return -1;

	hci = calloc(1, sizeof(struct hda_codec_inst));
	if (!hci)
		return -1;

	hci->hda = sc;
	hci->hops = &hops;
	hci->cad = sc->codecs_no;
	hci->codec = codec;

	sc->codecs[sc->codecs_no++] = hci;

	if (!codec->init) {
		DPRINTF("This codec does not implement the init function\n");
		return -1;
	}

	return (codec->init(hci, play, rec, ctl));
}

static int hda_send_command(struct pci_hda_vdev *sc, uint32_t verb) {
	struct hda_codec_inst *hci = NULL;
	struct hda_codec_class *codec = NULL;
	uint8_t cad = (verb >> HDA_CMD_CAD_SHIFT) & 0x0f;

	if (cad >= sc->codecs_no)
		return -1;

	DPRINTF("cad: 0x%x verb: 0x%x\n", cad, verb);

	hci = sc->codecs[cad];
	if (!hci)
		return -1;

	codec = hci->codec;
	if (!codec)
		return -1;

	if (!codec->command) {
		DPRINTF("This codec does not implement the command function\n");
		return -1;
	}

	return (codec->command(hci, verb));
}

static int hda_notify_codecs(struct pci_hda_vdev *sc, uint8_t run, uint8_t stream, uint8_t dir) {
	struct hda_codec_inst *hci = NULL;
	struct hda_codec_class *codec = NULL;
	int err;
	int i;

	/* Notify each codec */
	for (i = 0; i < sc->codecs_no; i++) {
		hci = sc->codecs[i];
		if (hci) {
			codec = hci->codec;

			if (codec && codec->notify) {
				err = codec->notify(hci, run, stream, dir);
				if (!err)
					break;
			}
		}
	}

	return (i == sc->codecs_no ? -1 : 0);
}

static void hda_reset(struct pci_hda_vdev *sc) {
	int i;
	struct hda_codec_inst *hci = NULL;
	struct hda_codec_class *codec = NULL;

	hda_reset_regs(sc);

	/* Reset each codec */
	for (i = 0; i < sc->codecs_no; i++) {
		hci = sc->codecs[i];

		codec = hci->codec;

		if (codec && codec->reset)
			codec->reset(hci);
	}

	sc->wall_clock_start = hda_get_clock_ns();
}

static void hda_reset_regs(struct pci_hda_vdev *sc) {
	uint32_t off = 0;
	uint8_t i;

	DPRINTF("Reset the HDA controller registers ...\n");

	memset(sc->regs, 0, sizeof(sc->regs));

	hda_set_reg_by_offset(sc, HDAC_GCAP,
		HDAC_GCAP_64OK | (HDA_ISS_NO << HDAC_GCAP_ISS_SHIFT) | (HDA_OSS_NO << HDAC_GCAP_OSS_SHIFT));
	hda_set_reg_by_offset(sc, HDAC_VMAJ, 0x01);
	hda_set_reg_by_offset(sc, HDAC_OUTPAY, 0x3c);
	hda_set_reg_by_offset(sc, HDAC_INPAY, 0x1d);
	hda_set_reg_by_offset(sc, HDAC_CORBSIZE, HDAC_CORBSIZE_CORBSZCAP_256 | HDAC_CORBSIZE_CORBSIZE_256);
	hda_set_reg_by_offset(sc, HDAC_RIRBSIZE, HDAC_RIRBSIZE_RIRBSZCAP_256 | HDAC_RIRBSIZE_RIRBSIZE_256);

	for (i = 0; i < HDA_IOSS_NO; i++) {
		off = hda_get_offset_stream(i);
		hda_set_reg_by_offset(sc, off + HDAC_SDFIFOS, HDA_FIFO_SIZE);
	}
}

static void hda_stream_reset(struct pci_hda_vdev *sc, uint8_t stream_ind) {
	struct hda_stream_desc *st = &sc->streams[stream_ind];
	uint32_t off = hda_get_offset_stream(stream_ind);

	DPRINTF("Reset the HDA stream: 0x%x\n", stream_ind);

	/* Reset the Stream Descriptor registers */
	memset(sc->regs + HDA_STREAM_REGS_BASE + off, 0, HDA_STREAM_REGS_LEN);

	/* Reset the Stream Descriptor */
	memset(st, 0, sizeof(*st));

	hda_set_field_by_offset(sc, off + HDAC_SDSTS, HDAC_SDSTS_FIFORDY, HDAC_SDSTS_FIFORDY);
	hda_set_field_by_offset(sc, off + HDAC_SDCTL0, HDAC_SDCTL_SRST, HDAC_SDCTL_SRST);
}

static int hda_stream_start(struct pci_hda_vdev *sc, uint8_t stream_ind) {
	struct hda_stream_desc *st = &sc->streams[stream_ind];
	struct hda_bdle_desc *bdle_desc = NULL;
	struct hda_bdle *bdle = NULL;
	uint32_t lvi = 0;
	uint32_t bdl_cnt = 0;
	uint64_t bdpl = 0;
	uint64_t bdpu = 0;
	uint64_t bdl_paddr = 0;
	void *bdl_vaddr = NULL;
	uint32_t bdle_sz = 0;
	uint64_t bdle_addrl = 0;
	uint64_t bdle_addrh = 0;
	uint64_t bdle_paddr = 0;
	void *bdle_vaddr = NULL;
	uint32_t off = hda_get_offset_stream(stream_ind);
	uint32_t sdctl = 0;
	uint8_t strm = 0;
	uint8_t dir = 0;

	if (st->run) {
		DPRINTF("Stream 0x%x is already running\n", stream_ind);
		return (0);
	}

	lvi = hda_get_reg_by_offset(sc, off + HDAC_SDLVI);
	bdpl = hda_get_reg_by_offset(sc, off + HDAC_SDBDPL);
	bdpu = hda_get_reg_by_offset(sc, off + HDAC_SDBDPU);

	bdl_cnt = lvi + 1;
	if (!(bdl_cnt <= HDA_BDL_MAX_LEN)) {
		DPRINTF("Invalid BDL length: 0x%x\n", bdl_cnt);
		return -1;
	}

	bdl_paddr = bdpl | (bdpu << 32);
	bdl_vaddr = hda_dma_get_vaddr(sc, bdl_paddr, HDA_BDL_ENTRY_LEN * bdl_cnt);
	if (!bdl_vaddr) {
		DPRINTF("Fail to get the guest virtual address\n");
		return -1;
	}

	DPRINTF("start stream: 0x%x bdl_cnt: 0x%x bdl_paddr: 0x%lx\n", stream_ind, bdl_cnt, bdl_paddr);

	st->bdl_cnt = bdl_cnt;

	bdle = (struct hda_bdle *)bdl_vaddr;
	for (size_t i = 0; i < bdl_cnt; i++, bdle++) {
		bdle_sz = bdle->len;
		if ((bdle_sz % HDA_DMA_ACCESS_LEN)) {
			DPRINTF("Invalid bdle size: 0x%x\n", bdle_sz);
			return -1;
		}

		bdle_addrl = bdle->addrl;
		bdle_addrh = bdle->addrh;

		bdle_paddr = bdle_addrl | (bdle_addrh << 32);
		bdle_vaddr = hda_dma_get_vaddr(sc, bdle_paddr, bdle_sz);
		if (!bdle_vaddr) {
			DPRINTF("Fail to get the guest virtual address\n");
			return -1;
		}

		bdle_desc = &st->bdl[i];
		bdle_desc->addr = bdle_vaddr;
		bdle_desc->len = bdle_sz;
		bdle_desc->ioc = bdle->ioc;

		DPRINTF("bdle: 0x%zx bdle_sz: 0x%x\n", i, bdle_sz);
	}

	sdctl = hda_get_reg_by_offset(sc, off + HDAC_SDCTL0);
	strm = (sdctl >> 20) & 0x0f;
	dir = stream_ind >= HDA_ISS_NO;

	DPRINTF("strm: 0x%x, dir: 0x%x\n", strm, dir);

	sc->stream_map[dir][strm] = stream_ind;
	st->stream = strm;
	st->dir = dir;
	st->bp = 0;
	st->be = 0;

	hda_set_pib(sc, stream_ind, 0);

	st->run = 1;

	hda_notify_codecs(sc, 1, strm, dir);

	return (0);
}

static int hda_stream_stop(struct pci_hda_vdev *sc, uint8_t stream_ind) {
	struct hda_stream_desc *st = &sc->streams[stream_ind];
	uint8_t strm = st->stream;
	uint8_t dir = st->dir;

	DPRINTF("stop stream: 0x%x, strm: 0x%x, dir: 0x%x\n", stream_ind, strm, dir);

	st->run = 0;

	hda_notify_codecs(sc, 0, strm, dir);

	return (0);
}

static uint32_t hda_read(struct pci_hda_vdev *sc, uint32_t offset) {
	if (offset == HDAC_WALCLK || offset == HDAC_WALCLKA)
		return (24 * (hda_get_clock_ns() - sc->wall_clock_start) / 1000);

	return (hda_get_reg_by_offset(sc, offset));
}

static int hda_write(struct pci_hda_vdev *sc, uint32_t offset, uint8_t size, uint32_t value) {
	uint32_t old = hda_get_reg_by_offset(sc, offset);
	uint32_t masks[] = {0x00000000, 0x000000ff, 0x0000ffff, 0x00ffffff, 0xffffffff};
	hda_set_reg_handler set_reg_handler = NULL;

	if (offset < nitems(hda_set_reg_table))
		set_reg_handler = hda_set_reg_table[offset];

	hda_set_field_by_offset(sc, offset, masks[size], value);

	if (set_reg_handler)
		set_reg_handler(sc, offset, old);

	return (0);
}

#if DEBUG_HDA == 1
static inline void hda_print_cmd_ctl_data(struct hda_codec_cmd_ctl *p) {
	DPRINTF("%s size: %d\n", p->name, p->size);
	DPRINTF("%s dma_vaddr: %p\n", p->name, p->dma_vaddr);
	DPRINTF("%s wp: 0x%x\n", p->name, p->wp);
	DPRINTF("%s rp: 0x%x\n", p->name, p->rp);
}
#else
static inline void hda_print_cmd_ctl_data(struct hda_codec_cmd_ctl *p) {
}
#endif

static int hda_corb_start(struct pci_hda_vdev *sc) {
	struct hda_codec_cmd_ctl *corb = &sc->corb;
	uint8_t corbsize = 0;
	uint64_t corblbase = 0;
	uint64_t corbubase = 0;
	uint64_t corbpaddr = 0;

	corb->name = "CORB";

	corbsize = hda_get_reg_by_offset(sc, HDAC_CORBSIZE) & HDAC_CORBSIZE_CORBSIZE_MASK;
	corb->size = hda_corb_sizes[corbsize];

	if (!corb->size) {
		DPRINTF("Invalid corb size\n");
		return -1;
	}

	corblbase = hda_get_reg_by_offset(sc, HDAC_CORBLBASE);
	corbubase = hda_get_reg_by_offset(sc, HDAC_CORBUBASE);

	corbpaddr = corblbase | (corbubase << 32);
	DPRINTF("CORB dma_paddr: %p\n", (void *)corbpaddr);

	corb->dma_vaddr = hda_dma_get_vaddr(sc, corbpaddr, HDA_CORB_ENTRY_LEN * corb->size);
	if (!corb->dma_vaddr) {
		DPRINTF("Fail to get the guest virtual address\n");
		return -1;
	}

	corb->wp = hda_get_reg_by_offset(sc, HDAC_CORBWP);
	corb->rp = hda_get_reg_by_offset(sc, HDAC_CORBRP);

	corb->run = 1;

	hda_print_cmd_ctl_data(corb);

	return (0);
}

static int hda_corb_run(struct pci_hda_vdev *sc) {
	struct hda_codec_cmd_ctl *corb = &sc->corb;
	uint32_t verb = 0;
	int err;

	corb->wp = hda_get_reg_by_offset(sc, HDAC_CORBWP);
	if (corb->wp >= corb->size) {
		DPRINTF("Invalid HDAC_CORBWP %u >= size %u\n", corb->wp, corb->size);
		return -1;
	}

	while (corb->rp != corb->wp && corb->run) {
		corb->rp++;
		corb->rp %= corb->size;

		verb = hda_dma_ld_dword((uint8_t *)corb->dma_vaddr + HDA_CORB_ENTRY_LEN * corb->rp);

		err = hda_send_command(sc, verb);
		if (err) {
			DPRINTF("error sending command 0x%x\n", verb);
			return -1;
		}
	}

	hda_set_reg_by_offset(sc, HDAC_CORBRP, corb->rp);

	if (corb->run)
		hda_response_interrupt(sc);

	return (0);
}

static int hda_rirb_start(struct pci_hda_vdev *sc) {
	struct hda_codec_cmd_ctl *rirb = &sc->rirb;
	uint8_t rirbsize = 0;
	uint64_t rirblbase = 0;
	uint64_t rirbubase = 0;
	uint64_t rirbpaddr = 0;

	rirb->name = "RIRB";

	rirbsize = hda_get_reg_by_offset(sc, HDAC_RIRBSIZE) & HDAC_RIRBSIZE_RIRBSIZE_MASK;
	rirb->size = hda_rirb_sizes[rirbsize];

	if (!rirb->size) {
		DPRINTF("Invalid rirb size\n");
		return -1;
	}

	rirblbase = hda_get_reg_by_offset(sc, HDAC_RIRBLBASE);
	rirbubase = hda_get_reg_by_offset(sc, HDAC_RIRBUBASE);

	rirbpaddr = rirblbase | (rirbubase << 32);
	DPRINTF("RIRB dma_paddr: %p\n", (void *)rirbpaddr);

	rirb->dma_vaddr = hda_dma_get_vaddr(sc, rirbpaddr, HDA_RIRB_ENTRY_LEN * rirb->size);
	if (!rirb->dma_vaddr) {
		DPRINTF("Fail to get the guest virtual address\n");
		return -1;
	}

	rirb->wp = hda_get_reg_by_offset(sc, HDAC_RIRBWP);
	rirb->rp = 0x0000;

	rirb->run = 1;

	hda_print_cmd_ctl_data(rirb);

	return (0);
}

static void *hda_dma_get_vaddr(struct pci_hda_vdev *sc, uint64_t dma_paddr, size_t len) {
	struct pci_vdev *dev = sc->pci_dev;

	return (paddr_guest2host(dev->vmctx, (uintptr_t)dma_paddr, len));
}

static void hda_dma_st_dword(void *dma_vaddr, uint32_t data) {
	*(uint32_t *)dma_vaddr = data;
}

static uint32_t hda_dma_ld_dword(void *dma_vaddr) {
	return (*(uint32_t *)dma_vaddr);
}

static inline uint8_t hda_get_stream_by_offsets(uint32_t offset, uint8_t reg_offset) {
	uint8_t stream_ind = (offset - reg_offset) >> 5;

	return (stream_ind);
}

static inline uint32_t hda_get_offset_stream(uint8_t stream_ind) {
	return (stream_ind << 5);
}

static void hda_set_gctl(struct pci_hda_vdev *sc, uint32_t offset, uint32_t old) {
	uint32_t value = hda_get_reg_by_offset(sc, offset);

	if (!(value & HDAC_GCTL_CRST)) {
		hda_reset(sc);
	}
}

static void hda_set_statests(struct pci_hda_vdev *sc, uint32_t offset, uint32_t old) {
	uint32_t value = hda_get_reg_by_offset(sc, offset);

	hda_set_reg_by_offset(sc, offset, old);

	/* clear the corresponding bits written by the software (guest) */
	hda_set_field_by_offset(sc, offset, value & HDA_STATESTS_IRQ_MASK, 0);

	hda_update_intr(sc);
}

static void hda_set_corbwp(struct pci_hda_vdev *sc, uint32_t offset, uint32_t old) {
	hda_corb_run(sc);
}

static void hda_set_corbctl(struct pci_hda_vdev *sc, uint32_t offset, uint32_t old) {
	uint32_t value = hda_get_reg_by_offset(sc, offset);
	int err;
	struct hda_codec_cmd_ctl *corb = NULL;

	if (value & HDAC_CORBCTL_CORBRUN) {
		if (!(old & HDAC_CORBCTL_CORBRUN)) {
			err = hda_corb_start(sc);
			if (err) {
				return;
			}
		}
	} else {
		corb = &sc->corb;
		memset(corb, 0, sizeof(*corb));
	}

	hda_corb_run(sc);
}

static void hda_set_rirbctl(struct pci_hda_vdev *sc, uint32_t offset, uint32_t old) {
	uint32_t value = hda_get_reg_by_offset(sc, offset);
	int err;
	struct hda_codec_cmd_ctl *rirb = NULL;

	if (value & HDAC_RIRBCTL_RIRBDMAEN) {
		err = hda_rirb_start(sc);
		if (err) {
			return;
		}
	} else {
		rirb = &sc->rirb;
		memset(rirb, 0, sizeof(*rirb));
	}
}

static void hda_set_rirbsts(struct pci_hda_vdev *sc, uint32_t offset, uint32_t old) {
	uint32_t value = hda_get_reg_by_offset(sc, offset);

	hda_set_reg_by_offset(sc, offset, old);

	/* clear the corresponding bits written by the software (guest) */
	hda_set_field_by_offset(sc, offset, value & HDA_RIRBSTS_IRQ_MASK, 0);

	hda_update_intr(sc);
}

static void hda_set_dpiblbase(struct pci_hda_vdev *sc, uint32_t offset, uint32_t old) {
	uint32_t value = hda_get_reg_by_offset(sc, offset);
	uint64_t dpiblbase = 0;
	uint64_t dpibubase = 0;
	uint64_t dpibpaddr = 0;

	if ((value & HDAC_DPLBASE_DPLBASE_DMAPBE) != (old & HDAC_DPLBASE_DPLBASE_DMAPBE)) {
		if (value & HDAC_DPLBASE_DPLBASE_DMAPBE) {
			dpiblbase = value & HDAC_DPLBASE_DPLBASE_MASK;
			dpibubase = hda_get_reg_by_offset(sc, HDAC_DPIBUBASE);

			dpibpaddr = dpiblbase | (dpibubase << 32);
			DPRINTF("DMA Position In Buffer dma_paddr: %p\n", (void *)dpibpaddr);

			sc->dma_pib_vaddr = hda_dma_get_vaddr(sc, dpibpaddr, HDA_DMA_PIB_ENTRY_LEN * HDA_IOSS_NO);
			if (!sc->dma_pib_vaddr) {
				DPRINTF("Fail to get the guest \
					 virtual address\n");
				assert(0);
			}
		} else {
			DPRINTF("DMA Position In Buffer Reset\n");
			sc->dma_pib_vaddr = NULL;
		}
	}
}

static void hda_set_sdctl(struct pci_hda_vdev *sc, uint32_t offset, uint32_t old) {
	uint8_t stream_ind = hda_get_stream_by_offsets(offset, HDAC_SDCTL0);
	uint32_t value = hda_get_reg_by_offset(sc, offset);
	int err;

	DPRINTF("stream_ind: 0x%x old: 0x%x value: 0x%x\n", stream_ind, old, value);

	if (value & HDAC_SDCTL_SRST) {
		hda_stream_reset(sc, stream_ind);
	}

	if ((value & HDAC_SDCTL_RUN) != (old & HDAC_SDCTL_RUN)) {
		if (value & HDAC_SDCTL_RUN) {
			err = hda_stream_start(sc, stream_ind);
			if (err)
				return;
		} else {
			err = hda_stream_stop(sc, stream_ind);
			if (err)
				return;
		}
	}
}

static void hda_set_sdctl2(struct pci_hda_vdev *sc, uint32_t offset, uint32_t old) {
	uint32_t value = hda_get_reg_by_offset(sc, offset);

	hda_set_field_by_offset(sc, offset - 2, 0x00ff0000, value << 16);
}

static void hda_set_sdsts(struct pci_hda_vdev *sc, uint32_t offset, uint32_t old) {
	uint32_t value = hda_get_reg_by_offset(sc, offset);

	hda_set_reg_by_offset(sc, offset, old);

	/* clear the corresponding bits written by the software (guest) */
	hda_set_field_by_offset(sc, offset, value & HDA_SDSTS_IRQ_MASK, 0);

	hda_update_intr(sc);
}

static int hda_signal_state_change(struct hda_codec_inst *hci) {
	struct pci_hda_vdev *sc = NULL;
	uint32_t sdiwake = 0;

	DPRINTF("cad: 0x%x\n", hci->cad);

	sc = hci->hda;
	sdiwake = 1 << hci->cad;

	hda_set_field_by_offset(sc, HDAC_STATESTS, sdiwake, sdiwake);
	hda_update_intr(sc);

	return (0);
}

static int hda_response(struct hda_codec_inst *hci, uint32_t response, uint8_t unsol) {
	struct pci_hda_vdev *sc = NULL;
	struct hda_codec_cmd_ctl *rirb = NULL;
	uint32_t response_ex = 0;
	uint8_t rintcnt = 0;

	if (!hci || !(hci->cad <= HDA_CODEC_MAX)) {
		DPRINTF("Invalid parameters\n");
		return -1;
	}

	response_ex = hci->cad | unsol;
	sc = hci->hda;
	rirb = &sc->rirb;

	if (rirb->run) {
		rirb->wp++;
		rirb->wp %= rirb->size;

		hda_dma_st_dword((uint8_t *)rirb->dma_vaddr + HDA_RIRB_ENTRY_LEN * rirb->wp, response);
		hda_dma_st_dword((uint8_t *)rirb->dma_vaddr + HDA_RIRB_ENTRY_LEN * rirb->wp + 0x04, response_ex);

		hda_set_reg_by_offset(sc, HDAC_RIRBWP, rirb->wp);

		sc->rirb_cnt++;
	}

	rintcnt = hda_get_reg_by_offset(sc, HDAC_RINTCNT);
	if (sc->rirb_cnt == rintcnt)
		hda_response_interrupt(sc);

	return (0);
}

static int hda_transfer(struct hda_codec_inst *hci, uint8_t stream, uint8_t dir, uint8_t *buf, size_t count) {
	struct pci_hda_vdev *sc = NULL;
	struct hda_stream_desc *st = NULL;
	struct hda_bdle_desc *bdl = NULL;
	struct hda_bdle_desc *bdle_desc = NULL;
	uint8_t stream_ind = 0;
	uint32_t lpib = 0;
	uint32_t off = 0;
	size_t left = 0;
	uint8_t irq = 0;

	if (!hci || !hci->hda || buf == NULL || (count % HDA_DMA_ACCESS_LEN)) {
		DPRINTF("Invalid parameters\n");
		return -1;
	}

	if (!stream) {
		DPRINTF("Invalid stream\n");
		return -1;
	}

	sc = hci->hda;

	if (stream >= HDA_STREAM_TAGS_CNT) {
		DPRINTF("Invalid stream tag\n");
		return -1;
	}

	stream_ind = sc->stream_map[dir][stream];

	if ((!dir && !(stream_ind < HDA_ISS_NO)) || (dir && !(stream_ind >= HDA_ISS_NO && stream_ind < HDA_IOSS_NO))) {
		DPRINTF("Stream direction mismatch\n");
		return -1;
	}

	st = &sc->streams[stream_ind];
	if (!st->run) {
		DPRINTF("Stream 0x%x stopped\n", stream);
		return -1;
	}

	if (st->stream != stream) {
		DPRINTF("Stream tag mismatch\n");
		return -1;
	}

	off = hda_get_offset_stream(stream_ind);

	lpib = hda_get_reg_by_offset(sc, off + HDAC_SDLPIB);
	uint32_t cbl = hda_get_reg_by_offset(sc, off + HDAC_SDCBL);

	bdl = st->bdl;

	if (!(st->be < st->bdl_cnt) || !(st->bp < bdl[st->be].len)) {
		DPRINTF("Invalid bdle index or buffer pointer\n");
		return -1;
	}

	left = count;
	DPRINTF("%ld dma start: bd %d l %d c %ld lpib %d/%d \n", hda_get_clock_ns() / 1000, st->be, bdl[st->be].len,
		count, lpib, cbl);
	while (left) {
		bdle_desc = &bdl[st->be];

		if (dir)
			*(uint32_t *)buf = hda_dma_ld_dword((uint8_t *)bdle_desc->addr + st->bp);
		else
			hda_dma_st_dword((uint8_t *)bdle_desc->addr + st->bp, *(uint32_t *)buf);

		buf += HDA_DMA_ACCESS_LEN;
		st->bp += HDA_DMA_ACCESS_LEN;
		lpib += HDA_DMA_ACCESS_LEN;
		left -= HDA_DMA_ACCESS_LEN;

		if (st->bp == bdle_desc->len) {
			st->bp = 0;
			if (bdle_desc->ioc)
				irq = 1;
			st->be++;
			if (st->be == st->bdl_cnt) {
				st->be = 0;
				lpib = 0;
			}
			bdle_desc = &bdl[st->be];
			DPRINTF("dma bd next\n");
		}
	}

	hda_set_pib(sc, stream_ind, lpib);

	if (irq) {
		hda_set_field_by_offset(sc, off + HDAC_SDSTS, HDAC_SDSTS_BCIS, HDAC_SDSTS_BCIS);
		hda_update_intr(sc);
		DPRINTF("irq fired\n");
	}

	return (0);
}

static void hda_set_pib(struct pci_hda_vdev *sc, uint8_t stream_ind, uint32_t pib) {
	uint32_t off = hda_get_offset_stream(stream_ind);

	hda_set_reg_by_offset(sc, off + HDAC_SDLPIB, pib);
	/* LPIB Alias */
	hda_set_reg_by_offset(sc, 0x2000 + off + HDAC_SDLPIB, pib);
	if (sc->dma_pib_vaddr)
		*(uint32_t *)((uint8_t *)sc->dma_pib_vaddr + stream_ind * HDA_DMA_PIB_ENTRY_LEN) = pib;
}

static uint64_t hda_get_clock_ns(void) {
	struct timespec ts;
	int err;

	err = clock_gettime(CLOCK_MONOTONIC, &ts);
	if (err) {
		return 0;
	}

	return (ts.tv_sec * 1000000000LL + ts.tv_nsec);
}

/*
 * PCI HDA function definitions
 */
static int pci_hda_init(struct vmctx *ctx, struct pci_vdev *dev, char *opts) {
	struct pci_hda_vdev *sc = NULL;
	char rec[MAX_PCM_DEV_NAME_LEN] = {0}, play[MAX_PCM_DEV_NAME_LEN] = {0}, ctl[MAX_PCM_DEV_NAME_LEN] = {0};

	DPRINTF("Initialize the HDA PCI device ...\n");
	pci_set_cfgdata16(dev, PCIR_VENDOR, INTEL_VENDORID);
	pci_set_cfgdata16(dev, PCIR_DEVICE, HDA_INTEL_82801G);

	pci_set_cfgdata8(dev, PCIR_SUBCLASS, PCIS_MULTIMEDIA_HDA);
	pci_set_cfgdata8(dev, PCIR_CLASS, PCIC_MULTIMEDIA);

	/* select the Intel HDA mode */
	pci_set_cfgdata8(dev, PCIR_HDCTL, 0x01);

	/* allocate one BAR register for the Memory address offsets */
	pci_emul_alloc_bar(dev, 0, PCIBAR_MEM32, HDA_LAST_OFFSET);

	/* allocate an IRQ pin for our slot */
	pci_lintr_request(dev);

	if (hda_parse_opts(play, rec, ctl, opts))
		return -1;

	if (strnlen(play, MAX_PCM_DEV_NAME_LEN) == 0) {
		strcpy(play, "default");
	}
	if (strnlen(rec, MAX_PCM_DEV_NAME_LEN) == 0) {
		strcpy(rec, "default");
	}

	sc = hda_init(play, rec, ctl);
	if (!sc)
		return -1;

	sc->pci_dev = dev;
	dev->arg = sc;

	return (0);
}

static void pci_hda_write(
	struct vmctx *ctx, int vcpu, struct pci_vdev *dev, int baridx, uint64_t offset, int size, uint64_t value) {
	struct pci_hda_vdev *sc = dev->arg;

	DPRINTF("-> wr offset: 0x%lx value: 0x%lx\n", offset, value);

	hda_write(sc, offset, size, value);
}

static uint64_t pci_hda_read(struct vmctx *ctx, int vcpu, struct pci_vdev *dev, int baridx, uint64_t offset, int size) {
	struct pci_hda_vdev *sc = dev->arg;
	uint64_t value = 0;

	value = hda_read(sc, offset);

	DPRINTF("<- rd offset: 0x%lx value: 0x%lx\n", offset, value);

	return (value);
}

struct pci_vdev_ops pci_ops_hda = {.class_name = "hda-sound",
	.vdev_init = pci_hda_init,
	.vdev_barwrite = pci_hda_write,
	.vdev_barread = pci_hda_read};
DEFINE_PCI_DEVTYPE(pci_ops_hda);
