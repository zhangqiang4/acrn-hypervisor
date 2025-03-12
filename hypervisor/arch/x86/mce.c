/*
 * Copyright (C) 2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <softirq.h>
#include <irq.h>
#include <asm/irq.h>
#include <trace.h>
#include <asm/guest/virq.h>

/**
 * @defgroup hwmgmt_mce hwmgmt.mce
 * @ingroup hwmgmt
 * @brief Machine check event handling implementation
 *
 * This module provides the handling and injection of machine check events.
 *
 * @{
 */

/**
 * @file
 * @brief Implementation to handle machine check events.
 *
 * This file implements functions to initialize and handle machine check
 * events.
 */

/**
 * @brief Inject machine check event to governing vcpu
 *
 * This function injects machine check event to governing vcpu running
 * on pCPU with \p pcpu_id. This function handles the case where machine
 * check events are delivered when pCPU is in VMX root mode, as non-root
 * mode delivery will not trap to hypervisor.
 *
 * When machine check events are delivered in root mode, hypervisor tries
 * to find governing vcpu on each pcpu and inject event into governing vcpu.
 * The injection fails when any one of the following is true:
 * 1. Machine check capability was not configured to passthrough to governing VM
 * 2. Machine check capability was configured to passthrough, but a non-SOS
 *   governing VM does not claim to exclusively own assigned pCPU.
 * 3. Governing VM does not enable virtual CR4.MCE
 * 4. No governing vCPU was found on some pCPUs
 *
 * If the event is an exception and hypervisor failed to deliver it to any vCPU,
 * hypervisor halts current pCPU.
 *
 * If the event is a CMCI and hypervisor failed to deliver it to any vCPU,
 * the CMCI is dropped.
 *
 * @pre pcpu_id < ACRN_PLATFORM_LAPIC_IDS_MAX
 * @param[in] pcpu_id The pCPU ID on which governing vcpu runs to inject
 *   machine check event
 * @param[in] is_cmci Whether the event is a CMCI
 * @return None
 *
 * @remark The machine check event handling has below assumptions:
 *   - If machine check events are delivered through exceptions,
 *   we regard them as "unrecoverable".
 *   - All machine check exceptions are broadcast. Local machine check is not supported.
 *   - P-core hyperthreads in the same core and E-core in the same cluster will NOT
 *   be assigned to different VMs, and on each pCPU there will be one and only
 *   one vCPU that is responsible to handle machine check events. The vCPU
 *   is called governing vCPU, and the VM with governing vCPU is called governing VM.
 *   Service VM is by default a governing VM. A guest exclusively claiming ownership
 *   of pCPU is also a governing VM. If two post-launched guests are sharing pCPU,
 *   neither of them are governing VM.
 */
void inject_mc_event_to_governing_vcpu(uint16_t pcpu_id, bool is_cmci)
{
	struct acrn_vcpu *vcpu;
	struct acrn_vm *vm;
	uint16_t vm_id, i;
	bool injected = false;

	for (vm_id = 0; vm_id < CONFIG_MAX_VM_NUM; vm_id++) {
		vm = get_vm_from_vmid(vm_id);
		if (vm->state != VM_RUNNING) {
			continue;
		}

		/* There will be three cases for fatal error (MCE):
		 * 1. Governing vcpu in non-root mode. This case it won't trap and reach here.
		 * 2. Non-governing vcpu in non-root mode. We need to inject
		 *   to governing vm.
		 * 3. pCPU in root mode. We need to wake up governing vm.
		 */

		vcpu = vcpu_from_pcpu_id(vm, pcpu_id);
		if (vcpu == NULL) {
			continue;
		}

		if (is_mc_pt_enabled(vcpu)) {
			if (is_cmci) {
				(void)vlapic_set_local_intr(vcpu->vm, vcpu->vcpu_id, APIC_LVT_CMCI);
				injected = true;
			} else {
				if ((vcpu_get_cr4(vcpu) & CR4_MCE) != 0) {
					vcpu_inject_mc(vcpu);

					vcpu->arch.mc_injection_pending = true;

					/* Whatever it is waiting for, stop waiting and go back to non-root mode to
					 * handle #MC.
					 */
					for (i = 0; i < VCPU_EVENT_NUM; i++) {
						signal_event(&vcpu->events[i]);
					}
					injected = true;
				} else {
					pr_fatal("VM%d did not enable CR4.MCE.", vm->vm_id);
				}
			}
		} else {
			/*
			 * Currently do nothing to non-governing VM when #MC or CMCI comes in.
			 *
			 * Technically non-governing VM is not supposed to continue running when #MC comes,
			 * but we leave the handling of that to governing VM.
			 */
		}
	}

	if (!injected && !is_cmci) {
		pr_fatal("#MC was not injected as governing vcpu wasn't found on pcpu%d, \
				or governing vcpu didn't enable MC in CR4.", pcpu_id);
		pr_fatal("Halting current pCPU");
		cpu_dead();
		/* Loops forever */
	}

	if (!injected && is_cmci) {
		pr_err("CMCI dropped as governing vcpu wasn't found on pcpu%d\n", pcpu_id);
	}
}

/**
 * @brief CMCI handler
 *
 * This function is the registered CMCI handler. It queues a softirq
 * and returns. This function runs in interrupt context.
 *
 * @pre N/A
 * @post N/A
 * @param[in] irq Interrupt number. Unused.
 * @param[in] data Custom data. Unused.
 * @return None
 */
static void cmc_irq_handler(__unused uint32_t irq, __unused void *data)
{
	fire_softirq(SOFTIRQ_CMCI);
}

/**
 * @brief CMCI soft interrupt handler
 *
 * This function is the soft interrupt handler. It calls function
 * inject_mc_event_to_governing_vcpu and returns.
 *
 * @pre N/A
 * @post N/A
 * @param[in] pcpu_id pCPU ID on which the softirq fires
 * @return None
 */
static void cmci_softirq(uint16_t pcpu_id)
{
	inject_mc_event_to_governing_vcpu(pcpu_id, true);
}

/**
 * @brief Machine check event handler
 *
 * This function is the registered handler for machine check events.
 * It calls function inject_mc_event_to_governing_vcpu and returns.
 *
 * @pre N/A
 * @post N/A
 * @return None
 */
void handle_mce(void)
{
	inject_mc_event_to_governing_vcpu(get_pcpu_id(), false);
}

/**
 * @brief initialize machine check events
 *
 * Initialize machine check event handling.
 * If CMCI capability is not supported, do nothing and exit.
 * Otherwise, register global softirq handler cmci_softirq and interrupt service
 * routine cmc_irq_handler. The request operation needs to be done only once, so
 * do it on BSP. Also programs pre-defined CMCI vector into APIC LVT CMCI MSR.
 *
 * @pre N/A
 * @post N/A
 * @return None
 */
void init_machine_check_events(void)
{
	int32_t retval = 0;

	/* CMCI */
	if (is_cmci_supported()) {
		if (get_pcpu_id() == BSP_CPU_ID) {
			register_softirq(SOFTIRQ_CMCI, cmci_softirq);
			retval = request_irq(CMCI_IRQ, cmc_irq_handler, NULL, IRQF_NONE);
			if (retval < 0) {
				pr_err("Request CMCI irq failed");
			}
		}

		/* LVT CMCI traps and hypervisor inject vCMCI to guest */
		msr_write(MSR_IA32_EXT_APIC_LVT_CMCI, CMCI_VECTOR);
	}
}

/**
 * @}
 */
