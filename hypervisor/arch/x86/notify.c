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

static uint64_t smp_call_mask = 0UL;

/* run in interrupt context */
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

void handle_smp_call(void)
{
	kick_notification(0, NULL);
}

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

/*
 * @pre be called only by BSP initialization process
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

/*
 * posted interrupt handler
 * @pre (irq - POSTED_INTR_IRQ) < CONFIG_MAX_VM_NUM
 */
static void handle_pi_notification(uint32_t irq, __unused void *data)
{
	uint32_t vcpu_index = irq - POSTED_INTR_IRQ;

	ASSERT(vcpu_index < CONFIG_MAX_VM_NUM, "");
	vcpu_handle_pi_notification(vcpu_index);
}

/*pre-condition: be called only by BSP initialization proccess*/
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
