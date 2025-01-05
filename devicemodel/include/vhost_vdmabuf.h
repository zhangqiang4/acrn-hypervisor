/*
 * Copyright (C) 2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

/**
 * @file vhost_vdmabuf.h
 */

#ifndef __VHOST_VDMABUF_H__
#define __VHOST_VDMABUF_H__

#include "virtio.h"
#include "vhost.h"

#define VDMABUF_VQ_RECV  0
#define	VDMABUF_VQ_SEND  1
#define VDMABUF_VQ_MAX   2

#define VHOST_VDMABUF_QUEUE_SIZE 128

#define VHOST_VDMABUF_FEATURES (1ULL << VIRTIO_F_VERSION_1)

struct virtio_vdmabuf_config {
	uint64_t vmid;
}__attribute__((packed));

struct virtio_vdmabuf {
	struct virtio_base base;
	pthread_mutex_t mtx;
	struct virtio_vq_info queues[VDMABUF_VQ_MAX];
	struct virtio_vdmabuf_config config;
	struct vhost_vdmabuf *vhost_vdmabuf;
	uint64_t        features;
	uint64_t size;
};

struct vhost_vdmabuf {
	struct vhost_dev vdev;
	struct vhost_vq vqs[VDMABUF_VQ_MAX];
	int vhost_fd;
	bool vhost_started;
};
#endif
