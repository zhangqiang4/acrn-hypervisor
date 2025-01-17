/*
 * Copyright (C) 2023 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

/*
 * virtio sound
 * audio mediator device model
 */

#include <err.h>
#include <errno.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sysexits.h>

#include <alsa/asoundlib.h>
#include <alsa/control.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/stat.h>

#define pr_prefix "virtio-sound: "
#include "dm.h"
#include "pci_core.h"
#include "virtio.h"
#include "virtio_sound.h"
#include "log.h"

#define VIRTIO_SOUND_RINGSZ	256
#define VIRTIO_SOUND_VQ_NUM	4

/*
 * Host capabilities
 */
#define VIRTIO_SND_S_HOSTCAPS		((1UL << VIRTIO_F_VERSION_1) | (1UL << VIRTIO_SND_F_CTLS))

#define VIRTIO_SOUND_CTL_SEGS	8
#define VIRTIO_SOUND_EVENT_SEGS	2
#define VIRTIO_SOUND_XFER_SEGS	4

#define VIRTIO_SOUND_CARD	4
#define VIRTIO_SOUND_STREAMS	16
#define VIRTIO_SOUND_CTLS	128
#define VIRTIO_SOUND_JACKS	64
#define VIRTIO_SOUND_CHMAPS	64

#define VIRTIO_SOUND_CARD_NAME	64
#define VIRTIO_SOUND_DEVICE_NAME	64
#define VIRTIO_SOUND_IDENTIFIER	128
#define VIRTIO_SOUND_CTRL_PATH	128

#define VIRTIO_TLV_SIZE	1024

#define HDA_JACK_LINE_OUT	0
#define HDA_JACK_SPEAKER	1
#define HDA_JACK_HP_OUT	2
#define HDA_JACK_CD	3
#define HDA_JACK_SPDIF_OUT	4
#define HDA_JACK_DIG_OTHER_OUT	5
#define HDA_JACK_LINE_IN	8
#define HDA_JACK_AUX 9
#define HDA_JACK_MIC_IN	10
#define HDA_JACK_SPDIF_IN	12
#define HDA_JACK_DIG_OTHER_IN	13
#define HDA_JACK_OTHER	0xf

#define HDA_JACK_LOCATION_INTERNAL	0x00
#define HDA_JACK_LOCATION_EXTERNAL	0x01
#define HDA_JACK_LOCATION_SEPARATE	0x02

#define HDA_JACK_LOCATION_NONE	0
#define HDA_JACK_LOCATION_REAR	1
#define HDA_JACK_LOCATION_FRONT	2

#define HDA_JACK_LOCATION_HDMI	0x18

#define HDA_JACK_DEFREG_DEVICE_SHIFT	20
#define HDA_JACK_DEFREG_LOCATION_SHIFT	24

enum {
	VIRTIO_SND_BE_INITED = 1,
	VIRTIO_SND_BE_PRE,
	VIRTIO_SND_BE_PENDING,
	VIRTIO_SND_BE_STARTING,
	VIRTIO_SND_BE_START,
	VIRTIO_SND_BE_STOP,
	VIRTIO_SND_BE_RELEASE,
	VIRTIO_SND_BE_DEINITED,
};
struct virtio_sound_pcm_param {
	uint32_t features;
	uint64_t formats;
	uint64_t rates;

	uint8_t channels_min;
	uint8_t channels_max;

	uint32_t buffer_bytes;
	uint32_t period_bytes;
	uint8_t	channels;
	uint8_t	format;
	uint8_t rate;

	uint32_t rrate;
};

struct virtio_sound_msg_node {
	STAILQ_ENTRY(virtio_sound_msg_node) link;
	struct iovec *iov;
	struct virtio_vq_info *vq;
	int cnt;
	int start_seg;
	int start_pos;
	uint16_t idx;
};

struct virtio_sound_ctrl_msg {
	struct iovec *iov;
	uint16_t idx;
};

struct virtio_sound_chmap {
	uint8_t channels;
	uint8_t positions[VIRTIO_SND_CHMAP_MAX_SIZE];
};

struct virtio_sound_pcm {
	snd_pcm_t *handle;
	int hda_fn_nid;
	int dir;
	int status;
	pthread_mutex_t ctl_mtx;
	int xfer_iov_cnt;
	int id;
	uint32_t ret_len;
	uint16_t ret_idx;

	struct virtio_sound *snd_card;

	pthread_t tid;
	int ctrl_fd[2];
	struct pollfd *poll_fd;
	unsigned int pfd_count;

	char dev_name[VIRTIO_SOUND_DEVICE_NAME];
	struct virtio_sound_pcm_param param;
	STAILQ_HEAD(, virtio_sound_msg_node) head;
	pthread_mutex_t mtx;

	struct virtio_sound_chmap *chmaps[VIRTIO_SOUND_CHMAPS];
	uint32_t chmap_cnt;

	FILE *dbg_fd;
};

struct vbs_ctl_elem {
	snd_hctl_elem_t *elem;
	struct vbs_card *card;

	struct virtio_sound *snd_card;
	uint16_t id;
};

struct vbs_jack_elem {
	snd_hctl_elem_t *elem;
	uint32_t hda_reg_defconf;
	uint32_t connected;
	struct vbs_card *card;
	char *identifier;

	struct virtio_sound *snd_card;
	uint16_t id;
	LIST_ENTRY(vbs_jack_elem) jack_list;
};

struct vbs_card {
	char card[VIRTIO_SOUND_CARD_NAME];
	snd_hctl_t *handle;
	int count;
	int start;
};

/*dev struct*/
struct virtio_sound {
	struct virtio_base base;
	struct virtio_vq_info vq[VIRTIO_SOUND_VQ_NUM];
	pthread_mutex_t mtx;
	struct virtio_snd_config snd_cfg;
	uint64_t	features;

	struct virtio_sound_pcm *streams[VIRTIO_SOUND_STREAMS];
	int stream_cnt;
	int chmap_cnt;

	struct vbs_ctl_elem *ctls[VIRTIO_SOUND_CTLS];
	int ctl_cnt;

	struct vbs_jack_elem *jacks[VIRTIO_SOUND_JACKS];
	int jack_cnt;

	struct vbs_card *cards[VIRTIO_SOUND_CARD];
	int card_cnt;
	pthread_t ctl_tid;

	int max_tx_iov_cnt;
	int max_rx_iov_cnt;
	int status;
};

static int virtio_sound_cfgread(void *vdev, int offset, int size, uint32_t *retval);
static int virtio_sound_send_event(struct virtio_sound *virt_snd, struct virtio_snd_event *event);
static int virtio_sound_event_callback(snd_hctl_elem_t *helem, unsigned int mask);
static void virtio_sound_reset(void *vdev);
static int virtio_sound_create_pcm_thread(struct virtio_sound_pcm *stream);
static int virtio_sound_xfer(struct virtio_sound_pcm *stream);
static int virtio_sound_process_ctrl_cmds(struct virtio_sound_pcm *stream, int *timeout);
static int virtio_sound_create_poll_fds(struct virtio_sound_pcm *stream);
static int virtio_sound_send_pending_msg(struct virtio_sound_pcm *stream);
static void virtio_sound_deinit(struct vmctx *ctx, struct pci_vdev *dev, char *opts);

static LIST_HEAD(listhead, vbs_jack_elem) jack_list_head;

/*	environment variable: VIRTIO_SOUND_WRITE2FILE
	0: no file 1: write to alsa 2: receive from BE */
static int write2file = 0;
/* environment variable: VIRTIO_SOUND_PRINT_CTRL_MSG
	0: no print 1: print received msg 2: receive and transfer process */
static int print_ctrl_msg = 0;

static struct virtio_ops virtio_snd_ops = {
	"virtio_sound",		/* our name */
	VIRTIO_SOUND_VQ_NUM,	/* we support 4 virtqueue */
	sizeof(struct virtio_snd_config),			/* config reg size */
	virtio_sound_reset,	/* reset */
	NULL,	/* device-wide qnotify */
	virtio_sound_cfgread,				/* read virtio config */
	NULL,				/* write virtio config */
	NULL,				/* apply negotiated features */
	NULL,   /* called on guest set status */
};

/*
 * This array should be added as the same order
 * as enum of VIRTIO_SND_PCM_FMT_XXX.
 */
static const snd_pcm_format_t virtio_sound_v2s_format[] = {
	SND_PCM_FORMAT_IMA_ADPCM,
	SND_PCM_FORMAT_MU_LAW,
	SND_PCM_FORMAT_A_LAW,
	SND_PCM_FORMAT_S8,
	SND_PCM_FORMAT_U8,
	SND_PCM_FORMAT_S16_LE,
	SND_PCM_FORMAT_U16_LE,
	SND_PCM_FORMAT_S18_3LE,
	SND_PCM_FORMAT_U18_3LE,
	SND_PCM_FORMAT_S20_3LE,
	SND_PCM_FORMAT_U20_3LE,
	SND_PCM_FORMAT_S24_3LE,
	SND_PCM_FORMAT_U24_3LE,
	SND_PCM_FORMAT_S20_LE,
	SND_PCM_FORMAT_U20_LE,
	SND_PCM_FORMAT_S24_LE,
	SND_PCM_FORMAT_U24_LE,
	SND_PCM_FORMAT_S32_LE,
	SND_PCM_FORMAT_U32_LE,
	SND_PCM_FORMAT_FLOAT_LE,
	SND_PCM_FORMAT_FLOAT64_LE,
	SND_PCM_FORMAT_DSD_U8,
	SND_PCM_FORMAT_DSD_U16_LE,
	SND_PCM_FORMAT_DSD_U32_LE,
	SND_PCM_FORMAT_IEC958_SUBFRAME_LE
};

/*
 * This array should be added as the same order
 * as enum of VIRTIO_SND_PCM_RATE_XXX.
 */
static const uint32_t virtio_sound_t_rate[] = {
	5512, 8000, 11025, 16000, 22050, 32000, 44100, 48000, 64000, 88200, 96000, 176400, 192000
};

static const int32_t virtio_sound_s2v_type[] = {
	-1,
	VIRTIO_SND_CTL_TYPE_BOOLEAN,
	VIRTIO_SND_CTL_TYPE_INTEGER,
	VIRTIO_SND_CTL_TYPE_ENUMERATED,
	VIRTIO_SND_CTL_TYPE_BYTES,
	VIRTIO_SND_CTL_TYPE_IEC958,
	VIRTIO_SND_CTL_TYPE_INTEGER64
};

static const uint8_t virtio_sound_s2v_chmap[] = {
	VIRTIO_SND_CHMAP_NONE,
	VIRTIO_SND_CHMAP_NA,
	VIRTIO_SND_CHMAP_MONO,
	VIRTIO_SND_CHMAP_FL,
	VIRTIO_SND_CHMAP_FR,
	VIRTIO_SND_CHMAP_RL,
	VIRTIO_SND_CHMAP_RR,
	VIRTIO_SND_CHMAP_FC,
	VIRTIO_SND_CHMAP_LFE,
	VIRTIO_SND_CHMAP_SL,
	VIRTIO_SND_CHMAP_SR,
	VIRTIO_SND_CHMAP_RC,
	VIRTIO_SND_CHMAP_FLC,
	VIRTIO_SND_CHMAP_FRC,
	VIRTIO_SND_CHMAP_RLC,
	VIRTIO_SND_CHMAP_RRC,
	VIRTIO_SND_CHMAP_FLW,
	VIRTIO_SND_CHMAP_FRW,
	VIRTIO_SND_CHMAP_FLH,
	VIRTIO_SND_CHMAP_FCH,
	VIRTIO_SND_CHMAP_FRH,
	VIRTIO_SND_CHMAP_TC,
	VIRTIO_SND_CHMAP_TFL,
	VIRTIO_SND_CHMAP_TFR,
	VIRTIO_SND_CHMAP_TFC,
	VIRTIO_SND_CHMAP_TRL,
	VIRTIO_SND_CHMAP_TRR,
	VIRTIO_SND_CHMAP_TRC,
	VIRTIO_SND_CHMAP_TFLC,
	VIRTIO_SND_CHMAP_TFRC,
	VIRTIO_SND_CHMAP_TSL,
	VIRTIO_SND_CHMAP_TSR,
	VIRTIO_SND_CHMAP_LLFE,
	VIRTIO_SND_CHMAP_RLFE,
	VIRTIO_SND_CHMAP_BC,
	VIRTIO_SND_CHMAP_BLC,
	VIRTIO_SND_CHMAP_BRC
};

static void
virtio_sound_create_write_fd(struct virtio_sound_pcm *stream)
{
	char path[VIRTIO_SOUND_CTRL_PATH] = {0};

	snprintf(path, VIRTIO_SOUND_CTRL_PATH, "~/vsnd%d-%ld", stream->id, time(NULL));
	stream->dbg_fd = fopen(path, "w");
}

static inline int
virtio_sound_get_frame_size(struct virtio_sound_pcm *stream)
{
	return snd_pcm_format_physical_width(virtio_sound_v2s_format[stream->param.format]) / 8
		* stream->param.channels;
}

static int
virtio_sound_cfgread(void *vdev, int offset, int size, uint32_t *retval)
{
	struct virtio_sound *virt_snd = vdev;
	void* ptr;
	ptr = (uint8_t *)&virt_snd->snd_cfg + offset;
	memcpy(retval, ptr, size);
	return 0;
}

static void
virtio_sound_reset(void *vdev)
{
	struct virtio_sound *virt_snd = vdev;
	/* now reset rings, MSI-X vectors, and negotiated capabilities */
	virtio_reset_dev(&virt_snd->base);
}

static int
virtio_sound_get_card_name(char *card_str, char *card_name)
{
	int idx, ret = 0;

	idx = snd_card_get_index(card_str);
	if (idx >= 0 && idx < 32)
#if defined(SND_LIB_VER)
#if SND_LIB_VER(1, 2, 5) <= SND_LIB_VERSION
		snprintf(card_name, VIRTIO_SOUND_CARD_NAME, "sysdefault:%i", idx);
#else
		snprintf(card_name, VIRTIO_SOUND_CARD_NAME, "hw:%i", idx);
#endif
#else
		snprintf(card_name, VIRTIO_SOUND_CARD_NAME, "hw:%i", idx);
#endif
	else {
		/* For Sound Device defined by customer which don't have a card name.*/
		snprintf(card_name, VIRTIO_SOUND_CARD_NAME, "%s", card_str);
	}
	return ret;
}

static void
virtio_sound_notify_xfer(struct virtio_sound *virt_snd, struct virtio_vq_info *vq, int iov_cnt)
{
	struct virtio_sound_msg_node *msg_node;
	struct virtio_snd_pcm_xfer *xfer_hdr;
	int n, s = -1, i;

	while (vq_has_descs(vq)) {
		msg_node = malloc(sizeof(struct virtio_sound_msg_node));
		if (msg_node == NULL) {
			pr_err("%s: malloc data node fail!\n", __func__);
			return;
		}
		msg_node->iov = malloc(sizeof(struct iovec) * iov_cnt);
		if (msg_node->iov == NULL) {
			pr_err("%s: malloc iov nodes fail!\n", __func__);
			free(msg_node);
			return;
		}
		pthread_mutex_lock(&vq->mtx);
		n = vq_getchain(vq, &msg_node->idx, msg_node->iov, iov_cnt, NULL);
		pthread_mutex_unlock(&vq->mtx);
		if (n <= 0) {
			pr_err("%s: fail to getchain, cnt %d!\n", __func__, iov_cnt);
			free(msg_node->iov);
			free(msg_node);
			return;
		}
		msg_node->cnt = n;
		msg_node->vq = vq;
		/* Seg[0] is pcm xfer request header, pcm data start from seg[1].*/
		msg_node->start_seg = 1;
		msg_node->start_pos = 0;

		xfer_hdr = (struct virtio_snd_pcm_xfer *)msg_node->iov[0].iov_base;
		s = xfer_hdr->stream_id;

		pthread_mutex_lock(&virt_snd->streams[s]->mtx);
		if (STAILQ_EMPTY(&virt_snd->streams[s]->head)) {
			STAILQ_INSERT_HEAD(&virt_snd->streams[s]->head, msg_node, link);
		} else {
			STAILQ_INSERT_TAIL(&virt_snd->streams[s]->head, msg_node, link);
		}
		pthread_mutex_unlock(&virt_snd->streams[s]->mtx);
		if (write2file == 2 && virt_snd->streams[s]->dbg_fd != NULL) {
			for (i = msg_node->start_seg; i < msg_node->cnt - 1; i++) {
				fwrite(msg_node->iov[i].iov_base + msg_node->start_pos, msg_node->iov[i].iov_len,
					1, virt_snd->streams[s]->dbg_fd);
			}
		}
	}
	if (s >= 0 && virt_snd->streams[s]->status == VIRTIO_SND_BE_PENDING) {
		if (STAILQ_NEXT(STAILQ_FIRST(&virt_snd->streams[s]->head), link) == NULL)
			virtio_sound_send_pending_msg(virt_snd->streams[s]);
	}
}

static void
virtio_sound_notify_tx(void *vdev, struct virtio_vq_info *vq)
{
	struct virtio_sound *virt_snd = vdev;
	virtio_sound_notify_xfer(virt_snd, vq, virt_snd->max_tx_iov_cnt);
}

static void
virtio_sound_notify_rx(void *vdev, struct virtio_vq_info *vq)
{
	struct virtio_sound *virt_snd = vdev;
	virtio_sound_notify_xfer(virt_snd, vq, virt_snd->max_rx_iov_cnt);
}

static int
virtio_sound_set_hwparam(struct virtio_sound_pcm *stream)
{
	snd_pcm_hw_params_t *hwparams;
	snd_pcm_uframes_t buffer_size, period_size;
	int dir = stream->dir;
	int err;

	snd_pcm_hw_params_alloca(&hwparams);
	err = snd_pcm_hw_params_any(stream->handle, hwparams);
	if(err < 0) {
		pr_err("%s: no configurations available, error number %d!\n", __func__, err);
		return -1;
	}

	err = snd_pcm_hw_params_set_access(stream->handle, hwparams, SND_PCM_ACCESS_MMAP_INTERLEAVED);
	if(err < 0) {
		pr_err("%s: set access, error number %d!\n", __func__, err);
		return -1;
	}

	err = snd_pcm_hw_params_set_format(stream->handle, hwparams,
		virtio_sound_v2s_format[stream->param.format]);
	if(err < 0) {
		pr_err("%s: set format(%d), error number %d!\n", __func__,
			virtio_sound_v2s_format[stream->param.format], err);
		return -1;
	}
	err = snd_pcm_hw_params_set_channels(stream->handle,
		hwparams, stream->param.channels);
	if(err < 0) {
		pr_err("%s: set channels(%d) fail, error number %d!\n", __func__,
			stream->param.channels, err);
		return -1;
	}
	stream->param.rrate = virtio_sound_t_rate[stream->param.rate];
	err = snd_pcm_hw_params_set_rate_near(stream->handle, hwparams, &stream->param.rrate, &dir);
	if(err < 0) {
		pr_err("%s: set rate(%u) fail, error number %d!\n", __func__,
			virtio_sound_t_rate[stream->param.rate], err);
		return -1;
	}

	buffer_size = stream->param.buffer_bytes / virtio_sound_get_frame_size(stream);
	err = snd_pcm_hw_params_set_buffer_size_near(stream->handle, hwparams, &buffer_size);
	if(err < 0) {
		pr_err("%s: set buffer_size(%ld) fail, error number %d!\n", __func__, buffer_size, err);
		return -1;
	}
	period_size = stream->param.period_bytes / virtio_sound_get_frame_size(stream);
	dir = stream->dir;
	err = snd_pcm_hw_params_set_period_size_near(stream->handle, hwparams,
		&period_size, &dir);
	if(err < 0) {
		pr_err("%s: set period_size(%ld) fail, error number %d!\n", __func__, period_size, err);
		return -1;
	}

	err = snd_pcm_hw_params(stream->handle, hwparams);
	if (err < 0) {
		pr_err("%s: set hw params fail, error number %d!\n", __func__, err);
		return -1;
	}

	err = snd_pcm_hw_params_get_buffer_size(hwparams, &buffer_size);
	if(err < 0) {
		pr_err("%s: get buffer_size(%ld) fail, error number %d!\ne", __func__, buffer_size, err);
		return -1;
	}
	stream->param.buffer_bytes = buffer_size * virtio_sound_get_frame_size(stream);

	dir = stream->dir;
	err = snd_pcm_hw_params_get_period_size(hwparams, &period_size, &dir);
	if(err < 0) {
		pr_err("%s: set period_size(%ld) fail, error number %d!\n", __func__, period_size, err);
		return -1;
	}
	stream->param.period_bytes = period_size * virtio_sound_get_frame_size(stream);

	return 0;
}


static int
virtio_sound_set_swparam(struct virtio_sound_pcm *stream)
{
	snd_pcm_sw_params_t *swparams;
	snd_pcm_format_t period_size;
	int err;

	snd_pcm_sw_params_alloca(&swparams);
	err = snd_pcm_sw_params_current(stream->handle, swparams);
	if(err < 0) {
		pr_err("%s: no sw params available, error number %d!\n", __func__, err);
		return -1;
	}

	err = snd_pcm_sw_params_set_start_threshold(stream->handle, swparams, 1);
	if(err < 0) {
		pr_err("%s: set threshold fail, error number %d!\n", __func__, err);
		return -1;
	}
	period_size = stream->param.period_bytes / virtio_sound_get_frame_size(stream);
	err = snd_pcm_sw_params_set_avail_min(stream->handle, swparams, period_size);
	if(err < 0) {
		pr_err("%s: set avail min fail, error number %d!\n", __func__, err);
		return -1;
	}
	err = snd_pcm_sw_params_set_period_event(stream->handle, swparams, 1);
	if(err < 0) {
		pr_err("%s: set period event fail, error number %d!\n", __func__, err);
		return -1;
	}
	err = snd_pcm_sw_params(stream->handle, swparams);
	if (err < 0) {
		pr_err("%s: set sw params fail, error number %d!\n", __func__, err);
		return -1;
	}
	return 0;
}

static int
virtio_sound_recover(struct virtio_sound_pcm *stream)
{
	snd_pcm_state_t state = snd_pcm_state(stream->handle);
	struct virtio_snd_event event;
	int err = -1, i;

	if (state == SND_PCM_STATE_XRUN && stream->status == VIRTIO_SND_BE_START) {
		event.hdr.code = VIRTIO_SND_EVT_PCM_XRUN;
		event.data = stream->id;
		virtio_sound_send_event(stream->snd_card, &event);
	}
	if (state == SND_PCM_STATE_XRUN || state == SND_PCM_STATE_SETUP) {
		err = snd_pcm_prepare(stream->handle);
		if(err < 0) {
			pr_err("%s: recorver from xrun prepare fail, error number %d!\n", __func__, err);
			return -1;
		}
		if (stream->dir == SND_PCM_STREAM_CAPTURE) {
			err = snd_pcm_start(stream->handle);
			if(err < 0) {
				pr_err("%s: recorver from xrun start fail, error number %d!\n", __func__, err);
				return -1;
			}
		}
	} else if (state == SND_PCM_STATE_SUSPENDED) {
		for (i = 0; i < 10; i++) {
			err = snd_pcm_resume(stream->handle);
			if (err == -EAGAIN) {
				pr_err("%s: waiting for resume!\n", __func__);
				usleep(5000);
				continue;
			}
			err = snd_pcm_prepare(stream->handle);
			if(err < 0) {
				pr_err("%s: recorver form suspend prepare fail, error number %d!\n", __func__, err);
				return -1;
			}

			if (stream->dir == SND_PCM_STREAM_CAPTURE) {
				err = snd_pcm_start(stream->handle);
				if(err < 0) {
					pr_err("%s: recorver from suspend start fail, error number %d!\n",
						__func__, err);
					return -1;
				}
			}
			break;
		}
	}
	return err;
}

static void
virtio_snd_release_data_node(struct virtio_sound_pcm *stream, struct virtio_sound_msg_node *msg_node, int len)
{
	struct virtio_snd_pcm_status *ret_status;
	snd_pcm_sframes_t sd;
	int err;
	ret_status = (struct virtio_snd_pcm_status *)msg_node->iov[msg_node->cnt - 1].iov_base;
	ret_status->status = VIRTIO_SND_S_OK;
	err = snd_pcm_delay(stream->handle, &sd);
	if (err < 0) {
		pr_err("%s: get pcm delay, error number %d, %ld!\n", __func__, err, sd);
		sd = 0;
	}
	ret_status->latency_bytes = sd * virtio_sound_get_frame_size(stream);
	pthread_mutex_lock(&msg_node->vq->mtx);
	vq_relchain(msg_node->vq, msg_node->idx, len + sizeof(struct virtio_snd_pcm_status));
	vq_endchains(msg_node->vq, 0);
	pthread_mutex_unlock(&msg_node->vq->mtx);
	pthread_mutex_lock(&stream->mtx);
	STAILQ_REMOVE_HEAD(&stream->head, link);
	pthread_mutex_unlock(&stream->mtx);
	free(msg_node->iov);
	free(msg_node);
}

static int
virtio_sound_xfer(struct virtio_sound_pcm *stream)
{
	const snd_pcm_channel_area_t *pcm_areas;
	struct virtio_sound_msg_node *msg_node;
	snd_pcm_sframes_t avail, xfer = 0;
	snd_pcm_uframes_t pcm_offset, frames;
	void *buf;
	int err, i, frame_size, to_copy, len = 0, left, commit;

	avail = snd_pcm_avail_update(stream->handle);
	if (avail < 0) {
		pr_err("%s: recorver form suspend prepare fail, available %ld!\n", __func__, avail);
		return -1;
	}
	frame_size = virtio_sound_get_frame_size(stream);
	frames = stream->param.period_bytes / frame_size;
	/*
	* Backend copy a period data in one time as hw parameter in SOS.
	*/
	if (avail < frames || STAILQ_EMPTY(&stream->head)) {
		return 0;
	}
	err = snd_pcm_mmap_begin(stream->handle, &pcm_areas, &pcm_offset, &frames);
	if (err < 0) {
		pr_err("%s: mmap begin fail, error number %d!\n", __func__, err);
		return -1;
	}
	/*
	* 'pcm_areas' is an array which contains num_of_channels elements in it.
	* For interleaved, all elements in the array has the same addr but different offset ("first" in the structure).
	*/
	buf = pcm_areas[0].addr + pcm_offset * frame_size;
	do{
		if ((msg_node = STAILQ_FIRST(&stream->head)) == NULL)
			break;
		for (i = msg_node->start_seg; i < msg_node->cnt - 1; i++) {
			left = (msg_node->iov[i].iov_len- msg_node->start_pos) / frame_size;
			to_copy = MIN(left, (frames - xfer));
			/*
			* memcpy can only be used when SNDRV_PCM_INFO_INTERLEAVED.
			*/
			if (stream->dir == SND_PCM_STREAM_PLAYBACK) {
				memcpy(buf, msg_node->iov[i].iov_base + msg_node->start_pos, to_copy * frame_size);
			} else {
				memcpy(msg_node->iov[i].iov_base + msg_node->start_pos, buf, to_copy * frame_size);
			}
			if (write2file == 1 && stream->dbg_fd != NULL) {
				fwrite(msg_node->iov[i].iov_base + msg_node->start_pos, to_copy, frame_size, stream->dbg_fd);
			}

			xfer += to_copy;
			buf += to_copy * frame_size;
			msg_node->start_pos += to_copy * frame_size;
			if (msg_node->start_pos >= msg_node->iov[i].iov_len) {
					msg_node->start_seg++;
					msg_node->start_pos = 0;
			} else {
					break;
			}
		}
		if (msg_node->start_seg >= msg_node->cnt - 1) {
			/* Capture need return the read legth to FE.
			 * For playback all data has writen, len is not needed.
			 */
			if (stream->dir == SND_PCM_STREAM_CAPTURE) {
					for (i = 1; i < msg_node->cnt - 1; i++)
							len += msg_node->iov[i].iov_len;
			}
			virtio_snd_release_data_node(stream, msg_node, len);
		}
	} while(xfer < frames);

	commit = snd_pcm_mmap_commit(stream->handle, pcm_offset, xfer);
	if(xfer < 0 || xfer != commit) {
		pr_err("%s: mmap commit fail, xfer %ld!\n", __func__, xfer);
		return -1;
	}

	return xfer;
}

static void
virtio_sound_clean_vq(struct virtio_sound_pcm *stream) {
	struct virtio_sound *virt_snd = stream->snd_card;
	struct virtio_sound_msg_node *msg_node;
	struct virtio_vq_info *vq = NULL;
	struct virtio_snd_pcm_status *ret_status;

	if (stream->dir == SND_PCM_STREAM_PLAYBACK) {
		virtio_sound_notify_xfer(virt_snd, &virt_snd->vq[2], virt_snd->max_tx_iov_cnt);
	} else {
		virtio_sound_notify_xfer(virt_snd, &virt_snd->vq[3], virt_snd->max_tx_iov_cnt);
	}

	while ((msg_node = STAILQ_FIRST(&stream->head)) != NULL) {
		vq = msg_node->vq;
		ret_status = (struct virtio_snd_pcm_status *)msg_node->iov[msg_node->cnt - 1].iov_base;
		ret_status->status = VIRTIO_SND_S_BAD_MSG;
		ret_status->latency_bytes = 0;
		pthread_mutex_lock(&vq->mtx);
		vq_relchain(vq, msg_node->idx, sizeof(struct virtio_snd_pcm_status));
		pthread_mutex_unlock(&vq->mtx);
		pthread_mutex_lock(&stream->mtx);
		STAILQ_REMOVE_HEAD(&stream->head, link);
		pthread_mutex_unlock(&stream->mtx);
		free(msg_node->iov);
		free(msg_node);
	}

	if (vq) {
		pthread_mutex_lock(&vq->mtx);
		vq_endchains(vq, 0);
		pthread_mutex_unlock(&vq->mtx);
	}
}

static int
virtio_sound_create_poll_fds(struct virtio_sound_pcm *stream)
{
	int err = 0;

	if (stream->poll_fd)
		free(stream->poll_fd);
	if (stream->handle && stream->status == VIRTIO_SND_BE_START) {
		stream->pfd_count = snd_pcm_poll_descriptors_count(stream->handle) + 1;
		stream->poll_fd = malloc(sizeof(struct pollfd) * stream->pfd_count);
		if (stream->poll_fd == NULL) {
			pr_err("%s: malloc poll fd fail\n", __func__);
			return -1;
		}
		stream->poll_fd[0].fd = stream->ctrl_fd[0];
		stream->poll_fd[0].events = POLLIN | POLLERR | POLLNVAL;
		if ((err = snd_pcm_poll_descriptors(stream->handle,
			&stream->poll_fd[1], stream->pfd_count - 1)) <= 0) {
			pr_err("%s: get poll descriptor fail, error number %d!\n", __func__, err);
			return -1;
		}
	} else {
		stream->pfd_count = 1;
		stream->poll_fd = malloc(sizeof(struct pollfd) * stream->pfd_count);
		if (stream->poll_fd == NULL) {
			pr_err("%s: malloc poll fd fail\n", __func__);
			return -1;
		}
		stream->poll_fd[0].fd = stream->ctrl_fd[0];
		stream->poll_fd[0].events = POLLIN | POLLERR | POLLNVAL;
	}
	return 0;
}


static void*
virtio_sound_pcm_thread(void *param)
{
	unsigned short revents;
	struct virtio_sound_pcm *stream = (struct virtio_sound_pcm*)param;
	struct virtio_sound *virt_snd = stream->snd_card;
	struct virtio_snd_event event;
	int err, timeout = -1;

	event.hdr.code = VIRTIO_SND_EVT_PCM_XRUN;
	event.data = stream->id;
	err = virtio_sound_create_poll_fds(stream);
	if (err) {
		pr_err("%s: stream create pool fd failed!\n", __func__);
	}
	do {
		err = poll(stream->poll_fd, stream->pfd_count, timeout);
		if (err < 0) {
			if (errno != EINTR)
				pr_err("%s: pool errno %d!\n", __func__, errno);
			continue;
		}
		/*
		 * For VIRTIO_SND_BE_STARTING state, PCM stream will wait
		 * for poll timeout(about a period time) to avoid FE offset ptr
		 * close loop before start.
		 */
		if (stream->status == VIRTIO_SND_BE_STARTING) {
			timeout = -1;
			stream->status = VIRTIO_SND_BE_START;
			err = virtio_sound_create_poll_fds(stream);
			if (err) {
				pr_err("%s: stream create pool fd failed!\n", __func__);
			}
			continue;
		}
		if (stream->poll_fd[0].revents & POLLIN) {
			if (virtio_sound_process_ctrl_cmds(stream, &timeout) == 1)
				continue;
		} else if (stream->poll_fd[0].revents) {
			pr_err("%s: stream ctrl fd error 0x%X!\n", __func__, stream->poll_fd[0].revents);
		}
		if (stream->status != VIRTIO_SND_BE_START) {
			continue;
		}
		snd_pcm_poll_descriptors_revents(stream->handle, &stream->poll_fd[1],
			stream->pfd_count - 1, &revents);
		if ((revents & POLLOUT || revents & POLLIN) && ((revents & POLLERR) == 0)) {
			err = virtio_sound_xfer(stream);
			if (err < 0) {
				err = virtio_sound_recover(stream);
				if (err < 0) {
					pr_err("%s: stream %d xfer error!\n", __func__, stream->id);
					virtio_sound_send_event(stream->snd_card, &event);
					continue;
				}
			}
		} else if (revents && stream->status == VIRTIO_SND_BE_START) {
			/*Do not recover stopped stream, or it will cause FE resume fail.*/

			err = virtio_sound_recover(stream);
			if (err < 0) {
				pr_err("%s: stream %d poll error %d!\n", __func__, stream->id,
					(int)snd_pcm_state(stream->handle));
				virtio_sound_send_event(stream->snd_card, &event);
				continue;
			}
		}
	} while (virt_snd->status != VIRTIO_SND_BE_DEINITED);

	free(stream->poll_fd);
	stream->poll_fd = NULL;
	close(stream->ctrl_fd[0]);
	close(stream->ctrl_fd[1]);
	pthread_exit(NULL);
}

static int
virtio_sound_create_pcm_thread(struct virtio_sound_pcm *stream)
{
	char ctrl_path[VIRTIO_SOUND_CTRL_PATH], tname[MAXCOMLEN + 1];
	struct timespec cur_time;
	int err;

	clock_gettime(CLOCK_MONOTONIC, &cur_time);
	snprintf(ctrl_path, VIRTIO_SOUND_CTRL_PATH, "/tmp/%s_%d_%ld", stream->dev_name,
		stream->id, cur_time.tv_nsec);
	err = mkfifo(ctrl_path, S_IFIFO | 0666);
	if (err == -1) {
		pr_err("%s: ctrl fd %s mkfifo fail!\n", __func__, ctrl_path);
		return -1;
	}

	stream->ctrl_fd[0] = open(ctrl_path, O_RDONLY | O_NONBLOCK);
	stream->ctrl_fd[1] = open(ctrl_path, O_WRONLY | O_NONBLOCK);
	if (stream->ctrl_fd[0] == -1 || stream->ctrl_fd[1] == -1) {
		pr_err("%s: create ctrl %s fd(%d)(%d) fail!\n", __func__, ctrl_path,
			stream->ctrl_fd[0], stream->ctrl_fd[1]);
		return -1;
	}
	snprintf(tname, sizeof(tname), "virtio-snd-%d", stream->id);
	pthread_create(&stream->tid, NULL, virtio_sound_pcm_thread, (void *)stream);
	pthread_setname_np(stream->tid, tname);
	return 0;
}

static void virtio_sound_update_iov_cnt(struct virtio_sound *virt_snd, int dir) {
	int i, cnt = 0;

	for (i = 0; i < virt_snd->stream_cnt; i++) {
		if (virt_snd->streams[i]->dir == dir && virt_snd->streams[i]->status != VIRTIO_SND_BE_INITED
			&& cnt < virt_snd->streams[i]->xfer_iov_cnt) {
			cnt = virt_snd->streams[i]->xfer_iov_cnt;
		}
	}
	if (dir == SND_PCM_STREAM_PLAYBACK)
		virt_snd->max_tx_iov_cnt = cnt;
	else
		virt_snd->max_rx_iov_cnt = cnt;
}

static snd_hctl_elem_t *
virtio_sound_get_ctl_elem(snd_hctl_t *hctl, char *identifier)
{
	snd_ctl_elem_id_t *id;
	snd_hctl_elem_t *elem;

	snd_ctl_elem_id_alloca(&id);
	if (snd_ctl_ascii_elem_id_parse(id, identifier) < 0) {
		pr_err("%s: wrong identifier %s!\n", __func__, identifier);
		return NULL;
	}
	if ((elem = snd_hctl_find_elem(hctl, id)) == NULL) {
		pr_err("%s: find elem fail, identifier is %s!\n", __func__, identifier);
		return NULL;
	}
	return elem;
}

static int
virtio_sound_get_jack_value(snd_hctl_elem_t *elem)
{
	snd_ctl_elem_info_t *ctl;
	snd_ctl_elem_value_t *ctl_value;
	int value;

	snd_ctl_elem_info_alloca(&ctl);
	if (snd_hctl_elem_info(elem, ctl) < 0 || snd_ctl_elem_info_is_readable(ctl) == 0) {
		pr_err("%s: access check fail, identifier is %s!\n", __func__, snd_hctl_elem_get_name(elem));
		return -1;
	}
	snd_ctl_elem_value_alloca(&ctl_value);
	if (snd_hctl_elem_read(elem, ctl_value) < 0) {
		pr_err("%s: read %s value fail!\n", __func__, snd_hctl_elem_get_name(elem));
		return -1;
	}
	value = snd_ctl_elem_value_get_boolean(ctl_value, 0);
	return value;
}

static void
virtio_sound_release_ctrl_msg(struct virtio_sound *virt_snd, struct virtio_sound_ctrl_msg *ctrl, uint32_t len)
{
	struct virtio_vq_info *ctl_vq = &virt_snd->vq[0];

	pthread_mutex_lock(&ctl_vq->mtx);
	vq_relchain(ctl_vq, ctrl->idx, len);
	vq_endchains(ctl_vq, 1);
	pthread_mutex_unlock(&ctl_vq->mtx);
	if (ctrl->iov)
		free(ctrl->iov);
}

static int
virtio_sound_r_jack_info(struct virtio_sound *virt_snd, struct iovec *iov, uint8_t n)
{
	struct virtio_snd_query_info *info = (struct virtio_snd_query_info *)iov[0].iov_base;
	struct virtio_snd_jack_info *jack_info = iov[2].iov_base;
	struct virtio_snd_hdr *ret = iov[1].iov_base;
	int j = 0, i = 0, ret_len;


	if (n != 3) {
		pr_err("%s: invalid seg num %d!\n", __func__, n);
		return 0;
	}
	if ((info->start_id + info->count) > virt_snd->jack_cnt) {
		pr_err("%s: invalid jack, start %d, count = %d!\n", __func__,
			info->start_id, info->count);
		ret->code = VIRTIO_SND_S_BAD_MSG;
		return (int)iov[1].iov_len;
	}
	ret_len = info->count * sizeof(struct virtio_snd_jack_info);
	if (ret_len > iov[2].iov_len) {
		pr_err("%s: too small buffer %d, required %d!\n", __func__,
			iov[2].iov_len, ret_len);
		ret->code = VIRTIO_SND_S_BAD_MSG;
		return (int)iov[1].iov_len;
	}

	memset(jack_info, 0, ret_len);
	j = info->start_id;
	for (i = 0; i < info->count; i++) {
		jack_info[i].connected = virt_snd->jacks[j]->connected;
		jack_info[i].hda_reg_defconf = virt_snd->jacks[j]->hda_reg_defconf;
		j++;
	}

	ret->code = VIRTIO_SND_S_OK;
	return ret_len + (int)iov[1].iov_len;
}

static int
virtio_sound_r_pcm_info(struct virtio_sound *virt_snd, struct iovec *iov, uint8_t n)
{
	int i, ret_len;
	struct virtio_snd_query_info *info = (struct virtio_snd_query_info *)iov[0].iov_base;
	struct virtio_snd_pcm_info *pcm_info = iov[2].iov_base;
	struct virtio_sound_pcm *stream;
	struct virtio_snd_hdr *ret = iov[1].iov_base;

	if (n != 3) {
		pr_err("%s: invalid seg num %d!\n", __func__, n);
		return 0;
	}
	if ((info->start_id + info->count) > virt_snd->stream_cnt) {
		pr_err("%s: invalid stream, start %d, count = %d!\n", __func__,
			info->start_id, info->count);
		ret->code = VIRTIO_SND_S_BAD_MSG;
		return (int)iov[1].iov_len;
	}
	ret_len = info->count * sizeof(struct virtio_snd_pcm_info);
	if (ret_len > iov[2].iov_len) {
		pr_err("%s: too small buffer %d, required %d!\n", __func__,
			iov[2].iov_len, ret_len);
		ret->code = VIRTIO_SND_S_BAD_MSG;
		return (int)iov[1].iov_len;
	}

	for (i = 0; i < info->count; i++) {
		stream = virt_snd->streams[info->start_id + i];
		if (stream == NULL) {
			pr_err("%s: invalid stream, start %d, count = %d!\n", __func__,
				info->start_id, info->count);
			ret->code = VIRTIO_SND_S_BAD_MSG;
			return (int)iov[1].iov_len;
		}
		pcm_info[i].hdr.hda_fn_nid = stream->hda_fn_nid;
		pcm_info[i].features = stream->param.features;
		pcm_info[i].formats = stream->param.formats;
		pcm_info[i].rates = stream->param.rates;
		pcm_info[i].direction = stream->dir;
		pcm_info[i].channels_min = stream->param.channels_min;
		pcm_info[i].channels_max = stream->param.channels_max;
		memset(pcm_info[i].padding, 0, sizeof(pcm_info[i].padding));
	}

	ret->code = VIRTIO_SND_S_OK;
	return ret_len + (int)iov[1].iov_len;
}

static int
virtio_sound_r_set_params(struct virtio_sound_pcm *stream, struct virtio_sound_ctrl_msg *ctrl)
{
	struct virtio_snd_pcm_set_params *params = (struct virtio_snd_pcm_set_params *)ctrl->iov[0].iov_base;
	struct virtio_snd_hdr *ret = ctrl->iov[1].iov_base;
	int err;
	if((stream->param.formats && (1 << params->format) == 0) ||
		(stream->param.rates && (1 << params->rate) == 0) ||
		(params->channels < stream->param.channels_min) ||
		(params->channels > stream->param.channels_max)) {
		pr_err("%s: invalid parameters sample format %d, frame rate %d, channels %d!\n", __func__,
		params->format, params->rate, params->channels);
		ret->code = VIRTIO_SND_S_BAD_MSG;
		virtio_sound_release_ctrl_msg(stream->snd_card, ctrl, ctrl->iov[1].iov_len);
		return -1;
	}
	ret->code = VIRTIO_SND_S_OK;
	stream->param.buffer_bytes = params->buffer_bytes;
	stream->param.period_bytes = params->period_bytes;
	stream->param.features = params->features;
	stream->param.channels = params->channels;
	stream->param.format = params->format;
	stream->param.rate = params->rate;

	/*
	 * In extreme case, each data page is disconinuous and the start and end
	 * of data buffer is not 4k align. The total xfer_iov_cnt is
	 * period bytes / 4k + 2 (start and end) + 2 (xfer msg + status).
	 */
	stream->xfer_iov_cnt = stream->param.period_bytes / 4096 + VIRTIO_SOUND_XFER_SEGS;
	virtio_sound_update_iov_cnt(stream->snd_card, stream->dir);
	if (!stream->handle)
		if (snd_pcm_open(&stream->handle, stream->dev_name,
			stream->dir, SND_PCM_NONBLOCK) < 0 || stream->handle == NULL) {
			pr_err("%s: stream %s open fail!\n", __func__, stream->dev_name);
			ret->code = VIRTIO_SND_S_BAD_MSG;
			virtio_sound_release_ctrl_msg(stream->snd_card, ctrl, ctrl->iov[1].iov_len);
			return -1;
		}
	err = virtio_sound_set_hwparam(stream);
	if (err < 0) {
		pr_err("%s: set hw params fail!\n", __func__);
		ret->code = VIRTIO_SND_S_BAD_MSG;
	}
	err = virtio_sound_set_swparam(stream);
	if (err < 0) {
		pr_err("%s: set sw params fail!\n", __func__);
		ret->code = VIRTIO_SND_S_BAD_MSG;
	}
	virtio_sound_release_ctrl_msg(stream->snd_card, ctrl, ctrl->iov[1].iov_len);
	return 0;
}

static int
virtio_sound_r_pcm_prepare(struct virtio_sound_pcm *stream, struct virtio_sound_ctrl_msg *ctrl)
{
	struct virtio_snd_hdr *ret = ctrl->iov[1].iov_base;
	int err;

	if (stream->status == VIRTIO_SND_BE_RELEASE) {
		pr_err("%s: stream %d is releasing!\n", __func__, stream->id);
		ret->code = VIRTIO_SND_S_BAD_MSG;
		virtio_sound_release_ctrl_msg(stream->snd_card, ctrl, ctrl->iov[1].iov_len);
		return -1;
	}
	ret->code = VIRTIO_SND_S_OK;
	if(!stream->handle) {
		if (snd_pcm_open(&stream->handle, stream->dev_name,
			stream->dir, SND_PCM_NONBLOCK) < 0  || stream->handle == NULL) {
			pr_err("%s: stream %s open fail!\n", __func__, stream->dev_name);
			goto err;
		}
		err = virtio_sound_set_hwparam(stream);
		if (err < 0) {
			pr_err("%s: set hw params fail!\n", __func__);
			goto err;
		}
		err = virtio_sound_set_swparam(stream);
		if (err < 0) {
			pr_err("%s: set sw params fail!\n", __func__);
			goto err;
		}
	}
	if (snd_pcm_prepare(stream->handle) < 0) {
		pr_err("%s: stream %s prepare fail!\n", __func__, stream->dev_name);
		goto err;
	}
	if (write2file != 0 && stream->dbg_fd == NULL)
		virtio_sound_create_write_fd(stream);

	stream->status = VIRTIO_SND_BE_PRE;
	virtio_sound_update_iov_cnt(stream->snd_card, stream->dir);
	virtio_sound_release_ctrl_msg(stream->snd_card, ctrl, ctrl->iov[1].iov_len);
	return 0;

err:
	virtio_sound_release_ctrl_msg(stream->snd_card, ctrl, ctrl->iov[1].iov_len);
	return -1;
}

static int
virtio_sound_r_pcm_release(struct virtio_sound_pcm *stream, struct virtio_sound_ctrl_msg *ctrl)
{
	struct virtio_snd_hdr *ret = ctrl->iov[1].iov_base;

	stream->status = VIRTIO_SND_BE_RELEASE;
	virtio_sound_update_iov_cnt(stream->snd_card, stream->dir);
	virtio_sound_clean_vq(stream);
	if (stream->handle) {
		if (snd_pcm_close(stream->handle) < 0) {
			pr_err("%s: stream %s close error!\n", __func__, stream->dev_name);
		}
		stream->handle = NULL;
	}
	if (stream->dbg_fd) {
		fclose(stream->dbg_fd);
		stream->dbg_fd = NULL;
	}
	stream->status = VIRTIO_SND_BE_INITED;
	ret->code = VIRTIO_SND_S_OK;
	virtio_sound_update_iov_cnt(stream->snd_card, stream->dir);
	virtio_sound_release_ctrl_msg(stream->snd_card, ctrl, ctrl->iov[1].iov_len);
	return 0;
}

static int
virtio_sound_r_pcm_start(struct virtio_sound_pcm *stream, struct virtio_sound_ctrl_msg *ctrl, int *timeout)
{
	struct virtio_snd_hdr *ret = ctrl->iov[1].iov_base;
	int i;
	ret->code = VIRTIO_SND_S_OK;
	if (stream->status == VIRTIO_SND_BE_RELEASE) {
		pr_err("%s: stream %d is releasing!\n", __func__, stream->id);
		ret->code = VIRTIO_SND_S_BAD_MSG;
		virtio_sound_release_ctrl_msg(stream->snd_card, ctrl, ctrl->iov[1].iov_len);
		return -1;
	}
	if (stream->dir == SND_PCM_STREAM_PLAYBACK) {
		*timeout = 1000 / (stream->param.rrate / (stream->param.period_bytes
			/ virtio_sound_get_frame_size(stream)));
		if (STAILQ_EMPTY(&stream->head)) {
			stream->status = VIRTIO_SND_BE_PENDING;
			virtio_sound_release_ctrl_msg(stream->snd_card, ctrl, ctrl->iov[1].iov_len);
			return 1;
		}
		/*
		 * For start threshold is 1, send 2 period before start.
		 * Less start periods benifit the frontend hw_ptr updating.
		 * After 1 period finished, xfer thread will full fill the buffer.
		 * Send 2 periods here to avoid the empty buffer which may cause
		 * pops and clicks.
		 */
		for (i = 0; i < 2; i++) {
			if (virtio_sound_xfer(stream) < 0) {
				pr_err("%s: stream fn_id %d xfer error!\n", __func__, stream->hda_fn_nid);
				ret->code = VIRTIO_SND_S_BAD_MSG;
				virtio_sound_release_ctrl_msg(stream->snd_card, ctrl, ctrl->iov[1].iov_len);
				return -1;
			}
		}
		stream->status = VIRTIO_SND_BE_STARTING;
	} else {
		stream->status = VIRTIO_SND_BE_START;
		if (virtio_sound_create_poll_fds(stream) < 0) {
			pr_err("%s: stream create pool fd failed!\n", __func__);
			ret->code = VIRTIO_SND_S_BAD_MSG;
			virtio_sound_release_ctrl_msg(stream->snd_card, ctrl, ctrl->iov[1].iov_len);
			return -1;
		}
	}

	if (snd_pcm_start(stream->handle) < 0) {
		pr_err("%s: stream %s start error!\n", __func__, stream->dev_name);
		ret->code = VIRTIO_SND_S_BAD_MSG;
		virtio_sound_release_ctrl_msg(stream->snd_card, ctrl, ctrl->iov[1].iov_len);
		return -1;
	}
	virtio_sound_release_ctrl_msg(stream->snd_card, ctrl, ctrl->iov[1].iov_len);
	return 0;
}

static int
virtio_sound_r_pcm_stop(struct virtio_sound_pcm *stream, struct virtio_sound_ctrl_msg *ctrl)
{
	struct virtio_snd_hdr *ret = ctrl->iov[1].iov_base;
	int err;

	if (stream->handle != NULL && snd_pcm_drop(stream->handle) < 0) {
		pr_err("%s: stream %s drop error!\n", __func__, stream->dev_name);
	}
	stream->status = VIRTIO_SND_BE_STOP;
	err = virtio_sound_create_poll_fds(stream);
	if (err) {
		pr_err("%s: stream create pool fd failed!\n", __func__);
		ret->code = VIRTIO_SND_S_BAD_MSG;
		virtio_sound_release_ctrl_msg(stream->snd_card, ctrl, ctrl->iov[1].iov_len);
		return -1;
	}

	ret->code = VIRTIO_SND_S_OK;
	virtio_sound_release_ctrl_msg(stream->snd_card, ctrl, ctrl->iov[1].iov_len);
	return 0;
}

static int
virtio_sound_r_chmap_info(struct virtio_sound *virt_snd, struct iovec *iov, uint8_t n)
{
	struct virtio_snd_query_info *info = (struct virtio_snd_query_info *)iov[0].iov_base;
	struct virtio_snd_chmap_info *chmap_info = iov[2].iov_base;
	struct virtio_sound_pcm *stream = virt_snd->streams[0];
	struct virtio_sound_chmap *chmap;
	struct virtio_snd_hdr *ret = iov[1].iov_base;
	int s, c = 0, i = 0, ret_len;

	if (n != 3) {
		pr_err("%s: invalid seg num %d!\n", __func__, n);
		return 0;
	}
	if ((info->start_id + info->count) > virt_snd->chmap_cnt) {
		pr_err("%s: invalid chmap, start %d, count = %d!\n", __func__,
			info->start_id, info->count);
		ret->code = VIRTIO_SND_S_BAD_MSG;
		return (int)iov[1].iov_len;
	}

	ret_len = info->count * sizeof(struct virtio_snd_chmap_info);
	if (ret_len > iov[2].iov_len) {
		pr_err("%s: too small buffer %d, required %d!\n", __func__,
			iov[2].iov_len, ret_len);
		ret->code = VIRTIO_SND_S_BAD_MSG;
		return (int)iov[1].iov_len;
	}

	for(s = 0; s < virt_snd->stream_cnt; s++) {
		if (info->start_id >= i && info->start_id < i + virt_snd->streams[s]->chmap_cnt) {
			c = info->start_id - i;
			stream = virt_snd->streams[s];
			break;
		}
		i += virt_snd->streams[s]->chmap_cnt;
	}

	for (i = 0; i < info->count; i++) {
		chmap = stream->chmaps[c];
		chmap_info[i].hdr.hda_fn_nid = stream->hda_fn_nid;
		chmap_info[i].direction = stream->dir;
		chmap_info[i].channels = chmap->channels;
		memcpy(chmap_info[i].positions, chmap->positions, VIRTIO_SND_CHMAP_MAX_SIZE);

		if (++c >= stream->chmap_cnt) {
			if(++s >= virt_snd->stream_cnt)
				break;
			stream = virt_snd->streams[s];
			c = 0;
		}
	}

	ret->code = VIRTIO_SND_S_OK;
	return ret_len + (int)iov[1].iov_len;
}

static int
virtio_sound_set_access(snd_ctl_elem_info_t *ctl)
{
	return (snd_ctl_elem_info_is_readable(ctl) ? 1 << VIRTIO_SND_CTL_ACCESS_READ : 0) |
		(snd_ctl_elem_info_is_writable(ctl) ? 1 << VIRTIO_SND_CTL_ACCESS_WRITE : 0) |
		(snd_ctl_elem_info_is_volatile(ctl) ? 1 << VIRTIO_SND_CTL_ACCESS_VOLATILE : 0) |
		(snd_ctl_elem_info_is_inactive(ctl) ? 1 << VIRTIO_SND_CTL_ACCESS_VOLATILE : 0) |
		(snd_ctl_elem_info_is_tlv_readable(ctl) ? 1 << VIRTIO_SND_CTL_ACCESS_TLV_READ : 0) |
		(snd_ctl_elem_info_is_tlv_writable(ctl) ? 1 << VIRTIO_SND_CTL_ACCESS_TLV_WRITE : 0) |
		(snd_ctl_elem_info_is_tlv_commandable(ctl) ? 1 << VIRTIO_SND_CTL_ACCESS_TLV_COMMAND : 0);
}

static int
virtio_sound_r_ctl_info(struct virtio_sound *virt_snd, struct iovec *iov, uint8_t n)
{
	struct virtio_snd_query_info *info = (struct virtio_snd_query_info *)iov[0].iov_base;
	struct virtio_snd_ctl_info *ctl_info = iov[2].iov_base;
	snd_hctl_elem_t *elem;
	snd_ctl_elem_info_t *ctl;
	struct virtio_snd_hdr *ret = iov[1].iov_base;
	int c = 0, i = 0, ret_len;

	if (n != 3) {
		pr_err("%s: invalid seg num %d!\n", __func__, n);
		return 0;
	}
	if ((info->start_id + info->count) > virt_snd->ctl_cnt) {
		pr_err("%s: invalid kcontrol, start %d, count = %d!\n", __func__,
			info->start_id, info->count);
		ret->code = VIRTIO_SND_S_BAD_MSG;
		return (int)iov[1].iov_len;
	}
	ret_len = info->count * sizeof(struct virtio_snd_ctl_info);
	if (ret_len > iov[2].iov_len) {
		pr_err("%s: too small buffer %d, required %d!\n", __func__,
			iov[2].iov_len, ret_len);
		ret->code = VIRTIO_SND_S_BAD_MSG;
		return (int)iov[1].iov_len;
	}

	snd_ctl_elem_info_alloca(&ctl);
	c = info->start_id;
	for (i = 0; i < info->count; i++) {
		elem = virt_snd->ctls[c]->elem;
		if (snd_hctl_elem_info(elem, ctl) < 0) {
			pr_err("%s: find elem info fail, identifier is %s!\n", __func__,
				snd_hctl_elem_get_name(virt_snd->ctls[c]->elem));
			ret->code = VIRTIO_SND_S_BAD_MSG;
			return (int)iov[1].iov_len;
		}
		ctl_info[i].type = virtio_sound_s2v_type[(int)snd_ctl_elem_info_get_type(ctl)];
		ctl_info[i].access = virtio_sound_set_access(ctl);
		ctl_info[i].count = snd_ctl_elem_info_get_count(ctl);
		ctl_info[i].index = snd_ctl_elem_info_get_index(ctl);
		memcpy(ctl_info[i].name, snd_ctl_elem_info_get_name(ctl), 44);

		switch (ctl_info[i].type) {
			case VIRTIO_SND_CTL_TYPE_INTEGER:
				ctl_info[i].value.integer.min = snd_ctl_elem_info_get_min(ctl);
				ctl_info[i].value.integer.max = snd_ctl_elem_info_get_max(ctl);
				ctl_info[i].value.integer.step = snd_ctl_elem_info_get_step(ctl);
				break;

			case VIRTIO_SND_CTL_TYPE_INTEGER64:
				ctl_info[i].value.integer64.min = snd_ctl_elem_info_get_min64(ctl);
				ctl_info[i].value.integer64.max = snd_ctl_elem_info_get_max64(ctl);
				ctl_info[i].value.integer64.step = snd_ctl_elem_info_get_step64(ctl);
				break;

			case VIRTIO_SND_CTL_TYPE_ENUMERATED:
				ctl_info[i].value.enumerated.items = snd_ctl_elem_info_get_items(ctl);
				break;

			default:
				break;
		}
		c++;
	}

	ret->code = VIRTIO_SND_S_OK;
	return ret_len + (int)iov[1].iov_len;
}

static int
virtio_sound_r_ctl_enum_items(struct virtio_sound *virt_snd, struct iovec *iov, uint8_t n)
{
	struct virtio_snd_ctl_hdr *info = (struct virtio_snd_ctl_hdr *)iov[0].iov_base;;
	snd_hctl_elem_t *elem;
	snd_ctl_elem_info_t *ctl;
	struct virtio_snd_hdr *ret = iov[1].iov_base;
	int items, i;

	if (n != 3) {
		pr_err("%s: invalid seg num %d!\n", __func__, n);
		return 0;
	}
	if (virt_snd->ctl_cnt <= info->control_id) {
		pr_err("%s: invalid ctrl, control_id %d!\n", __func__, info->control_id);
		ret->code = VIRTIO_SND_S_BAD_MSG;
		return (int)iov[1].iov_len;
	}

	snd_ctl_elem_info_alloca(&ctl);
	elem = virt_snd->ctls[info->control_id]->elem;
	if (snd_hctl_elem_info(elem, ctl) < 0) {
		pr_err("%s: get elem info fail, identifier is %s!\n", __func__,
			snd_hctl_elem_get_name(virt_snd->ctls[info->control_id]->elem));
		ret->code = VIRTIO_SND_S_BAD_MSG;
		return (int)iov[1].iov_len;
	}
	if (snd_ctl_elem_info_get_type(ctl) != SND_CTL_ELEM_TYPE_ENUMERATED) {
		pr_err("%s: elem is not enumerated, identifier is %s!\n", __func__,
			snd_hctl_elem_get_name(virt_snd->ctls[info->control_id]->elem));
		ret->code = VIRTIO_SND_S_BAD_MSG;
		return (int)iov[1].iov_len;
	}
	items = snd_ctl_elem_info_get_items(ctl);
	if (items != (iov[2].iov_len / sizeof(struct virtio_snd_ctl_enum_item))) {
		pr_err("%s: %s item count(%d) err!\n", __func__,
			snd_hctl_elem_get_name(virt_snd->ctls[info->control_id]->elem), items);
		ret->code = VIRTIO_SND_S_BAD_MSG;
		return (int)iov[1].iov_len;
	}
	for(i = 0; i < items; i++) {
		snd_ctl_elem_info_set_item(ctl, i);
		if (snd_hctl_elem_info(elem, ctl) < 0) {
			pr_err("%s: %s get item %d err!\n", __func__,
				snd_hctl_elem_get_name(virt_snd->ctls[info->control_id]->elem), i);
			ret->code = VIRTIO_SND_S_BAD_MSG;
			return (int)iov[1].iov_len;
		}
		strncpy((iov[2].iov_base + sizeof(struct virtio_snd_ctl_enum_item) * i),
			snd_ctl_elem_info_get_item_name(ctl), sizeof(struct virtio_snd_ctl_enum_item));
	}

	ret->code = VIRTIO_SND_S_OK;
	return (int)iov[2].iov_len + (int)iov[1].iov_len;
}

static int
virtio_sound_r_ctl_read(struct virtio_sound *virt_snd, struct iovec *iov, uint8_t n)
{
	struct virtio_snd_ctl_hdr *info = (struct virtio_snd_ctl_hdr *)iov[0].iov_base;;
	snd_ctl_elem_value_t *ctl_value;
	snd_hctl_elem_t *elem;
	snd_ctl_elem_info_t *ctl;
	struct virtio_snd_hdr *ret = iov[1].iov_base;

	if (n != 2) {
		pr_err("%s: invalid seg num %d!\n", __func__, n);
		return 0;
	}
	if (virt_snd->ctl_cnt <= info->control_id) {
		pr_err("%s: invalid ctrl, control_id %d!\n", __func__, info->control_id);
		ret->code = VIRTIO_SND_S_BAD_MSG;
		return (int)iov[1].iov_len;
	}

	elem = virt_snd->ctls[info->control_id]->elem;
	snd_ctl_elem_info_alloca(&ctl);
	if (snd_hctl_elem_info(elem, ctl) < 0 || snd_ctl_elem_info_is_readable(ctl) == 0) {
		pr_err("%s: access check fail, identifier is %s!\n", __func__,
			snd_hctl_elem_get_name(virt_snd->ctls[info->control_id]->elem));
		ret->code = VIRTIO_SND_S_BAD_MSG;
		return (int)iov[1].iov_len;
	}
	snd_ctl_elem_value_alloca(&ctl_value);
	if (snd_hctl_elem_read(elem, ctl_value) < 0) {
		pr_err("%s: read %s value fail!\n", __func__,
			snd_hctl_elem_get_name(virt_snd->ctls[info->control_id]->elem));
		ret->code = VIRTIO_SND_S_BAD_MSG;
		return (int)iov[1].iov_len;
	}
	memcpy(iov[1].iov_base + sizeof(struct virtio_snd_hdr), snd_ctl_elem_value_get_bytes(ctl_value),
		iov[1].iov_len - sizeof(struct virtio_snd_hdr));

	ret->code = VIRTIO_SND_S_OK;
	return (int)iov[1].iov_len;
}

static int
virtio_sound_r_ctl_write(struct virtio_sound *virt_snd, struct iovec *iov, uint8_t n)
{
	struct virtio_snd_ctl_hdr *info = (struct virtio_snd_ctl_hdr *)iov[0].iov_base;
	struct virtio_snd_ctl_value *val =
		(struct virtio_snd_ctl_value *)(iov[0].iov_base + sizeof(struct virtio_snd_ctl_hdr));
	struct virtio_snd_hdr *ret = iov[1].iov_base;
	snd_ctl_elem_value_t *ctl_value;
	snd_hctl_elem_t *elem;
	snd_ctl_elem_info_t *ctl;

	if (n != 2) {
		pr_err("%s: invalid seg num %d!\n", __func__, n);
		return 0;
	}
	if (virt_snd->ctl_cnt <= info->control_id) {
		pr_err("%s: invalid ctrl, control_id %d!\n", __func__, info->control_id);
		ret->code = VIRTIO_SND_S_BAD_MSG;
		return (int)iov[1].iov_len;
	}

	elem = virt_snd->ctls[info->control_id]->elem;
	snd_ctl_elem_info_alloca(&ctl);
	if (snd_hctl_elem_info(elem, ctl) < 0 || snd_ctl_elem_info_is_writable(ctl) == 0) {
		pr_err("%s: access check fail, identifier is %s!\n", __func__,
			snd_hctl_elem_get_name(virt_snd->ctls[info->control_id]->elem));
		ret->code = VIRTIO_SND_S_BAD_MSG;
		return (int)iov[1].iov_len;
	}
	snd_ctl_elem_value_alloca(&ctl_value);
	if (snd_hctl_elem_read(elem, ctl_value) < 0) {
		pr_err("%s: read %s value fail!\n", __func__,
			snd_hctl_elem_get_name(virt_snd->ctls[info->control_id]->elem));
		ret->code = VIRTIO_SND_S_BAD_MSG;
		return (int)iov[1].iov_len;
	}
	snd_ctl_elem_set_bytes(ctl_value, val, sizeof(struct virtio_snd_ctl_value));
	if (snd_hctl_elem_write(elem, ctl_value) < 0) {
		pr_err("%s: write %s value fail!\n", __func__,
			snd_hctl_elem_get_name(virt_snd->ctls[info->control_id]->elem));
		ret->code = VIRTIO_SND_S_BAD_MSG;
		return (int)iov[1].iov_len;
	}

	ret->code = VIRTIO_SND_S_OK;
	return (int)iov[1].iov_len;

}

static int
virtio_sound_r_ctl_tlv_read(struct virtio_sound *virt_snd, struct iovec *iov, uint8_t n)
{
	struct virtio_snd_ctl_hdr *info = (struct virtio_snd_ctl_hdr *)iov[0].iov_base;
	struct virtio_snd_hdr *ret = iov[1].iov_base;
	snd_hctl_elem_t *elem;
	snd_ctl_elem_info_t *ctl;

	if (n != 3) {
		pr_err("%s: invalid seg num %d!\n", __func__, n);
		return 0;
	}
	if (virt_snd->ctl_cnt <= info->control_id) {
		pr_err("%s: invalid ctrl, control_id %d!\n", __func__, info->control_id);
		ret->code = VIRTIO_SND_S_BAD_MSG;
		return (int)iov[1].iov_len;
	}

	elem = virt_snd->ctls[info->control_id]->elem;
	snd_ctl_elem_info_alloca(&ctl);
	if (snd_hctl_elem_info(elem, ctl) < 0 || snd_ctl_elem_info_is_tlv_readable(ctl) == 0) {
		pr_err("%s: access check fail, identifier is %s!\n", __func__,
			snd_hctl_elem_get_name(virt_snd->ctls[info->control_id]->elem));
		ret->code = VIRTIO_SND_S_BAD_MSG;
		return (int)iov[1].iov_len;
	}
	if (snd_hctl_elem_tlv_read(elem, iov[2].iov_base, iov[2].iov_len / sizeof(int)) < 0) {
		pr_err("%s: read %s tlv fail!\n", __func__,
			snd_hctl_elem_get_name(virt_snd->ctls[info->control_id]->elem));
		ret->code = VIRTIO_SND_S_BAD_MSG;
		return (int)iov[1].iov_len;
	}

	ret->code = VIRTIO_SND_S_OK;
	return iov[2].iov_len + iov[1].iov_len;
}

static int
virtio_sound_r_ctl_tlv_write(struct virtio_sound *virt_snd, struct iovec *iov, uint8_t n)
{
	struct virtio_snd_ctl_hdr *info = (struct virtio_snd_ctl_hdr *)iov[0].iov_base;
	struct virtio_snd_hdr *ret = iov[2].iov_base;
	snd_hctl_elem_t *elem;
	snd_ctl_elem_info_t *ctl;
	uint32_t *tlv = (uint32_t *)iov[1].iov_base;

	if (n != 3) {
		pr_err("%s: invalid seg num %d!\n", __func__, n);
		return 0;
	}
	if (virt_snd->ctl_cnt <= info->control_id) {
		pr_err("%s: invalid ctrl, control_id %d!\n", __func__, info->control_id);
		ret->code = VIRTIO_SND_S_BAD_MSG;
		return (int)iov[2].iov_len;
	}

	elem = virt_snd->ctls[info->control_id]->elem;
	snd_ctl_elem_info_alloca(&ctl);
	if (snd_hctl_elem_info(elem, ctl) < 0 || snd_ctl_elem_info_is_tlv_writable(ctl) == 0) {
		pr_err("%s: access check fail, identifier is %s!\n", __func__,
			snd_hctl_elem_get_name(virt_snd->ctls[info->control_id]->elem));
		ret->code = VIRTIO_SND_S_BAD_MSG;
		return (int)iov[2].iov_len;
	}
	if (snd_hctl_elem_tlv_write(elem, tlv) < 0) {
		pr_err("%s: write %s tlv fail!\n", __func__,
			snd_hctl_elem_get_name(virt_snd->ctls[info->control_id]->elem));
		ret->code = VIRTIO_SND_S_BAD_MSG;
		return (int)iov[2].iov_len;
	}

	ret->code = VIRTIO_SND_S_OK;
	return (int)iov[2].iov_len;
}

static int
virtio_sound_r_ctl_tlv_command(struct virtio_sound *virt_snd, struct iovec *iov, uint8_t n)
{
	struct virtio_snd_ctl_hdr *info = (struct virtio_snd_ctl_hdr *)iov[0].iov_base;
	struct virtio_snd_hdr *ret = iov[2].iov_base;
	snd_hctl_elem_t *elem;
	snd_ctl_elem_info_t *ctl;
	uint32_t *tlv = (uint32_t *)iov[1].iov_base;

	if (n != 3) {
		pr_err("%s: invalid seg num %d!\n", __func__, n);
		return 0;
	}
	if (virt_snd->ctl_cnt <= info->control_id) {
		pr_err("%s: invalid ctrl, control_id %d!\n", __func__, info->control_id);
		ret->code = VIRTIO_SND_S_BAD_MSG;
		return (int)iov[2].iov_len;
	}

	elem = virt_snd->ctls[info->control_id]->elem;
	snd_ctl_elem_info_alloca(&ctl);
	if (snd_hctl_elem_info(elem, ctl) < 0 || snd_ctl_elem_info_is_tlv_commandable(ctl) == 0) {
		pr_err("%s: access check fail, identifier is %s!\n", __func__,
			snd_hctl_elem_get_name(virt_snd->ctls[info->control_id]->elem));
		ret->code = VIRTIO_SND_S_BAD_MSG;
		return (int)iov[2].iov_len;
	}
	if (snd_hctl_elem_tlv_command(elem, tlv) < 0) {
		pr_err("%s: %s tlv command fail!\n", __func__,
			snd_hctl_elem_get_name(virt_snd->ctls[info->control_id]->elem));
		ret->code = VIRTIO_SND_S_BAD_MSG;
		return (int)iov[2].iov_len;
	}

	ret->code = VIRTIO_SND_S_OK;
	return (int)iov[2].iov_len;
}


static int
virtio_sound_r_pcm_pending(struct virtio_sound_pcm *stream, struct virtio_sound_ctrl_msg *ctrl, int *timeout)
{
	int i, err = 0;

	if (stream->status != VIRTIO_SND_BE_PENDING) {
		pr_err("%s: status(%d) err\n", __func__, stream->status);
		err = -1;
		goto end;
	}
	*timeout = 1000 / (stream->param.rrate / (stream->param.period_bytes
		/ virtio_sound_get_frame_size(stream)));
	for (i = 0; i < 2; i++) {
		if (virtio_sound_xfer(stream) < 0) {
			pr_err("%s: stream %d xfer error!\n", __func__, stream->id);
			err = -1;
			goto end;
		}
	}
	if (snd_pcm_start(stream->handle) < 0) {
		pr_err("%s: stream %s start error!\n", __func__, stream->dev_name);
		err = -1;
		goto end;
	}
	stream->status = VIRTIO_SND_BE_STARTING;
end:
	free(ctrl->iov[0].iov_base);
	free(ctrl->iov);
	return err;
}

static int
virtio_sound_process_ctrl_cmds(struct virtio_sound_pcm *stream, int *timeout)
{
	struct virtio_snd_hdr *hdr = NULL;
	struct virtio_sound_ctrl_msg ctrl;
	int size, cnt = 0, err = 0;

	/* loop read control events */
	do {
		size = read(stream->ctrl_fd[0], (char *)&ctrl + cnt, sizeof(ctrl) - cnt);
		cnt += size;
	} while (size > 0 && cnt < sizeof(ctrl));
	if (cnt != sizeof(ctrl)) {
		pr_err("%s: read data size(%d) err\n", __func__, cnt);
		return -1;
	}
	*timeout = -1;
	hdr = (struct virtio_snd_hdr *)ctrl.iov[0].iov_base;
	if (print_ctrl_msg > 1)
		pr_err("%s: deal ctrl msg 0x%x\n", __func__, hdr->code);
	switch (hdr->code) {
		case VIRTIO_SND_R_PCM_SET_PARAMS:
			err = virtio_sound_r_set_params(stream, &ctrl);
			break;
		case VIRTIO_SND_R_PCM_PREPARE:
			err = virtio_sound_r_pcm_prepare(stream, &ctrl);
			break;
		case VIRTIO_SND_R_PCM_RELEASE:
			err = virtio_sound_r_pcm_release(stream, &ctrl);
			break;
		case VIRTIO_SND_R_PCM_START:
			err = virtio_sound_r_pcm_start(stream, &ctrl, timeout);
			break;
		case VIRTIO_SND_R_PCM_STOP:
			err = virtio_sound_r_pcm_stop(stream, &ctrl);
			break;
		case VIRTIO_SND_R_PCM_PENDING:
			err = virtio_sound_r_pcm_pending(stream, &ctrl, timeout);
			break;
		default:
			pr_err("%s: unsupported request 0x%X!\n", __func__, hdr->code);
			err = -1;
			break;
	}
	return err;
}

static int
virtio_sound_send_pending_msg(struct virtio_sound_pcm *stream)
{
	struct virtio_snd_query_info *info;
	struct iovec *iov;
	struct virtio_sound_ctrl_msg ctrl;
	int size;

	info = malloc(sizeof(struct virtio_snd_query_info));
	iov = malloc(sizeof(struct iovec));
	if (info == NULL || iov == NULL) {
		pr_err("%s: malloc iov(%p) & info(%p) fail!\n", __func__, iov, info);
		if (info)
			free(info);
		if (iov)
			free(iov);
		return -1;
	}
	info->hdr.code = VIRTIO_SND_R_PCM_PENDING;
	iov->iov_base = info;
	ctrl.iov = iov;
	ctrl.idx = 0;

	size = write(stream->ctrl_fd[1], (char *)&ctrl, sizeof(ctrl));
	if (size != sizeof(ctrl)) {
		pr_err("%s: write error %d!\n", __func__, size);
		free(info);
		free(iov);
		return -1;
	}
	return 0;
}

/* Send stream control message to it’s transfer thread. */
static int
virtio_sound_send_ctrl(struct virtio_sound *virt_snd, struct iovec *iov, uint8_t n, uint16_t idx)
{
	struct virtio_snd_pcm_hdr *pcm = (struct virtio_snd_pcm_hdr *)iov[0].iov_base;
	struct virtio_sound_ctrl_msg ctrl;
	struct virtio_snd_hdr *ret = iov[1].iov_base;
	struct virtio_snd_query_info *info = NULL;
	int s, size;

	ctrl.idx = idx;

	if (n != 2) {
		pr_err("%s: invalid seg num %d!\n", __func__, n);
		return -1;
	}
	if ((s = pcm->stream_id) >= virt_snd->stream_cnt) {
		pr_err("%s: invalid stream %d!\n", __func__, s);
		ret->code = VIRTIO_SND_S_BAD_MSG;
		virtio_sound_release_ctrl_msg(virt_snd, &ctrl, iov[1].iov_len);
		return -1;
	}

	ctrl.iov = malloc(sizeof(struct iovec) * 2);
	if (ctrl.iov == NULL) {
		pr_err("%s: malloc iov fail\n", __func__);
		virtio_sound_release_ctrl_msg(virt_snd, &ctrl, iov[1].iov_len);
		return -1;
	}
	memcpy(ctrl.iov, iov, sizeof(struct iovec) * 2);

	if (print_ctrl_msg > 1) {
		info = (struct virtio_snd_query_info *)iov[0].iov_base;
		pr_err("%s: send ctrl msg 0x%x\n", __func__, info->hdr.code);
	}

	size = write(virt_snd->streams[s]->ctrl_fd[1], (char *)&ctrl, sizeof(ctrl));
	if (size != sizeof(ctrl)) {
		pr_err("%s: write error %d!\n", __func__, size);
		free(ctrl.iov);
		return -1;
	}
	return 0;
}

static void
virtio_sound_notify_ctl(void *vdev, struct virtio_vq_info *vq)
{
	struct virtio_sound *virt_snd = vdev;
	struct virtio_snd_hdr *hdr = NULL;
	struct iovec iov[VIRTIO_SOUND_CTL_SEGS];
	int n, ret_len = 0;
	uint16_t idx;

	while (vq_has_descs(vq)) {
		pthread_mutex_lock(&vq->mtx);
		n = vq_getchain(vq, &idx, iov, VIRTIO_SOUND_CTL_SEGS, NULL);
		pthread_mutex_unlock(&vq->mtx);
		if (n <= 0) {
			pr_err("%s: fail to getchain!\n", __func__);
			return;
		}

		hdr = (struct virtio_snd_hdr *)iov[0].iov_base;
		if (print_ctrl_msg > 0)
				pr_err("Receive ctrl msg: 0x%x\n", hdr->code);
		switch (hdr->code) {
			case VIRTIO_SND_R_JACK_INFO:
				ret_len = virtio_sound_r_jack_info (virt_snd, iov, n);
				break;
			// case VIRTIO_SND_R_JACK_REMAP:
			//	break;
			case VIRTIO_SND_R_PCM_INFO:
				ret_len = virtio_sound_r_pcm_info(virt_snd, iov, n);
				break;
			case VIRTIO_SND_R_PCM_SET_PARAMS:
			case VIRTIO_SND_R_PCM_PREPARE:
			case VIRTIO_SND_R_PCM_RELEASE:
			case VIRTIO_SND_R_PCM_START:
			case VIRTIO_SND_R_PCM_STOP:
				ret_len = virtio_sound_send_ctrl(virt_snd, iov, n, idx);
				break;
			case VIRTIO_SND_R_CHMAP_INFO:
				ret_len = virtio_sound_r_chmap_info(virt_snd, iov, n);
				break;
			case VIRTIO_SND_R_CTL_INFO:
				ret_len = virtio_sound_r_ctl_info(virt_snd, iov, n);
				break;
			case VIRTIO_SND_R_CTL_ENUM_ITEMS:
				ret_len = virtio_sound_r_ctl_enum_items(virt_snd, iov, n);
				break;
			case VIRTIO_SND_R_CTL_READ:
				ret_len = virtio_sound_r_ctl_read(virt_snd, iov, n);
				break;
			case VIRTIO_SND_R_CTL_WRITE:
				ret_len = virtio_sound_r_ctl_write(virt_snd, iov, n);
				break;
			case VIRTIO_SND_R_CTL_TLV_READ:
				ret_len = virtio_sound_r_ctl_tlv_read(virt_snd, iov, n);
				break;
			case VIRTIO_SND_R_CTL_TLV_WRITE:
				ret_len = virtio_sound_r_ctl_tlv_write(virt_snd, iov, n);
				break;
			case VIRTIO_SND_R_CTL_TLV_COMMAND:
				ret_len = virtio_sound_r_ctl_tlv_command(virt_snd, iov, n);
				break;
			default:
				pr_err("%s: unsupported request 0x%X!\n", __func__, n);
				break;
		}
		if (ret_len > 0) {
			pthread_mutex_lock(&vq->mtx);
			vq_relchain(vq, idx, ret_len);
			pthread_mutex_unlock(&vq->mtx);
		}
	}
	pthread_mutex_lock(&vq->mtx);
	vq_endchains(vq, 1);
	pthread_mutex_unlock(&vq->mtx);
}

static void
virtio_sound_notify_event(void *vdev, struct virtio_vq_info *vq)
{
}

/*init*/
static void
virtio_sound_cfg_init(struct virtio_sound *virt_snd)
{
	virt_snd->snd_cfg.streams = virt_snd->stream_cnt;
	virt_snd->snd_cfg.jacks = virt_snd->jack_cnt;
	virt_snd->snd_cfg.chmaps = virt_snd->chmap_cnt;
	virt_snd->snd_cfg.controls = virt_snd->ctl_cnt;
}

static bool
virtio_sound_format_support(snd_pcm_t *handle, uint32_t format)
{
	snd_pcm_hw_params_t *hwparams;

	snd_pcm_hw_params_alloca(&hwparams);
	if(snd_pcm_hw_params_any(handle, hwparams) < 0) {
		pr_err("%s: no configurations available!\n", __func__);
		return false;
	}
	return (snd_pcm_hw_params_test_format(handle, hwparams, format) == 0);
}

static bool
virtio_sound_rate_support(snd_pcm_t *handle, uint32_t rate, int dir)
{
	snd_pcm_hw_params_t *hwparams;
	uint32_t rrate;

	snd_pcm_hw_params_alloca(&hwparams);
	if(snd_pcm_hw_params_any(handle, hwparams) < 0) {
		pr_err("%s: no configurations available!\n", __func__);
		return false;
	}
	rrate = rate;
	return (snd_pcm_hw_params_set_rate_near(handle, hwparams, &rrate, &dir) == 0
		&& rrate == rate);
}

static int
virtio_sound_pcm_fake_param_init(struct virtio_sound_pcm *stream, int dir, char *name, int fn_id, char *param)
{
	struct str2mask {
		char str[8];
		int val;
	};
	struct str2mask format[]={
		{"S16", VIRTIO_SND_PCM_FMT_S16},
		{"U16", VIRTIO_SND_PCM_FMT_U16},
		{"S32", VIRTIO_SND_PCM_FMT_S32},
		{"U32", VIRTIO_SND_PCM_FMT_U32},
	};
	struct str2mask rate[]={
		{"16000", VIRTIO_SND_PCM_RATE_16000},
		{"48000", VIRTIO_SND_PCM_RATE_48000},
		{"96000", VIRTIO_SND_PCM_RATE_96000},
		{"192000", VIRTIO_SND_PCM_RATE_192000},
	};

	char *arg, *val, *cpy;
	int i, len;

	stream->dir = dir;
	stream->hda_fn_nid = fn_id;

	stream->param.formats = 1 << VIRTIO_SND_PCM_FMT_S16;
	stream->param.rates = 1 << VIRTIO_SND_PCM_RATE_48000;
	stream->param.features = (1 << VIRTIO_SND_PCM_F_EVT_XRUNS);
	stream->param.channels_min = 2;
	stream->param.channels_max = 2;

	do {
		val = strsep(&param, ",");
		cpy = strdup(val);
		arg = strsep(&val, "=");
		if (val == NULL || arg == NULL) {
			pr_err("%s: %s param err\n", __func__, cpy);
			free(cpy);
			continue;
		}

		if (strcmp(arg, "format") == 0) {
			len = sizeof(format) / sizeof(struct str2mask);
			for (i = 0; i < len; i++) {
				if (strcmp(val, format[i].str) == 0) {
					stream->param.formats = 1 << format[i].val;
					break;
				}
			}
			if (i == len)
				pr_err("%s: %s param err\n", __func__, cpy);
		} else if (strcmp(arg, "rate") == 0) {
			len = sizeof(rate) / sizeof(struct str2mask);
			for (i = 0; i < len; i++) {
				if (strcmp(val, rate[i].str) == 0) {
					stream->param.rates = 1 << rate[i].val;
					break;
				}
			}
			if (i == len)
				pr_err("%s: %s param err\n", __func__, cpy);
		} else if (strcmp(arg, "channels_min") == 0) {
			stream->param.channels_min = atoi(val);
		} else if (strcmp(arg, "channels_max") == 0) {
			stream->param.channels_max = atoi(val);
		} else {
			pr_err("%s: %s param err\n", __func__, cpy);
		}
		free(cpy);
	} while (param != NULL);

	return 0;
}

static int
virtio_sound_pcm_param_init(struct virtio_sound_pcm *stream, int dir, char *name, int fn_id)
{
	snd_pcm_hw_params_t *hwparams;
	snd_pcm_chmap_query_t **chmaps;
	uint32_t channels_min, channels_max, i, j;

	stream->dir = dir;
	stream->hda_fn_nid = fn_id;

	if (snd_pcm_open(&stream->handle, stream->dev_name,
		stream->dir, SND_PCM_NONBLOCK) < 0 || stream->handle == NULL) {
		pr_err("%s: stream %s open fail!\n", __func__, stream->dev_name);
		return -1;
	}

	for (i = 0; i < ARRAY_SIZE(virtio_sound_v2s_format); i++) {
		if (virtio_sound_format_support(stream->handle, virtio_sound_v2s_format[i]))
			stream->param.formats |= (1 << i);
	}
	for (i = 0; i < ARRAY_SIZE(virtio_sound_t_rate); i++) {
		if(virtio_sound_rate_support(stream->handle, virtio_sound_t_rate[i], dir))
			stream->param.rates |= (1 << i);
	}
	if (stream->param.rates == 0 || stream->param.formats == 0) {
		pr_err("%s: get param fail rates 0x%lx formats 0x%lx!\n", __func__,
			stream->param.rates, stream->param.formats);
		return -1;
	}
	stream->param.features = (1 << VIRTIO_SND_PCM_F_EVT_XRUNS);
	snd_pcm_hw_params_alloca(&hwparams);
	if(snd_pcm_hw_params_any(stream->handle, hwparams) < 0) {
		pr_err("%s: no configurations available!\n", __func__);
		return -1;
	}
	if (snd_pcm_hw_params_get_channels_min(hwparams, &channels_min) < 0 ||
		snd_pcm_hw_params_get_channels_max(hwparams, &channels_max) < 0) {
			pr_err("%s: get channel info fail!\n", __func__);
			return -1;
	}
	stream->param.channels_min = channels_min;
	stream->param.channels_max = channels_max;

	i = 0;
	chmaps = snd_pcm_query_chmaps(stream->handle);
	while (chmaps != NULL && chmaps[i] != NULL) {
		stream->chmaps[i] = malloc(sizeof(struct virtio_sound_chmap));
		if (stream->chmaps[i] == NULL) {
			pr_err("%s: malloc chmap buffer fail!\n", __func__);
			return -1;
		}
		stream->chmaps[i]->channels = chmaps[i]->map.channels;
		for (j = 0; j < chmaps[i]->map.channels; j++)
			stream->chmaps[i]->positions[j] = virtio_sound_s2v_chmap[chmaps[i]->map.pos[j]];
		stream->chmap_cnt++;
		i++;
	}
	snd_pcm_free_chmaps(chmaps);
	STAILQ_INIT(&stream->head);

	if (snd_pcm_close(stream->handle) < 0) {
		pr_err("%s: stream %s close error!\n", __func__, stream->dev_name);
		return -1;
	}
	stream->handle = NULL;

	return 0;
}

static int
virtio_sound_pcm_init(struct virtio_sound *virt_snd, char *device, char *hda_fn_nid, char *param, int dir)
{
	struct virtio_sound_pcm *stream;
	int err;

	if (device == NULL || hda_fn_nid == NULL) {
		pr_err("%s: pcm stream parameter error!\n", __func__);
		return -1;
	}
	if (virt_snd->stream_cnt >= VIRTIO_SOUND_STREAMS) {
		pr_err("%s: too many audio streams(%d)!\n", __func__, VIRTIO_SOUND_VQ_NUM);
		return -1;
	}
	stream = malloc(sizeof(struct virtio_sound_pcm));
	if (stream == NULL) {
		pr_err("%s: malloc data node fail!\n", __func__);
		return -1;
	}
	memset(stream, 0, sizeof(struct virtio_sound_pcm));

	stream->id = virt_snd->stream_cnt;
	strncpy(stream->dev_name, device, VIRTIO_SOUND_DEVICE_NAME);
	stream->hda_fn_nid = atoi(hda_fn_nid);
	stream->snd_card = virt_snd;
	if (param != NULL) {
		if (virtio_sound_pcm_fake_param_init(stream, dir, stream->dev_name, stream->hda_fn_nid, param) == 0) {
			virt_snd->streams[virt_snd->stream_cnt] = stream;
			virt_snd->stream_cnt++;
			virt_snd->chmap_cnt += stream->chmap_cnt;
		} else {
			pr_err("%s: stream %s param init error!\n", __func__, stream->dev_name);
			free(stream);
			return -1;
		}
	} else {
		if (virtio_sound_pcm_param_init(stream, dir, stream->dev_name, stream->hda_fn_nid) == 0) {
			virt_snd->streams[virt_snd->stream_cnt] = stream;
			virt_snd->stream_cnt++;
			virt_snd->chmap_cnt += stream->chmap_cnt;
		} else {
			pr_err("%s: stream %s param init error!\n", __func__, stream->dev_name);
			free(stream);
			return -1;
		}
	}
	stream->dbg_fd = NULL;
	err = virtio_sound_create_pcm_thread(stream);
	if (err) {
		/* Stream will be released when deinit virtio sound module. */
		pr_err("%s: stream create thread failed!\n", __func__);
		return -1;
	}
	err = pthread_mutex_init(&stream->mtx, NULL);
	if (err) {
		pr_err("%s: mutex init failed with error %d!\n", __func__, err);
		return -1;
	}
	err = pthread_mutex_init(&stream->ctl_mtx, NULL);
	if (err) {
		pr_err("%s: mutex init failed with error %d!\n", __func__, err);
		pthread_mutex_destroy(&stream->mtx);
		return -1;
	}
	return 0;
}

static uint32_t
virtio_snd_jack_parse(char *identifier)
{
	uint32_t location, device;

	if (strstr(identifier, "Dock")) {
		location = HDA_JACK_LOCATION_SEPARATE;
	} else if (strstr(identifier, "Internal")) {
		location = HDA_JACK_LOCATION_INTERNAL;
	} else if (strstr(identifier, "Rear")) {
		location = HDA_JACK_LOCATION_REAR;
	} else if (strstr(identifier, "Front")) {
		location = HDA_JACK_LOCATION_FRONT;
	} else {
		location = HDA_JACK_LOCATION_NONE;
	}

	if (strstr(identifier, "Line Out")) {
		device = HDA_JACK_LINE_OUT;
	} else if (strstr(identifier, "Line")) {
		device = HDA_JACK_LINE_IN;
	} else if (strstr(identifier, "Speaker")) {
		device = HDA_JACK_SPEAKER;
		location = HDA_JACK_LOCATION_INTERNAL;
	} else if (strstr(identifier, "Mic")) {
		device = HDA_JACK_MIC_IN;
	} else if (strstr(identifier, "CD")) {
		device = HDA_JACK_CD;
	} else if (strstr(identifier, "Headphone")) {
		device = HDA_JACK_HP_OUT;
	} else if (strstr(identifier, "Aux")) {
		device = HDA_JACK_AUX;
	} else if (strstr(identifier, "SPDIF In")) {
		device = HDA_JACK_SPDIF_IN;
	} else if (strstr(identifier, "Digital In")) {
		device = HDA_JACK_DIG_OTHER_IN;
	} else if (strstr(identifier, "SPDIF")) {
		device = HDA_JACK_SPDIF_OUT;
	} else if (strstr(identifier, "HDMI")) {
		device = HDA_JACK_DIG_OTHER_OUT;
		location = HDA_JACK_LOCATION_HDMI;
	} else {
		device = HDA_JACK_OTHER;
	}

	return (device << HDA_JACK_DEFREG_DEVICE_SHIFT) | (location << HDA_JACK_DEFREG_LOCATION_SHIFT);
}

static struct vbs_card*
virtio_sound_get_card(struct virtio_sound *virt_snd, char *card)
{
	int i, num;

	for (i = 0; i < virt_snd->card_cnt; i++) {
		if(strcmp(virt_snd->cards[i]->card, card) == 0) {
			return virt_snd->cards[i];
		}
	}
	if (virt_snd->card_cnt >= VIRTIO_SOUND_CARD) {
		pr_err("%s: too many cards %d!\n", __func__, virt_snd->card_cnt);
		return NULL;
	}
	num = virt_snd->card_cnt;
	virt_snd->cards[num] = malloc(sizeof(struct vbs_card));
	if (virt_snd->cards[num] == NULL) {
		pr_err("%s: malloc card node %d fail!\n", __func__, num);
		return NULL;
	}
	strncpy(virt_snd->cards[num]->card, card, VIRTIO_SOUND_CARD_NAME);
	if (snd_hctl_open(&virt_snd->cards[num]->handle, virt_snd->cards[num]->card, 0)) {
		pr_err("%s: hctl open fail, card %s!\n", __func__, virt_snd->cards[num]->card);
		return NULL;
	}
	if (snd_hctl_load(virt_snd->cards[num]->handle) < 0) {
		pr_err("%s: hctl load fail, card %s!\n", __func__, virt_snd->cards[num]->card);
		snd_hctl_close(virt_snd->cards[num]->handle);
		return NULL;
	}
	virt_snd->card_cnt++;
	return virt_snd->cards[num];
}

static int
virtio_sound_init_emu_ctl(struct virtio_sound *virt_snd, char *card_str, char *identifier)
{
	char *c, *cpy, *ident;

	if (strcmp(card_str, "emu") != 0) {
		return -1;
	}
	c = cpy = strdup(identifier);
	ident = strsep(&c, ":");
	if (strstr(ident, "Jack") != NULL) {
		virt_snd->jacks[virt_snd->jack_cnt] = malloc(sizeof(struct vbs_jack_elem));
		if (virt_snd->jacks[virt_snd->jack_cnt] == NULL) {
			pr_err("%s: malloc jack elem fail!\n", __func__);
			free(cpy);
			return -1;
		}
		virt_snd->jacks[virt_snd->jack_cnt]->identifier = malloc(VIRTIO_SOUND_IDENTIFIER);
		if (virt_snd->jacks[virt_snd->jack_cnt] == NULL) {
			pr_err("%s: malloc jack identifier fail!\n", __func__);
			free(virt_snd->jacks[virt_snd->jack_cnt]);
			free(cpy);
			return -1;
		}
		strncpy(virt_snd->jacks[virt_snd->jack_cnt]->identifier, ident, VIRTIO_SOUND_IDENTIFIER);
		virt_snd->jacks[virt_snd->jack_cnt]->elem = NULL;
		virt_snd->jacks[virt_snd->jack_cnt]->card = NULL;
		virt_snd->jacks[virt_snd->jack_cnt]->hda_reg_defconf = virtio_snd_jack_parse(ident);
		virt_snd->jacks[virt_snd->jack_cnt]->snd_card = virt_snd;
		virt_snd->jacks[virt_snd->jack_cnt]->id = virt_snd->jack_cnt;
		LIST_INSERT_HEAD(&jack_list_head, virt_snd->jacks[virt_snd->jack_cnt], jack_list);
		if (c != NULL && strcmp(c, "connect") == 0)
			virt_snd->jacks[virt_snd->jack_cnt]->connected = 1;
		else
			virt_snd->jacks[virt_snd->jack_cnt]->connected = 0;
		virt_snd->jack_cnt++;
	} else {
		pr_err("%s: emu card unsupport %s ctl elem!\n", __func__, ident);
	}
	free(cpy);
	return 0;
}

static int
virtio_sound_init_ctl_elem(struct virtio_sound *virt_snd, char *card_str, char *identifier)
{
	snd_ctl_elem_info_t *info;
	snd_hctl_elem_t *elem;

	struct vbs_card *card;
	char card_name[VIRTIO_SOUND_CARD_NAME];

	if(card_str == NULL || identifier == NULL) {
		pr_err("%s: kcontrol info unset!\n", __func__);
		return -1;
	}
	if (virtio_sound_init_emu_ctl(virt_snd, card_str, identifier) == 0) {
		return 0;
	}
	if (virtio_sound_get_card_name(card_str, card_name) < 0) {
		pr_err("%s: card(%s) err, get %s ctl elem fail!\n", __func__, card_str, identifier);
		return -1;
	}
	card = virtio_sound_get_card(virt_snd, card_name);
	if(card == NULL) {
		pr_err("%s: set card(%s) fail!\n", __func__, card_name);
		return -1;
	}
	snd_ctl_elem_info_alloca(&info);

	elem = virtio_sound_get_ctl_elem(card->handle, identifier);
	if (elem == NULL) {
		pr_err("%s: get %s ctl elem fail!\n", __func__, identifier);
		return -1;
	}
	if (strstr(identifier, "Jack") != NULL) {
		virt_snd->jacks[virt_snd->jack_cnt] = malloc(sizeof(struct vbs_jack_elem));
		if (virt_snd->jacks[virt_snd->jack_cnt] == NULL) {
			pr_err("%s: malloc jack elem fail!\n", __func__);
			return -1;
		}
		virt_snd->jacks[virt_snd->jack_cnt]->elem = elem;
		virt_snd->jacks[virt_snd->jack_cnt]->card = card;
		virt_snd->jacks[virt_snd->jack_cnt]->hda_reg_defconf = virtio_snd_jack_parse(identifier);
		virt_snd->jacks[virt_snd->jack_cnt]->connected = virtio_sound_get_jack_value(elem);
		virt_snd->jacks[virt_snd->jack_cnt]->identifier = NULL;
		virt_snd->jacks[virt_snd->jack_cnt]->snd_card = virt_snd;
		virt_snd->jacks[virt_snd->jack_cnt]->id = virt_snd->jack_cnt;
		LIST_INSERT_HEAD(&jack_list_head, virt_snd->jacks[virt_snd->jack_cnt], jack_list);
		snd_hctl_elem_set_callback(elem, virtio_sound_event_callback);
		snd_hctl_elem_set_callback_private(elem, virt_snd->jacks[virt_snd->jack_cnt]);
		virt_snd->jack_cnt++;
	} else {
		virt_snd->ctls[virt_snd->ctl_cnt] = malloc(sizeof(struct vbs_ctl_elem));
		if (virt_snd->ctls[virt_snd->ctl_cnt] == NULL) {
			pr_err("%s: malloc ctl elem fail!\n", __func__);
			return -1;
		}
		virt_snd->ctls[virt_snd->ctl_cnt]->elem = elem;
		virt_snd->ctls[virt_snd->ctl_cnt]->card = card;
		virt_snd->ctls[virt_snd->ctl_cnt]->snd_card = virt_snd;
		virt_snd->ctls[virt_snd->ctl_cnt]->id = virt_snd->ctl_cnt;
		snd_hctl_elem_set_callback(elem, virtio_sound_event_callback);
		snd_hctl_elem_set_callback_private(elem, virt_snd->ctls[virt_snd->ctl_cnt]);
		virt_snd->ctl_cnt++;
	}

	return 0;
}

static int
virtio_sound_parse_opts(struct virtio_sound *virt_snd, char *opts)
{
	char *str, *type, *cpy, *c, *param, *device, *identifier, *fn_id;

	/*
	 * Virtio sound command line should be:
	 * '-s n virtio-sound,...'.
	 * Playback substreams can be added by
	 * 	'pcmp=pcm1_name_str@hda_fn_nid[|pcm2_name_str@hda_fn_nid]'.
	 * Capture substreams can be added by
	 * 	'pcmc=pcm1_name_str@hda_fn_nid[|pcm2_name_str@hda_fn_nid]'.
	 * Kcontrol element can be added by
	 * 	'ctl=kctl1_identifer@card_name[|kctl2_identifer@card_name].
	 * The 'kctl_identifer' should be got by
	 * 	'amixer controls' such as
	 * 	'numid=99,iface=MIXER,name='PCM Playback Volume'.
	 * Substreams and kcontrols should be seperated by '&', as
	 * 	'-s n virtio-sound,pcmp=...&pcmc=...&ctl=...'.
	 */
	c = cpy = strdup(opts);
	while ((str = strsep(&cpy, "&")) != NULL) {
		type = strsep(&str, "=");
		if (strstr("pcmp", type)) {
			while ((param = strsep(&str, "|")) != NULL) {
				device = strsep(&param, "@");
				if (param == NULL) {
					pr_err("%s: fail to init pcmp stream %s!\n", __func__, device);
					free(c);
					return -1;
				}
				fn_id = strsep(&param, "@");
				if (virtio_sound_pcm_init(virt_snd, device, fn_id, param, VIRTIO_SND_D_OUTPUT) < 0) {
					pr_err("%s: fail to init pcmp stream %s!\n", __func__, device);
					free(c);
					return -1;
				}
			}
		} else if (strstr("pcmc", type)) {
			while ((param = strsep(&str, "|")) != NULL) {
				device = strsep(&param, "@");
				if (param == NULL) {
					pr_err("%s: fail to init pcmc stream %s!\n", __func__, device);
					free(c);
					return -1;
				}
				fn_id = strsep(&param, "@");
				if (virtio_sound_pcm_init(virt_snd, device, fn_id, param, VIRTIO_SND_D_INPUT) < 0) {
					pr_err("%s: fail to init pcmc stream %s!\n", __func__, device);
					free(c);
					return -1;
				}
			}
		} else if (strstr("ctl", type)) {
			while ((param = strsep(&str, "|")) != NULL) {
				identifier = strsep(&param, "@");
				if (virtio_sound_init_ctl_elem(virt_snd, param, identifier) < 0) {
					pr_err("%s: ctl elem %s init error!\n", __func__, identifier);
					free(c);
					return -1;
				}
			}
		} else {
			pr_err("%s: unknow type %s!\n", __func__, type);
			free(c);
			return -1;
		}
	}
	free(c);
	return 0;
}

static int
virtio_sound_send_event(struct virtio_sound *virt_snd, struct virtio_snd_event *event)
{
	struct virtio_vq_info *vq = &virt_snd->vq[VIRTIO_SND_VQ_EVENT];
	struct iovec iov[VIRTIO_SOUND_EVENT_SEGS];
	int n;
	uint16_t idx;

	if(!vq_has_descs(vq)) {
		pr_err("%s: vq has no descriptors!\n", __func__);
		return -1;
	}
	pthread_mutex_lock(&vq->mtx);
	n = vq_getchain(vq, &idx, iov, VIRTIO_SOUND_EVENT_SEGS, NULL);
	pthread_mutex_unlock(&vq->mtx);
	if (n <= 0) {
		pr_err("%s: fail to getchain!\n", __func__);
		return -1;
	}
	if (n > VIRTIO_SOUND_EVENT_SEGS) {
		pr_warn("%s: invalid chain, desc number %d!\n", __func__, n);
		vq_retchain(vq);
		return -1;
	}

	memcpy(iov[0].iov_base, event, sizeof(struct virtio_snd_event));
	pthread_mutex_lock(&vq->mtx);
	vq_relchain(vq, idx, sizeof(struct virtio_snd_event));
	vq_endchains(vq, 0);
	pthread_mutex_unlock(&vq->mtx);

	return 0;
}

int
virtio_sound_inject_jack_event(char *param)
{
	struct virtio_sound *virt_snd;
	struct vbs_jack_elem *jack = NULL;
	struct virtio_snd_event event;

	int logic_state = -1, match = 0;
	char *identifier, *c, *cpy;

	if (strstr(param, "Jack") == NULL) {
		return -1;
	}

	c = cpy = strdup(param);
	identifier = strsep(&c, "@");
	if (identifier == NULL || c == NULL) {
		pr_err("%s: parameter is error %s", __func__, cpy);
		free(cpy);
		return -1;
	}
	if (strcmp(c, "connect") == 0) {
		logic_state = 1;
	} else if (strcmp(c, "unconnect") == 0) {
		logic_state = 0;
	} else {
		pr_err("%s: connect status error %s\n", __func__, c);
		free(cpy);
		return -1;
	}
	LIST_FOREACH(jack, &jack_list_head, jack_list) {
		if (jack->identifier != NULL &&
			strcmp(identifier, jack->identifier) == 0) {
			match = 1;
			break;
		}
	}

	if (!match) {
		pr_err("%s: no match jack, param %s\n", __func__, cpy);
		free(cpy);
		return -1;
	}
	virt_snd = jack->snd_card;
	if (logic_state != jack->connected && logic_state == 1) {
		event.hdr.code = VIRTIO_SND_EVT_JACK_CONNECTED;
		event.data = jack->id;
		if (virtio_sound_send_event(virt_snd, &event) != 0) {
			pr_err("%s: event send fail!\n", __func__);
		}
	} else if (logic_state != jack->connected && logic_state == 0) {
		event.hdr.code = VIRTIO_SND_EVT_JACK_DISCONNECTED;
		event.data = jack->id;
		if (virtio_sound_send_event(virt_snd, &event) != 0) {
			pr_err("%s: event send fail!\n", __func__);
		}
	}
	jack->connected = logic_state;
	free(cpy);
	return 0;
}

static int
virtio_sound_event_callback(snd_hctl_elem_t *helem, unsigned int mask)
{
	struct virtio_sound *virt_snd;
	struct virtio_snd_event event;
	struct vbs_ctl_elem *ctrl;
	struct vbs_jack_elem *jack;

	if (strstr(snd_hctl_elem_get_name(helem), "Jack")) {
		jack = (struct vbs_jack_elem *)snd_hctl_elem_get_callback_private(helem);
		if (jack == NULL) {
			pr_err("%s: fail get jack elem %s!\n", __func__,
				snd_hctl_elem_get_name(helem));
			return 0;
		}

		virt_snd = jack->snd_card;
		/*
		* When close hctl handle while deinit virtio sound,
		* alsa lib will trigger a poll event. Just return immediately.
		*/
		if (virt_snd == NULL || virt_snd->status == VIRTIO_SND_BE_DEINITED)
			return 0;
		jack->connected = virtio_sound_get_jack_value(jack->elem);
		if (jack->connected < 0) {
			pr_err("%s: Jack %s read value fail!\n", __func__,
				snd_hctl_elem_get_name(helem));
			return 0;
		}
		if (jack->connected > 0)
			event.hdr.code = VIRTIO_SND_EVT_JACK_CONNECTED;
		else
			event.hdr.code = VIRTIO_SND_EVT_JACK_DISCONNECTED;
		event.data = jack->id;
	} else {
		ctrl = (struct vbs_ctl_elem *)snd_hctl_elem_get_callback_private(helem);
		if (ctrl == NULL) {
			pr_err("%s: fail get ctl elem %s!\n", __func__,
				snd_hctl_elem_get_name(helem));
			return 0;
		}

		virt_snd = ctrl->snd_card;
		if (virt_snd == NULL || virt_snd->status == VIRTIO_SND_BE_DEINITED)
			return 0;
		event.hdr.code = VIRTIO_SND_EVT_CTL_NOTIFY;
		event.data = (ctrl->id << 16) | (mask & 0xffff);
	}
	if (virtio_sound_send_event(virt_snd, &event) != 0) {
		pr_err("%s: event send fail!\n", __func__);
	}
	return 0;
}

static void *
virtio_sound_event_thread(void *param)
{
	struct virtio_sound *virt_snd = (struct virtio_sound *)param;
	struct pollfd *pfd;
	unsigned short *revents;
	int i, j, err, npfds = 0, max = 0;

	for (i = 0; i < virt_snd->card_cnt; i++) {
		npfds += virt_snd->cards[i]->count;
		max = (max > virt_snd->cards[i]->count) ? max : virt_snd->cards[i]->count;
	}

	pfd = alloca(sizeof(*pfd) * npfds);
	revents = alloca(sizeof(*revents) * max);
	for (i = 0; i < virt_snd->card_cnt; i++) {
		err = snd_hctl_poll_descriptors(virt_snd->cards[i]->handle, &pfd[virt_snd->cards[i]->start],
			virt_snd->cards[i]->count);
		if (err < 0) {
			pr_err("%s: fail to get poll descriptors!\n", __func__);
			pthread_exit(NULL);
		}
	}

	do {
		err = poll(pfd, npfds, -1);
		if (err < 0)
			continue;
		for (i = 0; i < virt_snd->card_cnt; i++) {
			snd_hctl_poll_descriptors_revents(virt_snd->cards[i]->handle, &pfd[virt_snd->cards[i]->start],
				virt_snd->cards[i]->count, revents);
			for (j = 0; j < virt_snd->cards[i]->count; j++) {
				if((revents[j] & (POLLIN | POLLOUT)) != 0) {
					snd_hctl_handle_events(virt_snd->cards[i]->handle);
					break;
				}
			}
		}
	} while (virt_snd->status != VIRTIO_SND_BE_DEINITED);

	pthread_exit(NULL);
}

static int
virtio_sound_event_init(struct virtio_sound *virt_snd, char *opts)
{
	int i, start = 0;
	char tname[MAXCOMLEN + 1];

	for (i = 0; i < virt_snd->card_cnt; i++) {
		virt_snd->cards[i]->count = snd_hctl_poll_descriptors_count(virt_snd->cards[i]->handle);
		virt_snd->cards[i]->start = start;
		start += virt_snd->cards[i]->count;
	}
	snprintf(tname, sizeof(tname), "virtio-snd-event");
	pthread_create(&virt_snd->ctl_tid, NULL, virtio_sound_event_thread, (void *)virt_snd);
	pthread_setname_np(virt_snd->ctl_tid, tname);
	return 0;
}

static int
virtio_sound_init(struct vmctx *ctx, struct pci_vdev *dev, char *opts)
{
	struct virtio_sound *virt_snd;
	pthread_mutexattr_t attr;
	int err;
	virt_snd = calloc(1, sizeof(struct virtio_sound));
	if (!virt_snd) {
		pr_err("virtio_sound: calloc returns NULL\n");
		return -1;
	}

	err = pthread_mutexattr_init(&attr);
	if (err) {
		pr_err("%s: mutexattr init failed with erro %d!\n", __func__, err);
		free(virt_snd);
		return -1;
	}
	err = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	if (err) {
		pr_err("%s: mutexattr_settype failed with error %d.\n",
			       __func__, err);
		free(virt_snd);
		return -1;
	}
	err = pthread_mutex_init(&virt_snd->mtx, &attr);
	if (err) {
		pr_err("mutex init failed with error %d!\n", err);
		free(virt_snd);
		return -1;
	}

	virtio_linkup(&virt_snd->base,
		      &virtio_snd_ops,
		      virt_snd,
		      dev,
		      virt_snd->vq,
		      BACKEND_VBSU);

	virt_snd->base.mtx = &virt_snd->mtx;
	virt_snd->base.device_caps = VIRTIO_SND_S_HOSTCAPS;

	virt_snd->vq[0].qsize = VIRTIO_SOUND_RINGSZ;
	virt_snd->vq[1].qsize = VIRTIO_SOUND_RINGSZ;
	virt_snd->vq[2].qsize = VIRTIO_SOUND_RINGSZ;
	virt_snd->vq[3].qsize = VIRTIO_SOUND_RINGSZ;
	virt_snd->vq[0].notify = virtio_sound_notify_ctl;
	virt_snd->vq[1].notify = virtio_sound_notify_event;
	virt_snd->vq[2].notify = virtio_sound_notify_tx;
	virt_snd->vq[3].notify = virtio_sound_notify_rx;

	/* initialize config space */
	pci_set_cfgdata16(dev, PCIR_DEVICE, VIRTIO_TYPE_SOUND + 0x1040);
	pci_set_cfgdata16(dev, PCIR_VENDOR, VIRTIO_VENDOR);
	pci_set_cfgdata8(dev, PCIR_CLASS, PCIC_MULTIMEDIA);
	pci_set_cfgdata8(dev, PCIR_SUBCLASS, PCIS_MULTIMEDIA_AUDIO);
	pci_set_cfgdata16(dev, PCIR_SUBDEV_0, VIRTIO_TYPE_SOUND);
	if (is_winvm == true)
		pci_set_cfgdata16(dev, PCIR_SUBVEND_0, ORACLE_VENDOR_ID);
	else
		pci_set_cfgdata16(dev, PCIR_SUBVEND_0, VIRTIO_VENDOR);

	virt_snd->stream_cnt = 0;
	virt_snd->chmap_cnt = 0;
	virt_snd->ctl_cnt = 0;
	virt_snd->jack_cnt = 0;
	virt_snd->card_cnt = 0;
	err = virtio_sound_parse_opts(virt_snd, opts);
	if (err != 0) {
		goto error;
	}

	err = virtio_sound_event_init(virt_snd, opts);
	if (err != 0) {
		goto error;
	}
	virtio_sound_cfg_init(virt_snd);

	if (virtio_interrupt_init(&virt_snd->base, virtio_uses_msix())) {
		goto error;
	}
	err = virtio_set_modern_bar(&virt_snd->base, false);
	if (err != 0) {
		goto error;
	}

	virt_snd->status = VIRTIO_SND_BE_INITED;
	if (getenv("VIRTIO_SOUND_WRITE2FILE") != NULL)
		write2file = atoi(getenv("VIRTIO_SOUND_WRITE2FILE"));
	if (getenv("VIRTIO_SOUND_PRINT_CTRL_MSG") != NULL)
		print_ctrl_msg = atoi(getenv("VIRTIO_SOUND_PRINT_CTRL_MSG"));
    return 0;
error:
	virtio_sound_deinit(ctx, dev, NULL);
	return err;

}

static void
virtio_sound_deinit(struct vmctx *ctx, struct pci_vdev *dev, char *opts)
{
	struct virtio_sound *virt_snd = (struct virtio_sound *)dev->arg;
	int s, i;
	virt_snd->status = VIRTIO_SND_BE_DEINITED;
	for (s = 0; s < virt_snd->stream_cnt; s++)
		if (virt_snd->streams[s]->tid > 0)
			pthread_kill(virt_snd->streams[s]->tid, SIGCONT);
	for (s = 0; s < virt_snd->stream_cnt; s++) {
		if (virt_snd->streams[s]->tid > 0)
			pthread_join(virt_snd->streams[s]->tid, NULL);
		if (virt_snd->streams[s]->handle && snd_pcm_close(virt_snd->streams[s]->handle) < 0) {
			pr_err("%s: stream %s close error!\n", __func__, virt_snd->streams[s]->dev_name);
		}
		pthread_mutex_destroy(&virt_snd->streams[s]->mtx);
		for (i = 0; i < virt_snd->streams[s]->chmap_cnt; i++) {
			free(virt_snd->streams[s]->chmaps[i]);
		}
		pthread_mutex_destroy(&virt_snd->streams[s]->ctl_mtx);
		free(virt_snd->streams[s]);
	}
	pthread_mutex_destroy(&virt_snd->mtx);
	for (i = 0; i < virt_snd->ctl_cnt; i++) {
		free(virt_snd->ctls[i]);
	}
	for (i = 0; i < virt_snd->jack_cnt; i++) {
		LIST_REMOVE(virt_snd->jacks[i], jack_list);
		if(virt_snd->jacks[i]->identifier)
			free(virt_snd->jacks[i]->identifier);
		free(virt_snd->jacks[i]);
	}
	if (virt_snd->ctl_tid > 0) {
		pthread_kill(virt_snd->ctl_tid, SIGCONT);
		pthread_join(virt_snd->ctl_tid, NULL);
	}
	for (i = 0; i < virt_snd->card_cnt; i++) {
		if (virt_snd->cards[i]->handle > 0)
			snd_hctl_close(virt_snd->cards[i]->handle);
		free(virt_snd->cards[i]);
	}
	free(virt_snd);
}

struct pci_vdev_ops pci_ops_virtio_sound = {
	.class_name	= "virtio-sound",
	.vdev_init	= virtio_sound_init,
	.vdev_deinit	= virtio_sound_deinit,
	.vdev_barwrite	= virtio_pci_write,
	.vdev_barread	= virtio_pci_read
};

DEFINE_PCI_DEVTYPE(pci_ops_virtio_sound);
