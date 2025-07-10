/*
 * Copyright (C) 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <errno.h>
#include <asm/lib/bits.h>
#include <asm/guest/virq.h>
#include <asm/lapic.h>
#include <asm/mmu.h>
#include <asm/vmx.h>
#include <asm/guest/vcpu.h>
#include <asm/guest/vmcs.h>
#include <asm/guest/vm.h>
#include <asm/guest/optee.h>
#include <asm/guest/lock_instr_emul.h>
#include <trace.h>
#include <logmsg.h>
#include <asm/irq.h>
#include <asm/mce.h>

/**
 * @defgroup vp-base_virq vp-base.virq
 * @ingroup vp-base
 * @brief The definition and implementation of Interrupt Virtualization
 *
 * @{
 */

/**
 * @file
 *
 * @brief Implementation for Interrupt Virtualization
 */

/**
 * @brief An array of exceptions types whose vector is smaller than 32
 *
 * Refer to SDM Vol3 6.15 Exception and Interrupt Reference
 */
static const uint16_t exception_type[32] = {
	[0] = VMX_INT_TYPE_HW_EXP,			/**< Divide Error Exception (DE) */
	[1] = VMX_INT_TYPE_HW_EXP,			/**< Debug Exception (DB) */
	[2] = VMX_INT_TYPE_HW_EXP,			/**< NMI Interrupt */
	[3] = VMX_INT_TYPE_HW_EXP,			/**< Breakpoint Exception (BP) */
	[4] = VMX_INT_TYPE_HW_EXP,			/**< Overflow Exception (OF) */
	[5] = VMX_INT_TYPE_HW_EXP,			/**< Bound range Exceeded Exception (BR) */
	[6] = VMX_INT_TYPE_HW_EXP,			/**< Invalid Opcode Exception (UD) */
	[7] = VMX_INT_TYPE_HW_EXP,			/**< Device Not Available Exception (NM) */
	[8] = VMX_INT_TYPE_HW_EXP | EXCEPTION_ERROR_CODE_VALID,	/**< Double Fault Exception (DF),
								  with error code */
	[9] = VMX_INT_TYPE_HW_EXP,			/**< Coprocessor Segment Overrun */
	[10] = VMX_INT_TYPE_HW_EXP | EXCEPTION_ERROR_CODE_VALID,	/**< Invalid TSS Exception (TS),
									  with error code */
	[11] = VMX_INT_TYPE_HW_EXP | EXCEPTION_ERROR_CODE_VALID,	/**< Segment Not Present (NP),
									  with error code */
	[12] = VMX_INT_TYPE_HW_EXP | EXCEPTION_ERROR_CODE_VALID,	/**< Stack Fault Exception (SS),
									  with error code */
	[13] = VMX_INT_TYPE_HW_EXP | EXCEPTION_ERROR_CODE_VALID,	/**< General Protection Exception (GP),
									  with error code */
	[14] = VMX_INT_TYPE_HW_EXP | EXCEPTION_ERROR_CODE_VALID,	/**< Page-Fault Exception (PF),
									  with error code */
	[15] = VMX_INT_TYPE_HW_EXP,			/**< Reserved */
	[16] = VMX_INT_TYPE_HW_EXP,			/**< x87 FPU Floating-Point Error (MF) */
	[17] = VMX_INT_TYPE_HW_EXP | EXCEPTION_ERROR_CODE_VALID,	/**< Alignment Check Exception (AC),
									  with error code */
	[18] = VMX_INT_TYPE_HW_EXP,			/**< Machine-Check Exception (MC) */
	[19] = VMX_INT_TYPE_HW_EXP,			/**< SIMD Floating-Point Exception (XM) */
	[20] = VMX_INT_TYPE_HW_EXP,			/**< Virtualization Exception (VE) */
	[21] = VMX_INT_TYPE_HW_EXP,			/**< Control Protection Exception (CP) */
	[22] = VMX_INT_TYPE_HW_EXP,			/**< Reserved */
	[23] = VMX_INT_TYPE_HW_EXP,			/**< Reserved */
	[24] = VMX_INT_TYPE_HW_EXP,			/**< Reserved */
	[25] = VMX_INT_TYPE_HW_EXP,			/**< Reserved */
	[26] = VMX_INT_TYPE_HW_EXP,			/**< Reserved */
	[27] = VMX_INT_TYPE_HW_EXP,			/**< Reserved */
	[28] = VMX_INT_TYPE_HW_EXP,			/**< Reserved */
	[29] = VMX_INT_TYPE_HW_EXP,			/**< Reserved */
	[30] = VMX_INT_TYPE_HW_EXP,			/**< Reserved */
	[31] = VMX_INT_TYPE_HW_EXP			/**< Reserved */
};

/**
 * @brief Get the exception type of a vector
 *
 * Vectors > 31 and IDT_NMI are for interrupts. DB, BP and OF are traps. DF and MC are
 * aborts. Other exceptions with vector <= 31 are faults.
 *
 * @param[in] vector     Vector of the exception
 *
 * @return Exception type
 * @retval EXCEPTION_INTERRUPT Given exception is a kind of interrupt
 * @retval EXCEPTION_TRAP Given exception is a kind of trap
 * @retval EXCEPTION_ABORT Given exception is a kind of abort
 * @retval EXCEPTION_FAULT Given exception is a kind of fault
 */
static uint8_t get_exception_type(uint32_t vector)
{
	uint8_t type;

	/* Treat #DB as trap until decide to support Debug Registers */
	if ((vector > 31U) || (vector == IDT_NMI)) {
		type = EXCEPTION_INTERRUPT;
	} else if ((vector == IDT_DB) || (vector == IDT_BP) || (vector ==  IDT_OF)) {
		type = EXCEPTION_TRAP;
	} else if ((vector == IDT_DF) || (vector == IDT_MC)) {
		type = EXCEPTION_ABORT;
	} else {
		type = EXCEPTION_FAULT;
	}

	return type;
}

#ifdef CONFIG_ENFORCE_APICv_BASIC_SUPPORT
/**
 * @brief Check whether local interrupt of a vCPU is blocked
 *
 * Check if the vCPU can respond to pending interrupts. CPU local interrupt is blocked if
 * RFLAGS.IF is 0. Besides, following special situations also blocked local interrupt even if
 * RFLAGS.IF is 1.
 * - Previous instruction is STI with RFLAGS.IF=0, local interrupt is blocked even current
 *   RFLAGS.IF=1
 * - Previous instruction is a MOV to SS or a POP to SS.
 *
 * @param[in] vcpu     Pointer to a vCPU structure against which we check the local interrupt
 *
 * @return A boolean indicating whether local interrupt of a vCPU is enabled
 * @retval true The vCPU can response to pending interrupts
 * @retval false The vCPU can not response to pending interrupts
 *
 * @pre vcpu != NULL
 */
static bool is_guest_irq_enabled(struct acrn_vcpu *vcpu)
{
	uint64_t guest_rflags, guest_state;
	bool status = false;

	/* Read the RFLAGS of the guest */
	guest_rflags = vcpu_get_rflags(vcpu);
	/* Check the RFLAGS[IF] bit first */
	if ((guest_rflags & HV_ARCH_VCPU_RFLAGS_IF) != 0UL) {
		/* Interrupts are allowed */
		/* Check for temporarily disabled interrupts */
		guest_state = exec_vmread32(VMX_GUEST_INTERRUPTIBILITY_INFO);

		if ((guest_state & (HV_ARCH_VCPU_BLOCKED_BY_STI |
				    HV_ARCH_VCPU_BLOCKED_BY_MOVSS)) == 0UL) {
			status = true;
		}
	}
	return status;
}
#else
static inline bool is_guest_irq_enabled(__unused struct acrn_vcpu *vcpu)
{
	return true;
}
#endif

/**
 * @brief Check whether current vCPU can accept NMI
 *
 * Current vCPU can accept NMI unless local interrupt is not blocked by STI, MOVSS or NMI.
 * RFLAGS.IF=0 can't block NMI handling.
 *
 * @return A boolean indicating whether current vCPU can accept NMI
 * @retval true The vCPU can accept an NMI
 * @retval false The vCPU can not accept an NMI
 */
static inline bool is_nmi_injectable(void)
{
	uint64_t guest_state;

	guest_state = exec_vmread32(VMX_GUEST_INTERRUPTIBILITY_INFO);

	return ((guest_state & (HV_ARCH_VCPU_BLOCKED_BY_STI |
		HV_ARCH_VCPU_BLOCKED_BY_MOVSS | HV_ARCH_VCPU_BLOCKED_BY_NMI)) == 0UL);
}

/**
 * @brief Request a vCPU thread to handle a event
 *
 * The event is handled in vCPU thread in VMX root operation before next VM enter. To ensure the
 * request will be handled as soon as possible, this function will kick pCPU for the vCPU to force
 * a VM exit if it's current running in VMX non root operation. It doesn't matter if the vCPU is
 * already in VMX root operation, the vCPU thread will handle the request before next VM enter. To
 * avoid the race condition that request is raised in between vCPU thread checking pending requests
 * and VM enter, vCPU thread must disable local interrupt before checking request and VM enter.
 * The kick signal will be blocked in that window and then cause a VM exit after VM enter.
 * If the caller is running on the pCPU for target vCPU, kick can be omitted since it's already in
 * VMX root operation.
 *
 * @param[inout] vcpu     Pointer to a vCPU structure to which the request is submitted
 * @param[in] eventid  Event to request
 *
 * @return None
 *
 * @pre vcpu != NULL
 */
void vcpu_make_request(struct acrn_vcpu *vcpu, uint16_t eventid)
{
	bitmap_set_lock(eventid, &vcpu->arch.pending_req);
	kick_vcpu(vcpu);
}

/**
 * @brief Get class of an exception
 *
 * DE, TS, NP, SS, GP exceptions are contributory exceptions.
 * PF and VE exceptions are page fault class exceptions.
 * Others are benign exceptions.
 *
 * @param[in] vector     Vector number of the exception
 *
 * @return The class of the exception
 * @retval EXCEPTION_CLASS_BENIGN It's a Benign Exception
 * @retval EXCEPTION_CLASS_CONT It's a Contributory Exception
 * @retval EXCEPTION_CLASS_PF It's a Page Fault Class Exception
 *
 * @pre vector < 32
 */
static int32_t get_excep_class(uint32_t vector)
{
	int32_t ret;

	if ((vector == IDT_DE) || (vector == IDT_TS) || (vector == IDT_NP) ||
		(vector == IDT_SS) || (vector == IDT_GP)) {
		ret = EXCEPTION_CLASS_CONT;
	} else if ((vector == IDT_PF) || (vector == IDT_VE)) {
		ret = EXCEPTION_CLASS_PF;
	} else {
		ret = EXCEPTION_CLASS_BENIGN;
	}

	return ret;
}

/**
 * @brief Queue an exception to guest.
 *
 * If there is already an exception pending to be injected, current exception may be upgraded to a
 * more serious one according to following rules:
 * - If previous exception is DF, and current exception is not benign class, upgrade it to triple
 *   fault and make a ACRN_REQUEST_TRP_FAULT request which will shutdown the VM.
 * - If both previous and current exceptions are contributory, or previous is page fault and new
 *   is benign, upgrade to DF exception.
 * - Otherwise, new exception override previous one.
 * Except triple fault, make a ACRN_REQUEST_EXCP request to inject exceptions. The err_code_arg is
 * used if the exception needs an error code.
 *
 * @param[inout] vcpu     Pointer to a vCPU to which an exception is queued
 * @param[in] vector_arg   Vector of the exception.
 * @param[in] err_code_arg Error Code to be injected.
 *
 * @return Error code or 0 on success
 * @retval 0 on success
 * @retval -EINVAL on error that vector is invalid.
 *
 * @pre vcpu != NULL
 */
int32_t vcpu_queue_exception(struct acrn_vcpu *vcpu, uint32_t vector_arg, uint32_t err_code_arg)
{
	struct acrn_vcpu_arch *arch = &vcpu->arch;
	uint32_t vector = vector_arg;
	uint32_t err_code = err_code_arg;
	int32_t ret = 0;

	/* VECTOR_INVALID is also greater than 32 */
	if (vector >= 32U) {
		pr_err("invalid exception vector %d", vector);
		ret = -EINVAL;
	} else {

		uint32_t prev_vector = arch->exception_info.exception;
		int32_t new_class, prev_class;

		/* SDM vol3 - 6.15, Table 6-5 - conditions for generating a
		 * double fault */
		prev_class = get_excep_class(prev_vector);
		new_class = get_excep_class(vector);
		if ((prev_vector == IDT_DF) && (new_class != EXCEPTION_CLASS_BENIGN)) {
			/* triple fault happen - shutdown mode */
			vcpu_make_request(vcpu, ACRN_REQUEST_TRP_FAULT);
		} else {
			if (((prev_class == EXCEPTION_CLASS_CONT) && (new_class == EXCEPTION_CLASS_CONT)) ||
				((prev_class == EXCEPTION_CLASS_PF) && (new_class != EXCEPTION_CLASS_BENIGN))) {
				/* generate double fault */
				vector = IDT_DF;
				err_code = 0U;
			} else {
				/* Trigger the given exception instead of override it with
				 * double/triple fault. */
			}

			arch->exception_info.exception = vector;

			if ((exception_type[vector] & EXCEPTION_ERROR_CODE_VALID) != 0U) {
				arch->exception_info.error = err_code;
			} else {
				arch->exception_info.error = 0U;
			}

			vcpu_make_request(vcpu, ACRN_REQUEST_EXCP);
		}
	}

	return ret;
}

/**
 * @brief Inject pending exception to a vCPU
 *
 * Populate the pending exception information (vector number, error code) to the VMCS and update
 * RIP and RFLAGS for the vCPU.
 * For fault exceptions, the length of current instruction will be forced to be 0 so that current
 * instruction will be re-executed after next VM enter, RFLAGS.RF is set as well.
 *
 * @param[inout] vcpu     Pointer to a vCPU to which an exception is injected
 *
 * @return None
 *
 * @pre vcpu != NULL
 * @pre vcpu->arch.exception_info.exception < 0x20U
 */
static void vcpu_inject_exception(struct acrn_vcpu *vcpu)
{
	uint32_t vector = vcpu->arch.exception_info.exception;
	/* SDM 253665-086 Vol3 28.6.1.3: error code is not valid for real mode. */
	uint32_t excep_type = (vcpu->arch.cpu_mode != CPU_MODE_REAL) ?
			exception_type[vector] : (exception_type[vector] & ~EXCEPTION_ERROR_CODE_VALID);

	exec_vmwrite32(VMX_ENTRY_INT_INFO_FIELD, VMX_INT_INFO_VALID |
			(excep_type << 8U) | (vector & 0xFFU));

	if ((excep_type & EXCEPTION_ERROR_CODE_VALID) != 0U) {
		exec_vmwrite32(VMX_ENTRY_EXCEPTION_ERROR_CODE, vcpu->arch.exception_info.error);
	}

	vcpu->arch.exception_info.exception = VECTOR_INVALID;

	/* If this is a fault, we should retain the RIP */
	if (get_exception_type(vector) == EXCEPTION_FAULT) {
		vcpu_retain_rip(vcpu);
	}

	/* SDM 17.3.1.1 For any fault-class exception except a debug exception generated in response to an
	 * instruction breakpoint, the value pushed for RF is 1.
	 * #DB is treated as Trap in get_exception_type, so RF will not be set for instruction breakpoint.
	 */
	if (get_exception_type(vector) == EXCEPTION_FAULT) {
		vcpu_set_rflags(vcpu, vcpu_get_rflags(vcpu) | HV_ARCH_VCPU_RFLAGS_RF);
	}
}

/**
 * @brief Inject NMI to a vCPU
 *
 * Make a NMI injection request to the vCPU thread. The NMI will be injected if no temporarily
 * blocked, before next VM enter. If NMI is currently blocked, the request will be retained and
 * handled once it's not blocked.
 * NMI is a kind of interrupt. So signal a VCPU_EVENT_VIRTUAL_INTERRUPT to wake up vCPU if it's
 * waiting on interrupts.
 *
 * @param[inout] vcpu Pointer to vCPU to which an NMI is injected.
 *
 * @return None
 *
 * @pre vcpu != NULL
 */
void vcpu_inject_nmi(struct acrn_vcpu *vcpu)
{
	vcpu_make_request(vcpu, ACRN_REQUEST_NMI);
	signal_event(&vcpu->events[VCPU_EVENT_VIRTUAL_INTERRUPT]);
}

/**
 * @brief Inject general protection exception(GP) to a vCPU
 *
 * Queue a GP exception with given error code to a vCPU by calling vcpu_queue_exception.
 *
 * @param[inout] vcpu     Pointer to vCPU to which a GP exception is injected.
 * @param[in] err_code Error Code along with the GP to be injected.
 *
 * @return None
 *
 * @pre vcpu != NULL
 */
void vcpu_inject_gp(struct acrn_vcpu *vcpu, uint32_t err_code)
{
	(void)vcpu_queue_exception(vcpu, IDT_GP, err_code);
}

/**
 * @brief Inject page fault exception(PF) to a vCPU
 *
 * Set the fault guest virtual address to Virtual CR2 and Queue a PF exception with given error
 * code to the vCPU.
 *
 * @param[inout] vcpu  Pointer to a vCPU to which a PF exception is injected.
 * @param[in] addr     Guest Virtual Address that result in PF.
 * @param[in] err_code Error Code to be injected.
 *
 * @pre vcpu != NULL
 */
void vcpu_inject_pf(struct acrn_vcpu *vcpu, uint64_t addr, uint32_t err_code)
{
	vcpu_set_cr2(vcpu, addr);
	(void)vcpu_queue_exception(vcpu, IDT_PF, err_code);
}

/**
 * @brief Inject invalid opcode exception(UD) to a vCPU
 *
 * Queue a UD exception to a vCPU
 *
 * @param[inout] vcpu Pointer to a vCPU to which a UD exception is injected.
 *
 * @return None
 *
 * @pre vcpu != NULL
 */
void vcpu_inject_ud(struct acrn_vcpu *vcpu)
{
	(void)vcpu_queue_exception(vcpu, IDT_UD, 0);
}

/**
 * @brief Inject stack fault exception(SS) to a vCPU
 *
 * Queue a SS exception to a vCPU
 *
 * @param[inout] vcpu Pointer to a vCPU to which a SS exception is injected.
 *
 * @return None
 *
 * @pre vcpu != NULL
 */
void vcpu_inject_ss(struct acrn_vcpu *vcpu)
{
	(void)vcpu_queue_exception(vcpu, IDT_SS, 0);
}

/**
 * @brief Inject machine check exception(MC) to a vCPU
 *
 * Queue a MC exception to a vCPU
 *
 * @param[inout] vcpu Pointer to a vCPU to which a MC exception is injected.
 *
 * @return None
 *
 * @pre vcpu != NULL
 */
void vcpu_inject_mc(struct acrn_vcpu *vcpu)
{
	(void)vcpu_queue_exception(vcpu, IDT_MC, 0);
}

/**
 * @brief Inject thermal sensor interrupt to a vCPU
 *
 * If the VM has Virtual Thermal Management enabled, inject a thermal LAPIC local interrupt for
 * the vCPU by calling vlapic_set_local_intr. Otherwise, do nothing.
 *
 * @param[in] vcpu Pointer to a vCPU to which a thermal sensor interrupt is injected.
 *
 * @return None
 *
 * @pre vcpu != NULL
 */
void vcpu_inject_thermal_interrupt(struct acrn_vcpu *vcpu)
{
	if (vm_cpu_feature_enabled(vcpu->vm, GUEST_CPU_FEATURE_TM_PT)) {
		(void)vlapic_set_local_intr(vcpu, APIC_LVT_THERMAL);
	}
}

/**
 * @brief Handle Interrupt Window VM Exits
 *
 * Interrupt Window Exiting is currently used to trigger a VM exit at the beginning of any
 * instruction if RFLAGS.IF = 1 and there are no other blocking of interrupts, in order to
 * inject pending interrupts (if no APICV support) and NMIs delayed in this round.
 * This handler just disable interrupt window exiting and retain current RIP. As a result of the
 * VM exit, if there are any pending interrupts in vLAPIC, one of the pending interrupts (the
 * highest priority) will be injected to vCPU.
 *
 * @param[inout] vcpu Pointer to a vCPU which handles the interrupt window VM exit.
 *
 * @return Error code or 0 on success
 * @retval 0 on success
 */
int32_t interrupt_window_vmexit_handler(struct acrn_vcpu *vcpu)
{
	TRACE_2L(TRACE_VMEXIT_INTERRUPT_WINDOW, 0UL, 0UL);

	/* Disable interrupt-window exiting first.
	 * acrn_handle_pending_request will continue handle for this vcpu
	 */
	vcpu->arch.irq_window_enabled = false;
	vcpu->arch.proc_vm_exec_ctrls &= ~(VMX_PROCBASED_CTLS_IRQ_WIN);
	exec_vmwrite32(VMX_PROC_VM_EXEC_CONTROLS, vcpu->arch.proc_vm_exec_ctrls);

	vcpu_retain_rip(vcpu);
	return 0;
}

/**
 * @brief Handle External Interrupt VM Exits
 *
 * If the external interrupt information is valid, fill a interrupt context and invoke
 * dispatch_interrupt. Otherwise return -EINVAL.
 * Anyway, the RIP is retained.
 *
 * @param[inout] vcpu Pointer to a vCPU which handles the external interrupt VM exit.
 *
 * @return Error code or 0 on success
 * @retval 0 on success
 * @retval -EINVAL on error that interrupt information is invalid
 *
 * @pre vcpu != NULL
 */
int32_t external_interrupt_vmexit_handler(struct acrn_vcpu *vcpu)
{
	uint32_t intr_info;
	struct intr_excp_ctx ctx;
	int32_t ret;

	intr_info = exec_vmread32(VMX_EXIT_INT_INFO);
	if (((intr_info & VMX_INT_INFO_VALID) == 0U) ||
		(((intr_info & VMX_INT_TYPE_MASK) >> 8U)
		!= VMX_INT_TYPE_EXT_INT)) {
		pr_err("Invalid VM exit interrupt info:%x", intr_info);
		vcpu_retain_rip(vcpu);
		ret = -EINVAL;
	} else {
		ctx.vector = (uint64_t)(intr_info & 0xFFU);
		ctx.rip    = vcpu_get_rip(vcpu);
		ctx.rflags = vcpu_get_rflags(vcpu);
		ctx.cs     = exec_vmread32(VMX_GUEST_CS_SEL);

		dispatch_interrupt(&ctx);
		vcpu_retain_rip(vcpu);

		TRACE_2L(TRACE_VMEXIT_EXTERNAL_INTERRUPT, ctx.vector, 0UL);
		ret = 0;
	}

	return ret;
}

/**
 * @brief Handle pending requests for a vCPU
 *
 * - ACRN_REQUEST_INIT_VMCS request is the first request for a new vCPU because some VMCS fields
 *   should be initialized in the vCPU thread context.
 * - ACRN_REQUEST_TRP_FAULT requests the vCPU to enter zombie state.
 * - ACRN_REQUEST_WAIT_WBINVD requests the vCPU to wait VCPU_EVENT_SYNC_WBINVD to be finished.
 * - ACRN_REQUEST_SPLIT_LOCK requests the vCPU to wait VCPU_EVENT_SPLIT_LOCK to be finished.
 * - ACRN_REQUEST_EPT_FLUSH requests the vCPU to invalid EPT TLB.
 * - ACRN_REQUEST_VPID_FLUSH requests the vCPU to invalid cache for this vCPU.
 * - ACRN_REQUEST_EOI_EXIT_BITMAP_UPDATE requests update eoi exit bitmap to VMCS.
 * - ACRN_REQUEST_SMP_CALL requests the LAPIC pass-through vCPU to handle pending SMP call because
 *   the IPI method can't be used for that vCPU. INIT signal is used to kick the vCPU and SMP call
 *   request is handled here.
 * - ACRN_REQUEST_EXCP requests the vCPU to inject queued exceptions.
 * - ACRN_REQUEST_NMI requests the vCPU to inject pending NMI if vCPU is not in NMI blocking state.
 *   This request will be postponed to next VM enter if ACRN_REQUEST_EXCP was handled in this round.
 * - If no ACRN_REQUEST_EXCP or ACRN_REQUEST_NMI requests in this round, inject pending vector from
 * last VM exit.
 * - Then Check pending interrupts for LAPIC virtualization config (Interrupts for LAPIC PT vCPU are
 *   blocked in VMX root operation and handled directly by the vCPU after returning to VMX non root
 *   operation). If we have injected a vector in this round, vLAPIC without Post Interrupt support
 *   will postpone this interrupt injection.
 * - If an NMI or interrupt is postponed in this round, ensure Interrupt Window Exiting is enabled
 *   so that we can inject them as soon as possible.
 *
 * This is called in vCPU thread, before next VM enter with local interrupt disabled.
 *
 * @param[inout] vcpu Pointer to vCPU which handles pending requests.
 *
 * @return Error code or 0 on success
 * @retval 0 on success
 * @retval -EINVAL on error that interrupt information is invalid
 */
int32_t acrn_handle_pending_request(struct acrn_vcpu *vcpu)
{
	bool injected = false;
	int32_t ret = 0;
	struct acrn_vcpu_arch *arch = &vcpu->arch;
	uint64_t *pending_req_bits = &arch->pending_req;

	if (*pending_req_bits != 0UL) {
		/* make sure ACRN_REQUEST_INIT_VMCS handler as the first one */
		if (bitmap_test_and_clear_lock(ACRN_REQUEST_INIT_VMCS, pending_req_bits)) {
			init_vmcs(vcpu);

			/* Now reuse ACRN_REQUEST_INIT_VMCS to indicate the vcpu is ready to run. */
			if (is_lapic_pt_configured(vcpu->vm)) {
				uint16_t pcpu_id = pcpuid_from_vcpu(vcpu);

				/* For local APIC pass-through, disable interrupt in root mode. */
				CPU_IRQ_DISABLE();

				per_cpu(mode_to_idle, pcpu_id) = IDLE_MODE_PAUSE;
				per_cpu(mode_to_kick_pcpu, pcpu_id) = DEL_MODE_INIT;
			}
		}

		if (bitmap_test_and_clear_lock(ACRN_REQUEST_TRP_FAULT, pending_req_bits)) {
			pr_fatal("Triple fault happen -> shutdown!");
			ret = -EFAULT;
		} else {
			if (bitmap_test_and_clear_lock(ACRN_REQUEST_WAIT_WBINVD, pending_req_bits)) {
				wait_event(&vcpu->events[VCPU_EVENT_SYNC_WBINVD]);
			}

			if (bitmap_test_and_clear_lock(ACRN_REQUEST_SPLIT_LOCK, pending_req_bits)) {
				wait_event(&vcpu->events[VCPU_EVENT_SPLIT_LOCK]);
			}

			if (bitmap_test_and_clear_lock(ACRN_REQUEST_EPT_FLUSH, pending_req_bits)) {
				invept(vcpu->vm->arch_vm.nworld_eptp);
			}

			if (bitmap_test_and_clear_lock(ACRN_REQUEST_VPID_FLUSH,	pending_req_bits)) {
				flush_vpid_single(arch->vpid);
			}

			if (bitmap_test_and_clear_lock(ACRN_REQUEST_EOI_EXIT_BITMAP_UPDATE, pending_req_bits)) {
				vcpu_set_vmcs_eoi_exit(vcpu);
			}

			if (bitmap_test_and_clear_lock(ACRN_REQUEST_SMP_CALL, pending_req_bits)) {
				handle_smp_call();
			}

			if (bitmap_test_and_clear_lock(ACRN_REQUEST_ENTER_REFRIGERATOR, pending_req_bits)) {
				enter_refrigerator(vcpu);
			}
		}
	}

	if (ret == 0) {
		/*
		 * Inject pending exception prior pending interrupt to complete the previous instruction.
		 */
		if ((*pending_req_bits != 0UL) && bitmap_test_and_clear_lock(ACRN_REQUEST_EXCP, pending_req_bits)) {
			if (is_mc_pt_enabled(vcpu) && (vcpu->arch.mc_injection_pending)) {
				/*
				 * If we have #MC coming in while we're processing other exceptions,
				 * We need to inject #MC instead of the original exception.
				 *
				 * If we reach here, we don't know which exception gets queued
				 * as we can queue at most one exception. We inject again to make sure
				 * we're going to inject #MC this time.
				 */
				vcpu->arch.exception_info.exception = IDT_MC;
				vcpu->arch.mc_injection_pending = false;
			}

			vcpu_inject_exception(vcpu);
			injected = true;
		} else {
			/* inject NMI before maskable hardware interrupt */

			if ((*pending_req_bits != 0UL) &&
				bitmap_test_and_clear_lock(ACRN_REQUEST_NMI, pending_req_bits)) {
				if (is_nmi_injectable()) {
					/* Inject NMI vector = 2 */
					exec_vmwrite32(VMX_ENTRY_INT_INFO_FIELD,
							VMX_INT_INFO_VALID | (VMX_INT_TYPE_NMI << 8U) | IDT_NMI);
					injected = true;
				} else {
					/* keep the NMI request for next vmexit */
					bitmap_set_lock(ACRN_REQUEST_NMI, pending_req_bits);
				}
			} else {
				/* handling pending vector injection:
				 * there are many reason inject failed, we need re-inject again
				 * here should take care
				 * - SW exception (not maskable by IF)
				 * - external interrupt, if IF clear, will keep in IDT_VEC_INFO_FIELD
				 *   at next vm exit?
				 */
				if ((arch->idt_vectoring_info & VMX_INT_INFO_VALID) != 0U) {
					exec_vmwrite32(VMX_ENTRY_INT_INFO_FIELD, arch->idt_vectoring_info);
					arch->idt_vectoring_info = 0U;
					injected = true;
				}
			}
		}

		/*
		 * Defer injection of interrupt to be after MTF VM exit,
		 * when emulating the split-lock.
		 */
		if (!is_lapic_pt_configured(vcpu->vm) && !vcpu->arch.emulating_lock) {
			if (bitmap_test_and_clear_lock(ACRN_REQUEST_EVENT, pending_req_bits)) {
				vlapic_inject_intr(vcpu_vlapic(vcpu), is_guest_irq_enabled(vcpu), injected);
			}
		}

		/*
		 * If "virtual-interrupt delivered" is enabled, CPU will evaluate
		 * and automatic inject the virtual interrupts in appropriate time.
		 * And from SDM Vol3 29.2.1, the apicv only trigger evaluation of
		 * pending virtual interrupts when "interrupt-window exiting" is 0.
		 *
		 * External interrupt(from vpic) can't be delivered by "virtual-
		 * interrupt delivery", it only deliver interrupt from vlapic.
		 *
		 * So need to enable "interrupt-window exiting", when there is
		 * an ExtInt or there is lapic interrupt and virtual interrupt
		 * deliver is disabled.
		 */
		if (!is_lapic_pt_configured(vcpu->vm) && !arch->irq_window_enabled) {
			/*
			 * TODO: Currently, NMI exiting and virtual NMIs are not enabled,
			 * so use interrupt window to inject NMI.
			 * After enable virtual NMIs, we can use NMI-Window
			 */
			if (bitmap_test(ACRN_REQUEST_NMI, pending_req_bits)
#ifdef CONFIG_ENFORCE_APICv_BASIC_SUPPORT
				|| vlapic_has_pending_delivery_intr(vcpu)
#endif
				) {

				vcpu->arch.proc_vm_exec_ctrls |= VMX_PROCBASED_CTLS_IRQ_WIN;
				exec_vmwrite32(VMX_PROC_VM_EXEC_CONTROLS, vcpu->arch.proc_vm_exec_ctrls);
				arch->irq_window_enabled = true;
			}
		}
	}

	return ret;
}

/**
 * @brief Handle Exception VM Exits
 *
 * Only GP, AC, MC exceptions are possibly trapped here. For GP, AC exceptions, check if they
 * are caused by bus lock instructions. If so, emulate the bus lock, otherwise inject it back to
 * guest. For MC exception, inject it to the governing vCPU. Note MC is trapped only on non
 * governing vCPUs.
 *
 * @param[inout] vcpu Pointer to a vCPU which handles exception VM exits.
 *
 * @return Error code or 0 on success
 * @retval 0 on success
 * @retval <0 Error propagated from emulate_lock_instr or vcpu_queue_exception and refer to their
 *	      documentation for detailed error cases.
 *
 * @pre vcpu != NULL
 */
int32_t exception_vmexit_handler(struct acrn_vcpu *vcpu)
{
	uint32_t intinfo, int_err_code = 0U;
	uint32_t exception_vector = VECTOR_INVALID;
	uint32_t cpl;
	int32_t status = 0;
#ifdef CONFIG_LOCK_INSTRUCTION_EMULATION
	bool queue_exception;
#endif

	pr_dbg(" Handling guest exception");

	/* Obtain VM-Exit information field pg 2912 */
	intinfo = exec_vmread32(VMX_EXIT_INT_INFO);
	if ((intinfo & VMX_INT_INFO_VALID) != 0U) {
		exception_vector = intinfo & 0xFFU;
		/* Check if exception caused by the guest is a HW exception.
		 * If the exit occurred due to a HW exception obtain the
		 * error code to be conveyed to get via the stack
		 */
		if ((intinfo & VMX_INT_INFO_ERR_CODE_VALID) != 0U) {
			int_err_code = exec_vmread32(VMX_EXIT_INT_ERROR_CODE);

			/* get current privilege level and fault address */
			cpl = exec_vmread32(VMX_GUEST_CS_ATTR);
			cpl = (cpl >> 5U) & 3U;

			if (cpl < 3U) {
				int_err_code &= ~4U;
			} else {
				int_err_code |= 4U;
			}
		}
	}

#ifdef CONFIG_LOCK_INSTRUCTION_EMULATION
	status = emulate_lock_instr(vcpu, exception_vector, &queue_exception);
	if ((status == 0) && queue_exception) {
		vcpu_retain_rip(vcpu);
		status = vcpu_queue_exception(vcpu, exception_vector, int_err_code);
	}
#endif

	if (exception_vector == IDT_MC) {
		/* If we reach here then #MC comes in when non-governing vcpu is running
		 * on this pcpu. Inject to governing vcpu instead.
		 */
		pr_fatal("Exception #MC got from guest!");
		inject_mc_event_to_governing_vcpu(pcpuid_from_vcpu(vcpu), false);
	}

	TRACE_4I(TRACE_VMEXIT_EXCEPTION_OR_NMI,
			exception_vector, int_err_code, 2U, 0U);

	return status;
}

/**
 * @brief Handle NMI Window VM Exits
 *
 * This handler just disable interrupt window exiting and retain current RIP.
 *
 * @param[inout] vcpu Pointer to a vCPU which handles NMI Window VM exits.
 *
 * @return Error code or 0 on success
 * @retval 0 on success
 *
 * @pre vcpu != NULL
 */
int32_t nmi_window_vmexit_handler(struct acrn_vcpu *vcpu)
{
	/*
	 * Disable NMI-window exiting here. We will process
	 * the pending request in acrn_handle_pending_request later
	 */
	vcpu->arch.proc_vm_exec_ctrls &= ~VMX_PROCBASED_CTLS_NMI_WINEXIT;
	exec_vmwrite32(VMX_PROC_VM_EXEC_CONTROLS, vcpu->arch.proc_vm_exec_ctrls);

	vcpu_retain_rip(vcpu);

	return 0;
}

/**
 * @}
 */
