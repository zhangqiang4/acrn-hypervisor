/*
 * Copyright (C) 2021-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <errno.h>
#include <asm/pgtable.h>
#include <boot.h>
#include <rtl.h>
#include <logmsg.h>

/**
 * @addtogroup boot
 *
 * @{
 */

/**
 * @file
 * @brief functions on ACRN boot info operation
 *
 * The file provides the implementation for operating on the ACRN boot info structure, including initialization
 * and sanitization. It also provides API to get module and calculate modules range in physical memory.
 */
/**
 * @brief ACRN Boot Information Instance
 *
 * This variable is an instance of the `acrn_boot_info` structure, which stores
 * the boot information required by the ACRN hypervisor. It includes details
 * about the boot protocol, command line arguments, loader name, modules, memory
 * map, ACPI, and UEFI information. This comprehensive set of data is used by
 * the hypervisor to initialize and manage the system effectively.
 *
 * The variable is statically initialized to zero, ensuring that all fields are
 * set to their default values before being populated with actual boot information.
 */
static struct acrn_boot_info acrn_bi = { 0 };

/**
 * @brief Get Boot Modules Address Range
 *
 * This function calculates the range of physical addresses occupied by the boot
 * modules. It iterates through the list of modules in the ACRN boot information
 * structure, determining the lowest and highest physical addresses used by the
 * modules. The calculated range is stored in the provided pointers `p_start` and
 * `p_end`.
 *
 * @param[out] p_start A pointer to a `uint64_t` where the start of the address range
 *                will be stored.
 * @param[out] p_end A pointer to a `uint64_t` where the end of the address range will
 *              be stored.
 *
 * @return None
 *
 * @pre p_start != NULL
 * @pre p_end != NULL
 *
 * @post N/A
 *
 * @remark This function uses `hva2hpa` to convert host virtual addresses to host
 *       physical addresses, ensuring that the range is calculated in terms of
 *       physical memory.
 */
void get_boot_mods_range(uint64_t *p_start, uint64_t *p_end)
{
	uint32_t i;
	uint64_t start = ~0UL, end = 0UL;
	struct acrn_boot_info *abi = get_acrn_boot_info();

	for (i = 0; i < abi->mods_count; i++) {
		if (hva2hpa(abi->mods[i].start) < start) {
			start = hva2hpa(abi->mods[i].start);
		}
		if (hva2hpa(abi->mods[i].start + abi->mods[i].size) > end) {
			end = hva2hpa(abi->mods[i].start + abi->mods[i].size);
		}
	}
	*p_start = start;
	*p_end = end;
}

/**
 * @brief Get module by Tag from ACRN Boot Information
 *
 * This function retrieves a module from the ACRN boot information based on a specified tag. It searches through
 * the list of modules and returns the one whose descriptive string starts with the given tag. The tag must be
 * located at the beginning of the string and followed by a space, tab, or end-of-line character.
 *
 * @param[in] abi A pointer to the `acrn_boot_info` structure containing boot information, including the list of
 *            modules.
 * @param[in] tag A pointer to a string representing the tag to search for in the module descriptions.
 *
 * @return Returns a pointer to the `abi_module` structure that matches the specified tag. Returns NULL if no
 *         matching module is found or if the module's start address is NULL.
 *
 * @pre abi != NULL
 * @pre tag != NULL
 *
 * @post N/A
 *
 * @remark The function checks for modules that start at address 0, ACRN config tool will make sure module
 *                size be greater than 1MB, as ACRN does not support the cases that module located at address 0
 *                or module size is less than 1MB.
 */
struct abi_module *get_mod_by_tag(const struct acrn_boot_info *abi, const char *tag)
{
	uint32_t i;
	struct abi_module *mod = NULL;
	struct abi_module *mods = (struct abi_module *)(&abi->mods[0]);
	uint32_t tag_len = strnlen_s(tag, MAX_MOD_TAG_LEN);

	for (i = 0U; i < abi->mods_count; i++) {
		const char *string = (char *)hpa2hva((uint64_t)(mods + i)->string);
		uint32_t str_len = strnlen_s(string, MAX_MOD_TAG_LEN);
		const char *p_chr = string + tag_len; /* point to right after the end of tag */

		/* The tag must be located at the first word in string and end with SPACE/TAB or EOL since
		 * when do file stitch by tool, the tag in string might be followed by EOL(0x0d/0x0a).
		 */
		if ((str_len >= tag_len) && (strncmp(string, tag, tag_len) == 0)
				&& (is_space(*p_chr) || is_eol(*p_chr))) {
			mod = mods + i;
			break;
		}
	}
	/* GRUB might put module at address 0 or under 1MB in the case that the module size is less then 1MB
	 * ACRN will not support these cases
	 */
	if ((mod != NULL) && (mod->start == NULL)) {
		pr_err("Unsupported module: start at HPA 0, size 0x%x .", mod->size);
		mod = NULL;
	}

	return mod;
}

/**
 * @brief Initialize ACRN Boot Information
 *
 * This function initializes the ACRN Boot Information structure using the provided boot protocol
 * registers. It currently supports initialization via the Multiboot protocol. The function is
 * designed to be extended to support additional boot protocols in the future.
 *
 * @param[in] registers A pointer to an array of `uint32_t` values, where the first
 *            element contains the magic number and the second element contains
 *            the address of the boot information structure.
 *
 * @return None
 *
 * @pre registers != NULL
 *
 * @post N/A
 *
 * @remark The function currently only supports the Multiboot protocol. Additional
 *       boot protocol support can be added by extending the function.
 */
void init_acrn_boot_info(uint32_t *registers)
{
	(void)init_multiboot_info(registers);
	/* TODO: add more boot protocol support here */
}

/**
 * @brief Sanitize ACRN Boot Information
 *
 * This function validates the ACRN boot information structure to ensure that it
 * contains all necessary data for the hypervisor to operate correctly. It checks
 * for the presence of boot modules, memory map entries, UEFI information, and the
 * bootloader name. If any required information is missing or invalid, the function
 * logs error and prints the boot environment and bootloader name if available, then
 * returns an error code.
 *
 * @param[in] abi A pointer to the ACRN boot information structure to be validated.
 *
 * @return Returns 0 if the boot information is valid or -EINVAL if the information is invalid.
 *
 * @retval -EINVAL if any required information is missing or invalid.
 * @retval 0 if the boot information is valid.
 *
 * @pre abi != NULL
 *
 * @post N/A
 */
int32_t sanitize_acrn_boot_info(struct acrn_boot_info *abi)
{
	int32_t abi_status = 0;

	if (abi->mods_count == 0U) {
		pr_err("no boot module info found");
		abi_status = -EINVAL;
	}

	if (abi->mmap_entries == 0U) {
		pr_err("no boot mmap info found");
		abi_status = -EINVAL;
	}

	printf("%s environment detected.\n", boot_from_uefi(abi) ? "UEFI" : "Non-UEFI");
	if (boot_from_uefi(abi) && ((abi->uefi_info.memmap == 0U) || (abi->uefi_info.memmap_hi != 0U))) {
		pr_err("no efi memmap found below 4GB space!");
		abi_status = -EINVAL;
	}

	if (abi->loader_name[0] == '\0') {
		pr_err("no bootloader name found!");
		abi_status = -EINVAL;
	} else {
		printf("%s Bootloader: %s\n", abi->protocol_name, abi->loader_name);
	}

	return abi_status;
}

/**
 * @brief Get ACRN Boot Information
 *
 * This function returns a pointer to the ACRN boot information structure. It
 * provides access to the global `acrn_bi` instance, which contains the boot
 * information required by the ACRN hypervisor. This information is used during
 * system initialization and operation to manage and utilize boot-related data.
 *
 * @return A pointer to the ACRN boot information structure.
 *
 * @pre N/A
 *
 * @post retval != NULL
 */
struct acrn_boot_info *get_acrn_boot_info(void)
{
	return &acrn_bi;
}

/**
 * @}
 */
