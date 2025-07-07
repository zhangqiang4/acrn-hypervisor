/*-
 * Copyright (c) 2011 NetApp, Inc.
 * Copyright (c) 2017-2025 Intel Corporation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <types.h>
#include <asm/lib/atomic.h>
#include <asm/cpufeatures.h>
#include <asm/pgtable.h>
#include <asm/cpu_caps.h>
#include <asm/mmu.h>
#include <asm/vmx.h>
#include <boot.h>
#include <asm/guest/vm.h>
#include <logmsg.h>
#include <misc_cfg.h>

/**
 * @addtogroup hwmgmt_memory
 *
 * @{
 */

/**
 * @file
 * @brief Memory Management Unit (MMU) and Paging Management
 *
 * This file implements the hypervisor's MMU functionality including page table management, Translation Lookaside
 * Buffers (TLB) invalidation, and cache management. It handles the setup of identity mapping for the hypervisor
 * address space, enables CPU memory protection features, and provides utilities for memory translation
 * and cache operations.
 */

 /**
 * @brief Root page table address
 *
 * Static pointer to the PML4 page table. This variable stores the base address of the page table hierarchy
 * used for the hypervisor's memory management.
 */
static void *ppt_mmu_pml4_addr;

/**
 * @brief The sanitized page
 *
 * The sanitized page is used to mitigate l1tf.
 *
 * @alignment PAGE_SIZE
 */
static uint8_t sanitized_page[PAGE_SIZE] __aligned(PAGE_SIZE);

/* PPT VA and PA are identical mapping */
/**
 * @brief Number of PML4 pages needed for the hypervisor page tables.
 *
 * PML4 is the top level of the page table hierarchy. This macro uses \p PML4_PAGE_NUM macro
 * with \p MAX_PHY_ADDRESS_SPACE to calculate the number of PML4 pages. Since each PML4 entry can map
 * 512GB of memory, typically only one PML4 page is needed.
 */
#define PPT_PML4_PAGE_NUM	PML4_PAGE_NUM(MAX_PHY_ADDRESS_SPACE)

/**
 * @brief Number of PDPT pages needed for the hypervisor page tables.
 *
 * This macro uses \p PDPT_PAGE_NUM macro with \p MAX_PHY_ADDRESS_SPACE to calculate the number of PDPT pages.
 */
#define PPT_PDPT_PAGE_NUM	PDPT_PAGE_NUM(MAX_PHY_ADDRESS_SPACE)

/**
 * @brief Number of PT pages needed for the hypervisor page tables.
 *
 * This value is set 0 as we do not support 4K granularity page mapping.
 */
#define PPT_PT_PAGE_NUM	0UL


/**
 * @brief High RAM region's min address
 *
 * The min address of the High RAM region(above 4GB).
 */
static uint64_t high64_min_ram = ~0UL;

/**
 * @brief High RAM region's max address
 *
 * The max address of the High RAM region(above 4GB).
 */
static uint64_t high64_max_ram = MEM_4G;

#ifdef HV_DEBUG
/**
 * @brief Get high RAM region's address range
 *
 * Get high RAM region's(above 4GB) address range.
 *
 * @param[out] high_min High RAM region's min address
 * @param[out] high_max High RAM region's max address
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
void get_high64_ram(uint64_t *high_min, uint64_t *high_max) {
	*high_min = high64_min_ram;
	*high_max = high64_max_ram;
}
#endif
/**
 * @brief Calculate the total number of pages needed for the primary page table.
 *
 * This function calculates the number of pages required for all page table levels:
 * - PML4 page: Typically one page for the entire address space (512GB per entry * 512 entries).
 * - PDPT pages: Based on maximum physical address space (1GB per entry when using 1GB pages).
 * - PD pages: Based on total physical memory size plus additional pages for PCI device mappings
 *   (2MB per entry when using 2MB pages).
 * - PT pages: Set to 0 as the hypervisor uses 2MB or 1GB pages instead of 4KB pages.
 *
 * The total is then rounded up to a multiple of 64 for alignment purposes.
 *
 * @return The total number of pages needed for all page table structures.
 *
 * @pre N/A
 *
 * @post N/A
 */
static uint64_t get_ppt_page_num(void)
{
       uint64_t ppt_pd_page_num = PD_PAGE_NUM(MEM_TOP_ADDRESS) + CONFIG_MAX_PCI_DEV_NUM * 6U;

       /* must be a multiple of 64 */
       return roundup((PPT_PML4_PAGE_NUM + PPT_PDPT_PAGE_NUM + ppt_pd_page_num + PPT_PT_PAGE_NUM), 64U);
}

static struct page_pool ppt_page_pool; /**< Primary page pool */

/**
 * @brief Determine if large page mapping is supported at the specified level.
 *
 * Determines whether large page mapping (2MB or 1GB) is supported at a given page table level:
 * - For PD level (IA32E_PD): Always returns true for 2MB pages
 * - For PDPT level (IA32E_PDPT): Returns true if 1GB pages are supported by CPU
 * - For all other levels: Returns false
 *
 * @param[in] level The page table level to check (IA32E_PML4, IA32E_PDPT, IA32E_PD, or IA32E_PT)
 * @param[in] prot Unused parameter, required for compatibility with callback type
 *
 * @return true if large page is supported at this level, false otherwise
 *
 * @pre N/A
 * @post N/A
 *
 * @remark The PPT and EPT have same page granularity
 */
static inline bool ppt_large_page_support(enum page_table_level level, __unused uint64_t prot)
{
	bool support;

	if (level == IA32E_PD) {
		support = true;
	} else if (level == IA32E_PDPT) {
		support = pcpu_has_cap(X86_FEATURE_PAGE1GB);
	} else {
		support = false;
	}

	return support;
}

/**
 * @brief Empty callback for cache line flushing during page walks.
 *
 * This is a placeholder callback function that does nothing. For PPT, we don't need to flush the cache during page
 * walks because there is no runtime MMU page table change.For other paging structures like EPT or DMAR, cache
 * flushes may be required.
 *
 * @param[in] entry Pointer to the page table entry (unused)
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
static inline void ppt_clflush_pagewalk(const void* entry __attribute__((unused)))
{
}

/**
 * @brief Page table structure for the primary page table (PPT).
 */
static const struct pgtable ppt_pgtable = {
	/**
	 * @brief Default access rights for PPT entries.
	 */
	.default_access_right = (PAGE_PRESENT | PAGE_RW | PAGE_USER),
	/**
	 * @brief Mask used to check if a page is present.
	 */
	.pgentry_present_mask = PAGE_PRESENT,
	/**
	 * @brief Pointer to the page pool used for PPT page table allocations.
	 */
	.pool = &ppt_page_pool,
	/**
	 * @brief Function to determine if large page mapping is supported.
	 */
	.large_page_support = ppt_large_page_support,
	/**
	 * @brief Cache line flush function for page table walks.
	 */
	.clflush_pagewalk = ppt_clflush_pagewalk,
};

/**
 * @brief Execute INVVPID instruction to invalidate TLB entries based on VPID.
 *
 * This function constructs an INVVPID descriptor and calls asm_invvpid() to execute the INVVPID instruction
 * to invalidate TLB entries for a specified VPID.
 *
 * @param[in] type The INVVPID invalidation type (see Intel SDM Vol. 3C, 28.3.3.1)
 * @param[in] vpid The Virtual Processor Identifier to invalidate
 * @param[in] gva The guest linear address for address-specific invalidations
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
static inline void local_invvpid(uint64_t type, uint16_t vpid, uint64_t gva)
{
	const struct invvpid_operand operand = { vpid, 0U, 0U, gva };

	if (asm_invvpid(operand, type) != 0) {
		pr_dbg("%s, failed. type = %lu, vpid = %u", __func__, type, vpid);
	}
}

/**
 * @brief Execute INVEPT instruction to invalidate cached EPT mappings.
 *
 * This function calls asm_invept() to execute the INVEPT instruction to invalidate mappings in the TLBs
 * and paging-structure caches derived from EPT.
 *
 * @param[in] type The INVEPT invalidation type (single context or all contexts)
 * @param[in] desc The INVEPT descriptor containing the EPTP value
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
static inline void local_invept(uint64_t type, struct invept_desc desc)
{
	if (asm_invept(type, desc) != 0) {
		pr_dbg("%s, failed. type = %lu, eptp = 0x%lx", __func__, type, desc.eptp);
	}
}

/**
 * @brief Invalidate TLB entries for a specific VPID.
 *
 * Invalidates all translations for the specified VPID (except VPID 0, which is
 * reserved for the hypervisor and not flushed here). This is used when modifying
 * guest page tables to ensure the guest sees the updated mappings.
 *
 * @param[in] vpid The VPID whose translations should be invalidated.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
void flush_vpid_single(uint16_t vpid)
{
	if (vpid != 0U) {
		local_invvpid(VMX_VPID_TYPE_SINGLE_CONTEXT, vpid, 0UL);
	}
}

/**
 * @brief Invalidates all mappings in the TLBs and paging-structure caches.
 *
 * This function calls local_invvpid() to invalidate all mappings (linear mappings, guest-physical mappings,
 * and combined mappings) in the TLBs and paging-structure caches that are tagged with all VPIDs.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
void flush_vpid_global(void)
{
	local_invvpid(VMX_VPID_TYPE_ALL_CONTEXT, 0U, 0UL);
}

/**
 * @brief Invalidate cached EPT mappings in the TLB and paging-structure caches
 *
 * This function executes the INVEPT instruction to invalidate mappings in the translation lookaside buffers (TLBs)
 * and invalidate cached mappings derived from Extended Page Tables (EPT). It first checks CPU capabilities to
 * determine which INVEPT type is supported and then performs the appropriate invalidation:
 * - If single-context INVEPT is supported, it invalidates mappings for the specified EPTP.
 * - If only global-context INVEPT is supported, it invalidates all mappings.
 * - If neither is supported, it does nothing.
 *
 * @param[in] eptp Pointer to the EPT paging structure root (EPTP)
 *
 * @return None
 *
 * @pre eptp != NULL
 * @post N/A
 *
 * @remark We need to check single-context INVEPT or global-context INVEPT must support, otherwise, the old TLB
 * cannot be invalidated.
 */
void invept(const void *eptp)
{
	struct invept_desc desc = {0};

	if (pcpu_has_vmx_ept_vpid_cap(VMX_EPT_INVEPT_SINGLE_CONTEXT)) {
		desc.eptp = hva2hpa(eptp) | (3UL << 3U) | 6UL;
		local_invept(INVEPT_TYPE_SINGLE_CONTEXT, desc);
	} else if (pcpu_has_vmx_ept_vpid_cap(VMX_EPT_INVEPT_GLOBAL_CONTEXT)) {
		local_invept(INVEPT_TYPE_ALL_CONTEXTS, desc);
	} else {
		/* Neither type of INVEPT is supported. Skip. */
	}
}

/**
 * @brief Enable paging.
 *
 * This function performs the following specific operations to enable paging:
 * - Sets the IA32_PAT MSR with the power-on value (refer to Chapter 12 Programming the PAT, Volume 3, SDM 325462-078)
 *   to define caching attributes for memory pages.
 * - Enables NXE (No-Execute Enable) bit in IA32_EFER MSR to enforce execution protection on pages marked with XD bit.
 * - Sets SCE (System Call Extensions) bit in IA32_EFER MSR for consistent MSR state between hypervisor and VM,
 *   avoiding unnecessary MSR switching during VMX transitions.
 * - Enables Write Protection (WP bit in CR0), inhibiting writing to read-only pages.
 * - Loads CR3 with the physical address of the PML4 page table, which is the root of the page table hierarchy.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
void enable_paging(void)
{
	uint64_t tmp64 = 0UL;

	/* Initialize IA32_PAT according to ISDM 11.12.4 Programming the PAT */
	msr_write(MSR_IA32_PAT, PAT_POWER_ON_VALUE);

	/*
	 * Enable MSR IA32_EFER.NXE bit,to prevent
	 * instruction fetching from pages with XD bit set.
	 */
	tmp64 = msr_read(MSR_IA32_EFER);

	/*
	 * SCE bit is not used by the host. However we set this bit so that
	 * it's highly likely that the value of IA32_EFER the host and the guest
	 * is identical, and we don't need to switch this MSR on VMX transitions
	 */
	tmp64 |= MSR_IA32_EFER_NXE_BIT | MSR_IA32_EFER_SCE_BIT;
	msr_write(MSR_IA32_EFER, tmp64);

	/* Enable Write Protect, inhibiting writing to read-only pages */
	CPU_CR_READ(cr0, &tmp64);
	CPU_CR_WRITE(cr0, tmp64 | CR0_WP);

	/* HPA->HVA is 1:1 mapping at this moment, simply treat ppt_mmu_pml4_addr as HPA. */
	CPU_CR_WRITE(cr3, ppt_mmu_pml4_addr);
}

/**
 * @brief Enable Supervisor Mode Execution Prevention (SMEP).
 *
 * Enables the SMEP feature by setting CR4.SMEP bit. When SMEP is enabled, code in supervisor mode cannot
 * execute code from pages marked as user-accessible.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
void enable_smep(void)
{
	uint64_t val64 = 0UL;

	/* Enable CR4.SMEP*/
	CPU_CR_READ(cr4, &val64);
	CPU_CR_WRITE(cr4, val64 | CR4_SMEP);
}

/**
 * @brief Enable Supervisor Mode Access Prevention (SMAP).
 *
 * Enables the SMAP feature by setting CR4.SMAP bit. When SMAP is enabled, code in supervisor mode cannot access
 * user-mode pages unless the code is running in a user context.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
void enable_smap(void)
{
	uint64_t val64 = 0UL;

	/* Enable CR4.SMAP*/
	CPU_CR_READ(cr4, &val64);
	CPU_CR_WRITE(cr4, val64 | CR4_SMAP);
}

/**
 * @brief Clean USER bit in page table to update memory pages to be owned by hypervisor.
 *
 * Updates page table entries for the specified memory region to be accessible only in
 * supervisor mode by clearing the USER bit. This is used to protect hypervisor memory
 * from guest access.
 *
 * The function rounds down the base address to a 2MB boundary and rounds up the
 * size accordingly to ensure proper alignment.
 *
 * @param[in] base Starting address of the memory region to protect
 * @param[in] size Size of the memory region in bytes
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
void set_paging_supervisor(uint64_t base, uint64_t size)
{
	uint64_t base_aligned;
	uint64_t size_aligned;
	uint64_t region_end = base + size;

	/*rounddown base to 2MBytes aligned.*/
	base_aligned = round_pde_down(base);
	size_aligned = region_end - base_aligned;

	pgtable_modify_or_del_map((uint64_t *)ppt_mmu_pml4_addr, base_aligned,
		round_pde_up(size_aligned), 0UL, PAGE_USER, &ppt_pgtable, MR_MODIFY);
}

/**
 * @brief Initialize the primary page table (PPT) page pool memory structures.
 *
 * This function allocates and initializes memory for the primary page table page pool, which is used
 * to manage memory for page table structures. It performs the following operations:
 * - Calculates the bitmap size needed to track page allocation status (1 bit per page).
 * - Allocates memory for page descriptors by calling e820_alloc_memory().
 * - Allocates memory for the bitmap to track page allocation status.
 * - Initializes the PPT page pool structure with the allocated memory.
 * - Sets the bitmap to zero to indicate all pages are free.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
void allocate_ppt_pages(void)
{
	uint64_t page_base;
	uint64_t bitmap_size = get_ppt_page_num() / 8UL;

	page_base = e820_alloc_memory(sizeof(struct page) * get_ppt_page_num(), MEM_4G);
	ppt_page_pool.bitmap = (uint64_t *)e820_alloc_memory(bitmap_size, MEM_4G);

	ppt_page_pool.start_page = (struct page *)(void *)page_base;
	ppt_page_pool.bitmap_size = bitmap_size / sizeof(uint64_t);
	ppt_page_pool.dummy_page = NULL;

	(void)memset(ppt_page_pool.bitmap, 0U, bitmap_size);
}

/**
 * @brief MMU page tables initialization
 *
 * This function sets up the complete paging environment for the hypervisor:
 * - Calls init_sanitized_page() to initialize the sanitized page for L1TF mitigation.
 * - Allocates memory for the Hypervisor PML4 table (root of the page table hierarchy).
 * - Analyzes system memory map to identify RAM and MMIO regions.
 * - Maps low RAM regions (below 4GB) with PAGE_CACHE_WB (Write-Back) caching attributes.
 * - Maps high RAM regions (above 4GB) with PAGE_CACHE_WB caching attributes if present.
 * - Maps MMIO regions with PAGE_CACHE_UC (Uncacheable) caching attributes.
 * - Sets appropriate page permissions for hypervisor memory regions:
 *   - Marks hypervisor memory as supervisor-only (clears USER bit).
 *   - Makes hypervisor code sections executable (remove 'NX' bit for pages that contain hv code section).
 * - Enables paging by calling enable_paging() function.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
void init_paging(void)
{
	uint64_t hv_hva;
	uint32_t i;
	uint64_t low32_max_ram = 0UL;
	struct acrn_boot_info *abi = get_acrn_boot_info();
	const struct abi_mmap *entry;
	uint32_t entries_count = abi->mmap_entries;
	const struct abi_mmap *p_mmap = abi->mmap_entry;

	pr_dbg("HV MMU Initialization");

	init_sanitized_page((uint64_t *)sanitized_page, hva2hpa_early(sanitized_page));

	/* Allocate memory for Hypervisor PML4 table */
	ppt_mmu_pml4_addr = pgtable_create_root(&ppt_pgtable);

	/* Modify WB attribute for E820_TYPE_RAM */
	for (i = 0U; i < entries_count; i++) {
		entry = p_mmap + i;
		if (entry->type == MMAP_TYPE_RAM) {
			uint64_t end = entry->baseaddr + entry->length;
			if (end < MEM_4G) {
				low32_max_ram = max(end, low32_max_ram);
			} else {
				high64_min_ram = min(entry->baseaddr, high64_min_ram);
				high64_max_ram = max(end, high64_max_ram);
			}
		}
	}

	low32_max_ram = round_pde_up(low32_max_ram);
	high64_max_ram = round_pde_down(high64_max_ram);

	/* Map [0, low32_max_ram) and [high64_min_ram, high64_max_ram) RAM regions as WB attribute */
	pgtable_add_map((uint64_t *)ppt_mmu_pml4_addr, 0UL, 0UL,
			low32_max_ram, PAGE_ATTR_USER | PAGE_CACHE_WB, &ppt_pgtable);

	if (high64_max_ram > high64_min_ram) {
		pgtable_add_map((uint64_t *)ppt_mmu_pml4_addr, high64_min_ram, high64_min_ram,
				high64_max_ram - high64_min_ram, PAGE_ATTR_USER | PAGE_CACHE_WB, &ppt_pgtable);
	}
	/* Map [low32_max_ram, 4G) and [MMIO64_START, MMIO64_END) MMIO regions as UC attribute */
	pgtable_add_map((uint64_t *)ppt_mmu_pml4_addr, low32_max_ram, low32_max_ram,
		MEM_4G - low32_max_ram, PAGE_ATTR_USER | PAGE_CACHE_UC, &ppt_pgtable);
	if ((MMIO64_START != ~0UL) && (MMIO64_END != 0UL)) {
		pgtable_add_map((uint64_t *)ppt_mmu_pml4_addr, MMIO64_START, MMIO64_START,
			(MMIO64_END - MMIO64_START), PAGE_ATTR_USER | PAGE_CACHE_UC, &ppt_pgtable);
	}

	/*
	 * set the paging-structure entries' U/S flag to supervisor-mode for hypervisor owned memory.
	 *
	 * Before the new PML4 take effect in enable_paging(), HPA->HVA is always 1:1 mapping,
	 * simply treat the return value of get_hv_image_base() as HPA.
	 */
	hv_hva = get_hv_image_base();
	pgtable_modify_or_del_map((uint64_t *)ppt_mmu_pml4_addr, hv_hva & PDE_MASK,
			get_hv_image_size() + (((hv_hva & (PDE_SIZE - 1UL)) != 0UL) ? PDE_SIZE : 0UL),
			PAGE_CACHE_WB, PAGE_CACHE_MASK | PAGE_USER, &ppt_pgtable, MR_MODIFY);

	/*
	 * remove 'NX' bit for pages that contain hv code section, as by default XD bit is set for
	 * all pages, including pages for guests.
	 */
	pgtable_modify_or_del_map((uint64_t *)ppt_mmu_pml4_addr, round_pde_down(hv_hva),
			round_pde_up((uint64_t)&ld_text_end) - round_pde_down(hv_hva), 0UL,
			PAGE_NX, &ppt_pgtable, MR_MODIFY);

	/* Enable paging */
	enable_paging();
}

/**
 * @brief Flush all processor caches.
 *
 * This function calls wbinvd() function to write back and invalidate all internal caches.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
void flush_invalidate_all_cache(void)
{
	wbinvd();
}

/**
 * @brief Flush cache lines for a range of memory.
 *
 * Executes the CLFLUSHOPT instruction for each cache line in the specified memory
 * range. This ensures memory coherency for a range of memory locations by writing
 * back and invalidating all cache lines that overlap with the range.
 *
 * @param[in] p Pointer to the start of the memory range
 * @param[in] size Size of the memory range in bytes
 *
 * @return None
 *
 * @pre p != NULL
 * @post N/A
 */
void flush_cache_range(const volatile void *p, uint64_t size)
{
	uint64_t i;

	for (i = 0UL; i < size; i += CACHE_LINE_SIZE) {
		clflushopt(p + i);
	}
}

/**
 * @}
 */