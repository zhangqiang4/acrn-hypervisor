/*
 * Copyright (C) 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <asm/lib/bits.h>
#include <asm/cpu.h>
#include <asm/per_cpu.h>
#include <softirq.h>

/**
 * @defgroup hwmgmt_softirq hwmgmt.softirq
 * @ingroup hwmgmt
 * @brief The definition and implementation of Soft Interrupt related stuff.
 *
 * @{
 */

/**
 * @file
 * @brief Implementation for soft interrupt handling
 */

/**
 * @brief The registered soft interrupt handlers.
 */
static softirq_handler softirq_handlers[NR_SOFTIRQS];

/**
 * @brief Initialize soft interrupt framework
 *
 * Do nothing here because soft interrupt handlers are registered by specific modules.
 *
 * @return None
 */
void init_softirq(void)
{
}

/**
 * @brief Register handler for a soft interrupt
 *
 * Set given handler into the global array softirq_handlers[]. Each element corresponds to a
 * handler function for a specific software interrupt.
 *
 * @param[in] nr The soft interrupt number
 * @param[in] handler The soft interrupt handler
 *
 * @return None
 *
 * @pre nr < NR_SOFTIRQS
 */
void register_softirq(uint16_t nr, softirq_handler handler)
{
	softirq_handlers[nr] = handler;
}

/**
 * @brief Raise a soft interrupt
 *
 * Unlike hardware interrupts, soft interrupts are raised by software, typically from hardware
 * interrupt handlers to leave time-consuming work handled by softirq handlers.
 * This function marks given soft interrupt pending on current processor.
 *
 * @param[in] nr The soft interrupt number
 *
 * @return None
 *
 * @pre nr < NR_SOFTIRQS
 */
void fire_softirq(uint16_t nr)
{
	bitmap_set_lock(nr, &per_cpu(softirq_pending, get_pcpu_id()));
}

/**
 * @brief Internal serialized check and handle logic for soft interrupts
 *
 * This function check pending soft interrupts on current pCPU by checking the percpu variable
 * softirq_pending and invoke corresponding handler one by one.
 * If multiple kinds of soft interrupts are pending, that with smaller number goes first.
 *
 * @param[in] cpu_id Physical processor ID for current processor
 *
 * @return None
 */
static void do_softirq_internal(uint16_t cpu_id)
{
	volatile uint64_t *softirq_pending_bitmap =
			&per_cpu(softirq_pending, cpu_id);
	uint16_t nr = ffs64(*softirq_pending_bitmap);

	while (nr < NR_SOFTIRQS) {
		bitmap_clear_lock(nr, softirq_pending_bitmap);
		(*softirq_handlers[nr])(cpu_id);
		nr = ffs64(*softirq_pending_bitmap);
	}
}

/**
 * @brief Handle pending soft interrupts on current processor
 *
 * It's called at the end of hardware interrupt context with local interrupts disabled. However
 * to reduce interrupt servicing latency, interrupts are enabled when executing soft interrupt
 * handlers. Thus this function is reentrant for every single processor. To make the internal
 * logic serialized and simpler, the softirq_servicing percpu flag is used to skip nested
 * do_softirq and return to the first one.
 *
 * During the handling process of soft interrupt with local interrupt enabled, new-coming
 * interrupts may have raised new soft interrupts. So after restoring the local interrupt
 * disabled, pending soft interrupts need to be checked again.
 *
 * @return None
 */
void do_softirq(void)
{
	uint16_t cpu_id = get_pcpu_id();

	if (per_cpu(softirq_servicing, cpu_id) == 0U) {
		per_cpu(softirq_servicing, cpu_id) = 1U;

		CPU_IRQ_ENABLE();
		do_softirq_internal(cpu_id);
		CPU_IRQ_DISABLE();

		do_softirq_internal(cpu_id);
		per_cpu(softirq_servicing, cpu_id) = 0U;
	}
}

/**
 * @}
 */
