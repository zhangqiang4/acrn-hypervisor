/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <asm/guest/vm.h>
#include <asm/guest/vm_reset.h>
#include <asm/guest/vmcs.h>
#include <asm/guest/vmexit.h>
#include <asm/guest/virq.h>
#include <asm/guest/guest_pm.h>
#include <hv_main.h>
#include <schedule.h>
#include <sprintf.h>
#include <trace.h>
#include <logmsg.h>
#ifdef HV_DEBUG
#include <profiling.h>
#endif

/**
 * @addtogroup vp-base_hv-main
 *
 * @{
 */

/**
 * @file
 * @brief hv_main thread relevant operations.
 *
 * This file implements the main hypervisor thread and provides external interfaces to manage the idle thread.
 */

 /**
 * @brief Manages vCPU threads by scheduling vCPU when necessary, checking and processing pending requests before
 * put the current vCPU into non-root mode, dispatching VM-exit handlers after the vCPU trap into root-mode.
 *
 * ACRN hypervisor uses two threads: vCPU thread and default idle thread. If there's no vCPU running the the current
 * pCPU, the default idle thread is running. Otherwise, vCPU thread is running and it schedules each vCPU running
 * the current pCPU.
 *
 * This function enters an infinite loop to continuously manage the vCPU. Within this loop, it performs the following
 * steps:
 * - If LAPIC passthrough is not configured, disable CPU interrupts by calling CPU_IRQ_DISABLE().
 * - Check if the current pCPU requires rescheduling. If so, call schedule() to perform the rescheduling.
 * - Process any pending requests (including interrupts) for the vCPU. If an error occurs (indicated as a negative
 *   value returned by \p acrn_handle_pending_request() ), the vCPU is marked as a zombie, indicating it should no
 *   longer be scheduled. This operation is performed under a lock.
 * - Reset the virtual interrupt event for the vCPU.
 * - Profile the vCPU before a VM entry using profiling_vmenter_handler().
 * - Run vCPU into non-root mode. If an error occurs (indicated as a non-zero value returned by
 *   \p run_vcpu() ), the vCPU is marked as a zombie, indicating it should no longer be scheduled. This operation
 *   is performed under a lock.
 * - Profile the vCPU before handling the VM exit by calling profiling_pre_vmexit_handler().
 * - If LAPIC passthrough is not configured, enable CPU interrupts by calling CPU_IRQ_ENABLE().
 * - Dispatch the VM exit handler for the vCPU. If an error occurs (indicated by a negative value returned by
 *   \p vmexit_handler() ), a \#GP is injected into the vCPU.
 * - Profile the vCPU after handling the VM exit by calling profiling_post_vmexit_handler().
 *
 * @param[in] obj A pointer to the thread to be managed.
 *
 * @return None
 *
 * @pre obj != NULL
 *
 * @post N/A
 */
void vcpu_thread(struct thread_object *obj)
{
	struct acrn_vcpu *vcpu = container_of(obj, struct acrn_vcpu, thread_obj);
	int32_t ret = 0;

	do {
		if (!is_lapic_pt_configured(vcpu->vm)) {
			CPU_IRQ_DISABLE();
		}

		/* Don't open interrupt window between here and vmentry */
		if (need_reschedule(pcpuid_from_vcpu(vcpu))) {
			schedule();
		}

		/* Check and process pending requests(including interrupt) */
		ret = acrn_handle_pending_request(vcpu);
		if (ret < 0) {
			pr_fatal("vcpu handling pending request fail");
			get_vm_lock(vcpu->vm);
			zombie_vcpu(vcpu);
			put_vm_lock(vcpu->vm);
			/* Fatal error happened (triple fault). Stop the vcpu running. */
			continue;
		}

		reset_event(&vcpu->events[VCPU_EVENT_VIRTUAL_INTERRUPT]);
		TRACE_2L(TRACE_VM_ENTER, 0UL, 0UL);
#ifdef HV_DEBUG
		profiling_vmenter_handler(vcpu);
		sample_vmexit_end(vcpu);
#endif
		ret = run_vcpu(vcpu);
		if (ret != 0) {
			pr_fatal("vcpu resume failed");
			get_vm_lock(vcpu->vm);
			zombie_vcpu(vcpu);
			put_vm_lock(vcpu->vm);
			/* Fatal error happened (resume vcpu failed). Stop the vcpu running. */
			continue;
		}
		TRACE_2L(TRACE_VM_EXIT, vcpu->arch.exit_reason, vcpu_get_rip(vcpu));
#ifdef HV_DEBUG
		sample_vmexit_begin(vcpu);
		profiling_pre_vmexit_handler(vcpu);
#endif
		if (!is_lapic_pt_configured(vcpu->vm)) {
			CPU_IRQ_ENABLE();
		}
		/* Dispatch handler */
		ret = vmexit_handler(vcpu);
		if (ret < 0) {
			pr_fatal("dispatch VM exit handler failed for reason"
				" %d, ret = %d!", vcpu->arch.exit_reason, ret);
			vcpu_inject_gp(vcpu, 0U);
			continue;
		}
#ifdef HV_DEBUG
		profiling_post_vmexit_handler(vcpu);
#endif
	} while (true);
}

/**
 * @brief Default idle thread for the pCPU.
 *
 * ACRN hypervisor uses two threads: vCPU thread and default idle thread.
 *
 * This function manages the idle thread by entering an infinite loop. Within this loop, it performs the
 * following actions:
 * - If the current pCPU requires rescheduling, call schedule() to perform the rescheduling.
 * - If the current pCPU needs to be taken offline, call cpu_dead() to offline the current pCPU.
 * - If the current pCPU needs to shut down the VM, call shutdown_vm_from_idle() to shut down the VM.
 * - If the current pCPU is BSP, and platform needs to enter S3 state, call platform_s3_from_idle() to enter S3 state.
 * - Otherwise, call cpu_do_idle() to idle the current pCPU.
 *
 * @param[in] obj A pointer to the thread to be managed. This parameter is unused.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
static void default_idle(__unused struct thread_object *obj)
{
	uint16_t pcpu_id = get_pcpu_id();

	while (true) {
		if (need_reschedule(pcpu_id)) {
			schedule();
		} else if (need_offline(pcpu_id)) {
			cpu_dead();
		} else if (need_shutdown_vm(pcpu_id)) {
			shutdown_vm_from_idle(pcpu_id);
		} else if ((pcpu_id == BSP_CPU_ID) && need_platform_s3(pcpu_id)) {
			pr_fatal("Pending platform S3 request...");
			platform_s3_from_idle();
		} else{
			cpu_do_idle();
		}
	}
}

/**
 * @brief Run the idle thread on the currently running pCPU.
 *
 * This function sets up the idle thread by assigning the current CPU ID, setting \p default_idle as the thread entry
 * function, and set the switch_out and switch_in callbacks for the idle thread as NULL. It then calls run_thread()
 * to run the idle thread. If the idle thread returns, it calls cpu_dead() to offline the current pCPU.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
void run_idle_thread(void)
{
	uint16_t pcpu_id = get_pcpu_id();
	struct thread_object *idle = &per_cpu(idle, pcpu_id);
	struct sched_params idle_params = {0};
	char idle_name[16];

	(void)snprintf(idle_name, 16U, "idle%hu", pcpu_id);
	(void)strncpy_s(idle->name, 16U, idle_name, 16U);
	idle->pcpu_id = pcpu_id;
	idle->thread_entry = default_idle;
	idle->switch_out = NULL;
	idle->switch_in = NULL;
#ifdef CONFIG_SCHED_PRIO
	idle_params.prio = PRIO_IDLE;
#endif
	init_thread_data(idle, &idle_params);

	run_thread(idle);

	/* Control should not come here */
	cpu_dead();
}

/**
 * @}
 */
