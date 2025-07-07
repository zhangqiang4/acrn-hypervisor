/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @addtogroup hwmgmt_memory
 *
 * @{
 */

/**
 * @file
 *
 * @brief Memory Management Unit (MMU) and Paging Management Header
 *
 * This header file contains the declarations of functions, macros, and data structures related to the Memory
 * Management Unit (MMU) and paging management in the ACRN hypervisor. It provides the necessary interfaces for
 * enabling paging, setting up page tables, flushing TLBs, and managing cache lines.
 */

#ifndef MMU_H
#define MMU_H

/**
 * @brief The flag that indicates that the page fault was caused by a non present page.
 */
#define PAGE_FAULT_P_FLAG	0x00000001U

/**
 * @brief The flag that indicates that the page fault was caused by a write access.
 */
#define PAGE_FAULT_WR_FLAG	0x00000002U

/**
 * @brief The flag that indicates that the page fault was caused in user mode.
 */
#define PAGE_FAULT_US_FLAG	0x00000004U

/**
 * @brief The flag that indicates that the page fault was caused by a reserved bit violation.
 */
#define PAGE_FAULT_RSVD_FLAG	0x00000008U

/**
 * @brief The flag that indicates that the page fault was caused by an instruction fetch.
 */
#define PAGE_FAULT_ID_FLAG	0x00000010U

/* Defines used for common memory sizes */
/**
 * @brief Common memory size: 1KB
 */
#define MEM_1K		1024U
/**
 * @brief Common memory size: 2KB
 */
#define MEM_2K		(MEM_1K * 2U)
/**
 * @brief Common memory size: 4KB
 */
#define MEM_4K		(MEM_1K * 4U)
/**
 * @brief Common memory size: 1MB
 */
#define MEM_1M		(MEM_1K * 1024U)
/**
 * @brief Common memory size: 2MB
 */
#define MEM_2M		(MEM_1M * 2U)
/**
 * @brief Common memory size: 1GB
 */
#define MEM_1G		(MEM_1M * 1024U)
/**
 * @brief Common memory size: 2GB
 */
#define MEM_2G		(MEM_1G * 2UL)
/**
 * @brief Common memory size: 4GB
 */
#define MEM_4G		(MEM_1G * 4UL)

#ifndef ASSEMBLER

#include <asm/page.h>
#include <asm/pgtable.h>

/**
 * @brief Define cache line size (in bytes)
 */
#define CACHE_LINE_SIZE		64U

/* IA32E Paging constants */
/**
 * @brief Physical address mask for IA-32e paging.
 */
#define IA32E_REF_MASK	((get_pcpu_info())->physical_address_mask)
/**
 * @brief INVEPT type: Single-context invalidation.
 */
#define INVEPT_TYPE_SINGLE_CONTEXT      1UL
/**
 * @brief INVEPT type: All-contexts invalidation.
 */
#define INVEPT_TYPE_ALL_CONTEXTS        2UL
/**
 * @brief Handle the result of VMX instructions.
 *
 * This assembly object-like macro is used to handle the result of certain VMX instructions.
 * The macro is designed to check the condition flags (CF and ZF) after the execution
 * of these instructions to determine if they failed and why.
 *
 * The macro interprets the Carry Flag (CF) and Zero Flag (ZF) as follows:
 * - If CF=0, ZF=0: VMsucceed - instruction succeeded (sets error=0)
 * - If CF=1:       VMfailInvalid - instruction failed due to invalid VMCS pointer or other
 *                  precondition (sets error=1)
 * - If CF=0, ZF=1: VMfailValid - instruction failed with valid error number stored in the
 *                  VM-instruction error field (sets error=2)
 *
 * Refer to Chapter 31.2 and 31.4, Vol.3, SDM 325462-078 for details about VMX instruction failure.
 */
#define VMFAIL_VMX_INSTRUCTIONS				\
	"       jnc 1f\n"				\
	"       mov $1, %0\n"    /* CF: error = 1 */	\
	"       jmp 3f\n"				\
	"1:     jnz 2f\n"				\
	"       mov $2, %0\n"    /* ZF: error = 2 */	\
	"       jmp 3f\n"				\
	"2:     mov $0, %0\n"				\
	"3:"

/**
 * @brief INVVPID descriptor
 *
 * Refer to Chapter 31.3 INVVPID - Invalidate Translations Based on VPID, Volume 3C, SDM 325462-078,
 * the INVVPID descriptor comprises 128 bits and contains a 64-bit linear address value in bits[127:64].
 * Bits[15:0] are the VPID value, and bits[63:16] are reserved.
 */
struct invvpid_operand {
	uint32_t vpid : 16; /**< VPID */
	uint32_t rsvd1 : 16; /**< Reserved */
	uint32_t rsvd2 : 32; /**< Reserved */
	uint64_t gva; /**< Linear address */
};

/**
 * @brief Execute INVVPID instruction to invalidate cached TLB entries.
 *
 * This function invokes INVVPID instruction to invalidate TLB entries for the specified VPID.
 * Refer to Chapter 31.3 INVVPID - Invalidate Translations Based on VPID, Volume 3C, SDM 325462-078 for more
 * information about this instruction.
 *
 * @param[in] operand The INVVPID descriptor containing the VPID and linear address
 * @param[in] type The INVVPID invalidation type
 *
 * @return error number
 *
 * @retval 0 Success
 * @retval 1 VMfailInvalid
 * @retval 2 VMfailValid
 *
 * @pre N/A
 * @post N/A
 */
static inline int32_t asm_invvpid(const struct invvpid_operand operand, uint64_t type)
{
	int32_t error;
	asm volatile ("invvpid %1, %2\n"
			VMFAIL_VMX_INSTRUCTIONS
			: "=r" (error)
			: "m" (operand), "r" (type)
			: "memory");
	return error;
}

/**
 * @brief INVEPT descriptor structure
 *
 * Refer to Chapter 31.3 INVEPT Invalidate Translations Derived from EPT, Volume 3C, SDM 325462-078,
 * the INVEPT descriptor comprises 128 bits and contains a 64-bit EPTP value in bits 63:0. Other bits
 * are reserved.
 */
struct invept_desc {
	uint64_t eptp; /**< EPT Pointer */
	uint64_t res; /**< Reserved */
};

/**
 * @brief Execute INVEPT instruction to invalidate cached EPT mappings.
 *
 * This function invokes INVEPT instruction to invalidate mappings in the TLBs.
 * Refer to Chapter 31.3 INVEPT Invalidate Translations Derived from EPT, Volume 3C, SDM 325462-078
 * for more details about this instruction.
 *
 * @param[in] type The INVEPT invalidation type (single context or all contexts)
 * @param[in] desc The INVEPT descriptor containing the EPTP value
 *
 * @return error number
 *
 * @retval 0 Success
 * @retval 1 VMfailInvalid
 * @retval 2 VMfailValid
 *
 * @pre N/A
 * @post N/A
 */
static inline int32_t asm_invept(uint64_t type, struct invept_desc desc)
{
	int32_t error;
	asm volatile ("invept %1, %2\n"
			VMFAIL_VMX_INSTRUCTIONS
			: "=r" (error)
			: "m" (desc), "r" (type)
			: "memory");
	return error;
}

struct acrn_vcpu;

/**
 * @brief Rounds up to the page boundary.
 *
 * This function rounds up an address to the nearest page boundary.
 *
 * @param[in] addr The address to round up.
 *
 * @return A uint64_t value which is aligned to the page boundary.
 *
 * @pre N/A
 * @post N/A
 *
 */
static inline uint64_t round_page_up(uint64_t addr)
{
	return (((addr + (uint64_t)PAGE_SIZE) - 1UL) & PAGE_MASK);
}

/**
 * @brief Rounds down to the page boundary.
 *
 * This function rounds down an address to the nearest page boundary.
 *
 * @param[in] addr The address to round down.
 *
 * @return A uint64_t value which is aligned to the page boundary.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint64_t round_page_down(uint64_t addr)
{
	return (addr & PAGE_MASK);
}

/**
 * @brief Rounds up to the PDE boundary.
 *
 * This function rounds up an address to the nearest Page Directory Entry (PDE) boundary.
 *
 * @param[in] val The address to round up.
 *
 * @return A uint64_t value which is aligned to the PDE boundary.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint64_t round_pde_up(uint64_t val)
{
	return (((val + (uint64_t)PDE_SIZE) - 1UL) & PDE_MASK);
}

/**
 * @brief Rounds down to the PDE boundary.
 *
 * This function rounds down an address to the nearest Page Directory Entry (PDE) boundary.
 *
 * @param[in] val The address to round down.
 *
 * @return A uint64_t value which is aligned to the PDE boundary.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint64_t round_pde_down(uint64_t val)
{
	return (val & PDE_MASK);
}

/**
 * @brief Page size: 4KB
 */
#define PAGE_SIZE_4K	MEM_4K
/**
 * @brief Page size: 2MB
 */
#define PAGE_SIZE_2M	MEM_2M
/**
 * @brief Page size: 1GB
 */
#define PAGE_SIZE_1G	MEM_1G

void enable_paging(void);

void enable_smep(void);

void enable_smap(void);

void init_paging(void);

void set_paging_supervisor(uint64_t base, uint64_t size);

void flush_vpid_single(uint16_t vpid);

void flush_vpid_global(void);

void invept(const void *eptp);

/**
 * @brief Get the PAE PDPT address from the CR3 value.
 *
 * @param[in] cr3 The CR3 value containing the PAE PDPT address.
 *
 * @return The PAE PDPT address extracted from the CR3 value.
 *
 * @pre N/A
 * @post N/A
 */
static inline uint64_t get_pae_pdpt_addr(uint64_t cr3)
{
	return (cr3 & 0xFFFFFFE0UL);
}

void flush_invalidate_all_cache(void);
void flush_cache_range(const volatile void *p, uint64_t size);
void allocate_ppt_pages(void);
#ifdef HV_DEBUG
void get_high64_ram(uint64_t *high_min, uint64_t *high_max);
#endif

#endif /* ASSEMBLER not defined */

#endif /* MMU_H */

/**
 * @}
 */