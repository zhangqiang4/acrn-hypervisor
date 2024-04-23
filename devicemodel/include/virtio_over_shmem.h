/*
 * Copyright (C) 2024 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#ifndef __BACKENDS_VIRTIO_OVER_SHMEM_H__
#define __BACKENDS_VIRTIO_OVER_SHMEM_H__

#include <stddef.h>
#include <stdint.h>
#include <linux/virtio_pci.h>

#include <pci_core.h>

#include "types.h"
#include "shmem.h"

#define MAX_BACKEND 16
#define MAX_IRQS    8



struct virtio_backend_info {
	struct shmem_ops *shmem_ops;
	char *shmem_devpath;	
	char *opts;
	struct pci_vdev_ops *pci_vdev_ops;


	struct virtio_shmem_header *virtio_header;
	struct funcinfo fi_funcs; 
	struct shmem_info shmem_info;
	int evt_fds[MAX_IRQS];
	struct mevent *mevents[MAX_IRQS];
	struct pci_vdev pci_vdev;

	void (*hook_before_init)(struct virtio_backend_info *info);
};

struct dm_backend {
	int be_cnt;
	struct virtio_backend_info *info[MAX_BACKEND];
};

#define BACKEND_FLAG_PRESENT   0x0001

struct virtio_shmem_header {
	uint32_t revision;
	uint32_t size;
	uint32_t device_id;
	uint32_t vendor_id;

    union {
		uint32_t write_transaction;
		struct {
			uint16_t write_offset;
			uint16_t write_size;
		};
	};
	uint8_t config_event;
	uint8_t queue_event;
	uint8_t __rsvd[2];
	union {
		uint32_t frontend_status;
		struct {
			uint16_t frontend_flags;
			uint16_t frontend_id;
		};
	};
	union {
		uint32_t backend_status;
		struct {
			uint16_t backend_flags;
			uint16_t backend_id;
		};
	};

	struct virtio_pci_common_cfg common_config;
	char config[];
};

#define VI_REG_OFFSET(reg) \
	__builtin_offsetof(struct shmem_virtio_header, reg)

#endif  /* __BACKENDS_VIRTIO_OVER_SHMEM_H__ */
