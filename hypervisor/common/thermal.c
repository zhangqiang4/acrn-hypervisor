/*
 * Copyright (C) 2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <softirq.h>
#include <trace.h>
#include <asm/guest/virq.h>
#include <hw/hw_thermal.h>
/**
 * @addtogroup hwmgmt_thermal
 *
 * @{
 */
/**
 * @file
 * @brief Implementation of thermal management in the ACRN Hypervisor.
 *
 * This file implements thermal management in the ACRN hypervisor, including thermal interrupt handling and
 * initialization. Thermal management handles thermal events and manages the system's thermal state.
 * It is implemented for the x86 architecture.
 */

/**
 * @brief Thermal softirq handler for a specific physical CPU.
 *
 * This function is called when a thermal softirq is triggered. It iterates through all virtual CPUs (vCPUs)
 * associated with the given physical CPU (pcpu_id) and call vcpu_inject_thermal_interrupt() to try to inject
 * the thermal interrupt into each valid vCPU.
 *
 * @param[in] pcpu_id The ID of the physical CPU for which the thermal softirq is handled.
 *
 * @return None
 */
static void thermal_softirq(uint16_t pcpu_id)
{
	struct acrn_vcpu *vcpu;
	uint32_t idx;

	for (idx = 0; idx < CONFIG_MAX_VM_NUM; idx++) {
		vcpu = per_cpu(vcpu_array, pcpu_id)[idx];
		if (vcpu != NULL) {
			vcpu_inject_thermal_interrupt(vcpu);
		}
	}
}

/**
 * @brief Initialize thermal management.
 *
 * This function initializes the thermal management system. It registers thermal_softirq() as the handler for
 * thermal softirqs if the current physical CPU is the Bootstrap Processor (BSP). It also initializes the hardware
 * thermal management by requesting an IRQ for thermal events and configuring the LVT Thermal Sensor Interrupt
 * Register.
 *
 * @return None
 */
void thermal_init(void)
{
	uint16_t pcpu_id = get_pcpu_id();

	if (pcpu_id == BSP_CPU_ID) {
		register_softirq(SOFTIRQ_THERMAL, thermal_softirq);
	}

	init_hw_thermal();
}
/**
 * @}
 */