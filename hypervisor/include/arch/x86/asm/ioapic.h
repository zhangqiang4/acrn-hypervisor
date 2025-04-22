/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ARCH_X86_IOAPIC_H
#define ARCH_X86_IOAPIC_H

#include <asm/apicreg.h>

/**
 * @addtogroup  hwmgmt_ioapic hwmgmt.ioapic
 *
 * @{
 */

 /**
 * @file ioapic.h
 *
 * @brief Interface to operate physical IOAPIC.
 *
 * This file define the interface to operate physical IOAPIC device.
 *
 */

#define NR_LEGACY_IRQ		16U /**< Defines the number of legacy IRQs */

/**
 * @brief Sets the number of legacy pins to be equal to the number of legacy IRQs.
 *
 * This macro defines `NR_LEGACY_PIN` as equal to `NR_LEGACY_IRQ`, ensuring
 * a one-to-one mapping between legacy interrupt pins and IRQs.
 *
 */
#define NR_LEGACY_PIN		NR_LEGACY_IRQ

 /**
 * @brief Structure representing I/O APIC information.
 *
 * This structure contains detailed information about an I/O APIC, including
 * its identifier, register address, and interrupt configuration. It is used
 * to manage and configure interrupt routing in systems with multiple I/O APICs.
 *
 */
struct ioapic_info {
	uint8_t id;		/**< IOAPIC ID as indicated in ACPI MADT */
	uint32_t addr;		/**< IOAPIC Register address */
	uint32_t gsi_base;	/**< Global System Interrupt where this IO-APIC's interrupt input start */
	uint32_t nr_pins;	/**< Number of Interrupt inputs as determined by Maximum Redirection Entry Register */
};

void ioapic_setup_irqs(void);

bool is_ioapic_irq(uint32_t irq);
uint32_t gsi_to_ioapic_pin(uint32_t gsi);
int32_t init_ioapic_id_info(void);
uint8_t ioapic_irq_to_ioapic_id(uint32_t irq);

uint8_t get_platform_ioapic_info (struct ioapic_info **plat_ioapic_info);


uint32_t ioapic_gsi_to_irq(uint32_t gsi);
void ioapic_set_rte(uint32_t irq, union ioapic_rte rte);

void ioapic_get_rte(uint32_t irq, union ioapic_rte *rte);

void suspend_ioapic(void);

void resume_ioapic(void);

void ioapic_gsi_mask_irq(uint32_t irq);
void ioapic_gsi_unmask_irq(uint32_t irq);

void ioapic_get_rte_entry(void *ioapic_base, uint32_t pin, union ioapic_rte *rte);

void *gsi_to_ioapic_base(uint32_t gsi);
uint32_t get_max_nr_gsi(void);
uint8_t get_gsi_to_ioapic_index(uint32_t gsi);
uint32_t get_pic_pin_from_ioapic_pin(uint32_t pin_index);
bool is_gsi_valid(uint32_t gsi);

#endif /* ARCH_X86_IOAPIC_H */
/**
 * @}
 */
