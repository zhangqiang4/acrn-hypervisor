/*
 * Copyright (C) 2018-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <asm/idt.h>


.altmacro

/**
 * @brief An in-memory IDTR structure

 * Define an initialized 64-bit IDTR in data section.
 */
.global HOST_IDT
.global HOST_IDTR

.section    .data
.align 8
    .long   0			/**< padding for alignment purpose */
    .short  0			/**< padding for alignment purpose */
HOST_IDTR:
    .short  HOST_IDT_SIZE - 1	/**< IDT size in bytes - 1 */
    .quad   HOST_IDT		/**< physical address of host IDT */

/*
 * @brief Macro to define an Interrupt Gate IDT entry
 *
 * The 64-bit offset field is temporarily stored at byte 8-15 and we'll rearrange and fix up it at
 * runtime in fixup_idt().
 *
 * @param[in] entry physical address of the handler. It's offset in the flat code segment.
 * @param[in] dpl Descriptor Privilege Level, default to 0.
 * @param[in] ist Dedicated Interrupt Stack Table entry, default value 0 means no dedicated stack.
 */
.macro interrupt_descriptor entry, dpl=0 ist=0
	/* 0x0008 = HOST_GDT_RING0_CODE_SEL */
	.long	0x0008 << 16
	.long	(1U << 15) + (dpl << 13) + (IDT_TYPE_INT_GATE << 8) + ist
	.quad	entry
.endm

/**
 * @brief Macro to define a 64-bit Trap Gate IDT entry
 *
 * The 64-bit offset field is temporarily stored at byte 8-15 and we'll rearrange and fix up it at
 * runtime in fixup_idt().
 *
 * @param[in] entry physical address of the handler. It's offset in the flat code segment.
 * @param[in] dpl Descriptor Privilege Level, default to 0.
 * @param[in] ist Dedicated Interrupt Stack Table entry, default value 0 means no dedicated stack.
 */
.macro	trap_descriptor entry, dpl=0, ist=0
        /* 0x0008 = HOST_GDT_RING0_CODE_SEL */
	.long	0x0008 << 16
	.long	(1U << 15) + (dpl <<13) + (IDT_TYPE_TRAP_GATE << 8) + ist
	.quad	entry
.endm


/**
 * @brief Helper macro to define IDT entries for external interrupts with dpl=0 and ist=0.
 */
.macro _external_interrupt_descriptor vector
    __external_interrupt_descriptor %vector
.endm

.macro	__external_interrupt_descriptor vector
	interrupt_descriptor external_interrupt_\vector
.endm

/**
 * @brief Define the in-memory IDT
 *
 * We'll use interrupt gates and interrupts will be temporarily masked when handling exceptions.
 * The only difference between trap gate and interrupt gate is that when entering an interrupt gate
 * RFLAGS.IF is cleared to mask local interrupts.
 */
.section    .rodata
.align 16
HOST_IDT:
interrupt_descriptor	excp_divide_error
interrupt_descriptor	excp_debug, 3
interrupt_descriptor	excp_nmi
interrupt_descriptor	excp_breakpoint, 3
interrupt_descriptor	excp_overflow, 3
interrupt_descriptor	excp_bounds_check
interrupt_descriptor	excp_illegal_opcode
interrupt_descriptor	excp_device_not_available
interrupt_descriptor	excp_double_fault, 0, DOUBLE_FAULT_IST
interrupt_descriptor	excp_rsvd_09
interrupt_descriptor	excp_invalid_tss
interrupt_descriptor	excp_segment_not_present
interrupt_descriptor	excp_stack_fault, 0, STACK_FAULT_IST
interrupt_descriptor	excp_general_protection
interrupt_descriptor	excp_page_fault
interrupt_descriptor	excp_rsvd_0f
interrupt_descriptor	excp_float_error
interrupt_descriptor	excp_alignment_check
interrupt_descriptor	expt_machine_check, 0, MACHINE_CHECK_IST
interrupt_descriptor	excp_simd_fp_error
interrupt_descriptor	excp_virtualization
interrupt_descriptor	excp_rsvd_21
interrupt_descriptor	excp_rsvd_22
interrupt_descriptor	excp_rsvd_23
interrupt_descriptor	excp_rsvd_24
interrupt_descriptor	excp_rsvd_25
interrupt_descriptor	excp_rsvd_26
interrupt_descriptor	excp_rsvd_27
interrupt_descriptor	excp_rsvd_28
interrupt_descriptor	excp_rsvd_29
interrupt_descriptor	excp_rsvd_30
interrupt_descriptor	excp_rsvd_31

/**
 * @brief Define the IDT entries for external interrupts (vector 0x20-0xFF)
 */
vector = 0x20
.rept	(0x100 - 0x20)
	_external_interrupt_descriptor vector
	vector = vector + 1
.endr

/**
 * @brief Entry points of exception handlers
 *
 * Hardware pushes error code to stack for some exceptions. To keep exception stack frame
 * consistent, following handlers for non-error code exceptions will push a pseudo error code 0.
 * All exception handlers push a vector number to identify the exception onto stack and than jump
 * to the common handler excp_save_frame which checks the vector number on stack to dispatch the
 * corresponding handler.
 */
.section .text
.align 16
excp_divide_error:
	pushq  $0x0			/* pseudo error code */
	pushq  $0x00
	jmp    excp_save_frame

.align 8
excp_debug:
	pushq  $0x0			/* pseudo error code */
	pushq  $0x01
	jmp    excp_save_frame

.align 8
excp_nmi:
	pushq  $0x0			/* pseudo error code */
	pushq  $0x2
	jmp    nmi_save_frame

.align 8
excp_breakpoint:
	pushq  $0x0			/* pseudo error code */
	pushq  $0x03
	jmp    excp_save_frame

.align 8
excp_overflow:
	pushq  $0x0			/* pseudo error code */
	pushq  $0x04
	jmp    excp_save_frame

.align 8
excp_bounds_check:
	pushq  $0x0			/* pseudo error code */
	pushq  $0x05
	jmp    excp_save_frame

.align 8
excp_illegal_opcode:
	pushq  $0x0			/* pseudo error code */
	pushq  $0x06
	jmp    excp_save_frame

.align 8
excp_device_not_available:
	pushq  $0x0			/* pseudo error code */
	pushq  $0x07
	jmp    excp_save_frame

.align 8
excp_double_fault:
	pushq  $0x08
	jmp    excp_save_frame

.align 8
excp_invalid_tss:
	pushq  $0x0A
	jmp    excp_save_frame

.align 8
excp_segment_not_present:
	pushq  $0x0B
	jmp    excp_save_frame

.align 8
excp_stack_fault:
	pushq  $0x0C
	jmp    excp_save_frame

.align 8
excp_general_protection:
	pushq  $0x0D
	jmp    excp_save_frame

.align 8
excp_page_fault:
	pushq  $0x0E
	jmp    excp_save_frame

.align 8
excp_float_error:
	pushq  $0x0			/* pseudo error code */
	pushq  $0x10
	jmp    excp_save_frame

.align 8
excp_alignment_check:
	pushq  $0x11
	jmp    excp_save_frame

.align 8
expt_machine_check:
	pushq  $0x0			/* pseudo error code */
	pushq  $0x12
	jmp    excp_save_frame

.align 8
excp_simd_fp_error:
	pushq  $0x0			/* pseudo error code */
	pushq  $0x13
	jmp    excp_save_frame

.align 8
excp_virtualization:
	pushq  $0x0			/* pseudo error code */
	pushq  $0x14
	jmp    excp_save_frame



/**
 * @brief Define IDT handlers for reserved vectors.
 *
 * Vectors 0x09, 0x0F, 0x15 through 0x1F are reserved.
 * The handler is excp_rsvd.
 *
 * @param[in] vector Number of the reserved vector
 */
.macro _rsvd_vector vector
    __rsvd_vector %vector
.endm

.macro __rsvd_vector vector
.align 8
excp_rsvd_\vector\():
	pushq  $0x0			/* pseudo error code */
	pushq  $\vector
	jmp    excp_rsvd
.endm

.align 8
excp_rsvd_09:
    _rsvd_vector 0x09

.align 8
excp_rsvd_0f:
    _rsvd_vector 0x0f

vector = 0x15
.rept	(0x20 - 0x15)
    _rsvd_vector vector
    vector = vector + 1
.endr



/**
 * @brief Define IDT handlers for external interrupts.
 *
 * Vectors 0x20 through 0xFF are for external interrupts.
 * The handler external_interrupt_{vector} matches the offset field in corresponding IDT entry.
 * 
 * @param[in] vector Number of the external interrupt
 */
.macro _external_interrupt vector
    __external_interrupt %vector
.endm

.macro __external_interrupt vector
.align 8
external_interrupt_\vector\():
	pushq  $0x0			/* pseudo error code */
	pushq  $\vector
	jmp    external_interrupt_save_frame
.endm

vector =0x20
.rept	(0x100 - 0x20)
    _external_interrupt vector
    vector = vector + 1
.endr

/**
 * @brief Save general purpose register in the exception/interrupt stack frame
 */
.macro save_frame
    pushq %r15
    pushq %r14
    pushq %r13
    pushq %r12
    pushq %r11
    pushq %r10
    pushq %r9
    pushq %r8
    pushq %rdi
    pushq %rsi
    pushq %rbp
    pushq %rsp
    pushq %rbx
    pushq %rdx
    pushq %rcx
    pushq %rax

    /* Put current stack pointer into 1st param register (rdi) */
    movq    %rsp, %rdi
.endm

/**
 * @brief Restore general purpose register from the exception/interrupt stack frame
 */
.macro restore_frame
    popq    %rax
    popq    %rcx
    popq    %rdx
    popq    %rbx
    popq    %rsp
    popq    %rbp
    popq    %rsi
    popq    %rdi
    popq    %r8
    popq    %r9
    popq    %r10
    popq    %r11
    popq    %r12
    popq    %r13
    popq    %r14
    popq    %r15

    /* Skip vector and error code*/
    add     $16, %rsp
.endm

/**
 * @brief Common entry point for defined exceptions
 */
.align 8
excp_save_frame:
    save_frame

    call   dispatch_exception

    restore_frame

    iretq


/**
 * @brief Common entry point for reserved exceptions.
 *
 * These should never execute.
 * We put a handler on them anyway to highlight the unexpected.
 */
.align 8
excp_rsvd:
    save_frame

    call   dispatch_exception

    restore_frame

    iretq

/**
 * @brief Common entry point for NMI interrupts
 */
.align 8
nmi_save_frame:
    save_frame

    call   handle_nmi

    restore_frame

    iretq


/**
 * @brief Common entry point for defined interrupts.
 *
 * It's the common handler for external interrupts 0x20 ~ 0xFF. It saves the GPRs in the stack and
 * calls dispatch_interrupt followed by restoring GPRS from the stack.
 */
.align 8
external_interrupt_save_frame:
    save_frame

    call   dispatch_interrupt

    restore_frame

    iretq

/**
 * @}
 */
