/*
 * Copyright (C) 2021-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef COMMON_IRQ_H
#define COMMON_IRQ_H

#include <lib/util.h>
#include <asm/lib/spinlock.h>

/**
 * @addtogroup hwmgmt_irq hwmgmt.irq
 *
 * @{
 */

/**
 * @file common/irq.h
 *
 * @brief Public APIs for common IRQ handling
 */

#define NR_IRQS			256U		/**< Max supported IRQ count */
#define IRQ_INVALID		0xffffffffU	/**< The number to mark an IRQ invalid */

#define IRQ_ALLOC_BITMAP_SIZE	INT_DIV_ROUNDUP(NR_IRQS, 64U)	/**< The bitmap size for all possible IRQs in 64-bit
								 * long integers */

#define IRQF_NONE	(0U)		/**< None IRQ flags present */
#define IRQF_LEVEL	(1U << 1U)	/**< 1: level trigger; 0: edge trigger */
#define IRQF_PT		(1U << 2U)	/**< 1: for passthrough dev */

/**
 * @brief The function prototype of an irq action handler
 *
 * It accepts an irq number and an irq private data and returns nothing. The priv_data is useful
 * when one function is used for different interrupts with different private data.
 */
typedef void (*irq_action_t)(uint32_t irq, void *priv_data);

/**
 * @brief Interrupt descriptor
 *
 * Any field change in below required lock protection with irqsave
 */
struct irq_desc {
	uint32_t irq;		/**< Index to irq_desc_base */

	void *arch_data;	/**< Architecture specific data */

	irq_action_t action;	/**< Callback registered from component */
	void *priv_data;	/**< irq_action private data */
	uint32_t flags;		/**< Flags for trigger mode/ptdev */

	spinlock_t lock;	/**< The lock for this IRQ descriptor */
};

uint32_t reserve_irq_num(uint32_t req_irq);
int32_t request_irq(uint32_t req_irq, irq_action_t action_fn, void *priv_data,
			uint32_t flags);
void free_irq(uint32_t irq);
void set_irq_trigger_mode(uint32_t irq, bool is_level_triggered);
void do_irq(const uint32_t irq);

void init_interrupt(uint16_t pcpu_id);

void init_interrupt_arch(uint16_t pcpu_id);
void init_irq_descs_arch(struct irq_desc *descs);
void setup_irqs_arch(void);
void free_irq_arch(uint32_t irq);
bool request_irq_arch(uint32_t irq);
void pre_irq_arch(const struct irq_desc *desc);
void post_irq_arch(const struct irq_desc *desc);

/**
 * @}
 */
#endif /* COMMON_IRQ_H */
