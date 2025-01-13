/*
 * Copyright (C) 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <errno.h>
#include <asm/lib/bits.h>
#include <asm/lib/atomic.h>
#include <asm/irq.h>
#include <asm/cpu.h>
#include <asm/per_cpu.h>
#include <asm/lapic.h>
#include <asm/guest/vm.h>
#include <asm/guest/virq.h>

/**
 * @defgroup hwmgmt_smpcall hwmgmt.smpcall
 * @ingroup hwmgmt
 * @brief The definition and implementation of SMP call and Posted Interrupt notifications.
 *
 * @{
 */

/**
 * @brief Implementations for SMP call mechanism.
 */

/**
 * @brief Target physical processor ID bit mask of current SMP call.
 *
 * It's set by caller of smp_call_function() and cleared by target processors in execution of SMP
 * call interrupt handler .
 */
static uint64_t smp_call_mask = 0UL;

/**
 * @brief The SMP call notification handler run in interrupt context.
 * 
 * This handler executes the SMP callback set by the invoker if current processor is on the target
 * processor bit mask. Otherwise, this is a spurious interrupt.
 *
 * @param irq IRQ number for this interrupt handler
 * @param data Pointer to private data for the interrupt handler
 *
 * @return None
 */
static void kick_notification(__unused uint32_t irq, __unused void *data)
{
	/* Notification vector is used to kick target cpu out of non-root mode.
	 * And it also serves for smp call.
	 */
	uint16_t pcpu_id = get_pcpu_id();

	if (bitmap_test(pcpu_id, &smp_call_mask)) {
		struct smp_call_info_data *smp_call =
			&per_cpu(smp_call_info, pcpu_id);

		if (smp_call->func != NULL) {
			smp_call->func(smp_call->data);
		}
		bitmap_clear_lock(pcpu_id, &smp_call_mask);
	}
}

/**
 * @brief Handle SMP call notification request for vCPUs configured with Local APIC Pass-through.
 *
 * For processor running in vCPU context with Local APIC Pass-through enabled, after receiving the
 * INIT signal, the VM-exit handler will check the notification request and invoke this handler.
 * Note this is called in vCPU thread in VMX root operation, instead of in interrupt context.
 * This handler just calls the kick_notification handler with a dummy irq number 0 since it's not
 * from interrupt context.
 *
 * @return None
 */
void handle_smp_call(void)
{
	kick_notification(0, NULL);
}

/**
 * @brief Invoke a SMP call to let target processors execute given function.
 *
 * This function first set the mask of target processor IDs in smp_call_mask, and then triggers
 * every processor on given bit mask to execute the function, and wait all bits on smp_call_mask
 * to be cleared by target processors.
 * For each active target processor, if it's the invoker, just execute the function directly.
 * If it's not, make a ACRN_REQUEST_SMP_CALL request if it's configured for an Local APIC
 * Pass-through VM and the vCPU is running. For other cases, trigger an IPI with the notification
 * vector. In either way, if target processor is in VM context, it will exit VMX non-root operation
 * and the hypervisor will handle the notification interrupt.  If target processor is in root
 * operation, i.e. the hypervisor context, the IPI will be handled by hypervisor directly.
 *
 * For VM configured with Local APIC Pass-through, the invoker can't simply issue an IPI with
 * the notification vector because such IPI will be taken as a real interrupt by guest
 * Instead, the invoker requests vCPUs on target processors to exit to hypervisor context.
 * This is accomplished by vcpu_make_request, which triggers INIT signal via Local APIC
 * to trigger VM-exit of target vCPUs.
 *
 * @param mask The bit mask of target processor IDs.
 * @param func The function to execute from target processors.
 * @param data The data parameter pointer for func
 *
 * @return None
 */
void smp_call_function(uint64_t mask, smp_call_func_t func, void *data)
{
	uint16_t pcpu_id;
	struct smp_call_info_data *smp_call;

	/* wait for previous smp call complete, which may run on other cpus */
	while (atomic_cmpxchg64(&smp_call_mask, 0UL, mask) != 0UL);
	pcpu_id = ffs64(mask);
	while (pcpu_id < MAX_PCPU_NUM) {
		bitmap_clear_nolock(pcpu_id, &mask);
		if (pcpu_id == get_pcpu_id()) {
			func(data);
			bitmap_clear_nolock(pcpu_id, &smp_call_mask);
		} else if (is_pcpu_active(pcpu_id)) {
			smp_call = &per_cpu(smp_call_info, pcpu_id);
			smp_call->func = func;
			smp_call->data = data;

			struct acrn_vcpu *vcpu = get_ever_run_vcpu(pcpu_id);

			if ((vcpu != NULL) && (is_lapic_pt_configured(vcpu->vm))) {
				if (vcpu->state == VCPU_RUNNING) {
					vcpu_make_request(vcpu, ACRN_REQUEST_SMP_CALL);
				} else {
					pr_err("vm%d:vcpu%d for lapic_pt is not running, can't handle smp call!", vcpu->vm->vm_id, vcpu->vcpu_id);
					bitmap_clear_nolock(pcpu_id, &smp_call_mask);
				}
			} else {
				send_single_ipi(pcpu_id, NOTIFY_VCPU_VECTOR);
			}
		} else {
			/* pcpu is not in active, print error */
			pr_err("pcpu_id %d not in active!", pcpu_id);
			bitmap_clear_nolock(pcpu_id, &smp_call_mask);
		}
		pcpu_id = ffs64(mask);
	}
	/* wait for current smp call complete */
	wait_sync_change(&smp_call_mask, 0UL);
}

/**
 * @brief Set up SMP call notification interrupt handler
 *
 * This must be called in the BSP initialization process to enable the SMP call mechanism. By
 * design, IRQ number and vector for the per-cpu notification interrupt are constant. The setup
 * process just requests the IRQ with given handler.
 *
 * @return None
 */
void setup_notification(void)
{
	if (request_irq(NOTIFY_VCPU_IRQ, kick_notification, NULL, IRQF_NONE) < 0) {
		pr_err("Failed to register handler for notify irq 0x%x with vector 0x%x",
		        NOTIFY_VCPU_IRQ, irq_to_vector(NOTIFY_VCPU_IRQ));
	} else {
	        dev_dbg(DBG_LEVEL_IRQ, "Registered handler for notify irq 0x%x with vector 0x%x",
		        NOTIFY_VCPU_IRQ, irq_to_vector(NOTIFY_VCPU_IRQ));
        }
}

/**
 * @brief The Posted Interrupt notification handler run in interrupt context.
 *
 * This handles Posted Interrupt notification when CPU is running in VMX root operation (either in
 * the target vCPU thread context or other contexts) and local interrupt is enabled. We just
 * requests the vCPU to inject the notification vector to guest via self IPI after disabling local
 * interrupt. The Posted Interrupt hardware will then trigger interrupt after next VM-enter.
 *
 * @param irq IRQ number for this interrupt handler
 * @param data Pointer to private data for the interrupt handler
 *
 * @return None
 *
 * @pre (irq - POSTED_INTR_IRQ) < CONFIG_MAX_VM_NUM
 */
static void handle_pi_notification(uint32_t irq, __unused void *data)
{
	uint32_t vcpu_index = irq - POSTED_INTR_IRQ;

	ASSERT(vcpu_index < CONFIG_MAX_VM_NUM, "");
	vcpu_handle_pi_notification(vcpu_index);
}

/**
 * @brief Set up Posted Interrupt notification interrupt handlers
 *
 * This must be called in the BSP initialization process to enable the Posted Interrupt mechanism.
 * By design, IRQ numbers and vectors for the Posted Interrupts are statically allocated, one pair
 * for one VM. It's based on the design that at most one vCPU of a VM can be run on a pCPU. For
 * each pCPU, there are at most CONFIG_MAX_VM_NUM vCPUs (each from a different VM). To post
 * interrupts to there vCPUs, CONFIG_MAX_VM_NUM Posted Interrupt notification vectors are enough.
 * The setup process just requests the IRQs with given handler.
 *
 * @return None
 */
void setup_pi_notification(void)
{
	uint32_t i;

	for (i = 0U; i < CONFIG_MAX_VM_NUM; i++) {
		if (request_irq(POSTED_INTR_IRQ + i, handle_pi_notification, NULL, IRQF_NONE) < 0) {
			pr_err("Failed to setup pi notification");
			break;
		}
	}
}

/**
 * @}
 */
