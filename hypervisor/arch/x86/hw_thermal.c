/*
 * Copyright (C) 2025 Intel Corporation.
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
#include <hw/hw_thermal.h>
/**
 * @defgroup hwmgmt_thermal hwmgmt.thermal
 * @ingroup hwmgmt
 * @brief Definitions and implementations for thermal management on the x86 platform.
 *
 * This module provides external APIs for managing thermal conditions. It also includes the implementation of hardware
 * thermal management for the x86 architecture.
 *
 * @{
 */

/**
 * @file
 * @brief Implementation of hardware thermal management for x86 architecture in the ACRN hypervisor.
 *
 * This file contains the implementation of thermal interrupt handling and initialization for the ACRN hypervisor on
 * the x86 architecture.
 */

/**
 * @brief Thermal interrupt request (IRQ) handler.
 *
 * Handles thermal interrupts by triggering a software interrupt (SOFTIRQ_THERMAL) for thermal events.
 *
 * @param[in] irq The IRQ number (not used).
 * @param[in] data Pointer to additional data (not used).
 *
 * @return None.
 */
static void thermal_irq_handler(__unused uint32_t irq, __unused void *data)
{
	fire_softirq(SOFTIRQ_THERMAL);
}

/**
 * @brief Initialize hardware thermal management.
 *
 * This function sets up hardware thermal management by requesting an IRQ for thermal events and
 * configuring the thermal vector in the APIC.
 *
 * The steps performed are as follows:
 * - Check if the current CPU is the Boot Strap Processor (BSP) or an Application Processor (AP).
 * - If it is the BSP, request an IRQ for thermal events using thermal_irq_handler() as the callback
 *   and THERMAL_IRQ as the IRQ number.
 * - If the IRQ request on the BSP is successful or the current CPU is an AP, update the fixed
 *   thermal vector (THERMAL_VECTOR) in the IA32_EXT_APIC_LVT_THERMAL MSR.
 *
 * @return None.
 */
void init_hw_thermal(void)
{
	int32_t retval = 0;

	if (get_pcpu_id() == BSP_CPU_ID) {
		retval = request_irq(THERMAL_IRQ, thermal_irq_handler, NULL, IRQF_NONE);
		if (retval < 0) {
			pr_err("Thermal irq setup failed\n");
		}
	}

	if (retval >= 0) {
		uint32_t val = THERMAL_VECTOR;

		msr_write(MSR_IA32_EXT_APIC_LVT_THERMAL, val);
	}
}

/**
 * @}
 */