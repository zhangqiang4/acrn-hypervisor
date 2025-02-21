/*
 * Copyright (C) 2018-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef SECURITY_H
#define SECURITY_H

/**
 * @defgroup hwmgmt_security hwmgmt.security
 * @ingroup hwmgmt
 * @brief Implementation of security related utilities functions and API detecting the security capabilities.
 *
 * This module detects security mitigation system software interface for known processor vulnerabilities,
 * such as Spectre, L1TF, MDS, MCE on underlying platform, wraps utility functions for processor
 * vulnerabilities mitigation, or initializes the runtime stack protector in hypervisor.
 *
 * Usage Remarks: Init module uses this module to verify security system software interfaces and setup stack
 * protector context, and vCPU module uses this module to clear CPU internal buffers.
 *
 * Dependency Justification: this module uses hwmgmt.cpu module to access MSR registers, and also uses
 * hwmgmt.hw_caps to fetch hardware capabilities.
 *
 * @{
 */

/**
 * @file
 * @brief This file declares the external data structures and APIs of the hwmgmt.security module.
 */

#define IBRS_NONE  0 /**<No hardware-base speculation control support, retpoline is used. */
#define IBRS_RAW   1 /**<Raw IBRS + IBPB speculation control support */
#define IBRS_OPT   2 /**<With STIBP optimization speculation control support */

#ifndef ASSEMBLER
int32_t get_ibrs_type(void);
void cpu_l1d_flush(void);
bool check_cpu_security_cap(void);
void cpu_internal_buffers_clear(void);
uint64_t get_random_value(void);
void disable_rrsba(void);

#ifdef STACK_PROTECTOR
/**
 * @brief Structure to define stack proctor canary type.
 *
 * GCC compiler will generate extra code and use IA32_FS_BASE+28H to access canary data.
 * The base address of instance with this type will be set to IA32_FS_BASE MSR.
 *
 * @consistency n/a
 *
 * @alignment 8
 *
 * @remark Canary should be initialized with a random value.
 */
struct stack_canary {
	uint8_t reserved[40]; /**< Reserved. */
	uint64_t canary; /**< Canary variable. */
};

void set_fs_base(void);
#endif

#endif /* ASSEMBLER */
/**
 * @}
 */

#endif /* SECURITY_H */
