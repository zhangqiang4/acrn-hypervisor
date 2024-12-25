/*
 * Copyright (C) 2018-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef IDT_H
#define IDT_H

/**
 * @addtogroup hwmgmt_irq hwmgmt.irq
 *
 * @{
 */

/**
 * @file arch/x86/idt.S
 *
 * @brief Definition of IDT, IDTR, and exception/interrupt entry point handlers.
 */

/**
 * @file
 *
 * @brief Declaration of Interrupt Descriptor Table (IDT) and IDTR register
 *
 * Refer to Chapter 7.10 INTERRUPT DESCRIPTOR TABLE, Vol.3, SDM-325384-085US for details.
 */

#define     X64_IDT_DESC_SIZE   (0x10U)	/**< Interrupt Descriptor Table (LDT) entry size for IA-32e is 16 byte */
#define     HOST_IDT_ENTRIES    (0x100U)/**< Max IDT vectors (256) for each logical CPU. */
/**
 * @brief Size of the IDT
 *
 * It's the maximum size of IDT on x86-64 platform. We use it to define a static IDT.
 */
#define     HOST_IDT_SIZE       (HOST_IDT_ENTRIES * X64_IDT_DESC_SIZE)

/* IST allocations for special traps */
#define MACHINE_CHECK_IST	(1U)	/**< 1 is for machine check exception stack */
#define DOUBLE_FAULT_IST	(2U)	/**< 2 is for double fault exception stack */
#define STACK_FAULT_IST		(3U)	/**< 3 is for stack fault exception stack */

/* IDT Type Definitions for both 32-bit protected mode and IA-32e mode */
#define IDT_TYPE_TSS_AVAIL_16	(1U)	/**< 16-bit TSS (Available). 32-bit mode only */
#define IDT_TYPE_LDT		(2U)	/**< LDT */
#define IDT_TYPE_TSS_BUSY_16	(3U)	/**< 16-bit TSS (Busy). 32-bit mode only */
#define IDT_TYPE_CALL_GATE_16	(4U)	/**< 16-bit Call Gate. 32-bit mode only */
#define IDT_TYPE_TASK_GATE	(5U)	/**< Task Gate. 32-bit mode only */
#define IDT_TYPE_INT_GATE_16	(6U)	/**< 16-bit Interrupt Gate. 32-bit mode only */
#define IDT_TYPE_TRAP_GATE_16	(7U)	/**< 16-bit Trap Gate. 32-bit mode only */
#define IDT_TYPE_TSS_AVAIL	(9U)	/**< TSS (Available) */
#define IDT_TYPE_TSS_BUSY	(11U)	/**< TSS (Busy) */
#define IDT_TYPE_CALL_GATE	(12U)	/**< Call Gate */
#define IDT_TYPE_INT_GATE	(14U)	/**< Interrupt Gate */
#define IDT_TYPE_TRAP_GATE	(15U)	/**< Trap Gate */


#ifndef ASSEMBLER

/**
 * @brief Definition of a 16-byte IDT entry for IA-32e mode
 *
 * There are three kinds of IDT descriptors: task-gate descriptors, interrupt-gate descriptors, and
 * trap-gate descriptors.
 *
 * Refer to Chapter 7.11 IDT DESCRIPTORS, Vol.3, SDM-325384-085US for details.
 *
 * @alignment 16
 *
 * @remark This structure shall be packed.
 */
struct idt_64_descriptor {
	union {
		uint32_t value;				/**< The dword value as a whole */
		struct {
			uint32_t offset_15_0:16;	/**< Bit 15-0 of offset to procedure entry point in the
							 * Segment */
			uint32_t seg_sel:16;		/**< Segment Selector for destination code segment */
		} bits;					/**< Bit field representations */
	} low32;					/**< The first dword of the 16-Byte IDT entry */
	union {
		uint32_t value;				/**< The dword value as a whole */
		struct {
			uint32_t ist:3;			/**< Interrupt Stack Table */
			uint32_t bit_3_clr:1;		/**< Always 0 in 64-bit IDT */
			uint32_t bit_4_clr:1;		/**< Always 0 in 64-bit IDT */
			uint32_t bits_5_7_clr:3;	/**< Always 0 in 64-bit IDT */
			uint32_t type:4;		/**< IDT Type. See IDT Type Definitions above */
			uint32_t bit_12_clr:1;		/**< Always 0 in 64-bit IDT */
			uint32_t dpl:2;			/**< Descriptor Privilege Level */
			uint32_t present:1;		/**< Whether this IDT entry is valid or not */
			uint32_t offset_31_16:16;	/**< Bit 31-16 of offset to procedure entry point in the
							 * Segment */
		} bits;					/**< Bit field representations */
	} high32;					/**< The second dword of the 16-Byte IDT entry */
	uint32_t offset_63_32;	/**< Bit 63-32 of offset to procedure entry point in the Segment */
	uint32_t rsvd;		/**< Reserved */
} __aligned(16) __packed;

/**
 * @brief The Interrupt Descriptor Table for 64-bit long mode as a whole
 *
 * @alignment 16
 */
struct host_idt {
	struct idt_64_descriptor host_idt_descriptors[HOST_IDT_ENTRIES]; /**< IDT entries */
} __aligned(16);

/**
 * @brief Definition of the IDT Regsiter (IDTR) in 64-bit mode.
 *
 * the processor locates the IDT using the IDTR register. This register holds both a 64-bit
 * base address and a 16-bit limit for the IDT.
 * This structure is used when CPU is running in IA-32e mode with identical mapping, so
 * it's safe to define idt as a pointer here.
 *
 * @remark This structure shall be packed.
 */
struct host_idt_descriptor {
	uint16_t len;			/**< Length of the IDT, total bytes - 1 */
	struct host_idt *idt;		/**< The physical address of IDT base. */
} __packed;

/**
 * @brief The static IDT
 *
 * We'll use interrupt gates and interrupts will be temporarily masked when handling exceptions,
 * which means interrupts are disabled when handing both interrupts and exceptions.
 * The only difference between trap gate and interrupt gate is that when entering an interrupt gate
 * RFLAGS.IF is cleared to mask local interrupts.
 * Because we load this 64-bit IDT in IA-32e mode, it must be aligned to a 16-byte boundary.
 */
extern struct host_idt HOST_IDT;

/**
 * @brief The static IDTR structure

 * A statically initialized 64-bit IDTR in data section.
 */
extern struct host_idt_descriptor HOST_IDTR;
#endif /* end #ifndef ASSEMBLER */

/**
 * @}
 */
#endif /* IDT_H */
