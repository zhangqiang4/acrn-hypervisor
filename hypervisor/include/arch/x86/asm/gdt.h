/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef GDT_H
#define GDT_H
/**
 * @addtogroup hwmgmt_cpu
 *
 * @{
 */

/**
 * @file
 * @brief Define structures and macros, declare APIs to hold and maintain segment descriptors.
 */
/* GDT is defined in assembly so it can be used to switch modes before long mode
 * is established.
 * With 64-bit EFI this is not required since are already in long mode when EFI
 * transfers control to the hypervisor.  However, for any instantiation of the
 * ACRN Hypervisor that requires a boot from reset the GDT will be
 * used as mode transitions are being made to ultimately end up in long mode.
 * For this reason we establish the GDT in assembly.
 * This should not affect usage and convenience of interacting with the GDT in C
 * as the complete definition of the GDT is driven by the defines in this file.
 *
 * Unless it proves to be not viable we will use a single GDT for all hypervisor
 * CPUs, with space for per CPU LDT and TSS.
 */

/*
 * Segment selectors in x86-64 and i386 are the same size, 8 bytes.
 * Local Descriptor Table (LDT) selectors are 16 bytes on x86-64 instead of 8
 * bytes.
 * Task State Segment (TSS) selectors are 16 bytes on x86-64 instead of 8 bytes.
 */

/**
 * @brief The size (in bytes) of Segment Descriptors.
 *
 * In long mode, Segment Descriptors are 8 bytes.
 */
#define X64_SEG_DESC_SIZE (0x8U)
/**
 * @brief The size (in bytes) of LDT Descriptors.
 *
 * In long mode, LDT Descriptors are 16 bytes.
 */
#define X64_LDT_DESC_SIZE (0x10U)
/**
 * @brief The size (in bytes) of TSS Descriptors.
 *
 * In long mode, TSS Descriptors are 16 bytes.
 */
#define X64_TSS_DESC_SIZE (0x10U)

/*****************************************************************************
 *
 * BEGIN: Definition of the GDT.
 *
 * NOTE:
 * If you change the size of the GDT or rearrange the location of descriptors
 * within the GDT you must change both the defines and the C structure header.
 *
 *****************************************************************************/
/**
 * @brief Number of global 8-byte segment descriptors.
 *
 * This includes 3 descriptors: Reserved, Code, and Data.
 */
#define    HOST_GDT_RING0_SEG_SELECTORS   (0x3U)

/* Offsets of global 8 byte segment descriptors */

/**
 * @brief Reserved Selector of host.
 */
#define    HOST_GDT_RING0_RSVD_SEL        (0x0000U)

/**
 * @brief Code Segment Selector of host.
 */
#define    HOST_GDT_RING0_CODE_SEL        (0x0008U)

/**
 * @brief Data Segment Selector of host.
 */
#define    HOST_GDT_RING0_DATA_SEL        (0x0010U)
/**
 * @brief Number of global 16 byte TSS descriptor(s)
 */
#define    HOST_GDT_RING0_TSS_SELECTORS   (0x1U)
/* One for each CPU in the hypervisor. */

/*****************************************************************************
 *
 * END: Definition of the GDT.
 *
 *****************************************************************************/

/**
 * @brief Offset to start of LDT Descriptors
 */
#define HOST_GDT_RING0_LDT_SEL		\
	(HOST_GDT_RING0_SEG_SELECTORS * X64_SEG_DESC_SIZE)
/**
 * @brief TSS Selector of host.
 */
#define HOST_GDT_RING0_CPU_TSS_SEL (HOST_GDT_RING0_LDT_SEL)
/**
 * @brief Size of the GDT
 */
#define HOST_GDT_SIZE							\
	(HOST_GDT_RING0_CPU_TSS_SEL +	(HOST_GDT_RING0_TSS_SELECTORS * X64_TSS_DESC_SIZE))

#ifndef ASSEMBLER

#include <types.h>
#include <asm/cpu.h>

/**
 * @brief The value of the type field of the TSS Descriptor. It indicates an inactive task.
 */
#define TSS_AVAIL (9U)

/**
 * @brief This structure represents the format of TSS Descriptors in 64-bit mode.
 *
 * @alignment 8
 */
struct tss_64_descriptor {
	uint32_t low32_value; /**< Bits[31:0] of TSS descriptor in 64 bit mode. */
	uint32_t high32_value; /**< Bits[63:32] of TSS descriptor in 64 bit mode. */
	uint32_t base_addr_63_32; /**< Bits[95:64] of TSS descriptor in 64 bit mode. */
	uint32_t offset_12; /**< Bits[127:96] of TSS descriptor in 64 bit mode. */
} __aligned(8);

/*****************************************************************************
 *
 * BEGIN: Definition of the GDT.
 *
 * NOTE:
 * If you change the size of the GDT or rearrange the location of descriptors
 * within the GDT you must change both the defines and the C structure header.
 *
 *****************************************************************************/
/**
 * @brief This structure represents the Global Descriptor Table (GDT) for the host.
 *
 * @alignment 8
 */
struct host_gdt {
	uint64_t rsvd; /**< Reserved. */
	uint64_t code_segment_descriptor; /**< Code segment descriptor. */
	uint64_t data_segment_descriptor; /**< Data segment descriptor. */
	struct tss_64_descriptor host_gdt_tss_descriptors; /**< TSS descriptor for the host GDT. */
} __aligned(8);

/*****************************************************************************
 *
 * END: Definition of the GDT.
 *
 *****************************************************************************/
/**
 * @brief Task State Segment (TSS) definition.
 *
 * @remark This structure shall be packed.
 * @alignment 16
 */
struct tss_64 {
	uint32_t rsvd1; /**< Reserved. */
	uint64_t rsp0;  /**< RING0's RSP. */
	uint64_t rsp1;  /**< RING1's RSP. */
	uint64_t rsp2;  /**< RING2's RSP. */
	uint32_t rsvd2; /**< Reserved. */
	uint32_t rsvd3; /**< Reserved. */
	uint64_t ist1;  /**< The address of machine check stack. */
	uint64_t ist2;  /**< The address of double fault stack. */
	uint64_t ist3;  /**< The address of segment fault stack. */
	uint64_t ist4;  /**< Exception stack address in TSS. */
	uint64_t ist5;  /**< Exception stack address in TSS, not used currently. */
	uint64_t ist6;  /**< Exception stack address in TSS, not used currently. */
	uint64_t ist7;  /**< Exception stack address in TSS, not used currently. */
	uint32_t rsvd4; /**< Reserved. */
	uint32_t rsvd5; /**< Reserved. */
	uint16_t rsvd6; /**< Reserved. */
	uint16_t io_map_base_addr; /**< I/O Map Base Address. */
} __packed __aligned(16);

/**
 * @brief Definition of the GDT descriptor.
 *
 * Representing the contents to be loaded to GDTR.
 *
 * @remark This structure shall be packed.
 */
struct host_gdt_descriptor {
	uint16_t len; /**< It represents the limit (size of table in bytes) of the global descriptor table (GDT). */
	struct host_gdt *gdt; /**< It represents the base address of the global descriptor table (GDT). */
} __packed;

void load_gdtr_and_tr(void);

#endif /* end #ifndef ASSEMBLER */

#endif /* GDT_H */
/**
 * @}
 */
