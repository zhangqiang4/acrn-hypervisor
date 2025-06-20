/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <acrn_hv_defs.h>
#include <asm/page.h>
#include <asm/e820.h>
#include <asm/mmu.h>
#include <boot.h>
#include <efi_mmap.h>
#include <logmsg.h>
#include <asm/guest/ept.h>

/**
 * @addtogroup hwmgmt_memory
 *
 * @{
 */

/**
 * @file
 * @brief E820 memory map management functions
 *
 * This file contains the implementations of functions that manage the E820 memory map, including initialization
 * from EFI or multiboot, allocation of memory regions, and retrieval of memory information. The hypervisor will
 * get memory info from its MMU setup and its memory will be hide from Service VM.
 */

/**
 * @brief Global variable indicating number of E820 entries in hypervisor.
 *
 * @remark The value of this global variable shall be no greater than E820_MAX_ENTRIES.
 */
static uint32_t hv_e820_entries_nr;

/**
 * @brief Global array containing E820 entries in the hypervisor.
 *
 * This array describes the memory layout the hypervisor uses. The number of valid entries is recorded in the global
 * variable \p hv_e820_entries_nr. The maximum of entries is limited as \p E820_MAX_ENTRIES.
 */
static struct e820_entry hv_e820[E820_MAX_ENTRIES];

#define DBG_LEVEL_E820	6U /**< Debug log level for the code of E820 */

/**
 * @brief Allocate memory from E820 map by finding and reserving a suitable RAM region.
 *
 * This function searches the E820 memory map from highest to lowest entry for a suitable RAM region
 * that meets the specified size and maximum address constraints. When a suitable region is found, it
 * is marked as reserved. The function handles two cases:
 * 1. If it finds an exact-size match, it marks the entire entry as reserved.
 * 2. If it finds a larger entry, it creates a new E820 entry by taking memory from the end of the found region.
 *
 * The function requires 4K alignment, so the input size is rounded up to the next page boundary. If no
 * suitable memory region is found or the only available address would be 0, the hypervisor enters a
 * safety state by calling panic().
 *
 * @param[in] size_arg Size of memory to be found and marked as reserved (will be rounded up to page boundary).
 * @param[in] max_addr Maximum address below which memory is to be allocated.
 *
 * @return Base address of the memory region allocated from E820.
 *
 * @retval INVALID_HPA  Failed: No memory available.
 * @retval 0            Failed: Memory would be allocated at address 0, which is not allowed.
 * @retval other value  Success: Base address of the reserved memory region.
 * 
 * @pre hv_e820_entries_nr > 0U
 *
 * @post retval != 0 && retval != INVALID_HPA
 */
uint64_t e820_alloc_memory(uint64_t size_arg, uint64_t max_addr)
{
	int32_t i;
	uint64_t size = round_page_up(size_arg);
	uint64_t ret = INVALID_HPA;
	struct e820_entry *entry, *new_entry;

	for (i = (int32_t)hv_e820_entries_nr - 1; i >= 0; i--) {
		entry = &hv_e820[i];
		uint64_t start, end, length;

		start = round_page_up(entry->baseaddr);
		end = round_page_down(entry->baseaddr + entry->length);
		length = (end > start) ? (end - start) : 0UL;

		if ((entry->type == E820_TYPE_RAM) && (length >= size) && ((start + size) <= max_addr)) {


			/* found exact size of E820 entry */
			if (length == size) {
				entry->type = E820_TYPE_RESERVED;
				ret = start;
			} else {

				/*
				 * found entry with available memory larger than requested (length > size)
				 * Reserve memory if
				 * 1) hv_e820_entries_nr < E820_MAX_ENTRIES
				 * 2) if end of this "entry" is <= max_addr
				 *    use memory from end of this E820 "entry".
				 */

				if ((hv_e820_entries_nr < E820_MAX_ENTRIES) && (end <= max_addr)) {

					new_entry = &hv_e820[hv_e820_entries_nr];
					new_entry->type = E820_TYPE_RESERVED;
					new_entry->baseaddr = end - size;
					new_entry->length = (entry->baseaddr + entry->length) - new_entry->baseaddr;
					/* Shrink the existing entry and total available memory */
					entry->length -= new_entry->length;
					hv_e820_entries_nr++;

				        ret = new_entry->baseaddr;
				}
			}

			if (ret != INVALID_HPA) {
				break;
			}
		}
	}

	if ((ret == INVALID_HPA) || (ret == 0UL)) {
		/* current memory allocation algorithm is to find the available address from the highest
		 * possible address below max_addr. if ret == 0, means all memory is used up and we have to
		 * put the resource at address 0, this is dangerous.
		 * Also ret == 0 would make code logic very complicated, since memcpy_s() doesn't support
		 * address 0 copy.
		 */
		panic("Requested memory from E820 cannot be reserved!!");
	}

	return ret;
}

/**
 * @brief Insert one E820 entry into the global hypervisor entry array.
 *
 * This function inserts a new E820 entry at the specified index in the E820 table. It moves all entries after the
 * specified index down by one to make room for the new entry. The total number of entries and the index shall not
 * exceed the maximum number of entries supported by the hypervisor, otherwise this function will return the error
 * number.
 *
 * @param[in] index Index at which to insert the new entry.
 * @param[in] addr Base address of the new entry.
 * @param[in] length Length of the new entry.
 * @param[in] type Type of the new entry.
 *
 * @return err The error code indicating the result of the operation.
 *
 * @retval 0        Success
 * @retval -EINVAL  E820 entry overflow.
 *
 * @pre hv_e820_entries_nr < E820_MAX_ENTRIES
 * @pre index <= hv_e820_entries_nr
 *
 * @post hv_e820_entries_nr <= E820_MAX_ENTRIES
 */
static int32_t insert_e820_entry(uint32_t index, uint64_t addr, uint64_t length, uint32_t type)
{
	uint32_t i;
	int32_t err = 0;

	hv_e820_entries_nr++;
	if ((hv_e820_entries_nr > E820_MAX_ENTRIES) || (index >= hv_e820_entries_nr)) {
		pr_err("%s E820 entry overflow, E820_MAX_ENTRIES: %d", __func__, E820_MAX_ENTRIES);
		err = -EINVAL;
	}
	for (i = hv_e820_entries_nr - 1U; i > index; i--) {
		hv_e820[i] = hv_e820[i - 1U];
	}

	hv_e820[index].baseaddr = addr;
	hv_e820[index].length = length;
	hv_e820[index].type = type;

	return err;
}

#ifdef CONFIG_MULTIBOOT2
/**
 * @brief Initialize the E820 table from EFI memory map.
 *
 * This function initializes the E820 table by reading the memory map from EFI if booted from UEFI. It iterates
 * through the EFI memory map entries and maps them to the E820 table. The EFI memory map is sorted by address
 * and type, so the E820 table will also be sorted. The function also merges contiguous memory regions of the
 * same type to optimize the E820 table.
 *
 * This function processes each EFI memory map entry and determines the corresponding E820 type based on the EFI
 * memory type and attributes. The mapping logic is as follows:
 * - EFI_LOADER_CODE, EFI_LOADER_DATA, EFI_CONVENTIONAL_MEMORY(if EFI_MEMORY_WB attribute is set) are mapped to
 *   E820_TYPE_RAM.
 * - EFI_UNUSABLE_MEMORY is mapped to E820_TYPE_UNUSABLE.
 * - EFI_ACPI_RECLAIM_MEMORY is mapped to E820_TYPE_ACPI_RECLAIM.
 * - EFI_ACPI_MEMORY_NVS is mapped to E820_TYPE_ACPI_NVS.
 * - All other types are mapped to E820_TYPE_RESERVED.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark The original EFI memory map could be above 4GB address space, so the EFI memory parsing process must be
 * done after enable_paging().
 */
static void init_e820_from_efi_mmap(void)
{
	uint32_t i, e820_idx = 0U;
	const struct efi_memory_desc *efi_mmap_entry = get_efi_mmap_entry();

	for (i = 0U; i < get_efi_mmap_entries_count(); i++) {
		if (e820_idx >= E820_MAX_ENTRIES) {
			pr_err("Too many efi memmap entries !");
			break;
		}

		hv_e820[e820_idx].baseaddr = efi_mmap_entry[i].phys_addr;
		hv_e820[e820_idx].length = efi_mmap_entry[i].num_pages * PAGE_SIZE;

		/* The EFI BOOT Service related regions need to be set to reserved and avoid being touched by
		 * hypervisor, because at least below software modules rely on them:
		 * 1. EFI ESRT(The EFI System Resource Table) which used for UEFI firmware upgrade;
		 * 2. Image resource in ACPI BGRT(Boottime Graphics Resource Table) which used for boot time logo;
		 */
		switch (efi_mmap_entry[i].type)	{
		case EFI_LOADER_CODE:
		case EFI_LOADER_DATA:
		case EFI_CONVENTIONAL_MEMORY:
			if ((efi_mmap_entry[i].attribute & EFI_MEMORY_WB) != 0UL) {
				hv_e820[e820_idx].type = E820_TYPE_RAM;
			} else {
				hv_e820[e820_idx].type = E820_TYPE_RESERVED;
			}
			break;
		case EFI_UNUSABLE_MEMORY:
			hv_e820[e820_idx].type = E820_TYPE_UNUSABLE;
			break;
		case EFI_ACPI_RECLAIM_MEMORY:
			hv_e820[e820_idx].type = E820_TYPE_ACPI_RECLAIM;
			break;
		case EFI_ACPI_MEMORY_NVS:
			hv_e820[e820_idx].type = E820_TYPE_ACPI_NVS;
			break;
		/* case EFI_RESERVED_MEMORYTYPE:
		 * case EFI_BOOT_SERVICES_CODE:
		 * case EFI_BOOT_SERVICES_DATA:
		 * case EFI_RUNTIME_SERVICES_CODE:
		 * case EFI_RUNTIME_SERVICES_DATA:
		 * case EFI_MEMORYMAPPED_IO:
		 * case EFI_MEMORYMAPPED_IOPORTSPACE:
		 * case EFI_PALCODE:
		 * case EFI_PERSISTENT_MEMORY:
		 */
		default:
			hv_e820[e820_idx].type = E820_TYPE_RESERVED;
			break;
		}

		/* Given the efi memmap has been sorted, the hv_e820[] is also sorted.
		 * Then the algorithm is very simple, just merge with previous mmap entry
		 * if type is same and base addr is continuous.
		 */
		if ((e820_idx > 0U) && (hv_e820[e820_idx].type == hv_e820[e820_idx - 1U].type)
				&& (hv_e820[e820_idx].baseaddr ==
					(hv_e820[e820_idx - 1U].baseaddr
					+ hv_e820[e820_idx - 1U].length))) {
			hv_e820[e820_idx - 1U].length += hv_e820[e820_idx].length;
		} else {
			dev_dbg(DBG_LEVEL_E820, "efi mmap hv_e820[%d]: type: 0x%x Base: 0x%016lx length: 0x%016lx",
			    e820_idx, hv_e820[e820_idx].type, hv_e820[e820_idx].baseaddr, hv_e820[e820_idx].length);
			e820_idx ++;
		}

	}

	hv_e820_entries_nr = e820_idx;

}
#endif

/**
 * @brief Initialize the E820 table from multiboot memory map.
 *
 * This function initializes the E820 table by reading the memory map from the multiboot header if it is booted from
 * multiboot. It iterates through the multiboot memory map entries and maps them to the E820 table.
 *
 * @param[in] abi Pointer to the ACRN boot information structure, which contains the memory map information.
 *
 * @return None
 *
 * @pre abi != NULL
 *
 * @post N/A
 */
static void init_e820_from_mmap(struct acrn_boot_info *abi)
{
	uint32_t i;

	struct abi_mmap *mmap = abi->mmap_entry;

	hv_e820_entries_nr = abi->mmap_entries;

	dev_dbg(DBG_LEVEL_E820, "mmap addr 0x%p entries %u\n",
		abi->mmap_entry, hv_e820_entries_nr);

	for (i = 0U; i < hv_e820_entries_nr; i++) {

		hv_e820[i].baseaddr = mmap[i].baseaddr;
		hv_e820[i].length = mmap[i].length;
		hv_e820[i].type = mmap[i].type;

		dev_dbg(DBG_LEVEL_E820, "mmap hv_e820[%d]: type: 0x%x Base: 0x%016lx length: 0x%016lx", i,
			mmap[i].type, mmap[i].baseaddr, mmap[i].length);
	}
}

/**
 * @brief Reserve specified region in the E820 table.
 *
 * This function reserves the specified region in the E820 table. It iterates through the E820 entries to find the
 * entry that contains the specified region. If the region is completely contained within an entry and this entry
 * has a type of E820_TYPE_RAM, the entry is split into two entries. If the region crosses multiple entries, the
 * function will panic.
 *
 * @param[in] start_hpa Start address of the region to be reserved.
 * @param[in] end_hpa End address of the region to be reserved.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
static void reserve_e820_region(uint64_t start_hpa, uint64_t end_hpa)
{
	uint32_t e820_index;
	uint64_t entry_start, entry_end;

	for (e820_index = 0; e820_index < hv_e820_entries_nr; e820_index++) {
		entry_start = hv_e820[e820_index].baseaddr;
		entry_end = hv_e820[e820_index].baseaddr + hv_e820[e820_index].length;

		/* No need handle in these cases*/
		if ((hv_e820[e820_index].type != E820_TYPE_RAM) ||
			 (entry_end <= start_hpa) || (entry_start >= end_hpa)) {
			continue;
		}

		if ((entry_start <= start_hpa) && (entry_end >= end_hpa)) {
			hv_e820[e820_index].length = start_hpa - entry_start;

			if (end_hpa < entry_end) {
				/*
				* .......|start_hpa......................end_hpa|.....
				* |entry_start..............................entry_end|
				*/
				(void)insert_e820_entry(e820_index + 1, end_hpa, entry_end - end_hpa, E820_TYPE_RAM);
			}
		} else {
			panic("%s: region 0x%016lx-0x%016lx crosses multiple e820 entries, check your bootloader!",
					   __func__, start_hpa, end_hpa);
		}
	}
}

/**
 * @brief Allocate memory region for the hypervisor.
 *
 * This function calls get_hv_image_base() and get_hv_image_size() to get the base address and size of the hypervisor.
 * It then calls reserve_e820_region() to reserve the memory region for the hypervisor in the E820 table.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
static void alloc_hv_memory(void)
{
	uint64_t hv_start = hva2hpa((void *)(get_hv_image_base()));
	uint64_t hv_end  = hv_start + get_hv_image_size();

	pr_err("%s: hv start: 0x%016lx, end: 0x%016lx", __func__, hv_start, hv_end);

	reserve_e820_region(hv_start, hv_end);
}

/**
 * @brief Allocate memory region for multiboot modules.
 *
 * This function calls get_acrn_boot_info() to get multiboot modules start and end address. The function iterates
 * every module and calls reserve_e820_region() to reserve the memory region for each module in the E820 table.
 * Then it iterates through the E820 entries to shrink the entries to page boundary.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
static void alloc_mods_memory(void)
{
	uint32_t mod_index, e820_index, target_index;
	uint64_t mod_start, mod_end;
	uint64_t entry_start, entry_end;
	struct acrn_boot_info *abi = get_acrn_boot_info();

	/* 1st pass: remove the exact region */
	for (mod_index = 0; mod_index < abi->mods_count; mod_index++) {
		mod_start = hva2hpa(abi->mods[mod_index].start);
		mod_end = mod_start + abi->mods[mod_index].size;

		pr_err("%s: mod %d, start: 0x%016lx, end: 0x%016lx", __func__, mod_index, mod_start, mod_end);

		reserve_e820_region(mod_start, mod_end);
	}

	/* 2nd pass: shrink the entries to page boundary */
	target_index = 0;
	for (e820_index = 0; e820_index < hv_e820_entries_nr; e820_index++) {
		entry_start = round_page_up(hv_e820[e820_index].baseaddr);
		entry_end = round_page_down(hv_e820[e820_index].baseaddr + hv_e820[e820_index].length);

		if (entry_start < entry_end) {
			hv_e820[target_index].baseaddr = entry_start;
			hv_e820[target_index].length = entry_end - entry_start;
			hv_e820[target_index].type = hv_e820[e820_index].type;
			target_index++;
		}
	}
	(void)memset(&hv_e820[target_index], 0, (hv_e820_entries_nr - target_index) * sizeof(struct e820_entry));
	hv_e820_entries_nr = target_index;
}

/**
 * @brief Initialize the E820 table.
 *
 * This function initializes the E820 table by reading the memory map from either EFI or multiboot, depending on
 * the boot method. It also calculates the E820 top size and allocates memory for hypervisor itself and multiboot
 * modules.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
void init_e820(void)
{
	struct acrn_boot_info *abi = get_acrn_boot_info();

#ifdef CONFIG_MULTIBOOT2
	if (boot_from_uefi(abi)) {
		init_efi_mmap_entries(&abi->uefi_info);
		init_e820_from_efi_mmap();
	} else
#endif
	{
		init_e820_from_mmap(abi);
	}

	alloc_hv_memory();
	/* reserve multiboot modules memory */
	alloc_mods_memory();
}

/**
 * @brief Get the number of E820 entries.
 *
 * This function returns the number of E820 entries as recorded during the initialization of the E820 table.
 *
 * ACRN hypervisor is the first owner to manage system memory, usually BIOS/bootloader will pass the platform
 * physical memory layout - E820 table to hypervisor, hypervisor then doing its memory management based on it.
 * Hypervisor maintains an array hv_e820[E820_MAX_ENTRIES] to illustrate: 1) the memory layout initially abstracted
 * from the physical E820 table, 2) how the memory is managed by hypervisor.
 *
 * @return The number of E820 entries.
 *
 * @pre N/A
 *
 * @post N/A
 */
uint32_t get_e820_entries_count(void)
{
	return hv_e820_entries_nr;
}

/**
 * @brief Get the E820 memory map entries.
 *
 * This function returns a pointer to the array of E820 entries as recorded during the initialization of the E820
 * table.
 *
 * ACRN hypervisor is the first owner to manage system memory, usually BIOS/bootloader will pass the platform
 * physical memory layout - E820 table to hypervisor, hypervisor then doing its memory management based on it.
 * Hypervisor maintains an array hv_e820[E820_MAX_ENTRIES] to illustrate: 1) the memory layout initially abstracted
 * from the physical E820 table, 2) how the memory is managed by hypervisor.
 *
 * @return A pointer to the array of E820 entries.
 *
 * @pre N/A
 *
 * @post N/A
 */
const struct e820_entry *get_e820_entry(void)
{
	return hv_e820;
}

/**
 * @}
 */
