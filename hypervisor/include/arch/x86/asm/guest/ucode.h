/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef UCODE_H
#define UCODE_H

/**
 * @addtogroup vp-base_vmsr
 *
 * @{
 */

/**
 * @file
 * @brief Define data structure and external APIs for ucode update
 *
 * This file includes ucode header data structure definition and function prototypes for ucode update.
 */

/**
 * @brief Microcode header definition
 *
 * This data structure defines microcode update header. Refer to 10.11.1 Microcode Update, SDM
 * Vol.3 325462-078US for details.
 *
 * @consistency N/A
 * @alignment N/A
 *
 * @remark N/A
 */
struct ucode_header {
	uint32_t	header_ver; /**< Version number of the update header. */
	uint32_t	update_ver; /**< Unique version number for the update. */
	uint32_t	date; /**< Date of the update creation in binary. */
	uint32_t	proc_sig; /**< Extended family/model/type/stepping of processor suitable for the update. */
	uint32_t	checksum; /**< Checksum of Update Data and Header. */
	uint32_t	loader_ver; /**< Version number of the loader program needed to load this update. */
	uint32_t	proc_flags; /**< Platform type information encoded in the lower 8 bits of this 4-byte field */
	uint32_t	data_size; /**< Specifies the size of the encrypted data in bytes. */
	uint32_t	total_size; /**< Specifies the total size of the microcode update in bytes. */
	uint32_t	reserved[3]; /**< Reserved fields for future expansion. */
};

void acrn_update_ucode(struct acrn_vcpu *vcpu, uint64_t v);
uint64_t get_microcode_version(void);

/**
 * @}
 */
#endif /* UCODE_H */
