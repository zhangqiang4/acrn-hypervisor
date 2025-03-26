/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <asm/guest/vcpu.h>

/**
 * @addtogroup debug
 *
 * @{
 */

/**
 * @file
 * @brief Profiling Handler Stub Implementations in Release Mode
 *
 * This file provides stub implementations for profiling handler functions in a virtualized
 * environment. These functions are placeholders and currently do not perform any actual
 * operations. They are used to maintain a consistent interface and can be replaced with
 * actual implementations as needed.
 */

/**
 * @brief Handle profiling on VM entry.
 *
 * This stub function simulates handling profiling operations when a VM
 * is entered. It is intended to be used for performance monitoring and analysis.
 * In release mode, it performs no operation, serving as a placeholder.
 *
 * @param[in] vcpu The virtual CPU context.
 *
 * @return None.
 */
void profiling_vmenter_handler(__unused struct acrn_vcpu *vcpu) {}

/**
 * @brief Handle profiling before VM exit.
 *
 * This stub function simulates handling profiling operations before a VM
 * exits. It is intended to be used for performance monitoring and analysis.
 * In release mode, it performs no operation, serving as a placeholder.
 *
 * @param[in] vcpu The virtual CPU context.
 *
 * @return None.
 */
void profiling_pre_vmexit_handler(__unused struct acrn_vcpu *vcpu) {}

/**
 * @brief Handle profiling after VM exit.
 *
 * This stub function simulates handling profiling operations after a virtual machine
 * exits. It is intended to be used for performance monitoring and analysis.
 * In release mode, it performs no operation, serving as a placeholder.
 *
 * @param[in] vcpu The virtual CPU context.
 *
 * @return None.
 */
void profiling_post_vmexit_handler(__unused struct acrn_vcpu *vcpu) {}

/**
 * @brief Set up profiling.
 *
 * This stub function simulates setting up profiling operations. It is intended to
 * initialize any necessary resources or configurations for profiling.
 * In release mode, it performs no operation, serving as a placeholder.
 *
 * @return None.
 */
void profiling_setup(void) {}

/**
 * @brief Sample function marking the end of VM exit handling.
 *
 * This function is intended to mark the end of VM exit handling for profiling
 * purposes. In release mode, it performs no operation, serving as a placeholder.
 *
 * @param[in] vcpu The virtual CPU context.
 */
void sample_vmexit_end(__unused struct acrn_vcpu *vcpu) {}

/**
 * @brief Sample function marking the beginning of VM exit handling.
 *
 * This function is intended to mark the beginning of VM exit handling for
 * profiling purposes. In release mode, it performs no operation, serving as a placeholder.
 *
 * @param[in] vcpu The virtual CPU context.
 *
 * @return None.
 */
void sample_vmexit_begin(__unused struct acrn_vcpu *vcpu) {}

/**
 * @}
 */