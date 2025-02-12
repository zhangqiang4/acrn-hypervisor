/*
 * Copyright (C) 2021-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ARCH_X86_GUEST_VIRQ_H
#define ARCH_X86_GUEST_VIRQ_H

struct acrn_vcpu;
struct acrn_vm;

/**
 * @addtogroup vp-base_virq vp-base.virq
 * @{
 */

/**
 * @file
 *
 * @brief Macros and APIs for virtual interrupt
 */

#define EXCEPTION_ERROR_CODE_VALID  8U          /**< Mark an exception happens with an error code */

/**
 * @defgroup EXCEPTION_CLASS Exception Class definitions
 *
 * Exception Class determines whether simultaneous two exceptions can be handled serially or
 * triggers a double-fault.
 * Refer to SDM Vol3 -6.15, Table 6-4 - interrupt and exception classes.
 *
 * @{
 */
#define EXCEPTION_CLASS_BENIGN	1               /**< Benigh Exceptions and Interrupts */
#define EXCEPTION_CLASS_CONT	2               /**< Contributory Exceptions and Interrupts */
#define EXCEPTION_CLASS_PF	3               /**< Page Faults */
/**
 * @}
 */
/* End of EXCEPTION_CLASS */

/**
 * @defgroup EXCEPTION_TYPE Exception types definitions
 *
 * @{
 */
#define EXCEPTION_FAULT		0U              /**< Fault Exception */
#define EXCEPTION_TRAP		1U              /**< Trap Exception */
#define EXCEPTION_ABORT		2U              /**< Abort Exception */
#define EXCEPTION_INTERRUPT	3U              /**< Interrupt */
/**
 * @}
 */
/* End of EXCEPTION_TYPE */

/**
 * @defgroup RFLAGS RFLAGS bits related to exception and interrupt
 *
 * Refer to SDM Vol1 3.4.3.3 System Flags and IOPL Field
 *
 * @{
 */
#define HV_ARCH_VCPU_RFLAGS_TF              (1UL<<8U)   /**< Trap flag. Set to enable single step mode */
#define HV_ARCH_VCPU_RFLAGS_IF              (1UL<<9U)   /**< Processor local interrupt enable flag. */
#define HV_ARCH_VCPU_RFLAGS_RF              (1UL<<16U)  /**< Resume flag. Controls the processor's response to
							  debug exceptions */
/**
 * @}
 */
/* End of RFLAGS */

/**
 * @defgroup INTERRUPTIBILITY_STATE Interruptibility State definitions
 *
 * Refer to SDM Vol3 24.4.2 Guest Non-Register State. Table 24-3. Format of Interruptibility
 * State.
 *
 * @{
 */
#define HV_ARCH_VCPU_BLOCKED_BY_STI         (1UL<<0U)	/**< Blocked by execution of STI with RFLAGS.IF=0 on the
							  instruction boundary. */
#define HV_ARCH_VCPU_BLOCKED_BY_MOVSS       (1UL<<1U)	/**< Blocked by execution of a MOV to SS or a POP to SS on
							  the instruction boundary. */
#define HV_ARCH_VCPU_BLOCKED_BY_NMI         (1UL<<3U)	/**< Blocked by an in-servicing NMI */
/**
 * @}
 */
/* End of INTERRUPTIBILITY_STATE */

int32_t vcpu_queue_exception(struct acrn_vcpu *vcpu, uint32_t vector_arg, uint32_t err_code_arg);

void vcpu_inject_nmi(struct acrn_vcpu *vcpu);

void vcpu_inject_gp(struct acrn_vcpu *vcpu, uint32_t err_code);

void vcpu_inject_pf(struct acrn_vcpu *vcpu, uint64_t addr, uint32_t err_code);

void vcpu_inject_ud(struct acrn_vcpu *vcpu);

void vcpu_inject_ss(struct acrn_vcpu *vcpu);

void vcpu_inject_mc(struct acrn_vcpu *vcpu);

void vcpu_inject_thermal_interrupt(struct acrn_vcpu *vcpu);
void vcpu_make_request(struct acrn_vcpu *vcpu, uint16_t eventid);

int32_t exception_vmexit_handler(struct acrn_vcpu *vcpu);
int32_t nmi_window_vmexit_handler(struct acrn_vcpu *vcpu);
int32_t interrupt_window_vmexit_handler(struct acrn_vcpu *vcpu);
int32_t external_interrupt_vmexit_handler(struct acrn_vcpu *vcpu);
int32_t acrn_handle_pending_request(struct acrn_vcpu *vcpu);

/**
 * @}
 */

#endif /* ARCH_X86_GUEST_VIRQ_H */
