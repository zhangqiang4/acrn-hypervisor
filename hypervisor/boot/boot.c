/*
 * Copyright (C) 2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <errno.h>
#include <asm/pgtable.h>
#include <boot.h>
#include <rtl.h>
#include <logmsg.h>

static struct acrn_boot_info acrn_bi = { 0 };

/**
 * @pre (p_start != NULL) && (p_end != NULL)
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

/* @pre abi != NULL && tag != NULL
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

void init_acrn_boot_info(uint32_t *registers)
{
	(void)init_multiboot_info(registers);
	/* TODO: add more boot protocol support here */
}

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

/*
 * @post retval != NULL
 */
struct acrn_boot_info *get_acrn_boot_info(void)
{
	return &acrn_bi;
}
