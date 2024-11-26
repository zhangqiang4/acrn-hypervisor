/*
 * Copyright (C) 2018-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <util.h>
#include <acrn_hv_defs.h>
#include <asm/page.h>
#include <asm/mmu.h>
#include <logmsg.h>

/**
 * @addtogroup hwmgmt_page
 *
 * @{
 */

/**
 * @file
 * @brief Implementation page table management.
 *
 * This file implements the external APIs to establish, modify, delete, or look for the mapping information. It also
 * defines some helper functions to implement the features that are commonly used in this file.
 *
 */

#define DBG_LEVEL_MMU	6U	/**< MMU-related log level */

/**
 * @brief Host physical address of the sanitized page.
 *
 * The sanitized page is used to mitigate l1tf. This variable is used to store the host physical address of the
 * sanitized page.
 */
static uint64_t sanitized_page_hpa;

/**
 * @brief Sanitize a page table entry (PTE)
 *
 * This function invalidates a page table entry (PTE) by clearing its present bit, and sets is address to the
 * host physical address of "sanitized page" with no secret data to mitigate L1TF.
 *
 * @param[inout] ptep  Pointer to the page table entry (PTE) to be invalidated.
 * @param[in]    table Pointer to the page table struct which ptep belongs to.
 *
 * @return None
 *
 * @pre (ptep != NULL) && (table != NULL)
 *
 * @post N/A
 */
static void sanitize_pte_entry(uint64_t *ptep, const struct pgtable *table)
{
	set_pgentry(ptep, sanitized_page_hpa, table);
}

/**
 * @brief Sanitize a page table page with invalid entries
 *
 * This function sanitizes a page table page by calling sanitize_pte_entry for each page table entry in it
 * to mitigate L1TF.
 *
 * @param[inout] pt_page Pointer to the page table page to be initialized.
 * @param[in]    table   Pointer to the page table struct which pt_page belongs to.
 *
 * @return None
 *
 * @pre (ptep != NULL) && (table != NULL)
 *
 * @post N/A
 */
static void sanitize_pte(uint64_t *pt_page, const struct pgtable *table)
{
	uint64_t i;
	for (i = 0UL; i < PTRS_PER_PTE; i++) {
		sanitize_pte_entry(pt_page + i, table);
	}
}

/**
 * @brief Initializes a sanitized page.
 *
 * This function is responsible for initializing a sanitized page. It sets the page table entries in this sanitized
 * page to point to the host physical address of the sanitized page itself.
 *
 * The static variable 'sanitized_page_hpa' will be set and the `sanitized_page` will be initialized.
 *
 * @param[out] sanitized_page The page to be sanitized.
 * @param[in] hpa The host physical address that the page table entries in the sanitized page will point to.
 *
 * @return None
 *
 * @pre sanitized_page != NULL
 * @pre ((uint64_t)sanitized_page & (PAGE_SIZE - 1)) == 0x0U
 * @pre hpa != 0U
 * @pre (hpa & (PAGE_SIZE - 1)) == 0x0U
 *
 * @post N/A
 */
void init_sanitized_page(uint64_t *sanitized_page, uint64_t hpa)
{
	uint64_t i;

	sanitized_page_hpa = hpa;
	/* set ptep in sanitized_page point to itself */
	for (i = 0UL; i < PTRS_PER_PTE; i++) {
		*(sanitized_page + i) = sanitized_page_hpa;
	}
}

/**
 * @brief Free a page table page if all of its entries are not present
 *
 * When unmapping a page (type is MR_DEL), this function would check whether all the page table entries in this
 * page is not present, if so, it will free this page and clear its page directory entry by sanitize_pte_entry.
 * Otherwise it does nothing.
 *
 * Caller must ensure the given page is a page table page and the given PDE is points to that page.
 *
 * @param[in]    table   Pointer to the page table struct which pt_page belongs to.
 * @param[inout] pde     Pointer to the page directory entry (PDE) referring to pt_page.
 * @param[in]    pt_page Pointer to the page table page to be freed.
 * @param[in]    type    Type of operation to perform.
 *
 * @return None
 *
 * @pre (table != NULL) && (pde != NULL) && (pt_page != NULL)
 *
 * @post N/A
 */
static void try_to_free_pgtable_page(const struct pgtable *table,
			uint64_t *pde, uint64_t *pt_page, uint32_t type)
{
	if (type == MR_DEL) {
		uint64_t index;

		for (index = 0UL; index < PTRS_PER_PTE; index++) {
			uint64_t *pte = pt_page + index;
			if (pgentry_present(table, (*pte))) {
				break;
			}
		}

		if (index == PTRS_PER_PTE) {
			free_page(table->pool, (void *)pt_page);
			sanitize_pte_entry(pde, table);
		}
	}
}

/**
 * @brief Split a large page table into next level page table
 *
 * This function splits a large page table into next level page table with same properties. Only PDPTE
 * and PDE support large page.
 *
 * @param[inout] pte     Pointer to the page table entry (PDPTE or PDE) to split.
 * @param[in]    level   Level of pte, either IA32E_PDPT or IA32E_PD.
 * @param[in]    vaddr   Unused variable.
 * @param[in]    table   Pointer to the page table struct which pte belongs to.
 *
 * @return None
 *
 * @pre (pte != NULL) && (pgtable != NULL)
 * @pre (level == IA32E_PDPT) || (level == IA32E_PD)
 * @pre pgentry_present(table, (*pte)) == 1
 * @pre (*pte & PTE_SIZE) == 1
 *
 * @post N/A
 */
static void split_large_page(uint64_t *pte, enum _page_table_level level,
		__unused uint64_t vaddr, const struct pgtable *table)
{
	uint64_t *pbase;
	uint64_t ref_paddr, paddr, paddrinc;
	uint64_t i, ref_prot;

	switch (level) {
	case IA32E_PDPT:
		ref_paddr = (*pte) & PDPTE_PFN_MASK;
		paddrinc = PDE_SIZE;
		ref_prot = (*pte) & ~PDPTE_PFN_MASK;
		break;
	default:	/* IA32E_PD */
		ref_paddr = (*pte) & PDE_PFN_MASK;
		paddrinc = PTE_SIZE;
		ref_prot = (*pte) & ~PDE_PFN_MASK;
		ref_prot &= ~PAGE_PSE;
		table->recover_exe_right(&ref_prot);
		break;
	}

	pbase = (uint64_t *)alloc_page(table->pool);
	dev_dbg(DBG_LEVEL_MMU, "%s, paddr: 0x%lx, pbase: 0x%p\n", __func__, ref_paddr, pbase);

	paddr = ref_paddr;
	for (i = 0UL; i < PTRS_PER_PTE; i++) {
		set_pgentry(pbase + i, paddr | ref_prot, table);
		paddr += paddrinc;
	}

	ref_prot = table->default_access_right;
	set_pgentry(pte, hva2hpa((void *)pbase) | ref_prot, table);

	/* TODO: flush the TLB */
}

/**
 * @brief Modify or unmap a page table entry
 *
 * When modifying a page table entry (type is MR_MODIFY), this function clears then sets the given properties
 * on given page table entry.
 *
 * When unmapping a page table entry (type is MR_DEL), this function clears the given page table entry.
 *
 * @param[inout] pte      Pointer to the page table entry to operate on.
 * @param[in]    prot_set Properties to be set. (MR_MODIFY only)
 * @param[in]    prot_clr Properties to be cleared. (MR_MODIFY only)
 * @param[in]    type     Type of operation to perform.
 * @param[in]    table    Pointer to the page table struct which pte belongs to.
 *
 * @return None
 *
 * @pre (type == MR_MODIFY) || (type == MR_DELETE)
 * @pre (pte != NULL) && (pgtable != NULL)
 * @pre pgentry_present(table, (*pte)) == 1
 *
 * @post N/A
 */
static inline void local_modify_or_del_pte(uint64_t *pte,
		uint64_t prot_set, uint64_t prot_clr, uint32_t type, const struct pgtable *table)
{
	if (type == MR_MODIFY) {
		uint64_t new_pte = *pte;
		new_pte &= ~prot_clr;
		new_pte |= prot_set;
		set_pgentry(pte, new_pte, table);
	} else {
		sanitize_pte_entry(pte, table);
	}
}

/**
 * @brief Construct page directory entry with given page table page.
 *
 * This function sanitizes the given page table page and constructs page directory entry pointing to the given page
 * table page with given properties
 *
 * @param[inout] pde     Pointer to the page directory entry to operate on.
 * @param[in]    pt_page Pointer to the page whic pde is going to point to.
 * @param[in]    prot    Properties of pde.
 * @param[in]    table   Pointer to the page table struct which pde belongs to.
 *
 * @return None
 *
 * @pre (pte != NULL) && (pt_page != NULL) && (pgtable != NULL)
 * @pre pgentry_present(table, (*pde)) == 0
 *
 * @post N/A
 */
static inline void construct_pgentry(uint64_t *pde, void *pt_page, uint64_t prot, const struct pgtable *table)
{
	sanitize_pte((uint64_t *)pt_page, table);

	set_pgentry(pde, hva2hpa(pt_page) | prot, table);
}

/**
 * @brief Walk page table page in a given page directory entry, modify or unmap page table entries (PTEs) for specified
 * virtual address range
 *
 * When modifying a page table entry (type is MR_MODIFY), this function modifies the properties of page table entries
 * (PTEs) for specified virtual address range in given page table.
 *
 * When unmapping a page table entry (type is MR_DEL), this function clears page table entries (PTEs) for specified
 * virtual address range in given page table and frees the page directory entry and the page it refers to if possible.
 *
 * The virtual address range is [vaddr_start, vaddr_end), both vaddr_start and vaddr_end must align to page boundary.
 *
 * @param[inout] pde         Pointer to page directory entry (PDE) referring to page table page.
 * @param[in]    vaddr_start Starting virtual address of the range (including itself).
 * @param[in]    vaddr_end   Ending virtual address of the range (not including itself).
 * @param[in]    prot_set    Properties to be set. (MR_MODIFY only)
 * @param[in]    prot_clr    Properties to be cleared. (MR_MODIFY only)
 * @param[in]    table       Pointer to the page table struct which pde belongs to.
 * @param[in]    type        Type of operation to perform.
 *
 * @return None
 *
 * @pre (type == MR_MODIFY) || (type == MR_DELETE)
 * @pre (pde != NULL) && (pgtable != NULL)
 * @pre pgentry_present(table, (*pde)) == 1
 * @pre ((vaddr_start & PTE_MASK) == 0) && ((vaddr_end & PTE_MASK) == 0)
 *
 * @post N/A
 */
static void modify_or_del_pte(uint64_t *pde, uint64_t vaddr_start, uint64_t vaddr_end,
		uint64_t prot_set, uint64_t prot_clr, const struct pgtable *table, uint32_t type)
{
	uint64_t *pt_page = pde_page_vaddr(*pde);
	uint64_t vaddr = vaddr_start;
	uint64_t index = pte_index(vaddr);

	dev_dbg(DBG_LEVEL_MMU, "%s, vaddr: [0x%lx - 0x%lx]\n", __func__, vaddr, vaddr_end);
	for (; index < PTRS_PER_PTE; index++) {
		uint64_t *pte = pt_page + index;

		if (!pgentry_present(table, (*pte))) {
			/*suppress warning message for low memory (< 1MBytes),as service VM
			 * will update MTTR attributes for this region by default whether it
			 * is present or not.
			 */
			if ((type == MR_MODIFY) && (vaddr >= MEM_1M)) {
				pr_warn("%s, vaddr: 0x%lx pte is not present.\n", __func__, vaddr);
			}
		} else {
			local_modify_or_del_pte(pte, prot_set, prot_clr, type, table);
		}

		vaddr += PTE_SIZE;
		if (vaddr >= vaddr_end) {
			break;
		}
	}

	try_to_free_pgtable_page(table, pde, pt_page, type);
}

/**
 * @brief Walk page directory page in a given page directory pointer table entry, modify or unmap page table entries
 * (PDEs or PTEs) for specified virtual address range in given page directory
 *
 * If the virtual address range covered a whole page directory entry (PDE), modify or unmap this PDE; otherwise,
 * modify or unmap the PTE for thoese virtual addresses range. In this case, the large directory page for this PDE
 * would split into small table page.
 *
 * This function also try to free the given page directory pointer table entry.
 *
 * The virtual address range is [vaddr_start, vaddr_end), both vaddr_start and vaddr_end must align to page boundary,
 *
 * @param[inout] pdpte       Pointer to page directory pointer table entry (PDPTE) referring to page directory.
 * @param[in]    vaddr_start Starting virtual address of the range (including itself).
 * @param[in]    vaddr_end   Ending virtual address of the range (not including itself).
 * @param[in]    prot_set    Properties to be set. (MR_MODIFY only)
 * @param[in]    prot_clr    Properties to be cleared. (MR_MODIFY only)
 * @param[in]    table       Pointer to the page table struct which pdpte belongs to.
 * @param[in]    type        Type of operation to perform.
 *
 * @return None
 *
 * @pre (type == MR_MODIFY) || (type == MR_DELETE)
 * @pre (pdpte != NULL) && (pgtable != NULL)
 * @pre pgentry_present(table, (*pdpte)) == 1
 * @pre ((vaddr_start & PTE_MASK) == 0) && ((vaddr_end & PTE_MASK) == 0)
 *
 * @post N/A
 */
static void modify_or_del_pde(uint64_t *pdpte, uint64_t vaddr_start, uint64_t vaddr_end,
		uint64_t prot_set, uint64_t prot_clr, const struct pgtable *table, uint32_t type)
{
	uint64_t *pd_page = pdpte_page_vaddr(*pdpte);
	uint64_t vaddr = vaddr_start;
	uint64_t index = pde_index(vaddr);

	dev_dbg(DBG_LEVEL_MMU, "%s, vaddr: [0x%lx - 0x%lx]\n", __func__, vaddr, vaddr_end);
	for (; index < PTRS_PER_PDE; index++) {
		uint64_t *pde = pd_page + index;
		uint64_t vaddr_next = (vaddr & PDE_MASK) + PDE_SIZE;

		if (!pgentry_present(table, (*pde))) {
			if (type == MR_MODIFY) {
				pr_warn("%s, addr: 0x%lx pde is not present.\n", __func__, vaddr);
			}
		} else {
			if (pde_large(*pde) != 0UL) {
				if ((vaddr_next > vaddr_end) || (!mem_aligned_check(vaddr, PDE_SIZE))) {
					split_large_page(pde, IA32E_PD, vaddr, table);
				} else {
					local_modify_or_del_pte(pde, prot_set, prot_clr, type, table);
					if (vaddr_next < vaddr_end) {
						vaddr = vaddr_next;
						continue;
					}
					break;	/* done */
				}
			}
			modify_or_del_pte(pde, vaddr, vaddr_end, prot_set, prot_clr, table, type);
		}
		if (vaddr_next >= vaddr_end) {
			break;	/* done */
		}
		vaddr = vaddr_next;
	}

	try_to_free_pgtable_page(table, pdpte, pd_page, type);
}

/**
 * @brief Walk page directory pointer table page in a given page PML4 table entry, modify or unmap page table
 * entries (PDPTEs or PDEs or PTEs) for specified virtual address range in given page directory pointer table
 *
 * If the virtual address range covered a whole page directory pointer table entry (PDPTE), modify or unmap this PDPTE;
 * otherwise, modify or unmap the PDPTE for thoese virtual addresses range. In this case, the large directory page for
 * this PDPTE would split into small table page. If the address range is also not convered a whole page directory entry
 * (PDE), PDE would also be splited into next level page table.
 *
 * This function also try to free the given page directory pointer table entry.
 *
 * The virtual address range is [vaddr_start, vaddr_end), both vaddr_start and vaddr_end must align to page boundary,
 *
 * @param[inout] pml4e       Pointer to page PML4 table entry referring to page directory pointer table.
 * @param[in]    vaddr_start Starting virtual address of the range (including itself).
 * @param[in]    vaddr_end   Ending virtual address of the range (not including itself).
 * @param[in]    prot_set    Properties to be set. (MR_MODIFY only)
 * @param[in]    prot_clr    Properties to be cleared. (MR_MODIFY only)
 * @param[in]    table       Pointer to the page table struct which pml4e belongs to.
 * @param[in]    type        Type of operation to perform.
 *
 * @return None
 *
 * @pre (type == MR_MODIFY) || (type == MR_DELETE)
 * @pre (pml4e != NULL) && (pgtable != NULL)
 * @pre pgentry_present(table, (*pml4e)) == 1
 * @pre ((vaddr_start & PTE_MASK) == 0) && ((vaddr_end & PTE_MASK) == 0)
 *
 * @post N/A
 */
static void modify_or_del_pdpte(const uint64_t *pml4e, uint64_t vaddr_start, uint64_t vaddr_end,
		uint64_t prot_set, uint64_t prot_clr, const struct pgtable *table, uint32_t type)
{
	uint64_t *pdpt_page = pml4e_page_vaddr(*pml4e);
	uint64_t vaddr = vaddr_start;
	uint64_t index = pdpte_index(vaddr);

	dev_dbg(DBG_LEVEL_MMU, "%s, vaddr: [0x%lx - 0x%lx]\n", __func__, vaddr, vaddr_end);
	for (; index < PTRS_PER_PDPTE; index++) {
		uint64_t *pdpte = pdpt_page + index;
		uint64_t vaddr_next = (vaddr & PDPTE_MASK) + PDPTE_SIZE;

		if (!pgentry_present(table, (*pdpte))) {
			if (type == MR_MODIFY) {
				pr_warn("%s, vaddr: 0x%lx pdpte is not present.\n", __func__, vaddr);
			}
		} else {
			if (pdpte_large(*pdpte) != 0UL) {
				if ((vaddr_next > vaddr_end) ||
						(!mem_aligned_check(vaddr, PDPTE_SIZE))) {
					split_large_page(pdpte, IA32E_PDPT, vaddr, table);
				} else {
					local_modify_or_del_pte(pdpte, prot_set, prot_clr, type, table);
					if (vaddr_next < vaddr_end) {
						vaddr = vaddr_next;
						continue;
					}
					break;	/* done */
				}
			}
			modify_or_del_pde(pdpte, vaddr, vaddr_end, prot_set, prot_clr, table, type);
		}
		if (vaddr_next >= vaddr_end) {
			break;	/* done */
		}
		vaddr = vaddr_next;
	}
}

/**
 * @brief Modify or delete the mappings associated with the specified address range.
 *
 * This function modifies the properties of an existing mapping or deletes it entirely from the page table. The input
 * address range is specified by [vaddr_base, vaddr_base + size). It is used when changing the access permissions of a
 * memory region or when freeing a previously mapped region. This operation is critical for dynamic memory management,
 * allowing the system to adapt to changes in memory usage patterns or to reclaim resources.
 *
 * For error case behaviors:
 * - If the 'type' is MR_MODIFY and any page referenced by the PML4E in the specified address range is not present, the
 * function asserts that the operation is invalid.
 * For normal case behaviors(when the error case conditions are not satisfied):
 * - If any page referenced by the PDPTE/PDE/PTE in the specified address range is not present, there is no change to
 * the corresponding mapping and it continues the operation.
 * - If any PDPTE/PDE in the specified address range maps a large page and the large page address exceeds the specified
 * address range, the function splits the large page into next level page to allow for the modification or deletion of
 * the mappings and the execute right will be recovered by the callback function table->recover_exe_right() when a 2MB
 * page is split to 4KB pages.
 * - If the 'type' is MR_MODIFY, the function modifies the properties of the existing mapping to match the specified
 * properties.
 * - If the 'type' is MR_DEL, the function will set corresponding page table entries to point to the sanitized page.
 *
 * @param[inout] pml4_page A pointer to the specified PML4 table.
 * @param[in] vaddr_base The specified input address determining the start of the input address range whose mapping
 *                       information is to be updated.
 *                       For hypervisor's MMU, it is the host virtual address.
 *                       For each VM's EPT, it is the guest physical address.
 * @param[in] size The size of the specified input address range whose mapping information is to be updated.
 * @param[in] prot_set Bit positions representing the specified properties which need to be set.
 *                     Bits specified by prot_clr are cleared before each bit specified by prot_set is set to 1.
 * @param[in] prot_clr Bit positions representing the specified properties which need to be cleared.
 *                     Bits specified by prot_clr are cleared before each bit specified by prot_set is set to 1.
 * @param[in] table A pointer to the struct pgtable containing the information of the specified memory operations.
 * @param[in] type The type of operation to perform (MR_MODIFY or MR_DEL).
 *
 * @return None
 *
 * @pre pml4_page != NULL
 * @pre table != NULL
 * @pre (type == MR_MODIFY) || (type == MR_DEL)
 * @pre For x86 hypervisor, the following conditions shall be met if "type == MR_MODIFY".
 *      - (prot_set & ~(PAGE_RW | PAGE_USER | PAGE_PWT | PAGE_PCD | PAGE_ACCESSED | PAGE_DIRTY | PAGE_PSE | PAGE_GLOBAL
 *      | PAGE_PAT_LARGE | PAGE_NX) == 0)
 *      - (prot_clr & ~(PAGE_RW | PAGE_USER | PAGE_PWT | PAGE_PCD | PAGE_ACCESSED | PAGE_DIRTY | PAGE_PSE | PAGE_GLOBAL
 *      | PAGE_PAT_LARGE | PAGE_NX) == 0)
 * @pre For the VM EPT mappings, the following conditions shall be met if "type == MR_MODIFY".
 *      - (prot_set & ~(EPT_RD | EPT_WR | EPT_EXE | EPT_MT_MASK) == 0)
 *      - (prot_set & EPT_MT_MASK) == EPT_UNCACHED || (prot_set & EPT_MT_MASK) == EPT_WC ||
 *        (prot_set & EPT_MT_MASK) == EPT_WT || (prot_set & EPT_MT_MASK) == EPT_WP || (prot_set & EPT_MT_MASK) == EPT_WB
 *      - (prot_clr & ~(EPT_RD | EPT_WR | EPT_EXE | EPT_MT_MASK) == 0)
 *      - (prot_clr & EPT_MT_MASK) == EPT_UNCACHED || (prot_clr & EPT_MT_MASK) == EPT_WC ||
 *        (prot_clr & EPT_MT_MASK) == EPT_WT || (prot_clr & EPT_MT_MASK) == EPT_WP || (prot_clr & EPT_MT_MASK) == EPT_WB
 *
 * @post N/A
 *
 * @remark N/A
 */
void pgtable_modify_or_del_map(uint64_t *pml4_page, uint64_t vaddr_base, uint64_t size,
		uint64_t prot_set, uint64_t prot_clr, const struct pgtable *table, uint32_t type)
{
	uint64_t vaddr = round_page_up(vaddr_base);
	uint64_t vaddr_next, vaddr_end;
	uint64_t *pml4e;

	vaddr_end = vaddr + round_page_down(size);
	dev_dbg(DBG_LEVEL_MMU, "%s, vaddr: 0x%lx, size: 0x%lx\n",
		__func__, vaddr, size);

	while (vaddr < vaddr_end) {
		vaddr_next = (vaddr & PML4E_MASK) + PML4E_SIZE;
		pml4e = pml4e_offset(pml4_page, vaddr);
		if ((!pgentry_present(table, (*pml4e))) && (type == MR_MODIFY)) {
			ASSERT(false, "invalid op, pml4e not present");
		} else {
			modify_or_del_pdpte(pml4e, vaddr, vaddr_end, prot_set, prot_clr, table, type);
			vaddr = vaddr_next;
		}
	}
}

/**
 * @brief Add page table entries (PTEs) in given page table to map specified address range
 *
 * This function adds page table entries (PTEs) in given page table to map virtual address [vaddr_start, vaddr_end)
 * to physical address [paddr_start, ...). The size of virtual address range and physical address range are the same.
 *
 * Caller must ensure vaddr_start, vaddr_end, paddr_start are all aligned to 4K page boundary and the virtual address
 * range is not mapped before.
 *
 * @param[inout] pde         Pointer to page directory entry (PDE) referring to page table page.
 * @param[in]    paddr_start Starting physical address of the range (including itself).
 * @param[in]    vaddr_start Starting virtual address of the range (including itself).
 * @param[in]    vaddr_end   Ending virtual address of the range (not including itself).
 * @param[in]    prot        Properties of the new mapping.
 * @param[in]    table       Pointer to the page table struct which pde belongs to.
 *
 * @return None
 *
 * @pre pgentry_present(table, (*pde)) == 1
 * @pre ((paddr_start & PTE_MASK) == 0) && ((vaddr_start & PTE_MASK) == 0) && ((vaddr_end & PTE_MASK) == 0)
 *
 * @post N/A
 */
static void add_pte(const uint64_t *pde, uint64_t paddr_start, uint64_t vaddr_start, uint64_t vaddr_end,
		uint64_t prot, const struct pgtable *table)
{
	uint64_t *pt_page = pde_page_vaddr(*pde);
	uint64_t vaddr = vaddr_start;
	uint64_t paddr = paddr_start;
	uint64_t index = pte_index(vaddr);

	dev_dbg(DBG_LEVEL_MMU, "%s, paddr: 0x%lx, vaddr: [0x%lx - 0x%lx]\n",
		__func__, paddr, vaddr_start, vaddr_end);
	for (; index < PTRS_PER_PTE; index++) {
		uint64_t *pte = pt_page + index;

		if (pgentry_present(table, (*pte))) {
			pr_fatal("%s, pte 0x%lx is already present!\n", __func__, vaddr);
		} else {
			set_pgentry(pte, paddr | prot, table);
		}
		paddr += PTE_SIZE;
		vaddr += PTE_SIZE;

		if (vaddr >= vaddr_end) {
			break;	/* done */
		}
	}
}

/**
 * @brief Add page directory entries (PDEs) in given page directory to map specified address range
 *
 * This function adds page directory entries (PDEs) in given page directory to map virtual address [vaddr_start, vaddr_end)
 * to physical address [paddr_start, ...). The size of virtual address range and physical address range are the same.
 *
 * Caller must ensure vaddr_start, vaddr_end, paddr_start are all aligned to 4K page boundary and the virtual address
 * range is not mapped before. If the range is not aligned to 2M large page boundary, a new page table will be created
 * automatically.
 *
 * @param[inout] pdpte       Pointer to page directory pointer table entry (PDPTE) referring to page directory.
 * @param[in]    paddr_start Starting physical address of the range (including itself).
 * @param[in]    vaddr_start Starting virtual address of the range (including itself).
 * @param[in]    vaddr_end   Ending virtual address of the range (not including itself).
 * @param[in]    prot        Properties of the new mapping.
 * @param[in]    table       Pointer to the page table struct which pdpte belongs to.
 *
 * @pre (pdpte != NULL) && (pgtable != NULL)
 * @pre pgentry_present(table, (*pdpte)) == 1
 * @pre ((paddr_start & PTE_MASK) == 0) && ((vaddr_start & PTE_MASK) == 0) && ((vaddr_end & PTE_MASK) == 0)
 *
 * @post N/A
 */
static void add_pde(const uint64_t *pdpte, uint64_t paddr_start, uint64_t vaddr_start, uint64_t vaddr_end,
		uint64_t prot, const struct pgtable *table)
{
	uint64_t *pd_page = pdpte_page_vaddr(*pdpte);
	uint64_t vaddr = vaddr_start;
	uint64_t paddr = paddr_start;
	uint64_t index = pde_index(vaddr);
	uint64_t local_prot = prot;

	dev_dbg(DBG_LEVEL_MMU, "%s, paddr: 0x%lx, vaddr: [0x%lx - 0x%lx]\n",
		__func__, paddr, vaddr, vaddr_end);
	for (; index < PTRS_PER_PDE; index++) {
		uint64_t *pde = pd_page + index;
		uint64_t vaddr_next = (vaddr & PDE_MASK) + PDE_SIZE;

		if (pde_large(*pde) != 0UL) {
			pr_fatal("%s, pde 0x%lx is already present!\n", __func__, vaddr);
		} else {
			if (!pgentry_present(table, (*pde))) {
				if (table->large_page_support(IA32E_PD, prot) &&
					mem_aligned_check(paddr, PDE_SIZE) &&
					mem_aligned_check(vaddr, PDE_SIZE) &&
					(vaddr_next <= vaddr_end)) {
					table->tweak_exe_right(&local_prot);
					set_pgentry(pde, paddr | (local_prot | PAGE_PSE), table);
					if (vaddr_next < vaddr_end) {
						paddr += (vaddr_next - vaddr);
						vaddr = vaddr_next;
						continue;
					}
					break;	/* done */
				} else {
					void *pt_page = alloc_page(table->pool);
					construct_pgentry(pde, pt_page, table->default_access_right, table);
				}
			}
			add_pte(pde, paddr, vaddr, vaddr_end, prot, table);
		}
		if (vaddr_next >= vaddr_end) {
			break;	/* done */
		}
		paddr += (vaddr_next - vaddr);
		vaddr = vaddr_next;
	}
}

/**
 * @brief Add page directory pointer table entries (PDPTEs) in given page directory pointer table to map specified
 * address range
 *
 * This function adds page directory pointer table entries (PDPTEs) in given page directory pointer table to map
 * virtual address [vaddr_start, vaddr_end) to physical address [paddr_start, ...). The size of virtual address range
 * and physical address range are the same.
 *
 * Caller must ensure vaddr_start, vaddr_end, paddr_start are all aligned to 4K page boundary and the virtual address
 * range is not mapped before. If the range is not aligned to 1G large page boundary, lower level page tables(PD or PT)
 * will be created automatically.
 *
 * @param[inout] pml4e       Pointer to page PML4 table entry referring to page directory pointer table.
 * @param[in]    paddr_start Starting physical address of the range (including itself).
 * @param[in]    vaddr_start Starting virtual address of the range (including itself).
 * @param[in]    vaddr_end   Ending virtual address of the range (not including itself).
 * @param[in]    prot        Properties of the new mapping.
 * @param[in]    table       Pointer to the page table struct which pdpte belongs to.
 *
 * @pre (pml4e != NULL) && (pgtable != NULL)
 * @pre pgentry_present(table, (*pml4e)) == 1
 * @pre ((paddr_start & PTE_MASK) == 0) && ((vaddr_start & PTE_MASK) == 0) && ((vaddr_end & PTE_MASK) == 0)
 *
 * @post N/A
 */
static void add_pdpte(const uint64_t *pml4e, uint64_t paddr_start, uint64_t vaddr_start, uint64_t vaddr_end,
		uint64_t prot, const struct pgtable *table)
{
	uint64_t *pdpt_page = pml4e_page_vaddr(*pml4e);
	uint64_t vaddr = vaddr_start;
	uint64_t paddr = paddr_start;
	uint64_t index = pdpte_index(vaddr);
	uint64_t local_prot = prot;

	dev_dbg(DBG_LEVEL_MMU, "%s, paddr: 0x%lx, vaddr: [0x%lx - 0x%lx]\n", __func__, paddr, vaddr, vaddr_end);
	for (; index < PTRS_PER_PDPTE; index++) {
		uint64_t *pdpte = pdpt_page + index;
		uint64_t vaddr_next = (vaddr & PDPTE_MASK) + PDPTE_SIZE;

		if (pdpte_large(*pdpte) != 0UL) {
			pr_fatal("%s, pdpte 0x%lx is already present!\n", __func__, vaddr);
		} else {
			if (!pgentry_present(table, (*pdpte))) {
				if (table->large_page_support(IA32E_PDPT, prot) &&
					mem_aligned_check(paddr, PDPTE_SIZE) &&
					mem_aligned_check(vaddr, PDPTE_SIZE) &&
					(vaddr_next <= vaddr_end)) {
					table->tweak_exe_right(&local_prot);
					set_pgentry(pdpte, paddr | (local_prot | PAGE_PSE), table);
					if (vaddr_next < vaddr_end) {
						paddr += (vaddr_next - vaddr);
						vaddr = vaddr_next;
						continue;
					}
					break;	/* done */
				} else {
					void *pd_page = alloc_page(table->pool);
					construct_pgentry(pdpte, pd_page, table->default_access_right, table);
				}
			}
			add_pde(pdpte, paddr, vaddr, vaddr_end, prot, table);
		}
		if (vaddr_next >= vaddr_end) {
			break;	/* done */
		}
		paddr += (vaddr_next - vaddr);
		vaddr = vaddr_next;
	}
}

/**
 * @brief Add new page table mappings.
 *
 * This function maps a virtual address range specified by [vaddr_base, vaddr_base + size) to a physical address range
 * starting from 'paddr_base'.
 *
 * - If any subrange within [vaddr_base, vaddr_base + size) is already mapped, there is no change to the corresponding
 * mapping and it continues the operation.
 * - When a new 1GB or 2MB mapping is established, the callback function table->tweak_exe_right() is invoked to tweak
 * the execution bit.
 * - When a new page table referenced by a new PDPTE/PDE is created, all entries in the page table are initialized to
 * point to the sanitized page by default.
 * - Finally, the new mappings are established and initialized according to the specified address range and properties.
 *
 * @param[inout] pml4_page A pointer to the specified PML4 table hierarchy.
 * @param[in] paddr_base The specified physical address determining the start of the physical memory region.
 *                       It is the host physical address.
 * @param[in] vaddr_base The specified input address determining the start of the input address space.
 *                       For hypervisor's MMU, it is the host virtual address.
 *                       For each VM's EPT, it is the guest physical address.
 * @param[in] size The size of the specified input address space.
 * @param[in] prot Bit positions representing the specified properties which need to be set.
 * @param[in] table A pointer to the struct pgtable containing the information of the specified memory operations.
 *
 * @return None
 *
 * @pre pml4_page != NULL
 * @pre Any subrange within [vaddr_base, vaddr_base + size) shall already be unmapped.
 * @pre For x86 hypervisor mapping, the following condition shall be met.
 *      - prot & ~(PAGE_PRESENT| PAGE_RW | PAGE_USER | PAGE_PWT | PAGE_PCD | PAGE_ACCESSED | PAGE_DIRTY | PAGE_PSE |
 *      PAGE_GLOBAL | PAGE_PAT_LARGE | PAGE_NX) == 0
 * @pre For VM EPT mapping, the following conditions shall be met.
 *      - prot & ~(EPT_RD | EPT_WR | EPT_EXE | EPT_MT_MASK | EPT_IGNORE_PAT) == 0
 *      - (prot & EPT_MT_MASK) == EPT_UNCACHED || (prot & EPT_MT_MASK) == EPT_WC || (prot & EPT_MT_MASK) == EPT_WT ||
 *        (prot & EPT_MT_MASK) == EPT_WP || (prot & EPT_MT_MASK) == EPT_WB
 * @pre table != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
void pgtable_add_map(uint64_t *pml4_page, uint64_t paddr_base, uint64_t vaddr_base,
		uint64_t size, uint64_t prot, const struct pgtable *table)
{
	uint64_t vaddr, vaddr_next, vaddr_end;
	uint64_t paddr;
	uint64_t *pml4e;

	dev_dbg(DBG_LEVEL_MMU, "%s, paddr 0x%lx, vaddr 0x%lx, size 0x%lx\n", __func__, paddr_base, vaddr_base, size);

	/* align address to page size*/
	vaddr = round_page_up(vaddr_base);
	paddr = round_page_up(paddr_base);
	vaddr_end = vaddr + round_page_down(size);

	while (vaddr < vaddr_end) {
		vaddr_next = (vaddr & PML4E_MASK) + PML4E_SIZE;
		pml4e = pml4e_offset(pml4_page, vaddr);
		if (!pgentry_present(table, (*pml4e))) {
			void *pdpt_page = alloc_page(table->pool);
			construct_pgentry(pml4e, pdpt_page, table->default_access_right, table);
		}
		add_pdpte(pml4e, paddr, vaddr, vaddr_end, prot, table);

		paddr += (vaddr_next - vaddr);
		vaddr = vaddr_next;
	}
}

/**
 * @brief Create a new root page table.
 *
 * This function initializes and returns a new root page table. It is typically used during the setup of a new execution
 * context, such as initializing a hypervisor PML4 table or creating a virtual machine. The root page table is essential
 * for defining the virtual memory layout for the context.
 *
 * It creates a new root page table and every entries in the page table are initialized to point to the sanitized page.
 * Finally, the function returns the root page table pointer.
 *
 * @param[in] table A pointer to the struct pgtable containing the information of the specified memory operations.
 *
 * @return A pointer to the newly created root page table.
 *
 * @pre table != NULL
 *
 * @post N/A
 */
void *pgtable_create_root(const struct pgtable *table)
{
	uint64_t *page = (uint64_t *)alloc_page(table->pool);
	sanitize_pte(page, table);
	return page;
}

/**
 * @brief Look for the paging-structure entry that contains the mapping information for the specified input address.
 *
 * This function looks for the paging-structure entry that contains the mapping information for the specified input
 * address of the translation process. It is used to search the page table hierarchy for the entry corresponding to the
 * given virtual address. The function traverses the page table hierarchy from the PML4 down to the appropriate page
 * table level, returning the entry if found.
 *
 * - If specified address is mapped in the page table hierarchy, it will return a pointer to the page table entry that
 * maps the specified address.
 * - If the specified address is not mapped in the page table hierarchy, it will return NULL.
 *
 * @param[in] pml4_page A pointer to the specified PML4 table hierarchy.
 * @param[in] addr The specified input address whose mapping information is to be searched.
 *                 For hypervisor's MMU, it is the host virtual address.
 *                 For each VM's EPT, it is the guest physical address.
 * @param[out] pg_size A pointer to the size of the page controlled by the returned paging-structure entry.
 * @param[in] table A pointer to the struct pgtable which provides the page pool and callback functions to be used when
 *                  creating the new page.
 *
 * @return A pointer to the paging-structure entry that maps the specified input address.
 *
 * @retval non-NULL There is a paging-structure entry that contains the mapping information for the specified input
 *                  address.
 * @retval NULL There is no paging-structure entry that contains the mapping information for the specified input
 *              address.
 *
 * @pre pml4_page != NULL
 * @pre pg_size != NULL
 * @pre table != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
const uint64_t *pgtable_lookup_entry(uint64_t *pml4_page, uint64_t addr, uint64_t *pg_size, const struct pgtable *table)
{
	const uint64_t *pret = NULL;
	bool present = true;
	uint64_t *pml4e, *pdpte, *pde, *pte;

	pml4e = pml4e_offset(pml4_page, addr);
	present = pgentry_present(table, (*pml4e));

	if (present) {
		pdpte = pdpte_offset(pml4e, addr);
		present = pgentry_present(table, (*pdpte));
		if (present) {
			if (pdpte_large(*pdpte) != 0UL) {
				*pg_size = PDPTE_SIZE;
				pret = pdpte;
			} else {
				pde = pde_offset(pdpte, addr);
				present = pgentry_present(table, (*pde));
				if (present) {
					if (pde_large(*pde) != 0UL) {
						*pg_size = PDE_SIZE;
						pret = pde;
					} else {
						pte = pte_offset(pde, addr);
						present = pgentry_present(table, (*pte));
						if (present) {
							*pg_size = PTE_SIZE;
							pret = pte;
						}
					}
				}
			}
		}
	}

	return pret;
}

/**
 * @}
 */
