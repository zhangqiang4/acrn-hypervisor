/*
 * Copyright (C) 2021-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ARCH_X86_TSC_H
#define ARCH_X86_TSC_H

#include <types.h>

/**
 * @defgroup hwmgmt_time hwmgmt.time
 * @ingroup hwmgmt
 *
 * @brief Time management.
 *
 * This module provides functionalities for managing time-related operations in the ACRN hypervisor. It includes
 * functions for reading and calibrating the Time Stamp Counter (TSC), initializing the High Precision Event Timer
 * (HPET), and implementing busy-wait delays. The module also provides conversion functions between different time
 * units and CPU ticks.
 *
 * @{
 */

/**
 * @file
 * @brief Time Stamp Counter (TSC) Management
 *
 * This file contains the declarations and inline functions for managing the Time Stamp Counter (TSC) on x86
 * architecture. It provides functions to read the TSC, get its frequency, and calibrate it. Additionally, it includes
 * initialization routines for the High Precision Event Timer (HPET).
 */

#define TSC_PER_MS	((uint64_t)get_tsc_khz())  /**< TSC ticks per millisecond. */

/**
 * @brief Read Time Stamp Counter (TSC).
 *
 * This function invokes the RDTSC instruction to read the time-stamp counter. The RDTSC instruction reads the current
 * value of the processor's time-stamp counter (a 64-bit MSR IA32_TIME_STAMP_COUNTER) into the EDX:EAX registers. The
 * EDX register is loaded with the high-order 32 bits of the MSR, and the EAX register is loaded with the low-order 32
 * bits. Refer to Chapter 4.3 RDTSC Instruction, Vol. 2, SDM 325426-078 for more details.
 *
 * @return A uint64_t TSC value.
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark CPUID.01H:EDX.TSC[bit 4] shall be set to 1.
 */
static inline uint64_t rdtsc(void)
{
	uint32_t lo, hi;

	asm volatile("rdtsc" : "=a" (lo), "=d" (hi));
	return ((uint64_t)hi << 32U) | lo;
}

uint32_t get_tsc_khz(void);

void calibrate_tsc(void);

void hpet_init(void);

#endif /* ARCH_X86_TSC_H */

/**
 * @}
 */