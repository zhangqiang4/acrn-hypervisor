/*
 * Copyright (C) 2018-2022 Intel Corporation.
 * SPDX-License-Identifier: BSD-3-Clause
 */
/**
 * @file vmtrr.h
 *
 * @brief MTRR Virtualization
 */
#ifndef VMTRR_H
#define VMTRR_H

#include <board_info.h>

/**
 * @brief MTRR Virtualization
 *
 * @addtogroup acrn_mem ACRN Memory Management
 * @{
 */

#define VARIABLE_RANGE_MTRR_NUM_MAX	10U

union mtrr_cap_reg {
	uint64_t value;
	struct {
		uint32_t vcnt:8;
		uint32_t fix:1;
		uint32_t res0:1;
		uint32_t wc:1;
		uint32_t smrr:1;
		uint32_t prmrr:1;
		uint32_t res1:19;
		uint32_t res2:32;
	} bits;
};

union mtrr_def_type_reg {
	uint64_t value;
	struct {
		uint32_t type:8;
		uint32_t res0:2;
		uint32_t fixed_enable:1;
		uint32_t enable:1;
		uint32_t res1:20;
		uint32_t res2:32;
	} bits;
};

union mtrr_physbase{
	uint64_t value;
	struct {
		uint32_t type:8;
		uint32_t res0:4;
		uint64_t physbase:(MAXIMUM_PA_WIDTH - 8 - 4);
		uint32_t res1:(64 - MAXIMUM_PA_WIDTH);
	} bits;
};

union mtrr_physmask{
	uint64_t value;
	struct {
		uint32_t res0:11;
		uint32_t valid:1;
		uint64_t physmask:(MAXIMUM_PA_WIDTH - 8 - 4);
		uint32_t res1:(64 - MAXIMUM_PA_WIDTH);
	} bits;
};

/* Variable MTRRs are emulated */
struct mtrr_variable_range_reg {
	union mtrr_physbase physbase;
	union mtrr_physmask physmask;
};

struct acrn_vmtrr {
	union mtrr_cap_reg cap;
	union mtrr_def_type_reg def_type;
	struct mtrr_variable_range_reg variable_range[VARIABLE_RANGE_MTRR_NUM_MAX];
};

struct acrn_vcpu;
struct acrn_vm;
/**
 * @brief Virtual MTRR MSR write
 *
 * @param[inout] vcpu The pointer that points VCPU data structure
 * @param[in] msr Virtual MTRR MSR Address
 * @param[in] value The value that will be writen into virtual MTRR MSR
 */
int32_t write_vmtrr(struct acrn_vcpu *vcpu, uint32_t msr, uint64_t value);
/**
 * @brief Virtual MTRR MSR read
 *
 * @param[in] vcpu The pointer that points VCPU data structure
 * @param[in] msr Virtual MTRR MSR Address
 *
 * @return The specified virtual MTRR MSR value
 */
uint64_t read_vmtrr(const struct acrn_vcpu *vcpu, uint32_t msr);

/**
 * @brief Virtual MTRR initialization
 *
 * @param[inout] vm The pointer that points VM data structure
 */
void init_vmtrr(struct acrn_vm *vm);
void add_variable_mtrr(struct acrn_vm *vm, uint64_t base, uint64_t size, uint8_t type);

/**
 * @}
 */
#endif /* VMTRR_H */
