/*
 * Copyright (C) 2019-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @addtogroup vp-base_vm
 *
 * @{
 */

/**
 * @file
 * @brief VM virtual E820 management functions.
 *
 * This file contains implementation for managing VM's virtual E820
 * table, including initializing, editing and removing virtual E820
 * entries of a VM.
 */

#include <asm/e820.h>
#include <asm/mmu.h>
#include <asm/guest/vm.h>
#include <asm/guest/ept.h>
#include <reloc.h>
#include <vacpi.h>
#include <logmsg.h>
#include <ptdev.h>

/**
 * @brief GPA low address index
 *
 * Index to entry for low RAM address below 1MB in pre_ve820_template
 */
#define ENTRY_GPA_L		2U
/**
 * @brief GPA high address index
 *
 * Index to entry for high RAM address above 4GB in pre_ve820_template
 */
#define ENTRY_GPA_HI		7U

/**
 * @brief E820 entries array for Service VM
 *
 * This array records E820 entries for Service VM, with
 * a maximum of E820_MAX_ENTRIES entry.
 */
static struct e820_entry service_vm_e820[E820_MAX_ENTRIES];
/**
 * @brief E820 entries array for Pre-Launched VM
 *
 * This 2D array records E820 entries for a total of PRE_VM_NUM Pre-Launched VMs,
 * and a maximum of E820_MAX_ENTRIES for each Pre-Launched VM.
 */
static struct e820_entry pre_vm_e820[PRE_VM_NUM][E820_MAX_ENTRIES];

/**
 * @brief Find available memory region from virtual E820 table.
 *
 * This function tries to find a consecutive memory region from virtual E820 table
 * of \p vm. The search is confined within a search window with \p min_addr
 * as lower bound and \p max_addr as higher bound.
 *
 * The function checks all E820 RAM entries against search window, to calculate if
 * they have an intersection region with at least page up-rounded \p size bytes.
 * If it does, the start GPA of the highest page up-rounded \p size bytes in that
 * intersection region is returned.
 *
 * @remark This function is not responsible for book-keeping allocated memory region across
 * various calls, it simply returns a region that satisfies specified condition.
 * It is the caller's responsibility to synchronize the usage of those regions.
 *
 * @pre vm != NULL
 * @post N/A
 *
 * @param[in] vm The VM whose virtual E820 table to be searched
 * @param[in] size Size of the region
 * @param[in] min_addr The lower bound of search window
 * @param[in] max_addr The higher bound of search window
 *
 * @return Start GPA of the found memory region. INVALID_GPA if no available region was found.
 */
uint64_t find_space_from_ve820(struct acrn_vm *vm, uint32_t size, uint64_t min_addr, uint64_t max_addr)
{
	int32_t i;
	uint64_t gpa = INVALID_GPA;
	uint64_t round_min_addr = round_page_up(min_addr);
	uint64_t round_max_addr = round_page_down(max_addr);
	uint32_t round_size = round_page_up(size);

	for (i = (int32_t)(vm->e820_entry_num - 1U); i >= 0; i--) {
		struct e820_entry *entry = vm->e820_entries + i;
		uint64_t start, end, length;

		start = round_page_up(entry->baseaddr);
		end = round_page_down(entry->baseaddr + entry->length);
		length = (end > start) ? (end - start) : 0UL;

		if ((entry->type == E820_TYPE_RAM) && (length >= (uint64_t)round_size)
				&& (end > round_min_addr) && (start < round_max_addr)) {
			if (((start >= min_addr) && ((start + round_size) <= min(end, round_max_addr)))
				|| ((start < min_addr) && ((min_addr + round_size) <= min(end, round_max_addr)))) {
				gpa = (end > round_max_addr) ? (round_max_addr - round_size) : (end - round_size);
				break;
			}
		}
	}
	return gpa;
}

/**
 * @brief Sort VM's virtual E820 table from lowest to highest.
 *
 * This function sorts a VM's virtual E820 table according to entry base address,
 * from lowest to highest.
 *
 * @pre vm != NULL
 * @post N/A
 *
 * @param[inout] vm The VM whose virtual E820 table to be sorted
 *
 * @remark a sorted VM e820 table is critical for memory allocation or slot location,
 * for example, put ramdisk at the end of TOLUD(Top of LOW Usable DRAM) and
 * put kernel at its beginning to provide largest load capacity.
 *
 * @return None
 */
static void sort_vm_e820(struct acrn_vm *vm)
{
	uint32_t i,j;
	struct e820_entry tmp_entry;

	/* Bubble sort */
	for (i = 0U; i < (vm->e820_entry_num - 1U); i++) {
		for (j = 0U; j < (vm->e820_entry_num - i - 1U); j++) {
			if (vm->e820_entries[j].baseaddr > vm->e820_entries[j + 1U].baseaddr) {
				tmp_entry = vm->e820_entries[j];
				vm->e820_entries[j] = vm->e820_entries[j + 1U];
				vm->e820_entries[j + 1U] = tmp_entry;
			}
		}
	}
}

/**
 * @brief Filter memory regions from Service VM virtual E820
 *
 * This function filters memory region from start_pa to end_pa from virtual E820.
 * The removal action might cause original RAM E820 entry to be shrinked, splitted
 * or type-changed. The removed region is guaranteed not to be of type E820_TYPE_RAM,
 * but it is not necessarily marked as E820_TYPE_RESERVED. It is possible that
 * part of the region forms an E820 hole (the region is not described in E820 table
 * at all).
 *
 * @pre vm != NULL && is_service_vm(vm) == true
 * @post N/A
 *
 * @param[inout] vm The VM whose virtual E820 table to be filtered
 * @param[in] start_pa Start address of memory region to be filtered
 * @param[in] end_pa End address of memory region to be filtered
 *
 * @return None
 */
static void filter_mem_from_service_vm_e820(struct acrn_vm *vm, uint64_t start_pa, uint64_t end_pa)
{
	uint32_t i;
	uint64_t entry_start;
	uint64_t entry_end;
	uint32_t entries_count = vm->e820_entry_num;
	struct e820_entry *entry, new_entry = {0};

	for (i = 0U; i < entries_count; i++) {
		entry = &service_vm_e820[i];
		entry_start = entry->baseaddr;
		entry_end = entry->baseaddr + entry->length;

		/* No need handle in these cases*/
		if ((entry->type != E820_TYPE_RAM) || (entry_end <= start_pa) || (entry_start >= end_pa)) {
			continue;
		}

		/* filter out the specific memory and adjust length of this entry*/
		if ((entry_start < start_pa) && (entry_end <= end_pa)) {
			entry->length = start_pa - entry_start;
			continue;
		}

		/* filter out the specific memory and need to create a new entry*/
		if ((entry_start < start_pa) && (entry_end > end_pa)) {
			entry->length = start_pa - entry_start;
			new_entry.baseaddr = end_pa;
			new_entry.length = entry_end - end_pa;
			new_entry.type = E820_TYPE_RAM;
			continue;
		}

		/* This entry is within the range of specific memory
		 * change to E820_TYPE_RESERVED
		 */
		if ((entry_start >= start_pa) && (entry_end <= end_pa)) {
			entry->type = E820_TYPE_RESERVED;
			continue;
		}

		if ((entry_start >= start_pa) && (entry_start < end_pa) && (entry_end > end_pa)) {
			entry->baseaddr = end_pa;
			entry->length = entry_end - end_pa;
			continue;
		}
	}

	if (new_entry.length > 0UL) {
		entries_count++;
		ASSERT(entries_count <= E820_MAX_ENTRIES, "e820 entry overflow");
		entry = &service_vm_e820[entries_count - 1U];
		entry->baseaddr = new_entry.baseaddr;
		entry->length = new_entry.length;
		entry->type = new_entry.type;
		vm->e820_entry_num = entries_count;
	}

}

/**
 * @brief Create virtual E820 table for Service VM.
 *
 * This function creates virtual E820 table for Service VM. The Service VM
 * virtual E820 table uses physical E820 table as base, and filters out
 * the Pre-Launched VM owned memory regions and hypervisor owned regions.
 *
 * The vE820 table is sorted by calling `sort_vm_e820` before returning.
 *
 * @pre vm != NULL && is_service_vm(vm) == true
 * @post N/A
 *
 * @param[inout] vm The VM whose virtual E820 table to be created
 *
 * @return None
 */
void create_service_vm_e820(struct acrn_vm *vm)
{
	uint16_t vm_id;
	uint32_t i;
	uint32_t entries_count = get_e820_entries_count();
	struct acrn_vm_config *service_vm_config = get_vm_config(vm->vm_id);

	(void)memcpy_s((void *)service_vm_e820, entries_count * sizeof(struct e820_entry),
			(const void *)get_e820_entry(), entries_count * sizeof(struct e820_entry));

	vm->e820_entry_num = entries_count;
	vm->e820_entries = service_vm_e820;

	/* filter out prelaunched vm memory from e820 table */
	for (vm_id = 0U; vm_id < CONFIG_MAX_VM_NUM; vm_id++) {
		struct acrn_vm_config *vm_config = get_vm_config(vm_id);

		if (vm_config->load_order == PRE_LAUNCHED_VM) {
			for (i = 0; i < vm_config->memory.region_num; i++) {
				filter_mem_from_service_vm_e820(vm, vm_config->memory.host_regions[i].start_hpa,
						vm_config->memory.host_regions[i].start_hpa
						+ vm_config->memory.host_regions[i].size_hpa);
			}
		}
	}

	for (i = 0U; i < vm->e820_entry_num; i++) {
		struct e820_entry *entry = &service_vm_e820[i];
		if (entry->type == E820_TYPE_RAM) {
			service_vm_config->memory.size += entry->length;
		}
	}
	sort_vm_e820(vm);
}

/**
 * @brief Pre-Launch VM virtual E820 template
 *
 * This array stores template vE820 table for Pre-Launched VMs.
 * vE820 layout for pre-launched VM:
 *
 * - entry0: usable under 1MB
 * - entry1: reserved for MP Table/ACPI RSDP from 0xf0000 to 0xfffff
 * - entry2: hpa_low
 * - entry3: gpu_opregion (0x5000)
 * - entry4: ACPI Reclaim from 0x7fe00000 to 0x7fefffff (1M)
 * - entry5: ACPI NVS from 0x7ff00000 to 0x7fffffff (1M)
 *   -# First 64k reserved
 *   -# (CONFIG_SECURITY_VM_FIXUP=y) SMBIOS table in between 64k and 0xb0000
 *   -# (CONFIG_SECURITY_VM_FIXUP=y) TPM2 event log region, if supported by platform,
 *   is placed at 0x7ffb0000 to 0x7fffffff
 * - entry6: reserved for 32bit PCI hole from 0x80000000 to 0xffffffff
 * - (entry7): usable for hpa_hi, if available (above 4G)
 */
static const struct e820_entry pre_ve820_template[E820_MAX_ENTRIES] = {
	{	/* usable RAM under 1MB */
		.baseaddr = 0x0UL,
		.length   = 0xA0000UL,		/* 640KB */
		.type     = E820_TYPE_RAM
	},
	{	/* Video/BIOS extentions */
		.baseaddr = 0xA0000UL,
		.length   = 0x60000UL,		/* 384KB */
		.type     = E820_TYPE_RESERVED
	},
	{	/* hpa_low */
		.baseaddr = MEM_1M,		/* 1MB */
		.length   = GPU_OPREGION_GPA - MEM_1M,
		.type     = E820_TYPE_RAM
	},
	{	/* GPU OpRegion for pre-launched VM */
		.baseaddr = GPU_OPREGION_GPA,
		.length   = GPU_OPREGION_SIZE,
		.type     = E820_TYPE_RESERVED
	},
	{	/* ACPI Reclaim */
		.baseaddr = VIRT_ACPI_DATA_ADDR,/* consecutive from 0x7fe00000UL */
		.length   = VIRT_ACPI_DATA_LEN,
		.type	  = E820_TYPE_ACPI_RECLAIM
	},
	{	/* ACPI NVS */
		.baseaddr = VIRT_ACPI_NVS_ADDR,	/* consecutive after ACPI Reclaim */
		.length   = VIRT_ACPI_NVS_LEN,
		.type	  = E820_TYPE_ACPI_NVS
	},
	{	/* 32bit PCI hole */
		.baseaddr = 0x80000000UL,	/* 2048MB */
		.length   = MEM_2G,
		.type     = E820_TYPE_RESERVED
	},
};

/**
 * @brief Calculate total size of memory regions
 *
 * This function calculates the total size of an array of vm_hpa_regions with
 * \p num elements.
 *
 * @pre regions != NULL
 * @post N/A
 *
 * @param[in] regions VM HPA regions to calculate
 * @param[in] num Number of elements in \p regions
 *
 * @return Total size of regions.
 */
static inline uint64_t calculate_memory_size(struct vm_hpa_regions *regions, uint64_t num)
{
	uint64_t i;
	uint64_t size = 0;

	for(i = 0; i < num; i++) {
		size += regions[i].size_hpa;
	}

	return size;
}

/**
 * @brief Initialize an E820 entry to be E820_TYPE_RAM with given memory region.
 *
 * Initialize an E820 entry \p entry as type E820_TYPE_RAM, base GPA \p gpa and length
 * \p length.
 *
 * @pre entry != NULL
 * @post N/A
 *
 * @param[out] entry Output entry pointer
 * @param[in] gpa Start GPA of the region
 * @param[in] length Length of the region
 *
 * @return None
 */
static inline void add_ram_entry(struct e820_entry *entry, uint64_t gpa, uint64_t length)
{
	entry->baseaddr = gpa;
	entry->length = length;
	entry->type = E820_TYPE_RAM;
}

/**
 * @brief Create virtual E820 table for Pre-Launched VM
 *
 * Create virtual E820 table for Pre-Launched VM.
 * This function uses pre_ve820_template as base vE820, and
 * adjust/add RAM entries to match the actual memory size specified for the VM.
 *
 * - `--------------------- 0`
 * - First 1MB (Usable)
 * - `--------------------- 1MB`
 * - Low RAM (Usable)
 * - `--------------------- GPU_OPREGION_GPA`
 * - GPU_OPREGION
 * - `--------------------- VIRT_ACPI_DATA_ADDR`
 * - ACPI NVS/DATA (Usable)
 * - `--------------------- 2GB`
 * - PCI memory hole
 * - `--------------------- 4GB`
 * - High RAM (Usable)
 *
 * As shown above, the four "Usable" region (First 1MB, Low RAM, ACPI NVS/DATA, High RAM)
 * should add up to specified memory size of the VM.
 *
 * If the specified VM memory size is less than the first three added up, the
 * Low RAM region will be adjusted to the actual size (total_size - 1MB - ACPI_NVS/DATA).
 *
 * If the specified VM memory size is greater than the first three added up, the
 * High RAM entry will be created to host the remaining memory size
 * (total_size - 2GB + GPU_OPREGION).
 *
 * The vE820 table is sorted by calling `sort_vm_e820` before returning.
 *
 * @pre vm != NULL && is_prelaunched_vm(vm) == true
 * @post N/A
 *
 * @param[inout] vm The VM whose virtual E820 table to be setup
 *
 * @return None
 */
void create_prelaunched_vm_e820(struct acrn_vm *vm)
{
	struct acrn_vm_config *vm_config = get_vm_config(vm->vm_id);
	uint64_t gpa_start = 0x100000000UL;
	uint64_t gpa_hi_size;
	uint64_t lowmem_max_length = MEM_2G - GPU_OPREGION_SIZE;
	uint32_t entry_idx = ENTRY_GPA_HI;
	uint64_t memory_size = calculate_memory_size(vm_config->memory.host_regions, vm_config->memory.region_num);

	vm->e820_entries = pre_vm_e820[vm->vm_id];
	(void)memcpy_s((void *)vm->e820_entries,  E820_MAX_ENTRIES * sizeof(struct e820_entry),
		(const void *)pre_ve820_template, E820_MAX_ENTRIES * sizeof(struct e820_entry));

	if (memory_size > lowmem_max_length) {
		gpa_hi_size = memory_size - lowmem_max_length;
		add_ram_entry((vm->e820_entries + entry_idx), gpa_start, gpa_hi_size);
		entry_idx++;
	} else {
		/* need to revise length of hpa_low entry to its actual size, excluding size of used space */
		vm->e820_entries[ENTRY_GPA_L].length = memory_size - MEM_1M - VIRT_ACPI_DATA_LEN - VIRT_ACPI_NVS_LEN;
	}

	vm->e820_entry_num = entry_idx;
	sort_vm_e820(vm);
}

/**
 * @}
 */
