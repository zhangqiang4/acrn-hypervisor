/*
 * ACRN TRACE
 *
 * Copyright (C) 2017-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef TRACE_H
#define TRACE_H
#include <asm/per_cpu.h>

/**
 * @addtogroup debug
 *
 * @{
 */

/**
 * @file
 * @brief Trace Event Identifiers and Logging Functions
 *
 * This file defines trace event identifiers used for logging various system
 * events and actions. It also declares functions for logging trace events
 * with different data formats. These identifiers and functions are used to
 * monitor and analyze system behavior, particularly in virtualized environments.
 */

 /* TIMER EVENT */
 /** @brief Trace event for adding a timer action. */
#define TRACE_TIMER_ACTION_ADDED	0x1U
/** @brief Trace event for picking up a timer action. */
#define TRACE_TIMER_ACTION_PCKUP	0x2U
/** @brief Trace event for updating a timer action. */
#define TRACE_TIMER_ACTION_UPDAT	0x3U
/** @brief Trace event for a timer interrupt. */
#define TRACE_TIMER_IRQ			0x4U

/** @brief Trace event for VM exit. */
#define TRACE_VM_EXIT			0x10U
/** @brief Trace event for VM entry. */
#define TRACE_VM_ENTER			0X11U

/* event to calculate cpu usage with shared pcpu */
/** @brief Trace event for scheduling the next task. */
#define TRACE_SCHED_NEXT		0x20U

/** @brief Base value for VM exit trace events. */
#define TRACE_VMEXIT_ENTRY		0x10000U

/** @brief Trace event for VM exit due to exception or NMI. */
#define TRACE_VMEXIT_EXCEPTION_OR_NMI	    (TRACE_VMEXIT_ENTRY + 0x00000000U)
/** @brief Trace event for VM exit due to external interrupt. */
#define TRACE_VMEXIT_EXTERNAL_INTERRUPT     (TRACE_VMEXIT_ENTRY + 0x00000001U)
/** @brief Trace event for VM exit due to interrupt window. */
#define TRACE_VMEXIT_INTERRUPT_WINDOW	    (TRACE_VMEXIT_ENTRY + 0x00000002U)
/** @brief Trace event for VM exit due to CPUID instruction. */
#define TRACE_VMEXIT_CPUID		    (TRACE_VMEXIT_ENTRY + 0x00000004U)
/** @brief Trace event for VM exit due to RDTSC instruction. */
#define TRACE_VMEXIT_RDTSC		    (TRACE_VMEXIT_ENTRY + 0x00000010U)
/** @brief Trace event for VM exit due to VMCALL instruction. */
#define TRACE_VMEXIT_VMCALL		    (TRACE_VMEXIT_ENTRY + 0x00000012U)
/** @brief Trace event for VM exit due to control register access. */
#define TRACE_VMEXIT_CR_ACCESS		    (TRACE_VMEXIT_ENTRY + 0x0000001CU)
/** @brief Trace event for VM exit due to I/O instruction. */
#define TRACE_VMEXIT_IO_INSTRUCTION	    (TRACE_VMEXIT_ENTRY + 0x0000001EU)
/** @brief Trace event for VM exit due to RDMSR instruction. */
#define TRACE_VMEXIT_RDMSR		    (TRACE_VMEXIT_ENTRY + 0x0000001FU)
/** @brief Trace event for VM exit due to WRMSR instruction. */
#define TRACE_VMEXIT_WRMSR		    (TRACE_VMEXIT_ENTRY + 0x00000020U)
/** @brief Trace event for VM exit due to EPT violation. */
#define TRACE_VMEXIT_EPT_VIOLATION	    (TRACE_VMEXIT_ENTRY + 0x00000030U)
/** @brief Trace event for VM exit due to EPT misconfiguration. */
#define TRACE_VMEXIT_EPT_MISCONFIGURATION   (TRACE_VMEXIT_ENTRY + 0x00000031U)
/** @brief Trace event for VM exit due to RDTSCP instruction. */
#define TRACE_VMEXIT_RDTSCP		    (TRACE_VMEXIT_ENTRY + 0x00000033U)
/** @brief Trace event for VM exit due to APICV write. */
#define TRACE_VMEXIT_APICV_WRITE	    (TRACE_VMEXIT_ENTRY + 0x00000038U)
/** @brief Trace event for VM exit due to APICV access. */
#define TRACE_VMEXIT_APICV_ACCESS	    (TRACE_VMEXIT_ENTRY + 0x00000039U)
/** @brief Trace event for VM exit due to APICV virtual EOI. */
#define TRACE_VMEXIT_APICV_VIRT_EOI	    (TRACE_VMEXIT_ENTRY + 0x0000003AU)

/** @brief Trace event for unhandled VM exits. */
#define TRACE_VMEXIT_UNHANDLED		0x20000U

void TRACE_2L(uint32_t evid, uint64_t e, uint64_t f);
void TRACE_4I(uint32_t evid, uint32_t a, uint32_t b, uint32_t c, uint32_t d);
void TRACE_16STR(uint32_t evid, const char name[]);

/**
 * @}
 */
#endif /* TRACE_H */
