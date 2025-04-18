/*
 * Copyright (C) 2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <pthread.h>
#include <stdio.h>
#include <libudev.h>

#define pr_prefix "virtio_pxp: "
#include "drm.h"
#include "dm.h"
#include "pci_core.h"
#include "virtio.h"
#include "mevent.h"

#define VIRTIO_TYPE_PXP         0xFFF4
#define VIRTIO_DEV_PXP          0x107C

#define MAX_RINGS 64
/* PRELIM ioctl numbers go down from 0x5f */
#define PRELIM_DRM_I915_PXP_OPS		0x52
/* NOTE: PXP_OPS PRELIM ioctl code 0x52 maintains compatibility with DII-server products */

#define PRELIM_DRM_IOCTL_I915_PXP_OPS	DRM_IOWR(DRM_COMMAND_BASE + PRELIM_DRM_I915_PXP_OPS, \
						 struct prelim_drm_i915_pxp_ops)

/* End PRELIM ioctl's */

/*
 * struct pxp_set_session_status_params - Params to reserved, set or destroy
 * the session from the PXP state machine.
 */
struct prelim_drm_i915_pxp_set_session_status_params {
	__u32 pxp_tag; /* in/out, session identifier tag */
	__u32 session_type; /* in, session type */
	__u32 session_mode; /* in, session mode */
#define PRELIM_DRM_I915_PXP_MODE_LM 0
#define PRELIM_DRM_I915_PXP_MODE_HM 1
#define PRELIM_DRM_I915_PXP_MODE_SM 2

	__u32 req_session_state; /* in, new session state */
	/* Request KMD to allocate session id and move it to INIT */
#define PRELIM_DRM_I915_PXP_REQ_SESSION_ID_INIT 0
	/* Inform KMD that UMD has completed the initialization */
#define PRELIM_DRM_I915_PXP_REQ_SESSION_IN_PLAY 1
	/* Request KMD to terminate the session */
#define PRELIM_DRM_I915_PXP_REQ_SESSION_TERMINATE 2
} __attribute__((packed));

/*
 * struct pxp_tee_io_message_params - Params to send/receive message to/from TEE.
 */
struct prelim_drm_i915_pxp_tee_io_message_params {
	__u64 msg_in; /* in - pointer to buffer containing input message */
	__u32 msg_in_size; /* in - input message size */
	__u64 msg_out; /* in - pointer to buffer to store the output message */
	__u32 msg_out_buf_size; /* in -  provided output message buffer size */
	__u32 msg_out_ret_size; /* out- output message actual size returned from TEE */
} __attribute__((packed));

/*
 * struct drm_i915_pxp_query_tag - Params to query the PXP tag of specified
 * session id and whether the session is alive from PXP state machine.
 */
struct prelim_drm_i915_pxp_query_tag {
	__u32 session_is_alive;

	__u32 pxp_tag;
#define PRELIM_DRM_I915_PXP_TAG_SESSION_ID_MASK		(0x7f)
#define PRELIM_DRM_I915_PXP_TAG_INSTANCE_ID_MASK	(0xff << 8)
#define PRELIM_DRM_I915_PXP_TAG_SESSION_ENABLED		(0x1 << 16)
#define PRELIM_DRM_I915_PXP_TAG_SESSION_HM		(0x1 << 17)
#define PRELIM_DRM_I915_PXP_TAG_SESSION_SM		(0x1 << 19)
} __attribute__((packed));

struct prelim_drm_i915_pxp_host_session_handle_request {
	__u32 request_type; /* in - type of request for host-session-handle operation */
#define PRELIM_DRM_I915_PXP_GET_HOST_SESSION_HANDLE     1
	__u64 host_session_handle; /* out - returned host_session_handle */
} __attribute__((packed));


struct prelim_drm_i915_pxp_ops {
	__u32 action; /* in - specified action of this operation */
#define PRELIM_DRM_I915_PXP_ACTION_SET_SESSION_STATUS 0
#define PRELIM_DRM_I915_PXP_ACTION_TEE_IO_MESSAGE 1
#define PRELIM_DRM_I915_PXP_ACTION_QUERY_PXP_TAG 2
#define PRELIM_DRM_I915_PXP_ACTION_HOST_SESSION_HANDLE_REQ 3

	__u32 status; /* out - status output for this operation */
#define PRELIM_DRM_I915_PXP_OP_STATUS_SUCCESS 0
#define PRELIM_DRM_I915_PXP_OP_STATUS_RETRY_REQUIRED 1
#define PRELIM_DRM_I915_PXP_OP_STATUS_SESSION_NOT_AVAILABLE 2
#define PRELIM_DRM_I915_PXP_OP_STATUS_ERROR_UNKNOWN 3
#define PRELIM_DRM_I915_PXP_OP_STATUS_ERROR_INVALID 4
#define PRELIM_DRM_I915_PXP_OP_STATUS_ERROR_RETURN 0xfff

	__u64 params; /* in/out - pointer to data matching the action */
} __attribute__((packed));


struct virtio_pxp_config {
	uint32_t device_idx;
	uint32_t device_vfid;
	uint32_t sessions;
};

struct virtio_pxp_set_session_req {
	uint32_t action;
	uint32_t pxp_tag;
	uint32_t session_type;
	uint32_t session_mode;
	uint32_t req_session_state;
};
struct virtio_pxp_set_session_resp {
	int32_t status;
	uint32_t pxp_tag;
};

struct virtio_pxp_io_message_req {
	uint32_t action;
	uint32_t msg_in_size;
	uint32_t msg_out_buf_size;
};

struct virtio_pxp_io_message_resp {
	int32_t status;
	uint32_t msg_out_ret_size;
};

struct virtio_pxp_query_tag_req {
	uint32_t action;
	uint32_t session_is_alive;
	uint32_t pxp_tag;
};

struct virtio_pxp_query_tag_resp {
	int32_t status;
	uint32_t session_is_alive;
	uint32_t pxp_tag;
};

struct virtio_pxp_host_session_handle_req {
	uint32_t action;
	uint32_t request_type;
	uint64_t host_session_handle;
};
struct virtio_pxp_host_session_handle_resp {
	int32_t status;
	uint64_t host_session_handle;
};

#define VIRTIO_PXP_CONTROLQ 0
#define VIRTIO_PXP_EVENTQ 1
#define VIRTIO_PXP_MAXQ 2

#define MAX_SESSIONS 16

struct event_entry {
	int event;
	LIST_ENTRY(event_entry) link;
};

struct virtio_pxp {
	struct virtio_base	base;
	pthread_mutex_t	mtx;
	pthread_mutex_t	event_mtx;
	LIST_HEAD(,event_entry) event_list;
	int card;
	int vfid;
	int drm_fd;
	struct virtio_vq_info vq[VIRTIO_PXP_MAXQ];
	struct virtio_pxp_config config;
	struct udev *udev;
	struct udev_monitor *mon;
	int mon_fd;
	struct mevent *mevt;
};

static int
virtio_pxp_cfgread(void *vdev, int offset, int size, uint32_t *retval)
{
	struct virtio_pxp *pxp = vdev;
	void* ptr;
	ptr = (uint8_t *)&pxp->config + offset;
	memcpy(retval, ptr, size);
	return 0;
}

static void virtio_pxp_reset(void *vdev)
{
	struct virtio_pxp *pxp;
	pxp = vdev;
	pr_dbg("virtio pxp reset\n");
	virtio_reset_dev(&pxp->base);
}

static void
virtio_pxp_set_status(void *vdev, uint64_t status)
{
	struct virtio_pxp *pxp;

	pr_dbg("virtio_pxp: set status 0x%x\n", status);
	pxp = vdev;
	pxp->base.status = status;
}

static struct virtio_ops virtio_pxp_ops = {
	.name = "virtio-pxp",			/* our name */
	.cfgsize = sizeof(struct virtio_pxp_config),/* config reg size */
	.reset = virtio_pxp_reset,		/* reset */
	.qnotify = NULL,				/* device-wide qnotify */
	.cfgread = virtio_pxp_cfgread,		/* read PCI config */
	.set_status = virtio_pxp_set_status,		/* called on guest set status */
};

static int
process_set_session(struct virtio_pxp *pxp, struct iovec *iov, int n)
{
	int ret = 0;
	struct prelim_drm_i915_pxp_ops params;
	struct prelim_drm_i915_pxp_set_session_status_params extra_param;
	struct virtio_pxp_set_session_req *req = iov[0].iov_base;
	struct virtio_pxp_set_session_resp resp;
	memset(&params, 0, sizeof(params));
	memset(&extra_param, 0, sizeof(extra_param));
	params.action = req->action;
	params.params = (__u64)&extra_param;
	extra_param.pxp_tag = req->pxp_tag;
	extra_param.session_type = req->session_type;
	extra_param.session_mode = req->session_mode;
	extra_param.req_session_state = req->req_session_state;
	ret = ioctl(pxp->drm_fd, PRELIM_DRM_IOCTL_I915_PXP_OPS, &params);
	if (ret < 0) {
		pr_warn("virtio_pxp:%s, pxp_tag:%u, session_type:%d, session_mode:%d, req_session_state:%d ret:%d\n", __FUNCTION__,
			extra_param.pxp_tag, extra_param.session_type, extra_param.session_mode,
			extra_param.req_session_state, ret);
		resp.status = PRELIM_DRM_I915_PXP_OP_STATUS_ERROR_RETURN;
	} else {
		resp.status = params.status;
	}
	resp.pxp_tag = extra_param.pxp_tag;

	memcpy(iov[1].iov_base, &resp, sizeof(resp));
	return sizeof(resp);

}

static int
process_io_mssage(struct virtio_pxp *pxp, struct iovec *iov, int n)
{
	int ret = 0;
	struct prelim_drm_i915_pxp_ops params;
	struct prelim_drm_i915_pxp_tee_io_message_params extra_param;
	struct virtio_pxp_io_message_req *req = iov[0].iov_base;
	struct virtio_pxp_io_message_resp resp;
	memset(&params, 0, sizeof(params));
	memset(&extra_param, 0, sizeof(extra_param));
	params.action = req->action;
	params.params = (__u64)&extra_param;
	extra_param.msg_in_size = req->msg_in_size;
	extra_param.msg_out_buf_size = req->msg_out_buf_size;
	extra_param.msg_in = (__u64) iov[1].iov_base;
	extra_param.msg_out =(__u64) iov[2].iov_base;
	ret = ioctl(pxp->drm_fd, PRELIM_DRM_IOCTL_I915_PXP_OPS, &params);
	if (ret < 0) {
		pr_warn("virtio_pxp:%s,ret:%d, ret_size:%d\n", __FUNCTION__, ret, extra_param.msg_out_ret_size );
		resp.status = PRELIM_DRM_I915_PXP_OP_STATUS_ERROR_RETURN;
	} else {
		resp.status = params.status;
	}
	resp.msg_out_ret_size = extra_param.msg_out_ret_size;

	memcpy(iov[3].iov_base, &resp, sizeof(resp));
	return sizeof(resp);
}

static int
process_query_pxp(struct virtio_pxp *pxp, struct iovec *iov, int n)
{
	int ret = 0;
	struct prelim_drm_i915_pxp_ops params;
	struct prelim_drm_i915_pxp_query_tag extra_param;
	struct virtio_pxp_query_tag_req *req = iov[0].iov_base;
	struct virtio_pxp_query_tag_resp resp;
	memset(&params, 0, sizeof(params));
	memset(&extra_param, 0, sizeof(extra_param));
	params.action = req->action;
	params.params = (__u64)&extra_param;
	extra_param.pxp_tag = req->pxp_tag;
	extra_param.session_is_alive = req->session_is_alive;
	ret = ioctl(pxp->drm_fd, PRELIM_DRM_IOCTL_I915_PXP_OPS, &params);
	if (ret < 0) {
		pr_warn("virtio_pxp:%s,pxp_tag:%u, session_is_alive:%d ret:%d\n", __FUNCTION__,
			extra_param.pxp_tag, extra_param.session_is_alive, ret);
		resp.status = PRELIM_DRM_I915_PXP_OP_STATUS_ERROR_RETURN;
	} else {
		resp.status = params.status;
	}
	resp.pxp_tag = extra_param.pxp_tag;
	resp.session_is_alive = extra_param.session_is_alive;

	memcpy(iov[1].iov_base, &resp, sizeof(resp));
	return sizeof(resp);

}

static int
process_host_session_handle_req(struct virtio_pxp *pxp, struct iovec *iov, int n)
{
	int ret = 0;
	struct prelim_drm_i915_pxp_ops params;
	struct prelim_drm_i915_pxp_host_session_handle_request extra_param;
	struct virtio_pxp_host_session_handle_req *req = iov[0].iov_base;
	struct virtio_pxp_host_session_handle_resp resp;
	memset(&params, 0, sizeof(params));
	memset(&extra_param, 0, sizeof(extra_param));
	params.action = req->action;
	params.params = (__u64)&extra_param;
	extra_param.request_type = req->request_type;
	extra_param.host_session_handle = req->host_session_handle;
	ret = ioctl(pxp->drm_fd, PRELIM_DRM_IOCTL_I915_PXP_OPS, &params);
	if (ret < 0) {
		pr_warn("virtio_pxp:%s,request_type:%d, host_session_handle:%llu ret:%d\n", __FUNCTION__,
			extra_param.request_type, extra_param.host_session_handle, ret);
		resp.status = PRELIM_DRM_I915_PXP_OP_STATUS_ERROR_RETURN;
	} else {
		resp.status = params.status;
	}
	resp.host_session_handle = extra_param.host_session_handle;

	memcpy(iov[1].iov_base, &resp, sizeof(resp));
	return sizeof(resp);

}

static int
virtio_pxp_proc(struct virtio_pxp *pxp, struct iovec *iov, int n)
{
	int *action = iov[0].iov_base;
	int len = iov[0].iov_len;
	int ret = 4;

	pr_dbg("virtio_pxp: pxp action receive:%d, n:%d, len:%d\n", *action, n, len);
	if (*action == PRELIM_DRM_I915_PXP_ACTION_SET_SESSION_STATUS) {
		return process_set_session(pxp, iov, n);
	} else if (*action == PRELIM_DRM_I915_PXP_ACTION_TEE_IO_MESSAGE) {
		return process_io_mssage(pxp, iov, n);
	} else if (*action == PRELIM_DRM_I915_PXP_ACTION_QUERY_PXP_TAG) {
		return process_query_pxp(pxp, iov, n);
	} else if (*action == PRELIM_DRM_I915_PXP_ACTION_HOST_SESSION_HANDLE_REQ) {
		return process_host_session_handle_req(pxp, iov, n);
	} else {
		pr_err("unknown pxp action:%d\n", *action);
		return ret;
	}
	return ret;
}

static int
send_pxp_event(struct virtio_vq_info *vq, unsigned int event)
{
	struct iovec iov;
	uint32_t ret_value = event;
	int n;
	uint16_t idx;

	if(!vq_has_descs(vq)) {
		return -1;
	}

	n = vq_getchain(vq, &idx, &iov, 1, NULL);
	if (n < 0) {
		pr_err("invalid descriptors\n");
		return -1;
	}
	if (n == 0) {
		pr_err("get no available desciptors\n");
		return -1;
	}
	if (n != 1) {
		pr_err("get wrong number of available descriptors\n");
		vq_relchain(vq, idx, 4); /* Release the chain */
		return -1;
	}
	memcpy(iov.iov_base, &ret_value, 4);
	vq_relchain(vq, idx, 4);
	vq_endchains(vq, 1);
	return 0;
}

static void
process_pxp_event(struct virtio_pxp *pxp, unsigned int event)
{
	struct virtio_vq_info *vq;
	struct event_entry *entry = NULL;
	struct event_entry *tmp = NULL;
	int ret = 0;

	vq = &pxp->vq[VIRTIO_PXP_EVENTQ];

	if(vq_disallow_interrupt(vq))
		return;
	entry = malloc(sizeof(*entry));
	if (!entry) {
		pr_err("%s, failed to alloc buffer\n", __FUNCTION__);
		return;
	}
	entry->event = event;
	pthread_mutex_lock(&pxp->event_mtx);
	LIST_INSERT_HEAD(&pxp->event_list, entry, link);
	LIST_FOREACH(tmp, &pxp->event_list, link) {
		entry = tmp;
	}
	if (event) {
		ret = send_pxp_event(vq, entry->event);
		if (ret == 0) {
			LIST_REMOVE(entry, link);
			free(entry);
		}
	}
	pthread_mutex_unlock(&pxp->event_mtx);

}

static void
virtio_pxp_notify_controlq(void *vdev, struct virtio_vq_info *vq)
{
	struct iovec iov[4];
	struct virtio_pxp *pxp;
	uint16_t idx;
	int n;
	int ret;

	idx = vq->qsize;
	pxp = (struct virtio_pxp *)vdev;
	if (vq_has_descs(vq)) {
		n = vq_getchain(vq, &idx, iov, 4, NULL);
		ret = virtio_pxp_proc(pxp, iov, n);
		vq_relchain(vq, idx, ret);
		vq_endchains(vq, 1);
	}
}

static void
virtio_pxp_notify_eventq(void *vdev, struct virtio_vq_info *vq)
{
	struct event_entry *entry = NULL;
	struct event_entry *tmp = NULL;
	struct virtio_pxp *pxp;
	int ret = 0;
	pxp = (struct virtio_pxp *)vdev;
	if(!pxp || !vq || vq_disallow_interrupt(vq))
		return;

	pthread_mutex_lock(&pxp->event_mtx);
	LIST_FOREACH(tmp, &pxp->event_list, link) {
		entry = tmp;
	}
	if (entry) {
		ret = send_pxp_event(vq, entry->event);
		if (ret == 0) {
			LIST_REMOVE(entry, link);
			free(entry);
		}
	}
	pthread_mutex_unlock(&pxp->event_mtx);
}

static int parse_pxp_option(struct virtio_pxp *pxp, char *opts)
{
	char *str;
	int ret = 0;
	int vfid = 1;
	int idx = 0;
	int sessions = 15;
	pxp->config.device_idx = 0;
	pxp->config.device_vfid = 1;
	while ((str = strsep(&opts, ",")) != NULL) {
		if (strstr(str, "card=")) {
			ret = sscanf(str, "card=%d", &idx);
			if (ret != 1) {
				pr_err("incorrect device id option\n");
				return -1;
			}
			pxp->config.device_idx = idx;
		} else if (strstr(str, "vfid=")) {
			ret = sscanf(str, "vfid=%d", &vfid);
			if (ret != 1) {
				pr_err("incorrect vfid option\n");
				return -1;
			}
		} else if(strstr(str, "sessions=")) {
			ret = sscanf(str, "sessions=%d", &sessions);
			if (ret != 1 || sessions >= MAX_SESSIONS) {
				pr_err("incorrect session option, max value is:%d\n", MAX_SESSIONS);
				return -1;
			}
		}
	}
	pxp->config.device_idx = idx;
	pxp->config.device_vfid = vfid;
	pxp->config.sessions = sessions;
	pr_info("card:%d,vfid:%d,max sessions:%d\n", idx, vfid, sessions);
	return 1;
}

static void pxp_uevent_read(int fd, enum ev_type type, void *arg)
{
	struct virtio_pxp *pxp = arg;
	const char *val;
	int event;
	struct udev_device *device = udev_monitor_receive_device(pxp->mon);
	if (!device)
		return;
	val = udev_device_get_property_value(device, "PXP_EVENT");
	if (!val) {
		udev_device_unref(device);
		return;
	}
	event = atoi(val);
	pr_dbg("receive pxp event:%d\n", event);
	udev_device_unref(device);

	process_pxp_event(pxp, event);
}

static int init_gpu_pxp(struct virtio_pxp *pxp)
{
	int fd = 0;
	char name[256];
	snprintf(name, 256, "/dev/dri/card%d", pxp->config.device_idx);
	fd = open(name, O_RDWR);
	if (fd < 0) {
		pr_err("failed to open gpu:%s\n", name);
		return -1;
	}
	pxp->udev = udev_new();
	if (!pxp->udev) {
		pr_err("failed to new udev\n");
		close(fd);
		return -1;
	}
	pxp->mon = udev_monitor_new_from_netlink(pxp->udev, "kernel");
	if (!pxp->mon) {
		pr_err("failed to init udev\n");
		udev_unref(pxp->udev);
		close(fd);
		return -1;
	}
	pxp->mon_fd = udev_monitor_get_fd(pxp->mon);
	udev_monitor_filter_add_match_subsystem_devtype(pxp->mon, "drm", NULL);
	if (udev_monitor_enable_receiving(pxp->mon) < 0) {
		pr_err("failed to enable udev-monitor receiving\n");
		udev_monitor_unref(pxp->mon);
		udev_unref(pxp->udev);
		close(fd);
		return -1;
	}
	pxp->mevt = mevent_add(pxp->mon_fd, EVF_READ, pxp_uevent_read, pxp,
			NULL, NULL);
	if (pxp->mevt == NULL) {
		pr_err("failed to add mevent\n");
		udev_monitor_unref(pxp->mon);
		udev_unref(pxp->udev);
		close(fd);
		return -1;
	}

	return fd;
}

static int
virtio_pxp_init(struct vmctx *ctx, struct pci_vdev *dev, char *opts)
{
	struct virtio_pxp *pxp;
	pthread_mutexattr_t attr;
	int rc;

	pxp = calloc(1, sizeof(struct virtio_pxp));
	if (!pxp) {
		pr_err("failed to calloc virtio pxp\n");
		rc = -ENOMEM;
		return rc;
	}
	rc = parse_pxp_option(pxp, opts);
	if (rc < 0) {
		pr_err("failed to parse pxp\n");
		free(pxp);
		return rc;
	}

	rc = init_gpu_pxp(pxp);
	if (rc < 0) {
		pr_err("failed to init pxp\n");
		free(pxp);
		return rc;
	}
	pxp->drm_fd = rc;

	/* init mutex attribute properly to avoid deadlock */
	rc = pthread_mutexattr_init(&attr);
	if (rc) {
		pr_err("%s: mutexattr init failed with error %d.\n",
				__func__, rc);
		goto fail_mtx;
	}
	rc = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	if (rc) {
		pr_err("%s: mutexattr_settype failed with error %d.\n",
				__func__, rc);
		pthread_mutexattr_destroy(&attr);
		goto fail_mtx;
	}
	rc = pthread_mutex_init(&pxp->mtx, &attr);
	if (rc) {
		pr_err("%s: pthread_mutex_init failed with error %d.\n",
				__func__,rc);
		pthread_mutexattr_destroy(&attr);
		goto fail_mtx;
	}
	rc = pthread_mutex_init(&pxp->event_mtx, &attr);
	if (rc) {
		pr_err("%s: pthread_mutex_init event mutex failed with error %d.\n",
				__func__,rc);
		pthread_mutexattr_destroy(&attr);
		goto fail_event_mtx;
	}
	LIST_INIT(&pxp->event_list);
	pxp->base.mtx = &pxp->mtx;
	pxp->base.device_caps = (1UL << VIRTIO_F_VERSION_1);
	virtio_pxp_ops.nvq = VIRTIO_PXP_MAXQ;

	/* register the virtio_pxp_ops to virtio framework */
	virtio_linkup(&pxp->base,
			&virtio_pxp_ops,
			pxp,
			dev,
			pxp->vq,
			BACKEND_VBSU);

	pxp->vq[VIRTIO_PXP_CONTROLQ].qsize = 64;
	pxp->vq[VIRTIO_PXP_CONTROLQ].notify = virtio_pxp_notify_controlq;
	pxp->vq[VIRTIO_PXP_EVENTQ].qsize = 64;
	pxp->vq[VIRTIO_PXP_EVENTQ].notify = virtio_pxp_notify_eventq;

	/* initialize config space */
	pci_set_cfgdata16(dev, PCIR_DEVICE, VIRTIO_DEV_PXP);
	pci_set_cfgdata16(dev, PCIR_VENDOR, VIRTIO_VENDOR);
	pci_set_cfgdata16(dev, PCIR_REVID, 1);
	pci_set_cfgdata8(dev, PCIR_CLASS, PCIC_OTHER);
	pci_set_cfgdata16(dev, PCIR_SUBDEV_0, VIRTIO_TYPE_PXP);
	pci_set_cfgdata16(dev, PCIR_SUBVEND_0, VIRTIO_VENDOR);

	if (virtio_interrupt_init(&pxp->base, virtio_uses_msix())) {
		pr_err("failed to init interrupt\n");
		rc = -1;
		goto fail;
	}
	rc = virtio_set_modern_bar(&pxp->base, false);
	if (rc < 0) {
		pr_err("set modern bar ret:%d\n", rc);
		goto fail;
	}
	return 0;


fail:
	pthread_mutex_destroy(&pxp->event_mtx);
fail_event_mtx:
	pthread_mutex_destroy(&pxp->mtx);
fail_mtx:
	mevent_delete(pxp->mevt);
	udev_monitor_unref(pxp->mon);
	udev_unref(pxp->udev);
	close(pxp->drm_fd);
	free(pxp);
	dev->arg = NULL;
	return rc;
}

static void
virtio_pxp_deinit(struct vmctx *ctx, struct pci_vdev *dev, char *opts)
{
	struct virtio_pxp *pxp;
	struct event_entry *entry = NULL;
	pxp = (struct virtio_pxp *)dev->arg;
	if (pxp) {
		mevent_delete(pxp->mevt);
		virtio_pxp_reset(pxp);
		while (LIST_FIRST(&pxp->event_list)) {
			entry = LIST_FIRST(&pxp->event_list);
			if (entry) {
				LIST_REMOVE(entry, link);
				free(entry);
			}
		}
		pthread_mutex_destroy(&pxp->mtx);
		pthread_mutex_destroy(&pxp->event_mtx);
		udev_monitor_unref(pxp->mon);
		udev_unref(pxp->udev);
		close(pxp->drm_fd);
		free(pxp);
		dev->arg = NULL;
	}
}

struct pci_vdev_ops pci_ops_virtio_pxp = {
	.class_name     = "virtio-pxp",
	.vdev_init      = virtio_pxp_init,
	.vdev_deinit    = virtio_pxp_deinit,
	.vdev_barwrite	= virtio_pci_write,
	.vdev_barread	= virtio_pci_read,
};
DEFINE_PCI_DEVTYPE(pci_ops_virtio_pxp);
