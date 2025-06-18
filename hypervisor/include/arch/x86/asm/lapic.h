/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ARCH_X86_LAPIC_H
#define ARCH_X86_LAPIC_H

#include <types.h>

/**
 * @addtogroup hwmgmt_apic
 *
 * @{
 */

/**
 * @file
 *
 * @brief Local APIC management for ACRN hypervisor header
 *
 * This header file provides definitions and functions for managing the Local Advanced Programmable
 * Interrupt Controller (LAPIC).
 */

/**
 * @brief Union representing the x2APIC Interrupt Command Register (ICR).
 *
 * This union is used to represent the x2APIC Interrupt Command Register (ICR), which is responsible
 * for sending interrupts to processors in a multiprocessor system. It provides both a 64-bit view
 * and a structured view for accessing individual fields within the ICR.
 *
 * The ICR is a critical component in managing inter-processor interrupts, allowing configuration
 * of various parameters such as delivery mode, destination mode, and trigger mode.
 *
 * @remark This union is used to manipulate the x2APIC ICR settings, enabling precise control over
 *         interrupt delivery in multiprocessor environments.
 */
union apic_icr {
	uint64_t value; /**< Full 64-bit ICR value */
	struct {
		uint32_t lo_32; /**< Lower 32 bits of the ICR */
		uint32_t hi_32; /**< Higher 32 bits of the ICR */
	} value_32; /**< 32-bit access to ICR components */
	struct {
		uint32_t vector:8; /**< Interrupt vector */
		uint32_t delivery_mode:3; /**< Delivery mode for the interrupt */
		uint32_t destination_mode:1; /**< Destination mode (physical/logical) */
		uint32_t rsvd_1:2; /**< Reserved bits */
		uint32_t level:1; /**< Level (assert/deassert) */
		uint32_t trigger_mode:1; /**< Trigger mode (edge/level) */
		uint32_t rsvd_2:2; /**< Reserved bits */
		uint32_t shorthand:2; /**< Shorthand for destination */
		uint32_t rsvd_3:12; /**< Reserved bits */
		uint32_t dest_field:32; /**< Destination field specifying target processor(s) */
	} bits; /**< Field-based access to ICR components */
};

void early_init_lapic(void);

void suspend_lapic(void);

void resume_lapic(void);

uint32_t get_cur_lapic_id(void);

void init_lapic(void);
void send_lapic_eoi(void);
void send_startup_ipi(uint16_t dest_pcpu_id, uint64_t cpu_startup_start_address);
void send_dest_ipi_mask(uint32_t dest_mask, uint32_t vector);
void send_single_ipi(uint16_t pcpu_id, uint32_t vector);
void send_single_init(uint16_t pcpu_id);
void kick_pcpu(uint16_t pcpu_id);

#endif /* ARCH_X86_LAPIC_H */

/**
 * @}
 */
