/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <errno.h>
#include <asm/guest/vm.h>
#include <debug/hypercall.h>

/**
 * @addtogroup debug
 *
 * @{
 */

/**
 * @file
 * @brief Hypercall Stub Implementations in Release Mode
 *
 * This file provides stub implementations for hypercall functions related to
 * logging, hardware information retrieval, and profiling operations. These
 * functions are placeholders and currently return an error code indicating
 * that the operation is not permitted. They can be replaced with actual
 * implementations as needed.
 */

/**
 * @brief Set up hypervisor NPK logging.
 *
 * This stub function simulates setting up logging for the hypervisor using
 * the NPK (North Peak PCI device) log. It currently returns an error code
 * indicating that the operation is not permitted.
 *
 * @param[in] vcpu The virtual CPU context.
 * @param[in] target_vm The target virtual machine.
 * @param[in] param1 Additional parameter for the hypercall.
 * @param[in] param2 Additional parameter for the hypercall.
 *
 * @return An error code indicating the result of the operation.
 * @retval -EPERM Always returns -EPERM, indicating the operation is not permitted.
 */
int32_t hcall_setup_hv_npk_log(__unused struct acrn_vcpu *vcpu, __unused struct acrn_vm *target_vm,
		__unused uint64_t param1, __unused uint64_t param2)
{
	return -EPERM;
}

/**
 * @brief Retrieve hardware information.
 *
 * This stub function simulates retrieving hardware information for a virtual
 * machine. It currently returns an error code indicating that the operation
 * is not permitted.
 *
 * @param[in] vcpu The virtual CPU context.
 * @param[in] target_vm The target virtual machine.
 * @param[in] param1 Additional parameter for the hypercall.
 * @param[in] param2 Additional parameter for the hypercall.
 *
 * @return An error code indicating the result of the operation.
 * @retval -EPERM Always returns -EPERM, indicating the operation is not permitted.
 */
int32_t hcall_get_hw_info(__unused struct acrn_vcpu *vcpu, __unused struct acrn_vm *target_vm,
		__unused uint64_t param1, __unused uint64_t param2)
{
	return -EPERM;
}

/**
 * @brief Perform profiling operations.
 *
 * This stub function simulates performing profiling operations for a virtual
 * machine. It currently returns an error code indicating that the operation
 * is not permitted.
 *
 * @param[in] vcpu The virtual CPU context.
 * @param[in] target_vm The target virtual machine.
 * @param[in] param1 Additional parameter for the hypercall.
 * @param[in] param2 Additional parameter for the hypercall.
 *
 * @return An error code indicating the result of the operation.
 * @retval -EPERM Always returns -EPERM, indicating the operation is not permitted.
 */
int32_t hcall_profiling_ops(__unused struct acrn_vcpu *vcpu, __unused struct acrn_vm *target_vm,
		__unused uint64_t param1, __unused uint64_t param2)
{
	return -EPERM;
}

/**
 * @}
 */