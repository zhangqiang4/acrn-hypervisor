/*
 * Copyright (C) 2020-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef MMIO_DEV_H
#define MMIO_DEV_H

/**
 * @defgroup vp-dm_mmio-dev vp-dm.mmio-dev
 * @ingroup vp-dm
 * @brief Implementation of MMIO devices assign/deassign to/from a VM.
 *
 * This module provides the interface for the assignment and deassignment of MMIO devices to a VM. These
 * operations are essential for MMIO device passthrough. These functions ensure proper alignment and validity
 * of the memory regions before performing the operations.
 *
 * The hypervisor facilitates MMIO device passthrough by removing the specified MMIO regions from the Service OS
 * and updating the virtual ACPI table for the VM based on the physical ACPI table of the device.
 *
 * @{
 */

/**
 * @file
 * @brief Header file for MMIO device assignment functions.
 *
 * This file declares the interface for functions that assign and deassign MMIO devices to/from VMs.
 */

int32_t assign_mmio_dev(struct acrn_vm *vm, const struct acrn_mmiodev *mmiodev);
int32_t deassign_mmio_dev(struct acrn_vm *vm, const struct acrn_mmiodev *mmiodev);

#endif /* MMIO_DEV_H */

/**
 * @}
 */