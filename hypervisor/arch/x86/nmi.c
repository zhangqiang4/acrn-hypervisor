/*
 * Copyright (C) 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <asm/irq.h>
#include <asm/vmx.h>

#include <asm/guest/vcpu.h>
#include <asm/guest/virq.h>

/**
 * @addtogroup hwmgmt_irq hwmgmt.irq
 *
 * @{
 */

/**
 * @file arch/x86/nmi.c
 *
 * @brief NMI handler implementation
 */

/**
 * @brief Handle NMI
 *
 * Handle an NMI interrupt happened in hypervisor (VMX root operation) by injecting a virtual NMI
 * into current running vCPU on this pCPU.
 *
 * @param[in] ctx Pointer to interrupt exception context
 *
 * @return None
 */
void handle_nmi(__unused struct intr_excp_ctx *ctx)
{
	uint16_t pcpu_id = get_pcpu_id();
	struct acrn_vcpu *vcpu = get_running_vcpu(pcpu_id);

	/*
	 * If NMI occurs, inject it into current vcpu. Now just PMI is verified.
	 * For other kind of NMI, it may need to be checked further.
	 */
	vcpu_make_request(vcpu, ACRN_REQUEST_NMI);
}

/**
 * @}
 */
