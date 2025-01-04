/*
 * Copyright (C) 2020-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef IVSHMEM_H
#define IVSHMEM_H

/**
 * @addtogroup vp-dm_vperipheral
 *
 * @{
 */

/**
 * @file
 * @brief Definitions for Inter-VM shared memory device (ivshmem).
 *
 * This file defines macros, data structure and functions for Inter-VM shared memory device.
 */

#define	IVSHMEM_VENDOR_ID  0x1af4U /**< This macro defines vendor ID for Inter-VM shared memory device. */
#define	IVSHMEM_DEVICE_ID  0x1110U /**< This macro defines device ID for Inter-VM shared memory device. */
#define	IVSHMEM_INTEL_SUBVENDOR_ID  0x8086U /**< This macro defines subvendor ID for Inter-VM shared memory device. */
#ifdef CONFIG_IVSHMEM_ENABLED

/**
 * @brief This macro defines max peers number for each ivshmem region, which is determined by the `CONFIG_MAX_VM_NUM`
 * configuration parameter.
 */
#define MAX_IVSHMEM_PEER_NUM (CONFIG_MAX_VM_NUM)

/**
 * @brief This macro defines max number of MSIX table entries of shmem device. The value is set to 8.
 */
#define MAX_IVSHMEM_MSIX_TBL_ENTRY_NUM 8U

/**
 * @brief Data structure to illustrate a ivshmem device region.
 *
 * This structure defines a shared memory region for Inter-VM shared memory devices. It includes information such as
 * the name of the region, its ID, physical address, size, and doorbell peers.
 */
struct ivshmem_shm_region {
	char name[32]; /**< Name of the shared memory region. */
	uint16_t region_id; /**< Identifier for the shared memory region. */
	uint8_t reserved[6]; /**< Reserved space for alignment and future use. */
	uint64_t hpa; /**< Host physical address of the shared memory region. */
	uint64_t size; /**< Size of the shared memory region in bytes. */
	struct ivshmem_device *doorbell_peers[MAX_IVSHMEM_PEER_NUM];  /**< Array of pointers to doorbell peers. */
};

extern const struct pci_vdev_ops vpci_ivshmem_ops;

void init_ivshmem_shared_memory(void);

int32_t create_ivshmem_vdev(struct acrn_vm *vm, struct acrn_vdev *dev);

int32_t destroy_ivshmem_vdev(struct pci_vdev *vdev);
#endif /* CONFIG_IVSHMEM_ENABLED */

#endif /* IVSHMEM_H */

/**
 * @}
 */
