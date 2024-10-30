/*
 * Copyright (C) 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#include <sys/eventfd.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#include "pci_core.h"
#include "vmmapi.h"
#include "vhost.h"
#include "vhost_internal.h"

static int vhost_debug;
#define LOG_TAG "vhost: "
#define DPRINTF(fmt, args...) \
	do { if (vhost_debug) pr_dbg(LOG_TAG fmt, ##args); } while (0)
#define WPRINTF(fmt, args...) pr_err(LOG_TAG fmt, ##args)

static int
vhost_eventfd_test_and_clear(int fd)
{
	uint64_t count = 0;
	int rc;

	/*
	 * each successful read returns an 8-byte integer,
	 * a read will set the count to zero (EFD_SEMAPHORE
	 * is not specified when eventfd() is called in
	 * vhost_vq_init()).
	 */
	rc = read(fd, &count, sizeof(count));
	DPRINTF("read eventfd, rc = %d, errno = %d, count = %ld\n",
		rc, errno, count);
	return rc > 0 ? 1 : 0;
}

static int
vhost_vq_register_eventfd(struct vhost_dev *vdev,
			  int idx, bool is_register)
{
	struct acrn_ioeventfd ioeventfd = {0};
	struct acrn_irqfd irqfd = {0};
	struct virtio_base *base;
	struct vhost_vq *vq;
	struct virtio_vq_info *vqi;
	struct msix_table_entry *mte;
	struct acrn_msi_entry msi;
	int rc = -1;

	/* this interface is called only by vhost_vq_start,
	 * parameters have been checked there
	 */
	base = vdev->base;
	vqi = &vdev->base->queues[vdev->vq_idx + idx];
	vq = &vdev->vqs[idx];

	if (!is_register) {
		irqfd.flags = ACRN_IRQFD_FLAG_DEASSIGN;
	}

	virtio_register_ioeventfd(base, idx, is_register, vq->kick_fd);
	/* register irqfd for notify */
	mte = &vdev->base->dev->msix.table[vqi->msix_idx];
	msi.msi_addr = mte->addr;
	msi.msi_data = mte->msg_data;
	irqfd.fd = vq->call_fd;
	/* no additional flag bit should be set */
	irqfd.msi = msi;
	DPRINTF("[irqfd: %d][MSIX: %d]\n", irqfd.fd, vqi->msix_idx);
	rc = vm_irqfd(vdev->base->dev->vmctx, &irqfd);
	if (rc < 0) {
		WPRINTF("vm_irqfd failed rc = %d, errno = %d\n", rc, errno);
		/* unregister ioeventfd */
		if (is_register) {
			ioeventfd.flags |= ACRN_IOEVENTFD_FLAG_DEASSIGN;
			vm_ioeventfd(vdev->base->dev->vmctx, &ioeventfd);
		}
		return -1;
	}

	return 0;
}

static int
vhost_vq_init(struct vhost_dev *vdev, int idx)
{
	struct vhost_vq *vq;

	if (!vdev || !vdev->vqs)
		goto fail;

	vq = &vdev->vqs[idx];
	if (!vq)
		goto fail;

	vq->kick_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
	if (vq->kick_fd < 0) {
		WPRINTF("create kick_fd failed\n");
		goto fail_kick;
	}

	vq->call_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
	if (vq->call_fd < 0) {
		WPRINTF("create call_fd failed\n");
		goto fail_call;
	}

	vq->idx = idx;
	vq->dev = vdev;
	return 0;

fail_call:
	close(vq->kick_fd);
fail_kick:
	vq->kick_fd = -1;
	vq->call_fd = -1;
fail:
	return -1;
}

static int
vhost_vq_deinit(struct vhost_vq *vq)
{
	if (!vq)
		return -1;

	if (vq->call_fd > 0) {
		close(vq->call_fd);
		vq->call_fd = -1;
	}

	if (vq->kick_fd > 0) {
		close(vq->kick_fd);
		vq->kick_fd = -1;
	}

	return 0;
}

static int
vhost_vq_start(struct vhost_dev *vdev, int idx)
{
	struct vhost_vq *vq;
	struct virtio_vq_info *vqi;
	struct vhost_vring_state ring;
	struct vhost_vring_addr addr;
	struct vhost_vring_file file;
	int rc, q_idx;

	/* sanity check */
	if (!vdev->base || !vdev->base->queues || !vdev->base->vops ||
		!vdev->vqs) {
		WPRINTF("vhost_dev is not initialized\n");
		goto fail;
	}

	/*
	 * vq_idx is introduced to support multi-queue feature of vhost net.
	 * When multi-queue feature is enabled, every vhost_dev owns part of
	 * the virtqueues defined by virtio backend driver in device model,
	 * they are specified by
	 * [vdev->vq_idx, vdev->vq_idx + vhost_dev->nvqs)
	 * If multi-queue feature is not required, just leave vdev->vq_idx
	 * to zero.
	 */
	q_idx = idx + vdev->vq_idx;
	if (q_idx >= vdev->base->vops->nvq) {
		WPRINTF("invalid vq index: idx = %d, vq_idx = %d\n",
			idx, vdev->vq_idx);
		goto fail;
	}
	vqi = &vdev->base->queues[q_idx];
	vq = &vdev->vqs[idx];

	/* clear kick_fd and call_fd */
	vhost_eventfd_test_and_clear(vq->kick_fd);
	vhost_eventfd_test_and_clear(vq->call_fd);

	/* register ioeventfd & irqfd */
	rc = vhost_vq_register_eventfd(vdev, idx, true);
	if (rc < 0) {
		WPRINTF("register eventfd failed: idx = %d\n", idx);
		goto fail;
	}

	/* VHOST_SET_VRING_NUM */
	ring.index = idx;
	ring.num = vqi->qsize;
	rc = vdev->vhost_ops->vhost_set_vring_num(vdev, &ring);
	if (rc < 0) {
		WPRINTF("set_vring_num failed: idx = %d\n", idx);
		goto fail_vring;
	}

	/* VHOST_SET_VRING_BASE */
	ring.num = vqi->last_avail;
	rc = vdev->vhost_ops->vhost_set_vring_base(vdev, &ring);
	if (rc < 0) {
		WPRINTF("set_vring_base failed: idx = %d, last_avail = %d\n",
			idx, vqi->last_avail);
		goto fail_vring;
	}

	/* VHOST_SET_VRING_ADDR */
	addr.index = idx;
	addr.desc_user_addr = (uintptr_t)vqi->desc;
	addr.avail_user_addr = (uintptr_t)vqi->avail;
	addr.used_user_addr = (uintptr_t)vqi->used;
	addr.log_guest_addr = (uintptr_t)NULL;
	addr.flags = 0;
	rc = vdev->vhost_ops->vhost_set_vring_addr(vdev, &addr);
	if (rc < 0) {
		WPRINTF("set_vring_addr failed: idx = %d\n", idx);
		goto fail_vring;
	}

	/* VHOST_SET_VRING_CALL */
	file.index = idx;
	file.fd = vq->call_fd;
	rc = vdev->vhost_ops->vhost_set_vring_call(vdev, &file);
	if (rc < 0) {
		WPRINTF("set_vring_call failed\n");
		goto fail_vring;
	}

	/* VHOST_SET_VRING_KICK */
	file.index = idx;
	file.fd = vq->kick_fd;
	rc = vdev->vhost_ops->vhost_set_vring_kick(vdev, &file);
	if (rc < 0) {
		WPRINTF("set_vring_kick failed: idx = %d", idx);
		goto fail_vring_kick;
	}

	return 0;

fail_vring_kick:
	file.index = idx;
	file.fd = -1;
	vdev->vhost_ops->vhost_set_vring_call(vdev, &file);
fail_vring:
	vhost_vq_register_eventfd(vdev, idx, false);
fail:
	return -1;
}

static int
vhost_vq_stop(struct vhost_dev *vdev, int idx)
{
	struct virtio_vq_info *vqi;
	struct vhost_vring_file file;
	struct vhost_vring_state ring;
	int rc, q_idx;

	/* sanity check */
	if (!vdev->base || !vdev->base->queues || !vdev->base->vops ||
		!vdev->vqs) {
		WPRINTF("vhost_dev is not initialized\n");
		return -1;
	}

	q_idx = idx + vdev->vq_idx;
	if (q_idx >= vdev->base->vops->nvq) {
		WPRINTF("invalid vq index: idx = %d, vq_idx = %d\n",
			idx, vdev->vq_idx);
		return -1;
	}
	vqi = &vdev->base->queues[q_idx];

	file.index = idx;
	file.fd = -1;

	/* VHOST_SET_VRING_KICK */
	vdev->vhost_ops->vhost_set_vring_kick(vdev, &file);

	/* VHOST_SET_VRING_CALL */
	vdev->vhost_ops->vhost_set_vring_call(vdev, &file);

	/* VHOST_GET_VRING_BASE */
	ring.index = idx;
	rc = vdev->vhost_ops->vhost_get_vring_base(vdev, &ring);
	if (rc < 0)
		WPRINTF("get_vring_base failed: idx = %d", idx);
	else
		vqi->last_avail = ring.num;

	/* update vqi->save_used */
	vqi->save_used = vqi->used->idx;

	/* unregister ioeventfd & irqfd */
	rc = vhost_vq_register_eventfd(vdev, idx, false);
	if (rc < 0)
		WPRINTF("unregister eventfd failed: idx = %d\n", idx);

	return rc;
}

/**
 * @brief vhost_dev initialization.
 *
 * This interface is called to initialize the vhost_dev. It must be called
 * before the actual feature negotiation with the guest OS starts.
 *
 * @param vdev Pointer to struct vhost_dev.
 * @param base Pointer to struct virtio_base.
 * @param fd fd of the vhost chardev.
 * @param vq_idx The first virtqueue which would be used by this vhost dev.
 * @param vhost_features Subset of vhost features which would be enabled.
 * @param vhost_ext_features Specific vhost internal features to be enabled.
 * @param busyloop_timeout Busy loop timeout in us.
 *
 * @return 0 on success and -1 on failure.
 */
int
vhost_dev_init(struct vhost_dev *vdev,
	       struct virtio_base *base,
	       int fd,
	       int vq_idx,
	       uint64_t vhost_features,
	       uint64_t vhost_ext_features,
	       uint32_t busyloop_timeout)
{
	uint64_t features;
	int i, rc;

	/* sanity check */
	if (!base || !base->queues || !base->vops) {
		WPRINTF("virtio_base is not initialized\n");
		goto fail;
	}

	if (!vdev->vqs || vdev->nvqs == 0) {
		WPRINTF("virtqueue is not initialized\n");
		goto fail;
	}

	if (vq_idx + vdev->nvqs > base->vops->nvq) {
		WPRINTF("invalid vq_idx: %d\n", vq_idx);
		goto fail;
	}

	if (base->backend_type == BACKEND_VHOST) {
		vdev->vhost_ops = &vhost_kernel_ops;
	} else if (base->backend_type == BACKEND_VHOST_USER) {
		vdev->vhost_ops = &vhost_user_ops;
	} else {
		WPRINTF("invalid vhost backend type: %d\n", base->backend_type);
		goto fail;
	}

	vdev->vhost_ops->vhost_init(vdev, base, fd, vq_idx, busyloop_timeout);

	rc = vdev->vhost_ops->vhost_get_features(vdev, &features);
	if (rc < 0) {
		WPRINTF("vhost_get_features failed\n");
		goto fail;
	}

	for (i = 0; i < vdev->nvqs; i++) {
		rc = vhost_vq_init(vdev, i);
		if (rc < 0)
			goto fail;
	}

	/* specific backend features to vhost */
	vdev->vhost_ext_features = vhost_ext_features & features;

	/* features supported by vhost */
	vdev->vhost_features = vhost_features & features;

	/*
	 * If the features bits are not supported by either vhost kernel
	 * mediator or configuration of device model(specified by
	 * vhost_features), they should be disabled in device_caps,
	 * which expose as virtio host_features for virtio FE driver.
	 */
	vdev->base->device_caps &= ~(vhost_features ^ features);
	vdev->started = false;

	return 0;

fail:
	vhost_dev_deinit(vdev);
	return -1;
}

/**
 * @brief vhost_dev cleanup.
 *
 * This interface is called to cleanup the vhost_dev.
 *
 * @param vdev Pointer to struct vhost_dev.
 *
 * @return 0 on success and -1 on failure.
 */
int
vhost_dev_deinit(struct vhost_dev *vdev)
{
	int i;

	if (!vdev->base || !vdev->base->queues || !vdev->base->vops)
		return -1;

	for (i = 0; i < vdev->nvqs; i++)
		vhost_vq_deinit(&vdev->vqs[i]);

	vdev->vhost_ops->vhost_deinit(vdev);

	return 0;
}

/**
 * @brief start vhost data plane.
 *
 * This interface is called to start the data plane in vhost.
 *
 * @param vdev Pointer to struct vhost_dev.
 *
 * @return 0 on success and -1 on failure.
 */
int
vhost_dev_start(struct vhost_dev *vdev)
{
	struct vhost_vring_state state;
	uint64_t features;
	int i, rc;

	if (vdev->started)
		return 0;

	/* sanity check */
	if (!vdev->base || !vdev->base->queues || !vdev->base->vops) {
		WPRINTF("virtio_base is not initialized\n");
		goto fail;
	}

	if ((vdev->base->status & VIRTIO_CONFIG_S_DRIVER_OK) == 0) {
		WPRINTF("status error 0x%x\n", vdev->base->status);
		goto fail;
	}

	/* only msix is supported now */
	if (!pci_msix_enabled(vdev->base->dev)) {
		WPRINTF("only msix is supported\n");
		goto fail;
	}

	rc = vdev->vhost_ops->vhost_set_owner(vdev);
	if (rc < 0) {
		WPRINTF("vhost_set_owner failed\n");
		goto fail;
	}

	/* set vhost internal features */
	features = (vdev->base->negotiated_caps & vdev->vhost_features) |
		vdev->vhost_ext_features;
	rc = vdev->vhost_ops->vhost_set_features(vdev, features);
	if (rc < 0) {
		WPRINTF("set_features failed\n");
		goto fail;
	}
	DPRINTF("set_features: 0x%lx\n", features);

	/* set memory table */
	rc = vdev->vhost_ops->vhost_set_mem_table(vdev);
	if (rc < 0) {
		WPRINTF("set_mem_table failed\n");
		goto fail;
	}

	/* config busyloop timeout */
	if (vdev->busyloop_timeout &&
		vdev->vhost_ops->vhost_set_vring_busyloop_timeout) {
		state.num = vdev->busyloop_timeout;
		for (i = 0; i < vdev->nvqs; i++) {
			state.index = i;
			rc = vdev->vhost_ops->vhost_set_vring_busyloop_timeout(vdev,
				&state);
			if (rc < 0) {
				WPRINTF("set_busyloop_timeout failed\n");
				goto fail;
			}
		}
	}

	/* start vhost virtqueue */
	for (i = 0; i < vdev->nvqs; i++) {
		rc = vhost_vq_start(vdev, i);
		if (rc < 0)
			goto fail_vq;
	}

	vdev->started = true;
	return 0;

fail_vq:
	while (--i >= 0)
		vhost_vq_stop(vdev, i);
fail:
	return -1;
}

/**
 * @brief stop vhost data plane.
 *
 * This interface is called to stop the data plane in vhost.
 *
 * @param vdev Pointer to struct vhost_dev.
 *
 * @return 0 on success and -1 on failure.
 */
int
vhost_dev_stop(struct vhost_dev *vdev)
{
	int i, rc = 0;

	for (i = 0; i < vdev->nvqs; i++)
		vhost_vq_stop(vdev, i);

	/* the following are done by this reset:
	 * 1) resources of the vhost dev are freed
	 * 2) vhost virtqueues are reset
	 */
	if(vdev->vhost_ops->vhost_reset_device) {
		rc = vdev->vhost_ops->vhost_reset_device(vdev);
		if (rc < 0) {
			WPRINTF("vhost_reset_device failed\n");
			rc = -1;
		}
	}

	vdev->started = false;
	return rc;
}
