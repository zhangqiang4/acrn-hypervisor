/*
 * Copyright (C) 2020-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <errno.h>
#include <asm/pgtable.h>
#include <boot.h>
#include "multiboot_priv.h"
/**
 * @addtogroup boot
 *
 * @{
 */

/**
 * @file
 * @brief initialize ACRN boot info structure when system boot from multiboot protocol
 *
 * The file will convert the multiboot info structure to the ACRN boot info structure when
 * the system boots from the multiboot protocol.
 */

/**
 * @brief Convert Multiboot Information to ACRN Boot Information
 *
 * This function converts the Multiboot information structure into an ACRN boot
 * information structure. It extracts command line arguments, loader name, memory
 * map entries, and module information from the Multiboot structure and populates
 * the corresponding fields in the ACRN boot information structure.
 *
 * @param[inout] abi A pointer to the ACRN boot information structure to be populated.
 * @param[in] mb_info A pointer to the Multiboot information structure provided by
 *                the bootloader.
 *
 * @return Always returns 0 indicating conversion of the Multiboot information to the ACRN boot
 *              information structure is always successful.
 *
 * @pre abi != NULL
 * @pre mb_info != NULL
 *
 * @post N/A
 */
static int32_t multiboot_to_acrn_bi(struct acrn_boot_info *abi, void *mb_info) {
	uint32_t i;
	struct multiboot_info *mbi = (struct multiboot_info *)(hpa2hva_early((uint64_t)mb_info));
	struct multiboot_mmap *mmap = (struct multiboot_mmap *)hpa2hva_early((uint64_t)mbi->mi_mmap_addr);
	struct multiboot_module *mods = (struct multiboot_module *)hpa2hva_early((uint64_t)mbi->mi_mods_addr);

	(void)strncpy_s((void *)(abi->cmdline), MAX_BOOTARGS_SIZE, (char *)hpa2hva_early((uint64_t)mbi->mi_cmdline),
				strnlen_s((char *)hpa2hva_early((uint64_t)mbi->mi_cmdline), (MAX_BOOTARGS_SIZE - 1U)));

	(void)strncpy_s((void *)(abi->loader_name), MAX_LOADER_NAME_SIZE,
			(char *)hpa2hva_early((uint64_t)mbi->mi_loader_name),
			strnlen_s((char *)hpa2hva_early((uint64_t)mbi->mi_loader_name), (MAX_LOADER_NAME_SIZE - 1U)));

	abi->mmap_entries = mbi->mi_mmap_length / sizeof(struct multiboot_mmap);

	if (((mbi->mi_flags & MULTIBOOT_INFO_HAS_MMAP) != 0U) && (abi->mmap_entries != 0U) && (mmap != NULL)) {

		if (abi->mmap_entries > MAX_MMAP_ENTRIES) {
			abi->mmap_entries = MAX_MMAP_ENTRIES;
		}

		for (i = 0U; i < abi->mmap_entries; i++) {
			abi->mmap_entry[i].baseaddr = (mmap + i)->baseaddr;
			abi->mmap_entry[i].length = (mmap + i)->length;
			abi->mmap_entry[i].type = (mmap + i)->type;
		}
	} else {
		abi->mmap_entries = 0U;
	}

	abi->mods_count = mbi->mi_mods_count;
	if (((mbi->mi_flags & MULTIBOOT_INFO_HAS_MODS) != 0U) && (mbi->mi_mods_count != 0U) && (mods != NULL)) {
		if (abi->mods_count > MAX_MODULE_NUM) {
			abi->mods_count = MAX_MODULE_NUM;
		}

		for (i = 0U; i < abi->mods_count; i++) {
			abi->mods[i].start = hpa2hva_early((uint64_t)(mods + i)->mm_mod_start);
			if ((mods + i)->mm_mod_end > (mods + i)->mm_mod_start) {
				abi->mods[i].size = (mods + i)->mm_mod_end - (mods + i)->mm_mod_start;
			}
			(void)strncpy_s((void *)(abi->mods[i].string), MAX_MOD_STRING_SIZE,
				(char *)hpa2hva_early((uint64_t)(mods + i)->mm_string),
				strnlen_s((char *)hpa2hva_early((uint64_t)(mods + i)->mm_string), MAX_BOOTARGS_SIZE));
		}
	} else {
		abi->mods_count = 0U;
	}

	return 0;
}

/**
 * @brief Initialize Multiboot Information
 *
 * This function initializes the ACRN boot information structure based on the Multiboot or Multiboot2 protocol.
 * It uses the magic number and information structure address provided in the `registers` array to determine
 * the boot protocol and populate the `acrn_boot_info` structure accordingly.
 *
 * @param[in] registers A pointer to an array of uint32_t values, where registers[0] contains the magic number
 *                  and registers[1] contains the address of the boot information structure.
 *
 * @return Returns 0 on successful initialization of the boot information structure.
 *              Returns -ENODEV if the boot protocol is not recognized or initialization fails.
 *
 * @retval -ENODEV if the boot protocol is not recognized or initialization fails.
 * @retval 0 if successfully initialized the boot information structure.
 *
 * @pre registers != NULL
 *
 * @post N/A
 */
int32_t init_multiboot_info(uint32_t *registers)
{
	int32_t ret = -ENODEV;
	uint32_t magic = registers[0];
	uint32_t info = registers[1];
	struct acrn_boot_info *abi = get_acrn_boot_info();

	if (boot_from_multiboot(magic, info)) {
		if (multiboot_to_acrn_bi(abi, hpa2hva_early((uint64_t)info)) == 0) {
			strncpy_s(abi->protocol_name, MAX_PROTOCOL_NAME_SIZE,
					"Multiboot", (MAX_PROTOCOL_NAME_SIZE - 1U));
			ret = 0;
		}
#ifdef CONFIG_MULTIBOOT2
	} else if (boot_from_multiboot2(magic)) {
		if (multiboot2_to_acrn_bi(abi, hpa2hva_early((uint64_t)info)) == 0) {
			strncpy_s(abi->protocol_name, MAX_PROTOCOL_NAME_SIZE,
					"Multiboot2", (MAX_PROTOCOL_NAME_SIZE - 1U));
			ret = 0;
		}
#endif
	} else {
		/* Currently there are only multiboot and multiboot2 */
	}
	return ret;
}
/**
 * @}
 */
