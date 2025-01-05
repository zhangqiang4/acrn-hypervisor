/*
 * Copyright (C) OASIS Open 2018. All rights reserved.
 * Copyright (C) 2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * vhost-vdmabuf device
 *
 */

#include <sys/uio.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/errno.h>
#include <pthread.h>
#include <linux/vhost.h>
#include <sys/mman.h>

#define pr_prefix "vhost-vdmabuf: "
#include "dm.h"
#include "pci_core.h"
#include "virtio.h"
#include "vhost.h"
#include "dm_string.h"
#include "vhost_vdmabuf.h"

/* VHOST_VDMABUF specific defines */
#define VHOST_VDMABUF_SET_RUNNING       _IOW(VHOST_VIRTIO, 0x90, int)

#define VHOST_VDMABUF_SET_ID \
	_IOC(_IOC_NONE, 'G', 6, sizeof(struct vhost_vdmabuf_set))
struct vhost_vdmabuf_set {
	/* IN parameters */
	uint64_t vmid;
	/* IN parameters */
	char name[MAX_VM_NAME_LEN];
};

static int
vhost_vdmabuf_set_running(struct vhost_dev *vdev, int start)
{
	return vhost_kernel_ioctl(vdev, VHOST_VDMABUF_SET_RUNNING, &start);
}

static int
vhost_vdmabuf_set_vmid(struct vhost_dev *vdev,
		struct vhost_vdmabuf_set *set)
{
	return vhost_kernel_ioctl(vdev,
			VHOST_VDMABUF_SET_ID, set);
}

static int
vhost_vdmabuf_start(struct vhost_vdmabuf *vhost_vdmabuf)
{
	int rc;

	if (vhost_vdmabuf->vhost_started) {
		pr_err("The vhost vdmabuf is already started, re-using it.\n");
		return 0;
	}

	rc = vhost_dev_start(&vhost_vdmabuf->vdev);
	if (rc < 0) {
		pr_err("vhost_dev_start is failed.\n");
		return -1;
	}

	rc = vhost_vdmabuf_set_running(&vhost_vdmabuf->vdev, 1);
	if (rc < 0) {
		vhost_dev_stop(&vhost_vdmabuf->vdev);
		pr_err("vhost_vdmabuf_set_running is failed with %d.\n", rc);
		return -1;
	}

	vhost_vdmabuf->vhost_started = true;
	return 0;
}

static int
vhost_vdmabuf_stop(struct vhost_vdmabuf *vhost_vdmabuf)
{
	int rc;

	if (!vhost_vdmabuf->vhost_started) {
		pr_err("vhost vdmabuf is not started.\n");
		return 0;
	}

	rc = vhost_vdmabuf_set_running(&vhost_vdmabuf->vdev, 0);
	if (rc < 0) {
		pr_err("vhost_vdmabuf_set_running is failed with %d\n", rc);
		return -1;
	}

	rc = vhost_dev_stop(&vhost_vdmabuf->vdev);
	if (rc < 0) {
		pr_err("vhost_dev_stop is failed.\n");
		return -1;
	}

	vhost_vdmabuf->vhost_started = false;
	return 0;
}

static void
virtio_vdmabuf_set_status(void *vdev, uint64_t status)
{
	struct virtio_vdmabuf *vdmabuf = vdev;
	bool should_start = status & VIRTIO_CONFIG_S_DRIVER_OK;
	int rc;

	if (!vdmabuf->vhost_vdmabuf) {
		pr_err("virtio_vdmabuf_set_status vhost is NULL.\n");
		return;
	}

	if (should_start) {
		rc = vhost_vdmabuf_start(vdmabuf->vhost_vdmabuf);
		if (rc < 0) {
			pr_err("vhost_vdmabuf_start is failed.\n");
			return;
		}
	} else if (vdmabuf->vhost_vdmabuf->vhost_started &&
			should_start == 0) {
		rc = vhost_vdmabuf_stop(vdmabuf->vhost_vdmabuf);
		if (rc < 0) {
			pr_err("vhost_vdmabuf_stop is failed.\n");
			return;
		}
	}
}

static void
virtio_vdmabuf_apply_feature(void *vdev, uint64_t negotiated_features)
{
	struct virtio_vdmabuf *vdmabuf = vdev;

	vdmabuf->features = negotiated_features;
}

static int
virtio_vdmabuf_read_cfg(void *vdev, int offset, int size, uint32_t *retval)
{
	struct virtio_vdmabuf *vdmabuf = vdev;
	void *ptr;

	ptr = (uint8_t *)&vdmabuf->config + offset;
	memcpy(retval, ptr, size);

	return 0;
}

static void
virtio_vdmabuf_reset(void *vdev)
{
	struct virtio_vdmabuf *vdmabuf = vdev;

	pr_dbg("vdmabuf: device reset requested.\n");
	/* now reset rings, MSI-X vectors, and negotiated capabilities */
	virtio_reset_dev(&vdmabuf->base);
}

static struct virtio_ops virtio_vdmabuf_ops = {
	"vhost-vdmabuf",			/* our name */
	VDMABUF_VQ_MAX,		/* we currently support 2 virtqueues */
	sizeof(struct virtio_vdmabuf_config), /* config reg size */
	virtio_vdmabuf_reset,		/* reset */
	NULL,				/* device-wide qnotify -- not used */
	virtio_vdmabuf_read_cfg,		/* read PCI config */
	NULL,				/* write PCI config */
	virtio_vdmabuf_apply_feature,	/* apply negotiated features */
	virtio_vdmabuf_set_status,	/* called on guest set status */
};

static struct vhost_vdmabuf *
vhost_vdmabuf_init(struct virtio_base *base, int vq_idx)
{
	struct vhost_vdmabuf *vhost_vdmabuf = NULL;
	uint64_t vhost_features = VHOST_VDMABUF_FEATURES;
	int rc;

	vhost_vdmabuf = calloc(1, sizeof(struct vhost_vdmabuf));
	if (!vhost_vdmabuf) {
		pr_err("vdmabuf init out of memory.\n");
		return NULL;
	}

	/* pre-init before calling vhost_dev_init */
	vhost_vdmabuf->vdev.nvqs = VDMABUF_VQ_MAX;
	vhost_vdmabuf->vdev.vqs = vhost_vdmabuf->vqs;
	vhost_vdmabuf->vhost_fd = open("/dev/vhost-vdmabuf", O_RDWR);;
	if (vhost_vdmabuf->vhost_fd < 0) {
		pr_err("Open vhost-vdmabuf fail, pls open vdmabuf kernel config.\n");
		goto fail;
	}

	rc = vhost_dev_init(&vhost_vdmabuf->vdev, base, vhost_vdmabuf->vhost_fd, vq_idx,
			vhost_features, 0, 0);
	if (rc < 0) {
		pr_err("vhost_dev_init failed.\n");
		goto fail_vhost_init;
	}

	return vhost_vdmabuf;

fail_vhost_init:
	close(vhost_vdmabuf->vhost_fd);
fail:
	if (vhost_vdmabuf)
		free(vhost_vdmabuf);
	return NULL;
}

static void
vhost_vdmabuf_deinit(struct vhost_vdmabuf *vhost_vdmabuf)
{
	int rc;

	rc = vhost_vdmabuf_stop(vhost_vdmabuf);
	if (rc < 0)
		pr_err("vhost_dev_stop is failed.\n");

	rc = vhost_dev_deinit(&vhost_vdmabuf->vdev);
	if (rc < 0)
		pr_err("vhost_dev_deinit is failed.\n");

	close(vhost_vdmabuf->vhost_fd);
	free(vhost_vdmabuf);
}

static void
vhost_vdmabuf_handle_output(void *vdev, struct virtio_vq_info *vq) {
	/* do nothing */
}

static int
parse_option(struct virtio_vdmabuf *vdmabuf, char *opts)
{
	char *str;
	int ret = 0;
	int size = 64;
	while ((str = strsep(&opts, ",")) != NULL) {
		if (strstr(str, "size=")) {
			ret = sscanf(str, "size=%d", &size);
			if (ret != 1) {
				pr_err("vdmabuf:incorrect size option\n");
				return -1;
			}
		}
	}
	vdmabuf->size = size * 1024 * 1024;
	pr_dbg("share memory size is %lld\n", vdmabuf->size);

	return 1;
}

static int
virtio_vhost_vdmabuf_init(struct vmctx *ctx, struct pci_vdev *dev, char *opts)
{
	struct virtio_vdmabuf *vdmabuf;
	int rc;
	pthread_mutexattr_t attr;
	struct vhost_vdmabuf_set set;

	vdmabuf = calloc(1, sizeof(struct virtio_vdmabuf));
	if (!vdmabuf) {
		pr_err("vdmabuf: memory allocate failed.");
		return -1;
	}

	vdmabuf->config.vmid = ctx->vmid;

	rc = parse_option(vdmabuf, opts);
	if (rc < 0) {
		pr_err("vdmabuf:failed to parse option\n");
		goto fail_free_vdmabuf;
	}
	rc = pthread_mutexattr_init(&attr);
	if (rc) {
		pr_err("vdmabuf: mutexattr init failed with erro %d\n", rc);
		goto fail_free_vdmabuf;
	}
	rc = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	if (rc) {
		pr_err("vdmabuf: mutexattr_settype failed with error %d\n", rc);
		goto fail_free_vdmabuf;
	}
	rc = pthread_mutex_init(&vdmabuf->mtx, &attr);
	if (rc) {
		pr_err("vdmabuf: pthread_mutexattr_init failed with error %d\n", rc);
		goto fail_free_vdmabuf;
	}

	virtio_linkup(&vdmabuf->base, &virtio_vdmabuf_ops, vdmabuf, dev, vdmabuf->queues, BACKEND_VHOST);
	vdmabuf->base.mtx = &vdmabuf->mtx;
	vdmabuf->base.device_caps = (1UL << VIRTIO_F_VERSION_1) | VHOST_VDMABUF_FEATURES;

	vdmabuf->queues[VDMABUF_VQ_RECV].qsize = VHOST_VDMABUF_QUEUE_SIZE;
	vdmabuf->queues[VDMABUF_VQ_RECV].notify = vhost_vdmabuf_handle_output;
	vdmabuf->queues[VDMABUF_VQ_SEND].qsize = VHOST_VDMABUF_QUEUE_SIZE;
	vdmabuf->queues[VDMABUF_VQ_SEND].notify = vhost_vdmabuf_handle_output;

	/* initialize config space */
	pci_set_cfgdata16(dev, PCIR_DEVICE, VIRTIO_DEV_DMABUF);
	pci_set_cfgdata16(dev, PCIR_VENDOR, VIRTIO_VENDOR);
	pci_set_cfgdata8(dev, PCIR_CLASS, PCIC_OTHER);
	pci_set_cfgdata16(dev, PCIR_REVID, 1);

	virtio_set_modern_bar(&vdmabuf->base, false);

	vdmabuf->vhost_vdmabuf = vhost_vdmabuf_init(&vdmabuf->base, 0);
	if (!vdmabuf->vhost_vdmabuf) {
		pr_err("vdmabuf: vhost init failed.\n");
		goto fail_destroy_mtx;
	}
	set.vmid = vdmabuf->config.vmid;
	memcpy(set.name, ctx->name, MAX_VM_NAME_LEN);
	vhost_vdmabuf_set_vmid(&vdmabuf->vhost_vdmabuf->vdev, &set);

	if (virtio_interrupt_init(&vdmabuf->base, virtio_uses_msix())) {
		pr_err("vdmabuf: virtio interrupt failed.\n");
		goto fail_definit_vdmabuf;
	}
	return 0;

fail_definit_vdmabuf:
	vhost_vdmabuf_deinit(vdmabuf->vhost_vdmabuf);
fail_destroy_mtx:
	pthread_mutex_destroy(&vdmabuf->mtx);
fail_free_vdmabuf:
	free(vdmabuf);
	return -1;
}

static void
virtio_vhost_vdmabuf_deinit(struct vmctx *ctx, struct pci_vdev *dev, char *opts)
{
	struct virtio_vdmabuf *vdmabuf;

	if (dev->arg) {
		vdmabuf = (struct virtio_vdmabuf *) dev->arg;

		if (vdmabuf->vhost_vdmabuf)
			vhost_vdmabuf_deinit(vdmabuf->vhost_vdmabuf);
		pthread_mutex_destroy(&vdmabuf->mtx);
		pr_dbg("%s: vdmabuf deinit done\n", __func__);
		free(vdmabuf);
	}
}

struct pci_vdev_ops pci_ops_vhost_vdmabuf = {
	.class_name	= "vhost-vdmabuf",
	.vdev_init	= virtio_vhost_vdmabuf_init,
	.vdev_deinit	= virtio_vhost_vdmabuf_deinit,
	.vdev_barwrite	= virtio_pci_write,
	.vdev_barread	= virtio_pci_read
};
DEFINE_PCI_DEVTYPE(pci_ops_vhost_vdmabuf);
