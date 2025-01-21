/*
 * Copyright (C) 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef SOFTIRQ_H
#define SOFTIRQ_H

/**
 * @addtogroup hwmgmt_softirq hwmgmt.softirq
 *
 * @{
 */

/**
 * @file
 * @brief Definitions and public APIs for soft interrupts
 */

#define SOFTIRQ_TIMER		0U	/**< IRQ number for timer soft interrupt */
#define SOFTIRQ_PTDEV		1U	/**< IRQ number for pass-through device soft interrupt */
#define SOFTIRQ_THERMAL		2U	/**< IRQ number for thermal soft interrupt */
#define SOFTIRQ_CMCI            3U	/**< IRQ number for CMCI soft interrupt */
#define NR_SOFTIRQS             4U	/**< Total number of soft interrupts */

/**
 * @brief The prototype of soft interrupt handlers
 *
 * A soft interrupt handler takes the physical processor ID as the argument.
 */
typedef void (*softirq_handler)(uint16_t cpu_id);

void init_softirq(void);
void register_softirq(uint16_t nr, softirq_handler handler);
void fire_softirq(uint16_t nr);
void do_softirq(void);

/**
 * @}
 */
#endif /* SOFTIRQ_H */
