/*
 * Copyright (C) 2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <softirq.h>
#include <irq.h>
#include <asm/irq.h>
#include <trace.h>
#include <asm/guest/virq.h>

/*
 * Governing vCPU: Per MCA assumptions, there will be one and only
 * one vCPU on each pCPU that belongs to either service vm or
 * partitioned guest. This function injects #MC to this governing vcpu.
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

		vcpu = vcpu_from_pid(vm, pcpu_id);
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
		panic("#MC was not injected as governing vcpu wasn't found on pcpu%d, or governing vcpu didn't enable MC in CR4.\n", pcpu_id);
		cpu_dead();
	}

	if (!injected && is_cmci) {
		pr_err("CMCI dropped as governing vcpu wasn't found on pcpu%d\n", pcpu_id);
	}
}

/* run in interrupt context */
static void cmc_irq_handler(__unused uint32_t irq, __unused void *data)
{
	fire_softirq(SOFTIRQ_CMCI);
}

static void cmci_softirq(uint16_t pcpu_id)
{
	inject_mc_event_to_governing_vcpu(pcpu_id, true);
}

void handle_mce(void)
{
	inject_mc_event_to_governing_vcpu(get_pcpu_id(), false);
}

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
