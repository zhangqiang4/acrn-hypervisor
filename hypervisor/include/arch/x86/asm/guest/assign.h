/*
 * Copyright (C) 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ASSIGN_H
#define ASSIGN_H

#include <types.h>
#include <ptdev.h>

/**
 * @addtogroup vp-dm_ptirq vp-dm.ptirq
 *
 * @{
 */

/**
 * @file
 *
 * @brief Public APIs for passthrough device interrupt assignment
 */

void ptirq_intx_ack(struct acrn_vm *vm, uint32_t virt_gsi);

int32_t ptirq_prepare_msix_remap(struct acrn_vm *vm, uint16_t virt_bdf,  uint16_t phys_bdf,
				uint16_t entry_nr, struct msi_info *info);

int32_t ptirq_intx_pin_remap(struct acrn_vm *vm, uint32_t virt_gsi);

int32_t ptirq_add_intx_remapping(struct acrn_vm *vm, uint32_t virt_gsi, uint32_t phys_gsi);

void ptirq_remove_intx_remapping(const struct acrn_vm *vm, uint32_t gsi, bool is_phy_gsi);

void ptirq_remove_msix_remapping(const struct acrn_vm *vm, uint16_t phys_bdf, uint32_t vector_count);

void ptirq_remove_configured_intx_remappings(const struct acrn_vm *vm);

/**
  * @}
  */

#endif /* ASSIGN_H */
