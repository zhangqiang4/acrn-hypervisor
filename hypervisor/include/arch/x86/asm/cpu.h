/*-
 * Copyright (c) 1989, 1990 William F. Jolitz
 * Copyright (c) 1990 The Regents of the University of California.
 * Copyright (c) 2017-2025 Intel Corporation.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: @(#)segments.h	7.1 (Berkeley) 5/9/91
 * $FreeBSD$
 */

#ifndef CPU_H
#define CPU_H

#include <types.h>
#include <util.h>
#include <acrn_common.h>
#include <asm/msr.h>
#include <errno.h>

/**
 * @addtogroup hwmgmt_cpu
 *
 * @{
 */
/**
 * @file
 * @brief Defines macros, data structures and APIs related to the register and the CPU status.
 */

/**
 * @brief The alignment of CPU stack.
 */
#define CPU_STACK_ALIGN         16UL

/* CR0 register definitions */
/**
 * @brief The paging enable bit in CR0.
 */
#define CR0_PG                  (1UL<<31U)
/**
 * @brief The cache disable bit in CR0.
 */
#define CR0_CD                  (1UL<<30U)
/**
 * @brief The not write through bit in CR0.
 */
#define CR0_NW                  (1UL<<29U)
/**
 * @brief The alignment mask bit in CR0.
 */
#define CR0_AM                  (1UL<<18U)
/**
 * @brief The write protect bit in CR0.
 */
#define CR0_WP                  (1UL<<16U)
/**
 * @brief The numeric error bit in CR0.
 */
#define CR0_NE                  (1UL<<5U)
/**
 * @brief The extension type bit in CR0.
 */
#define CR0_ET                  (1UL<<4U)
/**
 * @brief The task switched bit in CR0.
 */
#define CR0_TS                  (1UL<<3U)
/**
 * @brief The emulation bit in CR0.
 */
#define CR0_EM                  (1UL<<2U)
/**
 * @brief The monitor coprocessor bit in CR0.
 */
#define CR0_MP                  (1UL<<1U)
/**
 * @brief The protected mode enabled bit in CR0.
 */
#define CR0_PE                  (1UL<<0U)

/* CR3 register definitions */
/**
 * @brief The page-level write through bit in CR3.
 */
#define CR3_PWT                 (1UL<<3U)
/**
 * @brief The page-level cache disable bit in CR3.
 */
#define CR3_PCD                 (1UL<<4U)

/* CR4 register definitions */
/**
 * @brief The virtual 8086 mode extensions bit in CR4.
 */
#define CR4_VME                 (1UL<<0U)
/**
 * @brief The protected mode virtual interrupts bit in CR4.
 */
#define CR4_PVI                 (1UL<<1U)
/**
 * @brief The time stamp disable bit in CR4.
 */
#define CR4_TSD                 (1UL<<2U)
/**
 * @brief The debugging extensions bit in CR4.
 */
#define CR4_DE                  (1UL<<3U)
/**
 * @brief The page size extensions bit in CR4.
 */
#define CR4_PSE                 (1UL<<4U)
/**
 * @brief The physical address extensions bit in CR4.
 */
#define CR4_PAE                 (1UL<<5U)
/**
 * @brief The machine check enable bit in CR4.
 */
#define CR4_MCE                 (1UL<<6U)
/**
 * @brief The page global enable bit in CR4.
 */
#define CR4_PGE                 (1UL<<7U)
/**
 * @brief The performance monitoring counter enable bit in CR4.
 */
#define CR4_PCE                 (1UL<<8U)
/**
 * @brief The OS support for FXSAVE/FXRSTOR bit in CR4.
 */
#define CR4_OSFXSR              (1UL<<9U)
/**
 * @brief The OS support for unmasked SIMD floating point exceptions bit in CR4.
 */
#define CR4_OSXMMEXCPT          (1UL<<10U)
/**
 * @brief The user-mode instruction prevention bit in CR4.
 */
#define CR4_UMIP                (1UL<<11U)
/**
 * @brief The 57-bit linear address enable bit in CR4.
 */
#define CR4_LA57                (1UL<<12U)
/**
 * @brief The VMX enable bit in CR4.
 */
#define CR4_VMXE                (1UL<<13U)
/**
 * @brief The SMX enable bit in CR4.
 */
#define CR4_SMXE                (1UL<<14U)
/**
 * @brief The bits used to enable the instructions RDFSBASE, RDGSBASE, WRFSBASE, and WRGSBASE in CR4.
 */
#define CR4_FSGSBASE            (1UL<<16U)
/**
 * @brief The PCID enable bit in CR4.
 */
#define CR4_PCIDE               (1UL<<17U)
/**
 * @brief The XSAVE and Processor Extended States enable bit in CR4.
 */
#define CR4_OSXSAVE             (1UL<<18U)
/**
 * @brief The KeyLocker enable bit in CR4.
 */
#define CR4_KL                  (1UL<<19U)
/**
 * @brief The Supervisor-Mode Execution Prevention enable bit in CR4.
 */
#define CR4_SMEP                (1UL<<20U)
/**
 * @brief The Supervisor-Mode Access Prevention enable bit in CR4.
 */
#define CR4_SMAP                (1UL<<21U)
/**
 * @brief The Protect-key-enable bit in CR4.
 */
#define CR4_PKE                 (1UL<<22U)
/**
 * @brief The Control-flow Enforcement Technology enable bit in CR4.
 */
#define CR4_CET                 (1UL<<23U)
/**
 * @brief The protection keys for supervisor-mode pages enable bit in CR4.
 */
#define CR4_PKS                 (1UL<<24U)
/**
 * @brief The linear address space separation (LASS) enable bit in CR4.
 */
#define CR4_LASS                (1UL<<27U)
/**
 * @brief The linear-address masking (LAM) for supervisor addresses enable bit in CR4.
 */
#define CR4_LAM_SUP             (1UL<<28U)


/**
 * @brief Macro indicating the XCR0 SSE state.
 *
 * If the SSE state is enabled, the XSAVE feature set can be used to manage MXCSR and the XMM registers.
 */
#define XCR0_SSE		(1UL<<1U)
/**
 * @brief Macro indicating the XCR0 AVX state.
 *
 * If the AVX state is enabled, AVX instructions can be executed and the XSAVE feature set can be used to manage the
 * upper halves of the YMM registers.
 */
#define XCR0_AVX		(1UL<<2U)
/**
 * @brief Macro indicating the XCR0 BNDREGS state.
 *
 * If the BNDREGS state is enabled, MPX instructions can be executed and the XSAVE feature set can be used to manage
 * the bounds registers BND0-BND3.
 */
#define XCR0_BNDREGS		(1UL<<3U)
/**
 * @brief Macro indicating the XCR0 BNDCSR state.
 *
 * If the BNDCSR state is enabled, MPX instructions can be executed and the XSAVE feature set can be used to manage the
 * BNDCFGU and BNDSTATUS registers.
 */
#define XCR0_BNDCSR		(1UL<<4U)
/**
 * @brief Macro indicating the XCR0 PKRU state.
 *
 * If the PKRU state is enabled, the XSAVE feature set can be used to manage the PKRU register.
 */
#define XCR0_PKRU		(1UL<<9U)
/**
 * @brief Macro indicating the XCR0 reserved bits.
 *
 * According to SDM Vol1 13.3:
 *   XCR0[63:10] and XCR0[8] are reserved. Executing the XSETBV instruction causes a general-protection fault if
 *   ECX = 0 and any corresponding bit in EDX:EAX is not 0.
 */
#define	XCR0_RESERVED_BITS	((~((1UL << 10U) - 1UL)) | (1UL << 8U))

/*
 * Entries in the Interrupt Descriptor Table (IDT)
 */
/**
 * @brief Divide Error vector in the Interrupt Descriptor Table (IDT).
 */
#define IDT_DE      0U
/**
 * @brief Debug vector in the Interrupt Descriptor Table (IDT).
 */
#define IDT_DB      1U
/**
 * @brief Nonmaskable External Interrupt vector in the Interrupt Descriptor Table (IDT).
 */
#define IDT_NMI     2U
/**
 * @brief Breakpoint vector in the Interrupt Descriptor Table (IDT).
 */
#define IDT_BP      3U
/**
 * @brief Overflow vector in the Interrupt Descriptor Table (IDT).
 */
#define IDT_OF      4U
/**
 * @brief Bound Range Exceeded in the Interrupt Descriptor Table (IDT).
 */
#define IDT_BR      5U
/**
 * @brief Undefined/Invalid Opcode vector in the Interrupt Descriptor Table (IDT).
 */
#define IDT_UD      6U
/**
 * @brief No Math Coprocessor in the Interrupt Descriptor Table (IDT).
 */
#define IDT_NM      7U
/**
 * @brief Double Fault vector in the Interrupt Descriptor Table (IDT).
 */
#define IDT_DF      8U
/**
 * @brief Coprocessor Segment Overrun in the Interrupt Descriptor Table (IDT).
 */
#define IDT_FPUGP   9U
/**
 * @brief Invalid TSS vector in the Interrupt Descriptor Table (IDT).
 */
#define IDT_TS      10U
/**
 * @brief Segment Not Present vector in the Interrupt Descriptor Table (IDT).
 */
#define IDT_NP      11U
/**
 * @brief Stack Segment Fault vector in the Interrupt Descriptor Table (IDT).
 */
#define IDT_SS      12U
/**
 * @brief General Protection Fault vector in the Interrupt Descriptor Table (IDT).
 */
#define IDT_GP      13U
/**
 * @brief Page Fault vector in the Interrupt Descriptor Table (IDT).
 */
#define IDT_PF      14U
/**
 * @brief FPU Floating-Point Error vector in the Interrupt Descriptor Table (IDT).
 */
#define IDT_MF      16U
/**
 * @brief Alignment Check (or Access Control) vector in the Interrupt Descriptor Table (IDT).
 */
#define IDT_AC      17U
/**
 * @brief Machine Check vector in the Interrupt Descriptor Table (IDT).
 */
#define IDT_MC      18U  /* #MC: Machine Check */
/**
 * @brief SIMD Floating-Point Exception vector in the Interrupt Descriptor Table (IDT).
 */
#define IDT_XF      19U
/**
 * @brief Virtualization Exception vector in the Interrupt Descriptor Table (IDT).
 */
#define IDT_VE      20U

/* Bits in EFER special registers */
#define EFER_LMA 0x00000400U /**< Long Mode Active (R) bit in EFER register */

#define RFLAGS_C (1U<<0U) /**< Carry flag (CF) bit in RFLAGS register */
#define RFLAGS_P (1U<<2U) /**< Parity flag (PF) bit in RFLAGS register */
#define RFLAGS_A (1U<<4U) /**< Auxiliary Carry flag (AF) bit in RFLAGS register */
#define RFLAGS_Z (1U<<6U) /**< Zero flag (ZF) bit in RFLAGS register */
#define RFLAGS_S (1U<<7U) /**< Sign flag (SF) bit in RFLAGS register */
#define RFLAGS_O (1U<<11U) /**< Overflow flag (OF) bit in RFLAGS register */
#define RFLAGS_VM (1U<<17U) /**< Virtual Mode (VM) flag bit in RFLAGS register */
#define RFLAGS_AC (1U<<18U) /**< Alignment Check / Access Control (AC) flag bit in RFLAGS register */

/* CPU clock frequencies (FSB) */
#define CPU_FSB_83KHZ           83200  /**< The CPU clock frequency: 83KHz */
#define CPU_FSB_100KHZ          99840  /**< The CPU clock frequency: 100KHz */
#define CPU_FSB_133KHZ          133200 /**< The CPU clock frequency: 133KHz */
#define CPU_FSB_166KHZ          166400 /**< The CPU clock frequency: 166KHz */

/* Time conversions */
#define CPU_GHZ_TO_HZ           1000000000 /**< The factor of converting GHz to Hz */
#define CPU_GHZ_TO_KHZ          1000000 /**< The factor of converting GHz to KHz */
#define CPU_GHZ_TO_MHZ          1000 /**< The factor of converting GHz to MHz */
#define CPU_MHZ_TO_HZ           1000000 /**< The factor of converting MHz to Hz */
#define CPU_MHZ_TO_KHZ          1000 /**< The factor of converting MHz to KHz */

/**
 * @brief The number of general-purpose registers saved/restored for vCPU.
 */
#define NUM_GPRS                            16U
/**
 * @brief The maximum size in bytes of the XSAVE area.
 */
#define XSAVE_STATE_AREA_SIZE			4096U
/**
 * @brief The size of the legacy region of an XSAVE area.
 */
#define XSAVE_LEGACY_AREA_SIZE			512U
/**
 * @brief The size of the XSAVE header of an XSAVE area.
 */
#define XSAVE_HEADER_AREA_SIZE			64U
/**
 * @brief The size of the extended region of an XSAVE area.
 */
#define XSAVE_EXTEND_AREA_SIZE			(XSAVE_STATE_AREA_SIZE - \
						XSAVE_HEADER_AREA_SIZE - \
						XSAVE_LEGACY_AREA_SIZE)
/**
 * @brief The bit indicates the format of the extended region of the XSAVE area.
 *
 * It is the highest bit of XCOMP_BV in the XSAVE area header. If this bit is clear, the format is the standard format.
 * If this bit is set, the format is the compacted format.
 */
#define XSAVE_COMPACTED_FORMAT			(1UL << 63U)
/**
 * @brief The bit of FPU in the header of the XSAVE area.
 */
#define XSAVE_FPU				(1UL << 0U)
/**
 * @brief The bit of SSE in the header of the XSAVE area.
 */
#define XSAVE_SSE				(1UL << 1U)

#define	CPU_CONTEXT_OFFSET_RAX			0U /**< Offset of RAX register in CPU context. */
#define	CPU_CONTEXT_OFFSET_RCX			8U /**< Offset of RCX register in CPU context. */
#define	CPU_CONTEXT_OFFSET_RDX			16U /**< Offset of RDX register in CPU context. */
#define	CPU_CONTEXT_OFFSET_RBX			24U /**< Offset of RBX register in CPU context. */
#define	CPU_CONTEXT_OFFSET_RSP			32U /**< Offset of RSP register in CPU context. */
#define	CPU_CONTEXT_OFFSET_RBP			40U /**< Offset of RBP register in CPU context. */
#define	CPU_CONTEXT_OFFSET_RSI			48U /**< Offset of RSI register in CPU context. */
#define	CPU_CONTEXT_OFFSET_RDI			56U /**< Offset of RDI register in CPU context. */
#define	CPU_CONTEXT_OFFSET_R8			64U /**< Offset of R8 register in CPU context. */
#define	CPU_CONTEXT_OFFSET_R9			72U /**< Offset of R9 register in CPU context. */
#define	CPU_CONTEXT_OFFSET_R10			80U /**< Offset of R10 register in CPU context. */
#define	CPU_CONTEXT_OFFSET_R11			88U /**< Offset of R11 register in CPU context. */
#define	CPU_CONTEXT_OFFSET_R12			96U /**< Offset of R12 register in CPU context. */
#define	CPU_CONTEXT_OFFSET_R13			104U /**< Offset of R13 register in CPU context. */
#define	CPU_CONTEXT_OFFSET_R14			112U /**< Offset of R14 register in CPU context. */
#define	CPU_CONTEXT_OFFSET_R15			120U /**< Offset of R15 register in CPU context. */
#define	CPU_CONTEXT_OFFSET_CR0			128U /**< Offset of CR0 register in CPU context. */
#define	CPU_CONTEXT_OFFSET_CR2			136U /**< Offset of CR2 register in CPU context. */
#define	CPU_CONTEXT_OFFSET_CR4			144U /**< Offset of CR4 register in CPU context. */
#define	CPU_CONTEXT_OFFSET_RIP			152U /**< Offset of RIP register in CPU context. */
#define	CPU_CONTEXT_OFFSET_RFLAGS		160U /**< Offset of RFLAGS register in CPU context. */
#define	CPU_CONTEXT_OFFSET_IA32_SPEC_CTRL	168U /**< Offset of IA32_SPEC_CTRL register in CPU context. */
#define	CPU_CONTEXT_OFFSET_IA32_EFER		176U /**< Offset of IA32_EFER register in CPU context. */
#define	CPU_CONTEXT_OFFSET_EXTCTX_START		184U /**< Offset of EXTCTX_STAR register in CPU context. */
#define	CPU_CONTEXT_OFFSET_CR3			184U /**< Offset of CR3 register in CPU context. */
#define	CPU_CONTEXT_OFFSET_IDTR			192U /**< Offset of IDTR register in CPU context. */
#define	CPU_CONTEXT_OFFSET_LDTR			216U /**< offset of LDTR register in CPU context. */

#define VER_INFO_MAX_LENGTH	512	/**< maximum length of verion_info array */
extern const char version_info[VER_INFO_MAX_LENGTH];

#ifndef ASSEMBLER

#define ALL_CPUS_MASK		((1UL << get_pcpu_nums()) - 1UL) /**< Mask of all CPUs */
#define AP_MASK			(ALL_CPUS_MASK & ~(1UL << BSP_CPU_ID)) /**< Mask of all APs */

/**
 *
 * Identifiers for architecturally defined registers.
 *
 * These register names is used in condition statement.
 * Within the following groups,register name need to be
 * kept in order:
 * General register names group (CPU_REG_RAX~CPU_REG_R15);
 * Non general register names group (CPU_REG_CR0~CPU_REG_GDTR);
 * Segment register names group (CPU_REG_ES~CPU_REG_GS).
 */
/**
 * @brief This enumeration type defines identifiers for architecturally defined registers. The order of general-purpose
 * registers is kept the same as those in the structure acrn_gp_regs.
 */
enum cpu_reg_name {
	/* General purpose register layout should align with struct acrn_gp_regs */
	CPU_REG_RAX, /**< The RAX general purpose register */
	CPU_REG_RCX, /**< The RCX general purpose register */
	CPU_REG_RDX, /**< The RDX general purpose register */
	CPU_REG_RBX, /**< The RBX general purpose register */
	CPU_REG_RSP, /**< The RSP general purpose register */
	CPU_REG_RBP, /**< The RBP general purpose register */
	CPU_REG_RSI, /**< The RSI general purpose register */
	CPU_REG_RDI, /**< The RDI general purpose register */
	CPU_REG_R8, /**< The R8 general purpose register */
	CPU_REG_R9, /**< The R9 general purpose register */
	CPU_REG_R10, /**< The R10 general purpose register */
	CPU_REG_R11, /**< The R11 general purpose register */
	CPU_REG_R12, /**< The R12 general purpose register */
	CPU_REG_R13, /**< The R13 general purpose register */
	CPU_REG_R14, /**< The R14 general purpose register */
	CPU_REG_R15, /**< The R15 general purpose register */

	CPU_REG_CR0, /**< The CR0 control register */
	CPU_REG_CR2, /**< The CR2 control register */
	CPU_REG_CR3, /**< The CR3 control register */
	CPU_REG_CR4, /**< The CR4 control register */
	CPU_REG_DR7, /**< The DR7 register */
	CPU_REG_RIP, /**< The RIP register */
	CPU_REG_RFLAGS, /**< The RFLAGS register */
	/*CPU_REG_NATURAL_LAST*/
	CPU_REG_EFER, /**< The IA32_EFER MSR */
	CPU_REG_PDPTE0, /**< The first page-directory-pointer table entry (PDPTE) */
	CPU_REG_PDPTE1, /**< The second page-directory-pointer table entry (PDPTE) */
	CPU_REG_PDPTE2, /**< The third page-directory-pointer table entry (PDPTE) */
	CPU_REG_PDPTE3, /**< The fourth page-directory-pointer table entry (PDPTE) */
	/*CPU_REG_64BIT_LAST,*/
	CPU_REG_ES, /**< The ES segment selector */
	CPU_REG_CS, /**< The CS segment selector */
	CPU_REG_SS, /**< The SS segment selector */
	CPU_REG_DS, /**< The DS segment selector */
	CPU_REG_FS, /**< The FS segment selector */
	CPU_REG_GS, /**< The GS segment selector */
	CPU_REG_LDTR, /**< The local descriptor table register (LDTR) */
	CPU_REG_TR, /**< The task register */
	CPU_REG_IDTR, /**< The interrupt descriptor table register (IDTR) */
	CPU_REG_GDTR /**< The global descriptor table register (GDTR) */
	/*CPU_REG_LAST*/
};

/**********************************/
/* EXTERNAL VARIABLES             */
/**********************************/

/**
 * @brief Host linear address of init_secondary_pcpu()
 *
 * The trampoline code jumps to init_secondary_pcpu() after setting up the C execution environment.
 * This variable holds the host linear address of init_secondary_pcpu(), and shall be initialized properly
 * in the relocated trampoline section.
 */
extern uint64_t               main_entry[1];
/**
 * @brief Host linear address of the stack used by the trampoline
 *
 * This variable holds the host linear address of the stack that the trampoline code shall use after switching to
 * 64-bit mode and shall be initialized properly in the relocated trampoline section before the trampoline code is
 * executed.
 */
extern uint64_t               secondary_cpu_stack[1];

/*
 * To support per_cpu access, we use a special struct "per_cpu_region" to hold
 * the pattern of per CPU data. And we allocate memory for per CPU data
 * according to multiple this struct size and pcpu number.
 *
 *   +-------------------+------------------+---+------------------+
 *   | percpu for pcpu0  | percpu for pcpu1 |...| percpu for pcpuX |
 *   +-------------------+------------------+---+------------------+
 *   ^                   ^
 *   |                   |
 *   <per_cpu_region size>
 *
 * To access per cpu data, we use:
 *   per_cpu_base_ptr + sizeof(struct per_cpu_region) * curr_pcpu_id
 *   + offset_of_member_per_cpu_region
 * to locate the per cpu data.
 */

 /**
 * @brief The Bootstrap Processor CPU ID.
 */
#define BSP_CPU_ID             0U

 /**
 * @brief The invalid CPU ID.
 *
 * This macro is error code for error handling, this means that caller can't find a valid physical CPU or virtual CPU.
 */
#define INVALID_CPU_ID 0xffffU
 /**
 * @brief The broadcast CPU ID.
 *
 *This macro is used to notify all valid physical CPU or virtual CPU.
 */
#define BROADCAST_CPU_ID 0xfffeU

/**
 * @brief This data structure represents the content in system table pointer registers for the logical processor.
 *
 * The system table pointer registers are GDTR, LDTR, IDTR, task register.
 *
 * @remark This data structure is packed.
 */
struct descriptor_table {
	uint16_t limit; /**< A value is added to the base address to get the address of the last valid byte. */
	uint64_t base; /**< The start address of the system table or segment. */
} __packed;

/**
 * @brief Physical CPU's boot states.
 */
enum pcpu_boot_state {
	PCPU_STATE_RESET = 0U, /**< The physical CPU is reset. */
	PCPU_STATE_INITIALIZING, /**< The physical CPU is initializing. */
	PCPU_STATE_RUNNING, /**< The physical CPU is running. */
	PCPU_STATE_HALTED, /**< The physical CPU is halted. */
	PCPU_STATE_DEAD,/**< The physical CPU is dead. */
};

/**
 * @brief The message flag of CPU representing the CPU will do offline operation.
 */
#define	NEED_OFFLINE		(1U)
/**
 * @brief The message flag of CPU representing the VM will shut down, where the VM holds the CPU.
 */
#define	NEED_SHUTDOWN_VM	(2U)
/**
 * @brief The message flag of CPU representing the CPU is pending to trigger platform S3.
 * 
 * This flag should only be set on BSP.
 */
#define NEED_ENTER_PLATFORM_S3  (3U)

void make_pcpu_offline(uint16_t pcpu_id);
bool need_offline(uint16_t pcpu_id);

/**
 * @brief This data structure represents a segment selector in x86 architecture.
 */
struct segment_sel {
	uint16_t selector; /**< The segment selector value. */
	uint64_t base; /**< The base address of the segment. */
	uint32_t limit; /**< The limit of the segment. */
	uint32_t attr; /**< The attributes of the segment. */
};

/**
 * @brief The union represents the general purpose registers in x86 architecture.
 */
union cpu_regs_t {
	struct acrn_gp_regs regs; /**< The structure to store virtual CPU general purpose registers. */
	uint64_t longs[NUM_GPRS]; /**< The array to store virtual CPU general purpose registers. */
};

/**
 * @brief This structure is used to store the registers info for virtual CPU running context.
 */
struct run_context {
	/* Contains the guest register set.
	* NOTE: This must be the first element in the structure, so that the offsets
	* in vmx_asm.S match
	*/
	/**
	* @brief Guest general purpose registers. Must be the first to keep the offsets same as vmx_asm.S
	*/
	union cpu_regs_t cpu_regs;
	uint64_t cr0;      /**< Guest CR register 0 */

	/* CPU_CONTEXT_OFFSET_CR2 =
	 * offsetof(struct run_context, cr2) = 136
	 */
	uint64_t cr2;      /**< Guest CR register 2 */
	uint64_t cr4;      /**< Guest CR register 4 */

	uint64_t rip;      /**< Guest RIP */
	uint64_t rflags;   /**< Guest RFLAGS */

	/* CPU_CONTEXT_OFFSET_IA32_SPEC_CTRL =
	 * offsetof(struct run_context, ia32_spec_ctrl) = 168
	 */
	uint64_t ia32_spec_ctrl;   /**< Guest IA32_SPEC_CTRL MSR */
	uint64_t ia32_efer;        /**< Guest EFER */
};

/**
 * @brief The union represents an XSAVE area that stores attributes indicating the valid contents and layout of an
 * XSAVE area.
 */
union xsave_header {
	/**
	 * @brief An array representing all the raw contents in an XSS header.
	 */
	uint64_t value[XSAVE_HEADER_AREA_SIZE / sizeof(uint64_t)];
	/**
	 * @brief A structure that helps decode specific fields in an XSS header.
	 */
	struct {
		/**
		 * @brief The XSTATE_BV field in the XSAVE header which identifies the state components that are stored
		 * in an XSAVE area.
		 */
		uint64_t xstate_bv;
		/**
		 * @brief The XCOMP_BV field in the XSAVE header which specifies whether the state components are
		 * stored in standard or compact format.
		 */
		uint64_t xcomp_bv;
	} hdr;
};

/**
 * @brief This structure is used to store the register values of defined state components.
 *
 * @alignment 64
 */
struct xsave_area {
	/**
	 * @brief The legacy region in an XSAVE area.
	 */
	uint64_t legacy_region[XSAVE_LEGACY_AREA_SIZE / sizeof(uint64_t)];
	/**
	 * @brief The XSAVE header.
	 */
	union xsave_header xsave_hdr;
	/**
	 * @brief The extended region of an XSAVE area.
	 */
	uint64_t extend_region[XSAVE_EXTEND_AREA_SIZE / sizeof(uint64_t)];
} __aligned(64);

/**
 * @brief This structure is used to store extended contexts that are not saved/restored during VM exit/entry.
 */
struct ext_context {
	uint64_t cr3; /**< The guest CR3 register */

	/* Segment registers */
	struct segment_sel idtr; /**< The guest Interrupt Descriptor Table Register */
	struct segment_sel ldtr; /**< The guest Local Descriptor Table Register */
	struct segment_sel gdtr; /**< The guest Global Descriptor Table Register */
	struct segment_sel tr;   /**< The guest Task Register */
	struct segment_sel cs;   /**< The guest Code Segment Register */
	struct segment_sel ss;   /**< The guest Stack Segment Register */
	struct segment_sel ds;   /**< The guest Data Segment Register */
	struct segment_sel es;   /**< The guest ES Register */
	struct segment_sel fs;   /**< The guest FS Register */
	struct segment_sel gs;   /**< The guest GS Register */

	uint64_t ia32_star;      /**< The guest IA32_STAR MSR */
	uint64_t ia32_cstar;     /**< The guest IA32_CSTAR MSR */
	uint64_t ia32_lstar;     /**< The guest IA32_LSTAR MSR */
	uint64_t ia32_fmask;     /**< The guest IA32_FMASK MSR */
	uint64_t ia32_kernel_gs_base;  /**< The guest IA32_KERNEL_GS_BASE MSR */

	uint64_t tsc_aux;     /**< The guest IA32_TSC_AUX MSR */
	/**
	 * @brief An XSAVE area saving contents in the guest state components that are enabled by the vCPU.
	 */
	struct xsave_area xs_area;
	uint64_t xcr0; /**< The guest XCR0 register */
	enum vcpu_reset_mode reset_mode; /**< The guest reset mode */
};

/**
 * @brief This structure is used to store the running and extended contexts of a virtual CPU.
 */
struct cpu_context {
	struct run_context run_ctx;  /**< Structure to record the running context of the virtual CPU */
	struct ext_context ext_ctx;  /**< Structure to record the extended context of the virtual CPU */
};

/* Function prototypes */
void cpu_do_idle(void);
void cpu_dead(void);
void init_pcpu_pre(bool is_bsp);
/* The function should be called on the same CPU core as specified by pcpu_id,
 * hereby, pcpu_id is actually the current physical cpu id.
 */
void init_pcpu_post(uint16_t pcpu_id);
bool start_pcpus(uint64_t mask);
int32_t wait_pcpus_offline(uint64_t mask);
void stop_pcpus(void);
void wait_sync_change(volatile const uint64_t *sync, uint64_t wake_sync);

/**
 * @brief Read the specified segment.
 *
 * @param[in] seg The segment to read.
 * @param[inout] result_ptr The address to save the content read from the segment.
 *
 * @return None
 *
 * @pre result_ptr != NULL
 * @post N/A
 */
#define CPU_SEG_READ(seg, result_ptr)						\
{										\
	asm volatile ("mov %%" STRINGIFY(seg) ", %0": "=r" (*(result_ptr)));	\
}

/**
 * @brief Read the specified control register.
 *
 * @param[in] cr The control register to read.
 * @param[inout] result_ptr The address to save the content read from the control register.
 *
 * @pre result_ptr != NULL
 * @post N/A
 */
#define CPU_CR_READ(cr, result_ptr)				\
{								\
	asm volatile ("mov %%" STRINGIFY(cr) ", %0"		\
			: "=r"(*(result_ptr)));			\
}

/**
 * @brief Write the specified control register.
 *
 * Use mov instruction to write the specified value into the specified control register.
 *
 * @param[in] cr The control register to write.
 * @param[in] value The value that needs to be written into the control register.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
#define CPU_CR_WRITE(cr, value)					\
{								\
	asm volatile ("mov %0, %%" STRINGIFY(cr)		\
			: /* No output */			\
			: "r"(value));				\
}

/**
 * @brief Reads the specified XMM register.
 *
 * Use movdqu instruction to read the content of the specified XMM register into the specified memory location.
 *
 * @param[in] xmm The XMM register to read.
 * @param[inout] result_m128i_p Pointer to the memory location where the content of the specified XMM register will be
 * stored.
 * @return None
 *
 * @pre result_m128i_p != NULL
 * @post N/A
 */
#define CPU_XMM_READ(xmm, result_m128i_p)						\
{										\
	asm volatile ("movdqu %%" STRINGIFY(xmm) ", %0": "=m" (*(result_m128i_p)));	\
}

/**
 * @brief Write the specified XMM register.
 *
 * Use movdqu instruction to write the content of the specified memory location into the specified XMM register.
 *
 * @param[in] xmm The XMM register to write.
 * @param[in] input_m128i_p Pointer to the memory location where the content to write into the specified XMM register.
 * @return None
 *
 * @pre input_m128i_p != NULL
 * @post N/A
 */
#define CPU_XMM_WRITE(xmm, input_m128i_p)					\
{								\
	asm volatile ("movdqu %0, %%" STRINGIFY(xmm)		\
			: /* No output */			\
			: "m"(*(input_m128i_p)));				\
}

/**
 * @brief The inline function to get the base address of the GDT.
 *
 * @return A uint64_t value indicating the base address of the GDT.
 *
 * @pre N/A
 * @post N/A
 */
static inline uint64_t sgdt(void)
{
	struct descriptor_table gdtb = {0U, 0UL};
	asm volatile ("sgdt %0":"=m"(gdtb)::"memory");
	return gdtb.base;
}

/**
 * @brief The inline function to get the base address of the IDT.
 *
 * @return A uint64_t value indicating the base address of the IDT.
 *
 * @pre N/A
 * @post N/A
 */
static inline uint64_t sidt(void)
{
	struct descriptor_table idtb = {0U, 0UL};
	asm volatile ("sidt %0":"=m"(idtb)::"memory");
	return idtb.base;
}

/**
 * @brief The inline function to read a Model-Specific Register (MSR).
 *
 * @param[in] reg The identifier of MSR register to read.
 *
 * @return A uint64_t value of the specified MSR register.
 */
static inline uint64_t cpu_msr_read(uint32_t reg)
{
	uint32_t  msrl, msrh;

	asm volatile (" rdmsr ":"=a"(msrl), "=d"(msrh) : "c" (reg));
	return (((uint64_t)msrh << 32U) | msrl);
}

/**
 * @brief The inline function to write a Model-Specific Register (MSR).
 *
 * @param[in] reg The identifier of MSR register to write.
 * @param[in] msr_val The uint64_t value to write.
 *
 * @return N/A
 */
static inline void cpu_msr_write(uint32_t reg, uint64_t msr_val)
{
	asm volatile (" wrmsr " : : "c" (reg), "a" ((uint32_t)msr_val), "d" ((uint32_t)(msr_val >> 32U)));
}

/**
 * @brief The inline function to execute the PAUSE instruction on the current logical processor.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
static inline void asm_pause(void)
{
	asm volatile ("pause" ::: "memory");
}

/**
 * @brief The inline function to execute the HLT (halt) instruction on the current logical processor.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
static inline void asm_hlt(void)
{
	asm volatile ("hlt");
}

/* interrupts remain inhibited on the instruction boundary following
 * an execution of STI. This will make sure pending interrupts will
 * wake hlt.
 */
/**
 * @brief The inline function to halt the current logical processor safely.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
static inline void asm_safe_hlt(void)
{
	asm volatile ("sti; hlt; cli" : : : "cc");
}

/**
 * @brief Disables interrupts on the current logical processor.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
#define CPU_IRQ_DISABLE()                         \
{                                                           \
	asm volatile ("cli\n" : : : "cc");                  \
}

/**
 * @brief Enables interrupts on the current logical processor.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
#define CPU_IRQ_ENABLE()                          \
{                                                           \
	asm volatile ("sti\n" : : : "cc");                  \
}

/**
 * @brief The inline function to synchronize all write accesses to memory.
 *
 * This function executes SFENCE instruction, which is a memory barrier that ensures the ordering of store operations.
 * It prevents the CPU from reordering store instructions across the barrier, ensuring that all previous stores are
 * completed before any subsequent stores are executed.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
static inline void cpu_write_memory_barrier(void)
{
	asm volatile ("sfence\n" : : : "memory");
}


/**
 * @brief The inline function to synchronize all read and write accesses to/from memory.
 *
 * This function executes MFENCE instruction to create a full memory barrier, ensuring that all load and store
 * operations issued before the barrier are completed before any load and store operations issued after the barrier.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
static inline void cpu_memory_barrier(void)
{
	asm volatile ("mfence\n" : : : "memory");
}

/**
 * @brief The inline function to prevent compilers from reordering read/write access across this barrier.
 *
 * This function acts as a compiler barrier, ensuring that the compiler does not reorder instructions across this
 * barrier. It does not generate any actual CPU instructions but serves as a hint to the compiler to enforce ordering.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
static inline void cpu_compiler_barrier(void)
{
	asm volatile ("" : : : "memory");
}

/**
 * @brief The inline function to invalidate the TLB entry for a given address.
 *
 * This function executes INVLPG instruction to invalidate the Translation Lookaside Buffer (TLB) entry associated with
 * the specified address. This is typically used to ensure that changes to page tables are recognized by the CPU.
 *
 * @param[in] addr The address for which the TLB entry should be invalidated.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
static inline void invlpg(unsigned long addr)
{
	asm volatile("invlpg (%0)" ::"r" (addr) : "memory");
}

/**
 * @brief The inline function to write back and invalidate Cache.
 *
 * This function executes WBINVD instruction, which writes back all modified cache lines in the processor's internal
 * cache to main memory and invalidates the internal caches. This ensures that all data in the cache is written to
 * memory and the cache is cleared.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
static inline void wbinvd(void)
{
	asm volatile ("   wbinvd\n" : : : "memory");
}

/**
 * @brief The inline function to flush a cache line that contains the specified address.
 *
 * This function executes CLFLUSH instruction to flush a cache line that contains the specified address. This ensures
 * that any modifications to the memory at that address are written back to main memory and invalidated in the cache.
 *
 * @param[in] p Pointer to the memory address to be flushed.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
static inline void clflush(const volatile void *p)
{
	asm volatile ("clflush (%0)" :: "r"(p));
}

/**
 * @brief The inline function to perform a cache line flush optimized operation.
 *
 * This function executes CLFLUSHOPT instruction to flush a cache line that contains the specified memory address.
 * The CLFLUSHOPT instruction is an optimized version of the CLFLUSH instruction, which may provide better performance
 * on some processors.
 *
 * @param[in] p Pointer to the memory address to be flushed.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
static inline void clflushopt(const volatile void *p)
{
	asm volatile ("clflushopt (%0)" :: "r"(p));
}


/**
 * @brief The inline function to load the task register.
 *
 * Use LTR instruction to load the specified segment selector into the segment selector field of the task register.
 *
 * @param[in] ltr_ptr The segment selector that points to a task state segment (TSS) to load.
 *
 * @return None
 *
 * @pre ltr_ptr >= 8
 * @post N/A
 */
#define CPU_LTR_EXECUTE(ltr_ptr)                            \
{                                                           \
	asm volatile ("ltr %%ax\n" : : "a"(ltr_ptr));       \
}

/**
 * @brief Macro to save RFLAGS register the specified memory address.
 *
 * Use pushf instruction to save the content of the RFLAGS register to the stack, and then pop the value from the stack
 * to the specified address.
 *
 * @param[inout] rflags_ptr The address to save the content read from the RFLAGS register.
 *
 * @return None
 *
 * @pre rflags_ptr != NULL
 * @post N/A
 */
#define CPU_RFLAGS_SAVE(rflags_ptr)                     \
{                                                       \
	asm volatile (" pushf");                        \
	asm volatile (" pop %0"                         \
			: "=r" (*(rflags_ptr))          \
			: /* No inputs */);             \
}

/**
 * @brief Macro to restore RFLAGS register with the specified value.
 *
 * Use push instruction to push the specified value to the stack, and then pop the value from the stack to the RFLAGS
 * register.
 *
 * @param[in] rflags The value to write to the RFLAGS register.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
#define CPU_RFLAGS_RESTORE(rflags)                      \
{                                                       \
	asm volatile (" push %0\n\t"			\
			"popf	\n\t": : "r" (rflags)	\
			:"cc");				\
}

/**
 * @brief Save the RFLAGS register to the specified address and disable interrupts.
 *
 * @param[inout] p_rflags The address to save the RFLAGS register.
 *
 * @return None
 *
 * @pre p_rflags != NULL
 * @post N/A
 */
#define CPU_INT_ALL_DISABLE(p_rflags)               \
{                                                   \
	CPU_RFLAGS_SAVE(p_rflags);	             \
	CPU_IRQ_DISABLE();                    \
}

/**
 * @brief Restore the architecture status register used to lockout interrupts to the specified value.
 *
 * @param[in] rflags The value that the architectures status register used to lockout interrupts will be restored to.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
#define CPU_INT_ALL_RESTORE(rflags)                 \
{                                                   \
	CPU_RFLAGS_RESTORE(rflags);                 \
}

/**
 * @brief Define a pseudo MSR to cache the physical CPU ID.
 *
 * This pseudo MSR uses an unused MSR (MSR_IA32_SYSENTER_CS) for the purpose to cache the physical CPU ID.
 *
 */
#define ACRN_PSEUDO_PCPUID_MSR		MSR_IA32_SYSENTER_CS

/**
 * @brief Retrieve the physical CPU ID of the current logical processor.
 *
 * This function reads the value of the ACRN_PSEUDO_PCPUID_MSR register and returns it as a 16-bit unsigned integer.
 *
 * @return The current physical CPU ID.
 *
 * @pre N/A
 * @post N/A
 * @remark This API can be called only after init_pcpu_pre() has been called once on the current logical processor to
 * set up the pseudo MSR.
 */
static inline uint16_t get_pcpu_id(void)
{
	return (uint16_t)cpu_msr_read(ACRN_PSEUDO_PCPUID_MSR);
}

/**
 * @brief Read the value of a Model-Specific Register (MSR).
 *
 * This function reads the value of the MSR specified by the given register number.
 *
 * @param[in] reg_num The register number of the MSR to read.
 *
 * @return The value of the specified MSR.
 *
 * @pre N/A
 * @post N/A
 */
static inline uint64_t msr_read(uint32_t reg_num)
{
	return cpu_msr_read(reg_num);
}

/**
 * @brief Write a value to a Model-Specific Register (MSR).
 *
 * This function writes a 64-bit value to the specified MSR register.
 *
 * @param[in] reg_num The MSR register number to write to.
 * @param[in] value64 The 64-bit value to write to the MSR register.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
static inline void msr_write(uint32_t reg_num, uint64_t value64)
{
	cpu_msr_write(reg_num, value64);
}

/**
 * @brief Write a value to a Model-Specific Register (MSR) safely.
 *
 * This function attempts to write a 64-bit value to the specified MSR register. It first checks if the value to be
 * written does not have any bits set that are also set in the reserved mask. If the check passes, the value is written
 * to the MSR. Otherwise, an error code is returned.
 *
 * @param[in] reg_num The MSR register number to write to.
 * @param[in] value64 The 64-bit value to write to the MSR.
 * @param[in] rsvd_mask A 64-bit mask of reserved bits that must not be set.
 *
 * @return 0 on success, -EACCES if the value64 contains reserved bits.
 */
static inline int32_t msr_write_safe(uint32_t reg_num, uint64_t value64, uint64_t rsvd_mask)
{
	int32_t err = 0;

	if ((value64 & rsvd_mask) == 0) {
		msr_write(reg_num, value64);
	} else {
		err = -EACCES;
	}
	return err;
}

/**
 * @brief Structure to hold MSR (Model-Specific Register) data for SMP cross-call.
 *
 * This structure is used to store the index and values associated with a specific MSR. It contains fields for the MSR
 * index, the value read from the MSR, and the value to be written to the MSR.
 */
struct msr_data_struct {
	uint32_t msr_index; /**<  Index of the MSR. */
	uint64_t read_val; /**<  Value read from the MSR. */
	uint64_t write_val; /**<  Value to be written to the MSR. */
};

void msr_write_pcpu(uint32_t msr_index, uint64_t value64, uint16_t pcpu_id);
uint64_t msr_read_pcpu(uint32_t msr_index, uint16_t pcpu_id);

/**
 * @brief Write to an extended control register (XCR).
 *
 * This function uses the XGETBV instruction to write a 64-bit value to the specified extended control register (XCR).
 *
 * @param[in] reg The XCR register to write to.
 * @param[in] val The 64-bit value to write to the XCR register.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
static inline void write_xcr(int32_t reg, uint64_t val)
{
	asm volatile("xsetbv" : : "c" (reg), "a" ((uint32_t)val), "d" ((uint32_t)(val >> 32U)));
}

/**
 * @brief Reads the value of the specified extended control register (XCR).
 *
 * This function uses the XGETBV instruction to read the value of the specified extended control register.
 *
 * @param[in] reg The index of the extended control register to read.
 *
 * @return The 64-bit value of the specified extended control register.
 *
 * @pre N/A
 * @post N/A
 */
static inline uint64_t read_xcr(int32_t reg)
{
	uint32_t  xcrl, xcrh;

	asm volatile ("xgetbv ": "=a"(xcrl), "=d"(xcrh) : "c" (reg));
	return (((uint64_t)xcrh << 32U) | xcrl);
}

/**
 * @brief Save processor extended states to memory using the XSAVES instruction.
 *
 * This function uses the XSAVES instruction to save the specified state components to the specified memory.
 * The XSAVES instruction is part of the Intel architecture and is used to save the state of various processor
 * components, including the x87 FPU, SSE, AVX, and other extended states.
 *
 * @param[inout] region_addr Pointer to the memory region where the processor state will be saved to.
 * @param[in] mask A 64-bit mask specifying which state components to save.
 *
 * @return None
 *
 * @pre region_addr != NULL
 * @post N/A
 */
static inline void xsaves(struct xsave_area *region_addr, uint64_t mask)
{
	asm volatile("xsaves %0"
			: : "m" (*(region_addr)),
			"d" ((uint32_t)(mask >> 32U)),
			"a" ((uint32_t)mask):
			"memory");
}

/**
 * @brief Restore processor extended states using the XSAVES area.
 *
 * This function uses the XRSTORS instruction to restore the processor's extended states from the specified XSAVES
 * area. The XRSTORS instruction is part of the Intel architecture and is used to restore the state of various
 * processor components, such as the FPU, SSE, AVX, and other extended states.
 *
 * @param[in] region_addr Pointer to the XSAVES area from which the states will be restored.
 * @param[in] mask A 64-bit mask specifying which components to restore.
 *
 * @return None
 *
 * @pre region_addr != NULL
 * @post N/A
 */
static inline void xrstors(const struct xsave_area *region_addr, uint64_t mask)
{
	asm volatile("xrstors %0"
			: : "m" (*(region_addr)),
			"d" ((uint32_t)(mask >> 32U)),
			"a" ((uint32_t)mask):
			"memory");
}

/**
 * @brief Load the IWKey using a specific assembly instruction.
 *
 * This function uses an inline assembly block to execute a specific instruction sequence that loads the IWKey. The
 * instruction sequence is represented by the byte codes 0xf3, 0x0f, 0x38, 0xdc, 0xd1.
 *
 * @param[in] eax The 32-bit value to be loaded into the EAX register before executing the instruction sequence.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
static inline void asm_loadiwkey(uint32_t eax)
{
	asm volatile(".byte 0xf3, 0x0f, 0x38, 0xdc, 0xd1;": : "a" (eax));
}

/**
 * @brief Reads the values of the XMM0, XMM1, and XMM2 registers.
 *
 * This function uses the CPU_XMM_READ macro to read the values of the XMM0, XMM1, and XMM2 registers and stores them
 * in the memory locations pointed to by xmm0_addr, xmm1_addr, and xmm2_addr respectively.
 *
 * @param[inout] xmm0_addr Pointer to the memory location where the value of the XMM0 register will be stored to.
 * @param[inout] xmm1_addr Pointer to the memory location where the value of the XMM1 register will be stored to.
 * @param[inout] xmm2_addr Pointer to the memory location where the value of the XMM2 register will be stored to.
 *
 * @return None
 *
 * @pre xmm0_addr != NULL
 * @pre xmm1_addr != NULL
 * @pre xmm2_addr != NULL
 * @post N/A
 */
static inline void read_xmm_0_2(uint64_t *xmm0_addr, uint64_t *xmm1_addr, uint64_t *xmm2_addr)
{
	CPU_XMM_READ(xmm0, xmm0_addr);
	CPU_XMM_READ(xmm1, xmm1_addr);
	CPU_XMM_READ(xmm2, xmm2_addr);
}

/**
 * @brief Writes the values of XMM registers 0, 1, and 2 to the specified memory addresses.
 *
 * This function uses the CPU_XMM_WRITE macro to write the contents of the XMM0, XMM1, and XMM2 registers to the memory
 * locations pointed to by xmm0_addr, xmm1_addr, and xmm2_addr respectively.
 *
 * @param[in] xmm0_addr Pointer to the memory location where the value of the XMM0 register will be written.
 * @param[in] xmm1_addr Pointer to the memory location where the value of the XMM1 register will be written.
 * @param[in] xmm2_addr Pointer to the memory location where the value of the XMM2 register will be written.
 *
 * @return None
 *
 * @pre xmm0_addr != NULL
 * @pre xmm1_addr != NULL
 * @pre xmm2_addr != NULL
 * @post N/A
 */
static inline void write_xmm_0_2(uint64_t *xmm0_addr, uint64_t *xmm1_addr, uint64_t *xmm2_addr)
{
	CPU_XMM_WRITE(xmm0, xmm0_addr);
	CPU_XMM_WRITE(xmm1, xmm1_addr);
	CPU_XMM_WRITE(xmm2, xmm2_addr);
}

/*
 * stac/clac pair is used to access guest's memory protected by SMAP, following below flow:
 *
 *	stac();
 *	#access guest's memory.
 *	clac();
 *
 * Notes:Avoid inserting another stac/clac pair between stac and clac, As once clac after multiple stac will invalidate
 * SMAP protection and hence Page Fault crash. Logging message to memory buffer will induce this case, please disable
 * SMAP temporlly or don't log messages to shared memory buffer, if it is evitable for you for debug purpose.
 */
/**
 * @brief Set AC (Alignment Check / Access Control) flag in EFLAGS register.
 *
 * The function uses the STAC assembly instruction to set the AC flag in the EFLAGS register.
 *
 * Refer to Chapter 3.3, Vol.2, SDM 325426-078 for more detail about STAC instruction.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
static inline void stac(void)
{
	asm volatile ("stac" : : : "memory");
}

/**
 * @brief Clear AC (Alignment Check / Access Control) flag in EFLAGS register.
 *
 * This function uses the CLAC assembly instruction to clear the AC flag in the EFLAGS register.
 *
 * Refer to Chapter 3.3, Vol.2, SDM 325426-078 for more detail about CLAC instruction.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
static inline void clac(void)
{
	asm volatile ("clac" : : : "memory");
}

enum pcpu_boot_state pcpu_get_current_state(uint16_t pcpu_id);
uint16_t get_pcpu_nums(void);
bool is_pcpu_active(uint16_t pcpu_id);
uint64_t get_active_pcpu_bitmap(void);
uint32_t get_max_lapic_id(void);
#else /* ASSEMBLER defined */

#endif /* ASSEMBLER defined */

/**
 * @}
 */
#endif /* CPU_H */
