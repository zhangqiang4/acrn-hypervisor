/*
 * Copyright (C) 2021-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <softirq.h>
#include <irq.h>
#include <logmsg.h>
#include <asm/cpu.h>
#include <asm/msr.h>
#include <asm/irq.h>
#include <asm/apicreg.h>
#include <hw/hw_timer.h>

/**
 * @addtogroup hwmgmt_timer
 *
 * @{
 */

/**
 * @file
 * @brief TSC deadline timer management functions.
 *
 * This file contains the implementation of TSC deadline timer management functions, including initialization and
 * setting of hardware timeouts.
 */

/* run in interrupt context */

/**
 * @brief Timer expired handler.
 *
 * This function handles the timer expiration interrupt by firing the timer softirq.
 *
 * @param[in] irq  the IRQ number (unused).
 * @param[in] data The data associated with the IRQ (unused).
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
static void timer_expired_handler(__unused uint32_t irq, __unused void *data)
{
	fire_softirq(SOFTIRQ_TIMER);
}

/**
 * @brief Initialize APIC timer.
 *
 * This function writes LVT Timer Register to set the timer vector and TSC-Deadline timer mode in x2APIC mode.
 * Refer to Chapter 11.5.4.1, TSC-Deadline Mode, Volume 3, SDM 325462-078 for more details.
 *
 * If current CPU is BSP, this function would request timer IRQ at beginning.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
void init_hw_timer(void)
{
	int32_t retval = 0;

	if (get_pcpu_id() == BSP_CPU_ID) {
		retval = request_irq(TIMER_IRQ, timer_expired_handler, NULL, IRQF_NONE);
		if (retval < 0) {
			pr_err("Timer setup failed");
		}
	}

	if (retval >= 0) {
		uint32_t val = TIMER_VECTOR;
		val |= APIC_LVTT_TM_TSCDLT; /* TSC deadline and unmask */
		msr_write(MSR_IA32_EXT_APIC_LVT_TIMER, val);
		/* SDM 11.5.4.1: In x2APIC mode, the processor ensures the
                   ordering of this write and any subsequent WRMSR to the
                   deadline; no fencing is required. */

		/* disarm timer */
		msr_write(MSR_IA32_TSC_DEADLINE, 0UL);
	}
}

/**
 * @}
 */