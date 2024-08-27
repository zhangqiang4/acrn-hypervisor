/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <errno.h>
#include <asm/vmx.h>
#include <asm/guest/virq.h>
#include <asm/mmu.h>
#include <asm/guest/vcpu.h>
#include <asm/guest/vm.h>
#include <asm/guest/vmexit.h>
#include <asm/guest/vm_reset.h>
#include <asm/guest/vmx_io.h>
#include <asm/guest/lock_instr_emul.h>
#include <asm/guest/ept.h>
#include <asm/vtd.h>
#include <asm/cpuid.h>
#include <asm/guest/vcpuid.h>
#include <trace.h>
#include <common/hypercall.h>
#include <debug/console.h>

/**
 * @addtogroup vp-base_hv-main
 *
 * @{
 */

/**
 * @file
 * @brief VM exits handling implementation.
 *
 * This file provides the implementation of VM exits handling mechanisms. It includes the dispatch table for
 * various VM exits reasons and their corresponding handlers.
 */

static int32_t triple_fault_vmexit_handler(struct acrn_vcpu *vcpu);
static int32_t unhandled_vmexit_handler(struct acrn_vcpu *vcpu);
static int32_t xsetbv_vmexit_handler(struct acrn_vcpu *vcpu);
static int32_t wbinvd_vmexit_handler(struct acrn_vcpu *vcpu);
static int32_t undefined_vmexit_handler(struct acrn_vcpu *vcpu);
static int32_t pause_vmexit_handler(__unused struct acrn_vcpu *vcpu);
static int32_t hlt_vmexit_handler(struct acrn_vcpu *vcpu);
static int32_t mwait_vmexit_handler(struct acrn_vcpu *vcpu);
static int32_t monitor_vmexit_handler(struct acrn_vcpu *vcpu);
static int32_t loadiwkey_vmexit_handler(struct acrn_vcpu *vcpu);
static int32_t init_signal_vmexit_handler(__unused struct acrn_vcpu *vcpu);
static int32_t cpuid_vmexit_handler(struct acrn_vcpu *vcpu);
#ifdef CONFIG_LOCK_INSTRUCTION_EMULATION
static int32_t mtf_vmexit_handler(struct acrn_vcpu *vcpu);
#endif

/**
 * @brief VM-exit handler Dispatch table for Exit condition handling.
 *
 * This structure array stores handlers and qualification information for all these VM exits.
 * Refer to Appendix C VMX Basic Exit Reasons, Vol. 3, SDM 325462-078 for more details about VMX Basic Exit
 * Reasons.
 *
 * @remark ACRN hypervisor do not support some new VM exits, all these VM exits are treated as invalid VM
 * exit reason by ACRN hypervisor.
 */
static const struct vm_exit_dispatch dispatch_table[NR_VMX_EXIT_REASONS] = {
	/**
	 * @brief VM exit handler for Exception or non-maskable interrupt (NMI) is exception_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_EXCEPTION_OR_NMI] = {
		.handler = exception_vmexit_handler},
	/**
	 * @brief VM exit handler for External interrupt is external_interrupt_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_EXTERNAL_INTERRUPT] = {
		.handler = external_interrupt_vmexit_handler},
	/**
	 * @brief VM exit handler for Triple fault is triple_fault_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_TRIPLE_FAULT] = {
		.handler = triple_fault_vmexit_handler},
	/**
	 * @brief VM exit handler for INIT signal is init_signal_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_INIT_SIGNAL] = {
		.handler = init_signal_vmexit_handler},
	/**
	 * @brief VM exit handler for Start-up IPI (SIPI) is unhandled_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_STARTUP_IPI] = {
		.handler = unhandled_vmexit_handler},
	/**
	 * @brief VM exit handler for I/O system-management interrupt (SMI) is unhandled_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_IO_SMI] = {
		.handler = unhandled_vmexit_handler},
	/**
	 * @brief VM exit handler for Other SMI is unhandled_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_OTHER_SMI] = {
		.handler = unhandled_vmexit_handler},
	/**
	 * @brief VM exit handler for Interrupt window is interrupt_window_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_INTERRUPT_WINDOW] = {
		.handler = interrupt_window_vmexit_handler},
	/**
	 * @brief VM exit handler for NMI window is nmi_window_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_NMI_WINDOW] = {
		.handler = nmi_window_vmexit_handler},
	/**
	 * @brief VM exit handler for Task switch is unhandled_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_TASK_SWITCH] = {
		.handler = unhandled_vmexit_handler},
	/**
	 * @brief VM exit handler for CPUID is cpuid_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_CPUID] = {
		.handler = cpuid_vmexit_handler},
	/**
	 * @brief VM exit handler for GETSEC is unhandled_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_GETSEC] = {
		.handler = unhandled_vmexit_handler},
	/**
	 * @brief VM exit handler for HLT is hlt_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_HLT] = {
		.handler = hlt_vmexit_handler},
	/**
	 * @brief VM exit handler for INVD is unhandled_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_INVD] = {
		.handler = unhandled_vmexit_handler},
	/**
	 * @brief VM exit handler for INVLPG is unhandled_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_INVLPG] = {
		.handler = unhandled_vmexit_handler,},
	/**
	 * @brief VM exit handler for RDPMC is undefined_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_RDPMC] = {
		.handler = undefined_vmexit_handler},
	/**
	 * @brief VM exit handler for RDTSC is unhandled_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_RDTSC] = {
		.handler = unhandled_vmexit_handler},
	/**
	 * @brief VM exit handler for RSM is unhandled_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_RSM] = {
		.handler = unhandled_vmexit_handler},
	/**
	 * @brief VM exit handler for VMCALL is vmcall_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_VMCALL] = {
		.handler = vmcall_vmexit_handler},
	/**
	 * @brief VM exit handler for VMPTRST is undefined_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_VMPTRST] = {
		.handler = undefined_vmexit_handler},
	/**
	 * @brief VM exit handler for VMLAUNCH is undefined_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_VMLAUNCH] = {
		.handler = undefined_vmexit_handler},
	/**
	 * @brief VM exit handler for VMRESUME is undefined_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_VMRESUME] = {
		.handler = undefined_vmexit_handler},
	/**
	 * @brief VM exit handler for VMCLEAR is undefined_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_VMCLEAR] = {
		.handler = undefined_vmexit_handler},
	/**
	 * @brief VM exit handler for VMPTRLD is undefined_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_VMPTRLD] = {
		.handler = undefined_vmexit_handler},

	/**
	 * @brief VM exit handler for VMREAD is undefined_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_VMREAD] = {
		.handler = undefined_vmexit_handler},

	/**
	 * @brief VM exit handler for VMWRITE is undefined_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_VMWRITE] = {
		.handler = undefined_vmexit_handler},
	/**
	 * @brief VM exit handler for VMXOFF is undefined_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_VMXOFF] = {
		.handler = undefined_vmexit_handler},
	/**
	 * @brief VM exit handler for VMXON is undefined_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_VMXON] = {
		.handler = undefined_vmexit_handler},
	/**
	 * @brief VM exit handler for INVEPT is undefined_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_INVEPT] = {
		.handler = undefined_vmexit_handler},
	/**
	 * @brief VM exit handler for INVVPID is undefined_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_INVVPID] = {
		.handler = undefined_vmexit_handler},
	/**
	 * @brief VM exit handler for Control-register accesses is cr_access_vmexit_handler
	 * and the exit qualification information is necessary for the handler.
	 */
	[VMX_EXIT_REASON_CR_ACCESS] = {
		.handler = cr_access_vmexit_handler,
		.need_exit_qualification = 1},
	/**
	 * @brief VM exit handler for MOV DR is unhandled_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_DR_ACCESS] = {
		.handler = unhandled_vmexit_handler},
	/**
	 * @brief VM exit handler for I/O instruction is pio_instr_vmexit_handler
	 * and the exit qualification information is necessary for the handler.
	 */
	[VMX_EXIT_REASON_IO_INSTRUCTION] = {
		.handler = pio_instr_vmexit_handler,
		.need_exit_qualification = 1},
	/**
	 * @brief VM exit handler for RDMSR is rdmsr_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_RDMSR] = {
		.handler = rdmsr_vmexit_handler},
	/**
	 * @brief VM exit handler for WRMSR is wrmsr_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_WRMSR] = {
		.handler = wrmsr_vmexit_handler},
	/**
	 * @brief VM exit handler for VM-entry failure due to invalid guest state is unhandled_vmexit_handler
	 * and the exit qualification information is necessary for the handler.
	 */
	[VMX_EXIT_REASON_ENTRY_FAILURE_INVALID_GUEST_STATE] = {
		.handler = unhandled_vmexit_handler,
		.need_exit_qualification = 1},
	/**
	 * @brief VM exit handler for VM-entry failure due to MSR loading is unhandled_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_ENTRY_FAILURE_MSR_LOADING] = {
		.handler = unhandled_vmexit_handler},
	/**
	 * @brief VM exit handler for MWAIT is mwait_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_MWAIT] = {
		.handler = mwait_vmexit_handler},
#ifdef CONFIG_LOCK_INSTRUCTION_EMULATION
	/**
	 * @brief VM exit handler for Monitor trap flag is mtf_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_MONITOR_TRAP] = {
		.handler = mtf_vmexit_handler},
#endif
	/**
	 * @brief VM exit handler for MONITOR is monitor_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_MONITOR] = {
		.handler = monitor_vmexit_handler},
	/**
	 * @brief VM exit handler for PAUSE is pause_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_PAUSE] = {
		.handler = pause_vmexit_handler},
	/**
	 * @brief VM exit handler for VM-entry failure due to machine-check event is unhandled_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_ENTRY_FAILURE_MACHINE_CHECK] = {
		.handler = unhandled_vmexit_handler},
	/**
	 * @brief VM exit handler for TPR below threshold is tpr_below_threshold_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_TPR_BELOW_THRESHOLD] = {
		.handler = tpr_below_threshold_vmexit_handler},
	/**
	 * @brief VM exit handler for APIC access is unhandled_vmexit_handler
	 * and the exit qualification information is necessary for the handler.
	 */
	[VMX_EXIT_REASON_APIC_ACCESS] = {
		.handler = unhandled_vmexit_handler,
		.need_exit_qualification = 1},
	/**
	 * @brief VM exit handler for Virtualized EOI is veoi_vmexit_handler
	 * and the exit qualification information is necessary for the handler.
	 */
	[VMX_EXIT_REASON_VIRTUALIZED_EOI] = {
		.handler = veoi_vmexit_handler,
		.need_exit_qualification = 1},
	/**
	 * @brief VM exit handler for Access to GDTR or IDTR is unhandled_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_GDTR_IDTR_ACCESS] = {
		.handler = unhandled_vmexit_handler},
	/**
	 * @brief VM exit handler for Access to LDTR or TR is unhandled_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_LDTR_TR_ACCESS] = {
		.handler = unhandled_vmexit_handler},
	/**
	 * @brief VM exit handler for EPT violation is ept_violation_vmexit_handler, and the exit qualification
	 * information is necessary for the handler.
	 */
	[VMX_EXIT_REASON_EPT_VIOLATION] = {
		.handler = ept_violation_vmexit_handler,
		.need_exit_qualification = 1},
	/**
	 * @brief VM exit handler for EPT misconfiguration is ept_misconfig_vmexit_handler
	 * and the exit qualification information is necessary for the handler.
	 */
	[VMX_EXIT_REASON_EPT_MISCONFIGURATION] = {
		.handler = ept_misconfig_vmexit_handler,
		.need_exit_qualification = 1},
	/**
	 * @brief VM exit handler for RDTSCP is unhandled_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_RDTSCP] = {
		.handler = unhandled_vmexit_handler},
	/**
	 * @brief VM exit handler for VMX-preemption timer expired is unhandled_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_VMX_PREEMPTION_TIMER_EXPIRED] = {
		.handler = unhandled_vmexit_handler},
	/**
	 * @brief VM exit handler for WBINVD or WBNOINVD is wbinvd_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_WBINVD] = {
		.handler = wbinvd_vmexit_handler},
	/**
	 * @brief VM exit handler for XSETBV is xsetbv_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_XSETBV] = {
		.handler = xsetbv_vmexit_handler},
	/**
	 * @brief VM exit handler for APIC write is apic_write_vmexit_handler
	 * and the exit qualification information is necessary for the handler.
	 */
	[VMX_EXIT_REASON_APIC_WRITE] = {
		.handler = apic_write_vmexit_handler,
		.need_exit_qualification = 1},
	/**
	 * @brief VM exit handler for RDRAND is unhandled_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_RDRAND] = {
		.handler = unhandled_vmexit_handler},
	/**
	 * @brief VM exit handler for INVPCID is unhandled_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_INVPCID] = {
		.handler = unhandled_vmexit_handler},
	/**
	 * @brief VM exit handler for VMFUNC is undefined_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_VMFUNC] = {
		.handler = undefined_vmexit_handler},
	/**
	 * @brief VM exit handler for ENCLS is unhandled_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_ENCLS] = {
		.handler = unhandled_vmexit_handler},
	/**
	 * @brief VM exit handler for RDSEED is unhandled_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_RDSEED] = {
		.handler = unhandled_vmexit_handler},
	/**
	 * @brief VM exit handler for Page-modification log full is unhandled_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_PAGE_MODIFICATION_LOG_FULL] = {
		.handler = unhandled_vmexit_handler},
	/**
	 * @brief VM exit handler for XSAVES is unhandled_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_XSAVES] = {
		.handler = unhandled_vmexit_handler},
	/**
	 * @brief VM exit handler for XRSTORS is unhandled_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_XRSTORS] = {
		.handler = unhandled_vmexit_handler},
	/**
	 * @brief VM exit handler for LOADIWKEY is loadiwkey_vmexit_handler
	 * and the exit qualification information is not necessary for the handler.
	 */
	[VMX_EXIT_REASON_LOADIWKEY] = {
		.handler = loadiwkey_vmexit_handler}
};

/**
 * @brief Handle the VM-exits of the target vCPU.
 *
 * This function do the following to handle the VM-exit of the target vCPU:
 * - Check if the ID of the current running pCPU is different with the ID of the pCPU associated with the given vCPU.
 *   Return -EINVAL if they are different.
 * - Obtain the IDT-vectoring information of the target vCPU from the VMCS field to check whether VM exits occur
 *   during event delivery in VMX non-root operation. If yes, based on the interruption type:
 *   - For hardware exceptions, it queues the exception with error code if the Error code valid in IDT-Vectoring
 *     Information is valid unless it is a machine check \#MC.
 *   - For NMIs, it makes a request to the target vCPU to handle the NMI.
 * - Get the basic exit reason (low 16-bits) from the VMCS field and log the details of the exit.
 * - Ensure the basic exit reason is within the dispatch table. If not, return -EINVAL.
 * - Retrieve the appropriate dispatch entry from the table.
 * - Check if the dispatch entry's \p need_exit_qualification field is set. If so, read the exit qualification
 *   from VMCS into \p vcpu->arch.exit_qualification.
 * - If the exit reason is VMX_EXIT_REASON_EXTERNAL_INTERRUPT, disable interrupts before handling the external
 *   interrupt if the LAPIC passthrough is not configured. Call the VM-exit handler to handle the VM-exit.
 *   Then enable interrupts if the LAPIC passthrough is not configured.
 * - For other exit reasons, call the VM-exit handler to handle the VM-exit.
 * - Call console_vmexit_callback() to perform any additional handling or logging after the VM exit.
 *
 * @param[inout] vcpu The pointer to the target vCPU which triggers the VM-exit.
 *
 * @return An integer value as the return status of the VM-exit handling.
 *
 * @retval 0 on success
 * @retval -EINVAL If any of the following conditions are met:
 *                 - if the ID of the current running pCPU is different with the ID of the pCPU associated with
 *                   the given vCPU.
 *                 - if the basic exit reason is not within the dispatch table.
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
int32_t vmexit_handler(struct acrn_vcpu *vcpu)
{
	struct vm_exit_dispatch *dispatch = NULL;
	uint16_t basic_exit_reason;
	int32_t ret;

	if (get_pcpu_id() != pcpuid_from_vcpu(vcpu)) {
		pr_fatal("vcpu is not running on its pcpu!");
		ret = -EINVAL;
	} else {
		/* Obtain interrupt info */
		vcpu->arch.idt_vectoring_info = exec_vmread32(VMX_IDT_VEC_INFO_FIELD);
		/* Filter out HW exception & NMI */
		if ((vcpu->arch.idt_vectoring_info & VMX_INT_INFO_VALID) != 0U) {
			uint32_t vector_info = vcpu->arch.idt_vectoring_info;
			uint32_t vector = vector_info & 0xffU;
			uint32_t type = (vector_info & VMX_INT_TYPE_MASK) >> 8U;
			uint32_t err_code = 0U;

			if (type == VMX_INT_TYPE_HW_EXP) {
				if ((vector_info & VMX_INT_INFO_ERR_CODE_VALID) != 0U) {
					err_code = exec_vmread32(VMX_IDT_VEC_ERROR_CODE);
				}
				if (vector != IDT_MC) {
					/* Do NOT automatically queue \#MC here, it will be handled
					* later in exception_vmexit_handler
					*/
					(void)vcpu_queue_exception(vcpu, vector, err_code);
				}
				vcpu->arch.idt_vectoring_info = 0U;
			} else if (type == VMX_INT_TYPE_NMI) {
				vcpu_make_request(vcpu, ACRN_REQUEST_NMI);
				vcpu->arch.idt_vectoring_info = 0U;
			} else {
				/* No action on EXT_INT or SW exception. */
			}
		}

		/* Calculate basic exit reason (low 16-bits) */
		basic_exit_reason = (uint16_t)(vcpu->arch.exit_reason & 0xFFFFU);

		/* Log details for exit */
		pr_dbg("Exit Reason: 0x%08x ", vcpu->arch.exit_reason);

		/* Ensure exit reason is within dispatch table */
		if (basic_exit_reason >= ARRAY_SIZE(dispatch_table)) {
			pr_err("Invalid Exit Reason: 0x%08x ", vcpu->arch.exit_reason);
			ret = -EINVAL;
		} else {
			/* Calculate dispatch table entry */
			dispatch = (struct vm_exit_dispatch *)(dispatch_table + basic_exit_reason);

			/* See if an exit qualification is necessary for this exit handler */
			if (dispatch->need_exit_qualification != 0U) {
				/* Get exit qualification */
				vcpu->arch.exit_qualification = exec_vmread(VMX_EXIT_QUALIFICATION);
			}

			/* exit dispatch handling */
			if (basic_exit_reason == VMX_EXIT_REASON_EXTERNAL_INTERRUPT) {
				/* Handling external_interrupt should disable intr */
				if (!is_lapic_pt_configured(vcpu->vm)) {
					CPU_IRQ_DISABLE();
				}

				ret = dispatch->handler(vcpu);

				if (!is_lapic_pt_configured(vcpu->vm)) {
					CPU_IRQ_ENABLE();
				}
			} else {
				ret = dispatch->handler(vcpu);
			}
		}
	}

	console_vmexit_callback(vcpu);

	return ret;
}

/**
 * @brief Handle the unhandled VM exits by ACRN hypervisor.
 *
 * This function handles VM exits that ACRN hypervisor doesn't implement specific handling for. It prints the error
 * message with the exit reason and exit qualification information. Then it traces the unhandled VM exit condition.
 * While these VM exits aren't handled with specific functionality,
 * the function returns 0 to allow the VM execution to continue rather than terminating the VM.
 *
 * @param[in] vcpu The pointer to the vCPU whose VM exit needs to be handled.
 *
 * @return An integer value as the return status of the VM-exit handling.
 *
 * @retval 0 on success.
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 *
 * @remark ACRN hypervisor recognizes but doesn't implement specific handling for certain VM exit
 * types. This function provides minimal handling by logging their occurrence.
 */
static int32_t unhandled_vmexit_handler(struct acrn_vcpu *vcpu)
{
	pr_fatal("Error: Unhandled VM exit condition from guest at 0x%016lx ",
			exec_vmread(VMX_GUEST_RIP));

	pr_fatal("Exit Reason: 0x%08x ", vcpu->arch.exit_reason);

	pr_err("Exit qualification: 0x%016lx ",
			exec_vmread(VMX_EXIT_QUALIFICATION));

	TRACE_2L(TRACE_VMEXIT_UNHANDLED, vcpu->arch.exit_reason, 0UL);

	return 0;
}

#ifdef CONFIG_LOCK_INSTRUCTION_EMULATION
/* MTF is currently only used for split-lock emulation */
/**
 * @brief Handle the VM exit due to Monitor trap flag (MTF).
 *
 * This function handles the VM exit cause by Monitor trap flag (MTF). It clears the MTF VM-execution control
 * and retains the current guest RIP of the target vCPU. If the target vCPU is emulating a lock instruction, it sets
 * \p vcpu->arch.emulating_lock to false and calls vcpu_complete_lock_instr_emulation() to complete the lock
 * instruction emulation. In this handler, it always returns 0 to indicate the VM exit handling is successful.
 *
 * Refer to Chapter 26.5.2 Monitor Trap Flag, Vol. 3, SDM 325462-078 for more details about MTF.
 *
 * @param[in] vcpu The pointer to the vCPU whose VM exit needs to be handled.
 *
 * @return An integer value as the return status of the VM-exit handling.
 *
 * @retval 0 on success.
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 *
 * @remark MTF is currently only used for split-lock emulation.
 */
static int32_t mtf_vmexit_handler(struct acrn_vcpu *vcpu)
{
	vcpu->arch.proc_vm_exec_ctrls &= ~(VMX_PROCBASED_CTLS_MON_TRAP);
	exec_vmwrite32(VMX_PROC_VM_EXEC_CONTROLS, vcpu->arch.proc_vm_exec_ctrls);

	vcpu_retain_rip(vcpu);

	if (vcpu->arch.emulating_lock) {
		vcpu->arch.emulating_lock = false;
		vcpu_complete_lock_instr_emulation(vcpu);
	}

	return 0;
}
#endif

/**
 * @brief Handle the VM exit due to triple fault.
 *
 * This function handles the VM exit which exit reason is triple fault. It prints the error message with the
 * guest RIP and exit qualification information. Then it calls triple_fault_shutdown_vm() to shutdown the
 * VM. In this handler, it always returns 0 to indicate the VM exit handling is successful.
 *
 * @param[in] vcpu The pointer to the vCPU whose VM exit needs to be handled.
 *
 * @return An integer value as the return status of the VM-exit handling.
 *
 * @retval 0 on success.
 *
 * @pre vcpu != NULL
 */
static int32_t triple_fault_vmexit_handler(struct acrn_vcpu *vcpu)
{
	pr_fatal("VM%d: triple fault @ guest RIP 0x%016lx, exit qualification: 0x%016lx",
		vcpu->vm->vm_id, exec_vmread(VMX_GUEST_RIP), exec_vmread(VMX_EXIT_QUALIFICATION));
	triple_fault_shutdown_vm(vcpu);

	return 0;
}

/**
 * @brief Handle the VM exit due to PAUSE instruction.
 *
 * This function calls yield_current() to make reschedule request to the current pCPU. In this handler, it always
 * returns 0 to indicate the VM exit handling is successful.
 *
 * @param[in] vcpu The pointer to the target vCPU (unused parameter).
 *
 * @return An integer value as the return status of the VM-exit handling.
 *
 * @retval 0 on success.
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static int32_t pause_vmexit_handler(__unused struct acrn_vcpu *vcpu)
{
	yield_current();
	return 0;
}

/**
 * @brief Handle the VM exit due to HLT instruction.
 *
 * This function handles the VM exit caused by HLT instruction. It checks if the target vCPU has any pending
 * requests or pending interrupts. If there is no pending request and no pending interrupt, it suspends the vCPU
 * which triggering the VM-exit by waiting for a virtual interrupt event. In this handler, it always returns
 * 0 to indicate the VM exit handling is successful.
 *
 * @param[in] vcpu The pointer to the vCPU whose VM exit needs to be handled.
 *
 * @return An integer value as the return status of the VM-exit handling.
 *
 * @retval 0 on success.
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static int32_t hlt_vmexit_handler(struct acrn_vcpu *vcpu)
{
	if ((vcpu->arch.pending_req == 0UL) && (!vlapic_has_pending_intr(vcpu))) {
		/* HLT is emulated with MWAIT C1 (hint=0) */
		wait_event(&vcpu->events[VCPU_EVENT_VIRTUAL_INTERRUPT]);
	}
	return 0;
}

/**
 * @brief Handle the VM exit due to MWAIT instruction.
 *
 * This function handles the VM exit caused by MWAIT instruction. It first checks whether the MONITOR feature
 * is enabled in the guest by examining the MSR_IA32_MISC_ENABLE register. If the feature is not enabled
 * (MSR_IA32_MISC_ENABLE_MONITOR_ENA bit is clear), it injects \#UD exception to the guest.
 *
 * ACRN hypervisor only emulates monitor-less MWAIT. Guests need to set ECX[2] for MWAIT to indicate that it is
 * calling a monitor-less MWAIT.
 * - For monitored MWAIT (ECX[2] is zero): hypervisor will yield the current thread and return.
 * - For monitor-less MWAIT (ECX[2] is non-zero): hypervisor will check EAX[7:4] bits, 1111B indicates C0,
 *   which means no idle, do nothing. Otherwise, hypervisor will set the idle hint to EAX[7:0] and waits
 *   for a virtual interrupt event. Then it restores the idle hint to 0 (C1) for default.
 *
 * This function always returns 0 to indicate the VM exit handling is successful.
 *
 * @param[in] vcpu The pointer to the vCPU whose VM exit needs to be handled.
 *
 * @return An integer value as the return status of the VM-exit handling.
 *
 * @retval 0 on success.
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static int32_t mwait_vmexit_handler(struct acrn_vcpu *vcpu)
{
	uint32_t eax, ecx;
	uint64_t guest_ia32_misc_enable = vcpu_get_guest_msr(vcpu, MSR_IA32_MISC_ENABLE);

	if ((guest_ia32_misc_enable & MSR_IA32_MISC_ENABLE_MONITOR_ENA) == 0UL) {
		vcpu_inject_ud(vcpu);
	} else {
		/*
		* Only monitor-less MWAIT can be emulated.
		* Guests need to mark ecx bit2 as an indicator to do monitor-less MWAIT.
		* Monitored MWAIT can not be emulated, we will yield and return immediately.
		*/
		if ((vcpu->arch.pending_req == 0UL) && (!vlapic_has_pending_intr(vcpu))) {
			eax = (uint32_t)vcpu_get_gpreg(vcpu, CPU_REG_RAX);
			ecx = (uint32_t)vcpu_get_gpreg(vcpu, CPU_REG_RCX);

			if ((ecx & 0x4U) == 0) {
				yield_current();
			} else {
				/* EAX [7:4] 01111B indicates C0, which means no idle. */
				if ((eax & 0xF0U) != 0xF0U) {
					vcpu->idle_hint = eax & 0xFFU;
					wait_event(&vcpu->events[VCPU_EVENT_VIRTUAL_INTERRUPT]);
					/* restore the hint to 0(C1) for default */
					vcpu->idle_hint = 0U;
				}
			}
		}
	}

	return 0;
}

/**
 * @brief Handle the VM exit due to MONITOR instruction.
 *
 * This function handles the VM exit caused by MONITOR instruction. It checks whether the MONITOR feature
 * is enabled in the guest by examining the MSR_IA32_MISC_ENABLE register. If the feature is not enabled
 * (IA32_MISC_ENABLE_MONITOR.ENA bit is clear), it injects \#UD to the guest. In this handler, it always
 * returns 0 to indicate the VM exit handling is successful.
 *
 * @param[in] vcpu The pointer to the vCPU whose VM exit needs to be handled.
 *
 * @return An integer value as the return status of the VM-exit handling.
 *
 * @retval 0 on success.
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static int32_t monitor_vmexit_handler(__unused struct acrn_vcpu *vcpu)
{
	uint64_t guest_ia32_misc_enable = vcpu_get_guest_msr(vcpu, MSR_IA32_MISC_ENABLE);

	if ((guest_ia32_misc_enable & MSR_IA32_MISC_ENABLE_MONITOR_ENA) == 0UL) {
		vcpu_inject_ud(vcpu);
	}
	return 0;
}

/**
 * @brief Handle the VM exit due to CPUID instruction.
 *
 * This function handles the VM exit caused by CPUID instruction. It retrieves the current values from
 * the guest's general purpose registers as parameters for the CPUID instruction (EAX, EBX, ECX, EDX).
 * It records a trace entry with the input EAX and ECX values and calls guest_cpuid() to determine the
 * appropriate CPUID responses for the guest based on the hypervisor's configuration and capabilities.
 * Finally, it updates the guest's general purpose registers with the results and returns 0 to indicate
 * successful handling of the VM exit.
 *
 * @param[in] vcpu The pointer to the vCPU whose VM exit needs to be handled.
 *
 * @return An integer value as the return status of the VM-exit handling.
 *
 * @retval 0 on success.
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static int32_t cpuid_vmexit_handler(struct acrn_vcpu *vcpu)
{
	uint32_t eax, ebx, ecx, edx;

	eax = (uint32_t)vcpu_get_gpreg(vcpu, CPU_REG_RAX);
	ebx = (uint32_t)vcpu_get_gpreg(vcpu, CPU_REG_RBX);
	ecx = (uint32_t)vcpu_get_gpreg(vcpu, CPU_REG_RCX);
	edx = (uint32_t)vcpu_get_gpreg(vcpu, CPU_REG_RDX);
	TRACE_2L(TRACE_VMEXIT_CPUID, (uint64_t)eax, (uint64_t)ecx);
	guest_cpuid(vcpu, &eax, &ebx, &ecx, &edx);
	vcpu_set_gpreg(vcpu, CPU_REG_RAX, (uint64_t)eax);
	vcpu_set_gpreg(vcpu, CPU_REG_RBX, (uint64_t)ebx);
	vcpu_set_gpreg(vcpu, CPU_REG_RCX, (uint64_t)ecx);
	vcpu_set_gpreg(vcpu, CPU_REG_RDX, (uint64_t)edx);

	return 0;
}

/**
 * @brief Handle the VM exit due to XSETBV instruction.
 *
 * This function handles the VM exit caused by XSETBV instruction to write the contents of registers EDX:EAX into
 * the 64-bit extended control register (XCR) specified in the ECX register. It checks whether Enable XSAVES/XRSTORS
 * field in Secondary Processor-Based VM-Execution Controls is set and the CR4.OSXSAVE bit is set:
 * - If yes: It checks if the current privilege level is 0. If yes, it checkes whether the low 32-bits of ECX is 0
 *   since only XCR0 is supported currently. If the ECX is 0, it gets the supported XCR0 from CPUID.0DH and hides the
 *   bits about Intel MPX and PKRU. Then it checks if the XCR0[0] is set and the supported XCR0 is equal to the value
 *   from reading RAX and RDX. If checks are passed, it writes the value to XCR0 if the value of SSE and AVX is not
 *   10b. Then the handling is success and sets return value to 0. If any aforementioned checks fails, this handling
 *   is failed and sets return value to -1. Refer to Chapter 13.4 XSAVE Area, Vol. 1, SDM 325462-078 for more details
 *   about XSAVE.
 * - If not: It injects an \#UD and sets return value to 0.
 *
 * XSETBV instruction set's the XCR0 that is used to tell for which
 * components states can be saved on a context switch using xsave.
 *
 * According to Chapter 26.1.1 Relative Priority of Faults and VM exits, Vol. 3, SDM 325462-078:
 * Invalid-opcode exception (UD) and faults based on privilege level (include
 * virtual-8086 mode privileged instructions are not recognized) have higher
 * priority than VM exit.
 *
 * According to Chapter 6 XSETBV-Set Extended Control Register, Vol. 2, SDM 325462-078:
 * If CR4.OSXSAVE[bit 18] = 0,
 * execute "XSETBV" instruction will generate \#UD exception.
 * So VM exit won't happen with VMX_GUEST_CR4.CR4_OSXSAVE = 0.
 * CR4_OSXSAVE bit is controlled by guest (CR4_OSXSAVE bit
 * is set as guest expect to see).
 *
 * We don't need to handle those case here because we depends on VMX to handle them.
 *
 * @param[in] vcpu The pointer to the vCPU whose VM exit needs to be handled.
 *
 * @return An integer value as the return status of the VM-exit handling.
 *
 * @retval 0 on success.
 * @retval -1 if the handling is failed.
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static int32_t xsetbv_vmexit_handler(struct acrn_vcpu *vcpu)
{
	int32_t ret = -1;	/* ret < 0 call vcpu_inject_gp(vcpu, 0U) */
	uint32_t cpl;
	uint64_t val64;

	if (vcpu->arch.xsave_enabled && ((vcpu_get_cr4(vcpu) & CR4_OSXSAVE) != 0UL)) {
		/* get current privilege level */
		cpl = exec_vmread32(VMX_GUEST_CS_ATTR);
		cpl = (cpl >> 5U) & 3U;

		if (cpl == 0U) {
			/* to access XCR0,'ecx' should be 0 */
			if ((vcpu_get_gpreg(vcpu, CPU_REG_RCX) & 0xffffffffUL) == 0UL) {
				struct cpuinfo_x86 *cpu_info = get_pcpu_info();
				/* CPUID.0DH, EDX:EAX is a bitmap of all user state components managed by XSAVE */
				uint64_t supported_xcr0 = ((uint64_t)cpu_info->cpuid_leaves[FEAT_D_0_EDX] << 32U)
					+ cpu_info->cpuid_leaves[FEAT_D_0_EAX];

				/*
				* SDM Vol.1 13-4, XCR0[4:3] are associated with MPX state,
				* vcpu currently not support Intel MPX.
				*/
				supported_xcr0 &= ~(XCR0_BNDREGS | XCR0_BNDCSR);

				/* vcpu currently not support PKRU */
				supported_xcr0 &= ~XCR0_PKRU;

				val64 = (vcpu_get_gpreg(vcpu, CPU_REG_RAX) & 0xffffffffUL) |
						(vcpu_get_gpreg(vcpu, CPU_REG_RDX) << 32U);

				/* bit 0(x87 state) of XCR0 can't be cleared */
				if (((val64 & 0x01UL) != 0UL) && ((val64 & supported_xcr0) == val64)) {
					/*
					* XCR0[2:1] (SSE state & AVX state) can not be
					* set to 10b as it is necessary to set both bits
					* to use AVX instructions.
					*/
					if ((val64 & (XCR0_SSE | XCR0_AVX)) != XCR0_AVX) {
						write_xcr(0, val64);
						ret = 0;
					}
				}
			}
		}
	} else {
		/* CPUID.01H:ECX.XSAVE[bit 26] = 0 */
		vcpu_inject_ud(vcpu);
		ret = 0;
	}

	return ret;
}

/**
 * @brief Handle the VM exit due to WBINVD instruction.
 *
 * This function handles the VM exit caused by WBINVD instruction. It flushes and invalidates all caches if there
 * are no RTVMs in VM_CREATED or VM_RUNNING state. Otherwise, for RTVM, it walks through the EPT pages and flush
 * address space from the every page entries. For non-RTVM, it pauses other vCPUs and let them wait for the WBINVD
 * completion. Then it walks through the EPT pages and flush address space from the every page entries. Finally,
 * it signals the other vCPUs to continue. These operations for non-RTVM are done under a spinlock. In this handler,
 * it always returns 0 to indicate the VM exit handling is successful.
 *
 * @param[in] vcpu The pointer to the vCPU whose VM exit needs to be handled.
 *
 * @return An integer value as the return status of the VM-exit handling.
 *
 * @retval 0 on success.
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static int32_t wbinvd_vmexit_handler(struct acrn_vcpu *vcpu)
{
	uint16_t i;
	struct acrn_vcpu *other;

	/* GUEST_FLAG_RT has not set in post-launched RTVM before it has been created */
	if (!has_rt_vm()) {
		flush_invalidate_all_cache();
	} else {
		if (is_rt_vm(vcpu->vm)) {
			walk_ept_table(vcpu->vm, ept_flush_leaf_page);
		} else {
			spinlock_obtain(&vcpu->vm->wbinvd_lock);
			/* Pause other vcpus and let them wait for the wbinvd completion */
			foreach_vcpu(i, vcpu->vm, other) {
				if (other != vcpu) {
					vcpu_make_request(other, ACRN_REQUEST_WAIT_WBINVD);
				}
			}

			walk_ept_table(vcpu->vm, ept_flush_leaf_page);

			foreach_vcpu(i, vcpu->vm, other) {
				if (other != vcpu) {
					signal_event(&other->events[VCPU_EVENT_SYNC_WBINVD]);
				}
			}
			spinlock_release(&vcpu->vm->wbinvd_lock);
		}
	}

	return 0;
}

/**
 * @brief Handle the VM exit due to LOADIWKEY instruction.
 *
 * This function handles the VM exit caused by LOADIWKEY instruction. Since ACRN Hypervisor hides NoBackup and
 * randomization features from guest, this function would first check whether the EAX register is set. If yes,
 * injects a \#GP. Otherwise, it reads the XMM registers and sets the encryption and integrity keys in the IWKey
 * structure of the target vCPU. Then it calls \p asm_loadiwkey() to load the keys and sets the target vCPU as
 * the one that owns the IWKey. In this handler, it always returns 0 to indicate the VM exit handling is
 * successful.
 *
 * @param[in] vcpu The pointer to the vCPU whose VM exit needs to be handled.
 *
 * @return An integer value as the return status of the VM-exit handling.
 *
 * @retval 0 on success.
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static int32_t loadiwkey_vmexit_handler(struct acrn_vcpu *vcpu)
{
	uint64_t xmm[6] = {0};

	/* Wrapping key nobackup and randomization are not supported */
	if ((vcpu_get_gpreg(vcpu, CPU_REG_RAX) != 0UL)) {
		vcpu_inject_gp(vcpu, 0);
	} else {
		read_xmm_0_2(&xmm[0], &xmm[2], &xmm[4]);
		vcpu->arch.IWKey.encryption_key[0] = xmm[2];
		vcpu->arch.IWKey.encryption_key[1] = xmm[3];
		vcpu->arch.IWKey.encryption_key[2] = xmm[4];
		vcpu->arch.IWKey.encryption_key[3] = xmm[5];
		vcpu->arch.IWKey.integrity_key[0] = xmm[0];
		vcpu->arch.IWKey.integrity_key[1] = xmm[1];

		asm_loadiwkey(0);
		get_cpu_var(whose_iwkey) = vcpu;
	}

	return 0;
}

/**
 * @brief Handle the VM exit due to INIT signal.
 *
 * This handler is only triggered by INIT signal when poweroff from inside of RTVM. It retains the RIP of the
 * target vCPU. In this handler, it always returns 0 to indicate the VM exit handling is successful.
 *
 * Refer to Chapter 26.2 Other Causes of VM Exits, Vol. 3, SDM 325462-078 for more details about why it is safe
 * to ignore the signal but need to retain its RIP.
 *
 * @param[in] vcpu The pointer to the vCPU whose VM exit needs to be handled.
 *
 * @return An integer value as the return status of the VM-exit handling.
 *
 * @retval 0 on success.
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static int32_t init_signal_vmexit_handler(struct acrn_vcpu *vcpu)
{
	/*
	* Intel SDM Volume 3, 26.2:
	*   INIT signals. INIT signals cause VM exits. A logical processer performs none
	*   of the operations normally associated with these events. Such exits do not modify
	*   register state or clear pending events as they would outside of VMX operation (If
	*   a logical processor is the wait-for-SIPI state, INIT signals are blocked. They do
	*   not cause VM exits in this case).
	*
	* So, it is safe to ignore the signal but need retain its RIP.
	*/
	vcpu_retain_rip(vcpu);
	return 0;
}

/**
 * @brief Handle the VM exit which is unsupported or undefined for guest.
 *
 * This function handles the VM exit caused by an unsupported or undefined instruction for guest. It injects
 * a \#UD exception to the target vCPU. In this handler, it always returns 0 to indicate the VM exit handling
 * is successful.
 *
 * @param[in] vcpu The pointer to the vCPU whose VM exit needs to be handled.
 *
 * @return An integer value as the return status of the VM-exit handling.
 *
 * @retval 0 on success.
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 *
 * @remark ACRN does not enable VMFUNC, so VMFUNC is treated as undefined.
 */
static int32_t undefined_vmexit_handler(struct acrn_vcpu *vcpu)
{
	vcpu_inject_ud(vcpu);
	return 0;
}

/**
 * @}
 */
