/*
 * Copyright (C) 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <errno.h>
#include <asm/guest/vm.h>
#include <asm/guest/virq.h>
#include <asm/pgtable.h>
#include <asm/mmu.h>
#include <asm/guest/ept.h>
#include <asm/vmx.h>
#include <asm/vtd.h>
#include <logmsg.h>
#include <trace.h>

/**
 * @addtogroup vp-base_guest-mem
 *
 * @{
 */

/**
 * @file
 * @brief Implementation of the Extended Page Table (EPT) management
 *
 * The EPT is used to map guest physical addresses to host physical addresses, providing memory virtualization for
 * virtual machines (VMs). This file includes functions for initializing, modifying, and destroying EPTs, as well as
 * functions for translating addresses between guest and host contexts. Additionally, it provides mechanisms for
 * handling EPT-related VM exits and managing EPT page tables.
 */

#define DBG_LEVEL_EPT	6U	/**< EPT logging level */

#define EPT_EXTRA_PT_PAGE_NUM	32U	/**< extra PT pages for internal operations */

/**
 * @brief Number of EPT PML4 pages required for a single VM
 *
 * EPT address space will not beyond the platform physical address space
 */
#define EPT_PML4_PAGE_NUM	PML4_PAGE_NUM(MAX_PHY_ADDRESS_SPACE)
/**
 * @brief Number of EPT PDPT pages required for a single VM
 *
 * EPT address space will not beyond the platform physical address space
 */
#define EPT_PDPT_PAGE_NUM	PDPT_PAGE_NUM(MAX_PHY_ADDRESS_SPACE)
/**
 * @brief Number of EPT PD pages required for a single VM
 *
 * EPT_PD_PAGE_NUM consists of three parts:
 * 1) DRAM - and low MMIO are contiguous (we could assume this because ve820 was build by us),
 *            CONFIG_MAX_VM_NUM at most
 * 2) low MMIO - and DRAM are contiguous
 * 3) high MMIO - Only PCI BARs're high MMIO (we didn't build the high MMIO EPT mapping
 *                except writing PCI 64 bits BARs)
 *
 * The first two parts may use PD_PAGE_NUM(CONFIG_MEMORY_TOP_ADDRESS) PD pages to build EPT mapping
 * at most;
 * The high MMIO may use (CONFIG_MAX_PCI_DEV_NUM * 6U) PD pages (may plus some PDPT entries
 * if the high MMIO BAR size is larger than 1GB) to build EPT mapping at most
 */
#define EPT_PD_PAGE_NUM		(PD_PAGE_NUM(MEM_TOP_ADDRESS) + (CONFIG_MAX_PCI_DEV_NUM * 6U))
/**
 * @brief Number of EPT PT pages required for a single VM
 *
 * EPT_PT_PAGE_NUM consists of three parts:
 * 1) DRAM - and low MMIO are contiguous (we could assume this because ve820 was build by us),
 *            CONFIG_MAX_VM_NUM at most
 * 2) low MMIO - and DRAM are contiguous
 * 3) high MMIO - Only PCI BARs're high MMIO (we didn't build the high MMIO EPT mapping
 *                except writing PCI 64 bits BARs)
 *
 * The first two parts may use (E820_MAX_ENTRIES * 2U) PT pages to build EPT mapping at most,
 * this is because using 2MB PD to map continuous memory regions is preferred, as E820 entries are 4KB-aligned,
 * at most 2 more PT pages are needed to map the bounds. We also provide extra PT pages for internal operations
 * like handling ept violation caused by instruction fetch and virtual LAPIC.
 * The high MMIO may use (CONFIG_MAX_PCI_DEV_NUM * 6U) PT pages to build EPT mapping at most:
 * this is because: (a) each 64 bits MMIO BAR may spend one PT page at most to build EPT mapping,
 *                      MMIO BAR size must be a power of 2 from 16 bytes;
 *                      MMIO BAR base address must be power of two in size and are aligned with its size;
 *                      So if the MMIO BAR size is less than 2M, one PT page is enough to cover its EPT mapping,
 *                      if the MMIO size is larger than 2M, it must be multiple of 2M, we could use large pages
 *                      to build EPT mapping for it. The single exception is fliter the MSI-X structure part
 *                      from the MSI-X table BAR. In this case, it will also spend one PT page.
 *                  (b) each PCI device may have six 64 bits MMIO (three general BARs plus three VF BARs)
 *                  (c) The Maximum number of PCI devices for ACRN and the Maximum number of virtual PCI devices
 *                      for VM both are CONFIG_MAX_PCI_DEV_NUM
 */
#define EPT_PT_PAGE_NUM		((E820_MAX_ENTRIES * 2U) + (CONFIG_MAX_PCI_DEV_NUM * 6U) + EPT_EXTRA_PT_PAGE_NUM)

/**
 * @brief Number of EPT pages required for a single VM
 */
#define EPT_PAGE_NUM	roundup((EPT_PML4_PAGE_NUM + EPT_PDPT_PAGE_NUM + EPT_PD_PAGE_NUM + EPT_PT_PAGE_NUM), 64U)


static struct page ept_pages[CONFIG_MAX_VM_NUM][EPT_PAGE_NUM] __aligned((PAGE_SIZE));	/**< Per-VM EPT pages */
static uint64_t ept_page_bitmap[CONFIG_MAX_VM_NUM][EPT_PAGE_NUM / 8];			/**< Per-VM EPT bitmap */
static struct page ept_dummy_pages[CONFIG_MAX_VM_NUM];					/**< Per-VM EPT dummy page */
static struct page_pool ept_page_pool[CONFIG_MAX_VM_NUM];				/**< Per-VM EPT page pool*/

/**
 * @brief Check whether EPT has specified level large page support.
 *
 * Check whether EPT has specified level large page support.
 * For 2MB large page, it is always supported.
 * For 1GB large page, it depends on the VMX_EPT_1GB_PAGE bit of IA32_VMX_EPT_VPID_CAP MSR.
 *
 * @param[in] level Page level to check
 * @param[in] prot  [Unused] Page properties
 *
 * @return Large page supported on given page level or not
 */
static inline bool ept_large_page_support(enum page_table_level level, __unused uint64_t prot)
{
	bool support;

	if (level == IA32E_PD) {
		support = true;
	} else if (level == IA32E_PDPT) {
		support = pcpu_has_vmx_ept_vpid_cap(VMX_EPT_1GB_PAGE);
	} else {
		support = false;
	}

	return support;
}

/**
 * @brief Flush EPT page table entry in cache
 *
 * Flush EPT page table entry in cache.
 *
 * @param[in] etry Pointer to the EPT entry
 *
 * @return None
 *
 * @pre etry != NULL
 */
static inline void ept_clflush_pagewalk(const void* etry)
{
	iommu_flush_cache(etry, sizeof(uint64_t));
}

/**
 * @brief Initialize the EPT page table for a given VM.
 *
 * This function sets up the EPT (Extended Page Table) page table for a specified VM. It initializes the EPT page pool,
 * sets up the page table structure, and configures mitigation for the "Machine Check Error on Page Size Change" issue
 * by setting up the tweak and recover functions for execute right.
 *
 * @param[inout] table Pointer to the pgtable structure to be initialized.
 * @param[in]    vm_id ID of the VM for which the EPT page table is being initialized.
 *
 * @return None
 */
void init_ept_pgtable(struct pgtable *table, uint16_t vm_id)
{
	ept_page_pool[vm_id].start_page = ept_pages[vm_id];
	ept_page_pool[vm_id].bitmap_size = EPT_PAGE_NUM / 64;
	ept_page_pool[vm_id].bitmap = ept_page_bitmap[vm_id];
	ept_page_pool[vm_id].dummy_page = &ept_dummy_pages[vm_id];

	spinlock_init(&ept_page_pool[vm_id].lock);
	(void)memset((void *)ept_page_pool[vm_id].bitmap, 0, ept_page_pool[vm_id].bitmap_size * sizeof(uint64_t));
	ept_page_pool[vm_id].last_hint_id = 0UL;

	table->pool = &ept_page_pool[vm_id];
	table->default_access_right = EPT_RWX;
	table->pgentry_present_mask = EPT_RWX;
	table->clflush_pagewalk = ept_clflush_pagewalk;
	table->large_page_support = ept_large_page_support;
}

/*
 * To enable the identical map and support of legacy devices/ACPI method in Service VM,
 * ACRN presents the entire host 0-4GB memory region to Service VM, except the memory
 * regions explicitly assigned to pre-launched VMs or HV (DRAM and MMIO). However,
 * virtual e820 only contains the known DRAM regions. For this reason,
 * we can't know if the GPA range is guest valid or not, by checking with
 * its ve820 tables only.
 */
/**
 * @brief Check if the GPA range is guest valid GPA or not
 *
 * Check if the GPA range is guest valid by whether the GPA range is mapped
 * in EPT pagetable or not.
 *
 * @param[in] vm          Pointer to the VM structure
 * @param[in] mr_base_gpa Start of the GPA rage
 * @param[in] mr_size     Size of the GPA rage
 *
 * @return Whether the GPA range is guest valid
 */
bool ept_is_valid_mr(struct acrn_vm *vm, uint64_t mr_base_gpa, uint64_t mr_size)
{
	bool present = true;
	uint32_t sz;
	uint64_t end = mr_base_gpa + mr_size, address = mr_base_gpa;

	while (address < end) {
		if (local_gpa2hpa(vm, address, &sz) == INVALID_HPA) {
			present = false;
			break;
		}
		address += sz;
	}

	return present;
}

/**
 * @brief Destroy EPT page table for VM
 *
 * This function destroys EPT page table of specified VM by clearing the root EPT page.
 *
 * @param[in] vm Pointer to the VM structure
 *
 * @return None
 */
void destroy_ept(struct acrn_vm *vm)
{
	if (vm->arch_vm.nworld_eptp != NULL) {
		(void)memset(vm->arch_vm.nworld_eptp, 0U, PAGE_SIZE);
	}
}

/**
 * @brief Get the EPT base address for a given VM
 *
 * @param[in] vm Pointer to the VM structure
 *
 * @return EPT base address
 *
 * @pre vm != NULL
 */
static inline void *get_eptp(struct acrn_vm *vm)
{
	return vm->arch_vm.nworld_eptp;
}

/**
 * @brief Translate Guest Physical Address to Host Physical Address and reads the EPT page size
 *
 * Translate Guest Physical Address to Host Physical Address by walking EPT of the VM, and reads the EPT page size
 * GPA belongs to.
 *
 * @param[in]  vm   Pointer to the VM structure
 * @param[in]  gpa  Guest Physical Address to be translated
 * @param[out] size Pointer to the size of the EPT page GPA belongs to
 *
 * @return Translated Host Physical Address, or INVALID_HPA if the GPA is not mapped in the VM.
 */
uint64_t local_gpa2hpa(struct acrn_vm *vm, uint64_t gpa, uint32_t *size)
{
	/* using return value INVALID_HPA as error code */
	uint64_t hpa = INVALID_HPA;
	const uint64_t *pgentry;
	uint64_t pg_size = 0UL;
	void *eptp;

	eptp = get_eptp(vm);
	pgentry = pgtable_lookup_entry((uint64_t *)eptp, gpa, &pg_size, &vm->arch_vm.ept_pgtable);
	if (pgentry != NULL) {
		hpa = (((*pgentry & (~EPT_PFN_HIGH_MASK)) & (~(pg_size - 1UL)))
				| (gpa & (pg_size - 1UL)));
	}

	/**
	 * If specified parameter size is not NULL and
	 * the HPA of parameter gpa is found, pg_size shall
	 * be returned through parameter size.
	 */
	if ((size != NULL) && (hpa != INVALID_HPA)) {
		*size = (uint32_t)pg_size;
	}

	return hpa;
}

/**
 * @brief Translate Guest Physical Address to Host Physical Address
 *
 * Translate Guest Physical Address to Host Physical Address by walking EPT of the VM
 *
 * @param[in]  vm   Pointer to the VM structure
 * @param[in]  gpa  Guest Physical Address to be translated
 *
 * @return Translated Host Physical Address or INVALID_HPA if the GPA is not mapped in the VM.
 */
uint64_t gpa2hpa(struct acrn_vm *vm, uint64_t gpa)
{
	return local_gpa2hpa(vm, gpa, NULL);
}

/**
 * @brief Translate Host Physical Address to Guest Physical Address for Service VM
 *
 * Translate Host Physical Address to Guest Physical Address for Service VM, as Service VM uses identical mapping,
 * HPA equals to GPA.
 *
 * @param[in]  hpa  Host Physical Address to be translated
 *
 * @return Translated Guest Physical Address
 */
uint64_t service_vm_hpa2gpa(uint64_t hpa)
{
	return hpa;
}

/**
 * @brief VM-Exit handler for VM-Exit caused by EPT misconfiguration
 *
 * ACRN doesn't handle EPT misconfiguration. Print the GPA and HPA in log, and add a trace event for it.
 *
 * @param[in] vcpu [Unused] Pointer to the vCPU structure
 *
 * @return A value indicates the status of VM-Exit handling.
 *
 * @retval -EINVAL Always return -EINVAL as ACRN doesn't handle EPT misconfiguration.
 */
int32_t ept_misconfig_vmexit_handler(__unused struct acrn_vcpu *vcpu)
{
	int32_t status;

	status = -EINVAL;

	/* TODO - EPT Violation handler */
	pr_fatal("%s, Guest linear address: 0x%016lx ",
			__func__, exec_vmread(VMX_GUEST_LINEAR_ADDR));

	pr_fatal("%s, Guest physical address: 0x%016lx ",
			__func__, exec_vmread64(VMX_GUEST_PHYSICAL_ADDR_FULL));

	ASSERT(status == 0, "EPT Misconfiguration is not handled.\n");

	TRACE_2L(TRACE_VMEXIT_EPT_MISCONFIGURATION, 0UL, 0UL);

	return status;
}

/**
 * @brief Flush EPT TLB for given VM
 *
 * Flush EPT TLB for given VM by making a ACRN_REQUEST_EPT_FLUSH request on VM's each vCPU
 *
 * @param[in] vm Pointer to the VM structure
 *
 * @return None
 */
static inline void ept_flush_guest(struct acrn_vm *vm)
{
	uint16_t i;
	struct acrn_vcpu *vcpu;
	/* Here doesn't do the real flush, just makes the request which will be handled before vcpu vmenter */
	foreach_vcpu(i, vm, vcpu) {
		vcpu_make_request(vcpu, ACRN_REQUEST_EPT_FLUSH);
	}
}

/**
 * @brief Add a memory region to the EPT (Extended Page Table) of a VM
 *
 * This function maps a host physical address (HPA) to a guest physical address (GPA) with given property in the EPT
 * of the specified virtual machine (VM).
 *
 * @param[in] vm        Pointer to the VM structure.
 * @param[in] pml4_page Root pointer to the EPT page table.
 * @param[in] hpa       Host physical address to be mapped.
 * @param[in] gpa       Guest physical address to map to.
 * @param[in] size      Size of the memory region to be mapped.
 * @param[in] prot_orig Properties for the memory region.
 *
 * @return None
 *
 * @pre vm != NULL && pml4_page != NULL
 */
void ept_add_mr(struct acrn_vm *vm, uint64_t *pml4_page,
	uint64_t hpa, uint64_t gpa, uint64_t size, uint64_t prot_orig)
{
	uint64_t prot = prot_orig;

	pr_info("%s, vm[%d] hpa: 0x%016lx gpa: 0x%016lx size: 0x%016lx prot: 0x%016lx\n",
			__func__, vm->vm_id, hpa, gpa, size, prot);

	spinlock_obtain(&vm->ept_lock);

	pgtable_add_map(pml4_page, hpa, gpa, size, prot, &vm->arch_vm.ept_pgtable);

	spinlock_release(&vm->ept_lock);

	ept_flush_guest(vm);
}

/**
 * @brief Modify memory region in EPT (Extended Page Tables) of a VM
 *
 * This function modifies the properties of a memory region mapped in the EPT  of the specified virtual machine (VM).
 *
 * @param[in] vm        Pointer to the VM structure.
 * @param[in] pml4_page Root pointer to the EPT page table.
 * @param[in] gpa       Guest physical address to be modified.
 * @param[in] size      Size of the memory region to be modified.
 * @param[in] prot_set  Properties to set
 * @param[in] prot_clr  Properties to clear
 *
 * @return None
 *
 * @pre vm != NULL && pml4_page != NULL
 * @pre ept_is_valid_mr(vm, gpa, size) == true
 */
void ept_modify_mr(struct acrn_vm *vm, uint64_t *pml4_page,
		uint64_t gpa, uint64_t size,
		uint64_t prot_set, uint64_t prot_clr)
{
	uint64_t local_prot = prot_set;

	dev_dbg(DBG_LEVEL_EPT, "%s,vm[%d] gpa 0x%lx size 0x%lx\n", __func__, vm->vm_id, gpa, size);

	spinlock_obtain(&vm->ept_lock);

	pgtable_modify_or_del_map(pml4_page, gpa, size, local_prot, prot_clr, &(vm->arch_vm.ept_pgtable), MR_MODIFY);

	spinlock_release(&vm->ept_lock);

	ept_flush_guest(vm);
}

/**
 * @brief Delete a memory region from the EPT (Extended Page Table) of a VM.
 *
 * This function removes a specified memory region from the EPT of a given virtual machine (VM).s
 *
 * @param[in] vm        Pointer to the VM structure.
 * @param[in] pml4_page Root pointer to the EPT page table.
 * @param[in] gpa       Guest physical address of the memory region to be deleted.
 * @param[in] size      Size of the memory region to be deleted.
 *
 * @return None
 *
 * @pre vm != NULL && pml4_page != NULL
 * @pre ept_is_valid_mr(vm, gpa, size) == true
 */
void ept_del_mr(struct acrn_vm *vm, uint64_t *pml4_page, uint64_t gpa, uint64_t size)
{
	pr_info("%s,vm[%d] gpa 0x%lx size 0x%lx\n", __func__, vm->vm_id, gpa, size);

	spinlock_obtain(&vm->ept_lock);

	pgtable_modify_or_del_map(pml4_page, gpa, size, 0UL, 0UL, &(vm->arch_vm.ept_pgtable), MR_DEL);

	spinlock_release(&vm->ept_lock);

	ept_flush_guest(vm);
}

/**
 * @brief Flushes a leaf page in the Extended Page Table (EPT).
 *
 * This function flushes a leaf page in the EPT by invalidating the cache for the specified page if it is not marked
 * as uncached.
 *
 * @param[in] pge  Pointer to the EPT entry for the page.
 * @param[in] size Size of the page to be flushed.
 *
 * @return None
 *
 * @pre pge != NULL && size > 0
 */
void ept_flush_leaf_page(uint64_t *pge, uint64_t size)
{
	uint64_t base_hpa;

	if ((*pge & EPT_MT_MASK) != EPT_UNCACHED) {
		base_hpa = (*pge & (~(size - 1UL)));

		stac();
		flush_cache_range(hpa2hva(base_hpa), size);
		clac();
	}
}

/**
 * @brief Walk through the Extended Page Tables (EPT) of a given VM and apply a callback function to each page entry.
 *
 * This function traverses the EPT of the specified virtual machine (VM) and applies the provided callback function
 * to each page entry. The traversal is performed in a hierarchical manner, starting from the PML4E level down to
 * the PTE level.
 *
 * @param[in] vm Pointer to the VM structure whose EPT is to be walked
 * @param[in] cb Callback function to be applied to each page entry
 *
 * @return None
 *
 * @pre vm != NULL && cb != NULL
 */
void walk_ept_table(struct acrn_vm *vm, pge_handler cb)
{
	const struct pgtable *table = &vm->arch_vm.ept_pgtable;
	uint64_t *pml4e, *pdpte, *pde, *pte;
	uint64_t i, j, k, m;

	for (i = 0UL; i < PTRS_PER_PML4E; i++) {
		pml4e = pml4e_offset((uint64_t *)get_eptp(vm), i << PML4E_SHIFT);
		if (!pgentry_present(table, (*pml4e))) {
			continue;
		}
		for (j = 0UL; j < PTRS_PER_PDPTE; j++) {
			pdpte = pdpte_offset(pml4e, j << PDPTE_SHIFT);
			if (!pgentry_present(table, (*pdpte))) {
				continue;
			}
			if (pdpte_large(*pdpte) != 0UL) {
				cb(pdpte, PDPTE_SIZE);
				continue;
			}
			for (k = 0UL; k < PTRS_PER_PDE; k++) {
				pde = pde_offset(pdpte, k << PDE_SHIFT);
				if (!pgentry_present(table, (*pde))) {
					continue;
				}
				if (pde_large(*pde) != 0UL) {
					cb(pde, PDE_SIZE);
					continue;
				}
				for (m = 0UL; m < PTRS_PER_PTE; m++) {
					pte = pte_offset(pde, m << PTE_SHIFT);
					if (pgentry_present(table, (*pte))) {
						cb(pte, PTE_SIZE);
					}
				}
			}
			/*
			 * Walk through the whole page tables of one VM is a time-consuming
			 * operation. Preemption is not support by hypervisor scheduling
			 * currently, so the walk through page tables operation might occupy
			 * CPU for long time what starve other threads.
			 *
			 * Give chance to release CPU to make other threads happy.
			 */
			if (need_reschedule(get_pcpu_id())) {
				schedule();
			}
		}
	}
}

/**
 * @}
 */
