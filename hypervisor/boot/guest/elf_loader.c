/*
 * Copyright (C) 2021-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <asm/guest/vm.h>
#include <asm/guest/ept.h>
#include <asm/mmu.h>
#include <vboot.h>
#include <elf.h>
#include <logmsg.h>
#include <vacpi.h>

/**
 * @addtogroup vp-base_vboot
 *
 * @{
 */

/**
 * @file
 * @brief Functions to load ELF image for ACRN pre-launched VM
 *
 * The file provides functions to load and boot ELF image for ACRN pre-launched VM, including both 32-bit
 * and 64-bit architecture support.
 */

/**
 * @brief ELF Boot Parameters Structure
 *
 * This structure encapsulates various parameters used during the booting of an
 * ELF (Executable and Linkable Format) file. It includes information about the
 * Global Descriptor Table (GDT), loader name, ACPI (Advanced Configuration and
 * Power Interface) table, Multiboot information, command line arguments, and
 * memory map. These parameters are essential for initializing and managing the
 * system during the boot process.
 *
 * @alignment 8
 */
struct elf_boot_para {
	char init_gdt[40];             /**< Initial Global Descriptor Table (GDT). */
	char loader_name[20];          /**< Loader name. */
	struct acpi_table_rsdp rsdp;   /**< ACPI Root System Description Pointer (RSDP). */
	struct multiboot_info mb_info; /**< Multiboot information. */
	char cmdline[MEM_2K];          /**< Command line arguments. */
	char mmap[MEM_4K * 5U];        /**< Memory map. */
} __aligned(8);

/**
 * @brief Prepare ELF Command Line for Virtual Machine
 *
 * This function copies the command line arguments for an ELF (Executable and Linkable
 * Format) file into the guest physical address (GPA) space of ACRN pre-launched VM.
 *
 * @param[in] vm A pointer to the `acrn_vm` structure representing the VM for which the ELF command line
 *            is being prepared.
 * @param[in] param_cmd_gpa The guest physical address (GPA) where the command line arguments should be copied.
 *
 * @return Returns 0 on successful copying of the command line arguments. Returns a negative error code if
 *         the copying fails.
 *
 * @retval -EINVAL Invalid parameters
 * @retval -EFAULT Failed to copy memory
 * @retval 0 on Success
 *
 * @pre VM != NULL
 *
 * @post N/A
 */
static int32_t prepare_elf_cmdline(struct acrn_vm *vm, uint64_t param_cmd_gpa)
{
	return copy_to_gpa(vm, vm->sw.bootargs_info.src_addr, param_cmd_gpa,
		           vm->sw.bootargs_info.size);
}

/**
 * @brief Prepare multiboot memory map for ACRN VM
 *
 * This function prepares the memory map for ACRN pre-launched VM in the format expected by the Multiboot
 * specification. It copies the VM's E820 memory map entries into the guest physical address (GPA) space,
 * ensuring compatibility with Multiboot. The function will adjust the memory type to `MULTIBOOT_MEMORY_RESERVED`
 * if it exceeds `MULTIBOOT_MEMORY_BADRAM`.
 *
 * @param[in] vm A pointer to the `acrn_vm` structure representing the VM for which the Multiboot memory map
 *            is being prepared.
 * @param[in] param_mmap_gpa The guest physical address (GPA) where the memory map entries should be copied.
 *
 * @return Returns the total length of the memory map copied to the GPA. Returns 0 if the copying fails.
 *
 * @pre VM != NULL
 *
 * @post N/A
 */
static uint32_t prepare_multiboot_mmap(struct acrn_vm *vm, uint64_t param_mmap_gpa)
{
	uint32_t i, mmap_length = 0U;
	struct multiboot_mmap mmap_entry;
	uint64_t mmap_gpa = param_mmap_gpa;

	for (i = 0U; i < vm->e820_entry_num; i++) {
		mmap_entry.size = 20U;
		mmap_entry.baseaddr = vm->e820_entries[i].baseaddr;
		mmap_entry.length = vm->e820_entries[i].length;
		mmap_entry.type = vm->e820_entries[i].type;
		if (mmap_entry.type > MULTIBOOT_MEMORY_BADRAM) {
			mmap_entry.type = MULTIBOOT_MEMORY_RESERVED;
		}

		if (copy_to_gpa(vm, &mmap_entry, mmap_gpa,
			sizeof(struct multiboot_mmap)) != 0U) {
			mmap_length = 0U;
			break;
		}
		mmap_gpa += sizeof(struct multiboot_mmap);
		mmap_length += sizeof(struct multiboot_mmap);
	}

	return mmap_length;
}

/**
 * @brief Prepare the loader name for ACRN VM
 *
 * This function initializes the ACRN ELF loader name and copies it to a specified GPA within ACRN pre-launched VM.
 *
 * @param[in] vm Pointer to the `acrn_vm` structure representing the VM for which the loader name
 *            is being prepared.
 * @param[in] param_ldrname_gpa Guest physical address where the loader name should be copied.
 *
 * @return Status code from the copy operation, indicating success or failure.
 *
 * @retval -EINVAL Invalid parameters
 * @retval -EFAULT Failed to copy memory
 * @retval 0 Success
 *
 * @pre vm != NULL
 *
 * @post N/A
 */
static int32_t prepare_loader_name(struct acrn_vm *vm, uint64_t param_ldrname_gpa)
{
	char loader_name[MAX_LOADER_NAME_SIZE] = "ACRN ELF LOADER";

	return (copy_to_gpa(vm, (void *)loader_name, param_ldrname_gpa,
		MAX_LOADER_NAME_SIZE));
}

/**
 * @brief Load 64-bit ELF image into ACRN VM.
 *
 * This function processes a 64-bit ELF image, loading its segments into the guest physical memory of
 * ACRN pre-launched VM. It supports ELF images of type `ET_EXEC` and handles program headers of
 * type `PT_LOAD`. The function also identifies and clears `.bss` sections. It assumes that the ELF image
 * does not contain relocatable sections and that the entry point is below 4GB.
 *
 * @param[in] vm Pointer to the `acrn_vm` structure representing the VM for which the kernel image in ELF64
 *            format is being loaded.
 *
 * @return Entry point of the ELF image if successfully loaded, or NULL if the ELF type is unsupported.
 *
 * @pre vm != NULL
 *
 * @post N/A
 *
 * @remark The function must run in stac/clac context.
 */
static void *do_load_elf64(struct acrn_vm *vm)
{
	struct sw_kernel_info *sw_kernel = &(vm->sw.kernel_info);
	void *p_elf_img = (void *)sw_kernel->kernel_src_addr;
	struct elf64_hdr *p_elf_header64 = (struct elf64_hdr *)p_elf_img;
	struct elf64_prog_entry *p_prg_tbl_head64;
	struct elf64_sec_entry *p_sec_tbl_head64, *p_shstr_tbl_head64;
	const char *p_shstr_tbl, *p_sec_name;
	void *elf_entry = NULL, *p_elf_bss = NULL;
	uint32_t i;

	/* Currently only ET_EXEC is supported */
	if (p_elf_header64->e_type == ET_EXEC) {
		p_prg_tbl_head64 = (struct elf64_prog_entry *)(p_elf_img + p_elf_header64->e_phoff);
		/* Prepare program entries */
		for (i = 0U; i < p_elf_header64->e_phnum; i++) {
			/**
			 * We now only support PT_LOAD type. It needs to copy from file to ram
			 * TODO: More program types may be needed here
			 */
			if (p_prg_tbl_head64->p_type == PT_LOAD) {
				/**
				 * copy_to_gpa will check whether the gpa is in EPT, and print message
				 * if anything wrong.
				 * However, the guest OS may still fail to boot if they load segments
				 * to invalid gpa such as ACPI area defined in ve820.
				 *
				 * We assume that the guest elf can put segments to valid gpa.
				 */
				(void)copy_to_gpa(vm, p_elf_img + p_prg_tbl_head64->p_offset,
					p_prg_tbl_head64->p_paddr, (uint32_t)p_prg_tbl_head64->p_filesz);
				/* copy_to_gpa has its own stac/clac inside. Call stac again here to keep
				 * the context. */
				stac();
			}
			p_prg_tbl_head64++;
		}

		/* Find and clear bss sections */
		p_sec_tbl_head64 = (struct elf64_sec_entry *)(p_elf_img + p_elf_header64->e_shoff);
		p_shstr_tbl_head64 = p_sec_tbl_head64 + p_elf_header64->e_shstrndx;
		p_shstr_tbl = (char *)(p_elf_img + p_shstr_tbl_head64->sh_offset);
		/* Currently we don't support relocatable sections(sh_type is SHT_REL or SHT_RELA).
			Assume that the guest elf do not have relocatable sections. */
		for (i = 0U; i < p_elf_header64->e_shnum; i++) {
			/* A section entry's name is an offset, real string is in string tab */
			p_sec_name = p_shstr_tbl + p_sec_tbl_head64->sh_name;
			if ((strncmp(p_sec_name, "bss", 3) == 0) || (strncmp(p_sec_name, ".bss", 4) == 0)) {
				p_elf_bss = gpa2hva(vm, p_sec_tbl_head64->sh_addr);
				(void)memset(p_elf_bss, 0U, p_sec_tbl_head64->sh_size);
			}
			p_sec_tbl_head64++;
		}

		elf_entry = (void *)p_elf_header64->e_entry;
	} else {
		pr_err("%s, elf type(%x) not supported!", __func__, p_elf_header64->e_type);
	}
	/* For 64bit elf, entry address above 4G is not currently supported. Assume that it's below 4G. */
	return elf_entry;
}

/**
 * @brief Load 32-bit ELF image into ACRN VM.
 *
 * This function processes a 32-bit ELF image, loading its segments into the guest physical memory of
 * ACRN pre-launched VM. It supports ELF images of type `ET_EXEC` and handles program headers of
 * type `PT_LOAD`. The function also identifies and clears `.bss` sections. It assumes that the ELF image
 * does not contain relocatable sections.
 *
 * @param[in] vm Pointer to the `acrn_vm` structure representing the VM for which the kernel image in ELF32
 *            format is being loaded.
 *
 * @return Entry point of the ELF image if successfully loaded, or NULL if the ELF type is unsupported.
 *
 * @pre vm != NULL
 *
 * @post N/A
 *
 * @remark The function must run in stac/clac context.
 */
static void *do_load_elf32(struct acrn_vm *vm)
{
	struct sw_kernel_info *sw_kernel = &(vm->sw.kernel_info);
	void *p_elf_img = (void *)sw_kernel->kernel_src_addr;
	struct elf32_hdr *p_elf_header32 = (struct elf32_hdr *)p_elf_img;
	struct elf32_prog_entry *p_prg_tbl_head32;
	struct elf32_sec_entry *p_sec_tbl_head32, *p_shstr_tbl_head32;
	const char *p_shstr_tbl, *p_sec_name;
	void *elf_entry = NULL, *p_elf_bss = NULL;
	uint32_t i;

	/* Currently only ET_EXEC is supported */
	if (p_elf_header32->e_type == ET_EXEC) {
		p_prg_tbl_head32 = (struct elf32_prog_entry *)(p_elf_img + p_elf_header32->e_phoff);
		/* Copy program entries */
		for (i = 0U; i < p_elf_header32->e_phnum; i++) {
			/**
			 * We now only support PT_LOAD type. It needs to copy from file to ram
			 * TODO: More program types may be needed here
			 */
			if (p_prg_tbl_head32->p_type == PT_LOAD) {
				/**
				 * copy_to_gpa will check whether the gpa is in EPT, and print message
				 * if anything wrong.
				 * However, the guest OS may still fail to boot if they load segments
				 * to invalid gpa such as ACPI area defined in ve820.
				 *
				 * We assume that the guest elf can put segments to valid gpa.
				 */
				(void)copy_to_gpa(vm, p_elf_img + p_prg_tbl_head32->p_offset,
					p_prg_tbl_head32->p_paddr, p_prg_tbl_head32->p_filesz);
				/* copy_to_gpa has its own stac/clac inside. Call stac again here to keep
				 * the context. */
				stac();
			}
			p_prg_tbl_head32++;
		}

		/* Find and clear bss sections */
		p_sec_tbl_head32 = (struct elf32_sec_entry *)(p_elf_img + p_elf_header32->e_shoff);
		p_shstr_tbl_head32 = p_sec_tbl_head32 + p_elf_header32->e_shstrndx;
		p_shstr_tbl = (char *)(p_elf_img + p_shstr_tbl_head32->sh_offset);
		/* Currently we don't support relocatable sections(sh_type is SHT_REL or SHT_RELA).
			Assume that the guest elf do not have relocatable sections. */
		for (i = 0U; i < p_elf_header32->e_shnum; i++) {
			/* A section entry's name is an offset, real string is in string tab */
			p_sec_name = p_shstr_tbl + p_sec_tbl_head32->sh_name;
			if ((strncmp(p_sec_name, "bss", 3) == 0) || (strncmp(p_sec_name, ".bss", 4) == 0)) {
				p_elf_bss = gpa2hva(vm, p_sec_tbl_head32->sh_addr);
				(void)memset(p_elf_bss, 0U, p_sec_tbl_head32->sh_size);
			}
			p_sec_tbl_head32++;
		}

		elf_entry = (void *)(uint64_t)p_elf_header32->e_entry;
	} else {
		pr_err("%s, elf type(%x) not supported!", __func__, p_elf_header32->e_type);
	}

	return elf_entry;
}

/**
 * @brief Load an ELF image into ACRN VM
 *
 * This function determines the class of an ELF image (32-bit or 64-bit) and loads it into the guest physical
 * memory of ACRN pre-launched VM. It checks for the ELF magic number to verify the image format and uses
 * appropriate loading functions based on the ELF class. The entry point of the loaded ELF image is stored
 * in the VM's kernel information structure.
 *
 * @param[in] vm Pointer to the `acrn_vm` structure representing the VM for which the ELF kernel image
 *            is being loaded.
 *
 * @return 0 on success, or -EFAULT if the ELF image is unsupported or invalid.
 *
 * @retval -EFAULT if the ELF image is unsupported or invalid
 * @retval 0 on Success
 *
 * @pre vm != NULL
 *
 * @post N/A
 */
static int32_t load_elf(struct acrn_vm *vm)
{
	void *elf_entry = NULL;
	struct sw_kernel_info *sw_kernel = &(vm->sw.kernel_info);
	void *p_elf_img = (void *)sw_kernel->kernel_src_addr;
	int32_t ret = 0;

	stac();

	if (*(uint32_t *)p_elf_img == ELFMAGIC) {
		if (*(uint8_t *)(p_elf_img + EI_CLASS) == ELFCLASS64) {
			elf_entry = do_load_elf64(vm);
		} else if (*(uint8_t *)(p_elf_img + EI_CLASS) == ELFCLASS32) {
			elf_entry = do_load_elf32(vm);
		} else {
			pr_err("%s, unsupported elf class(%d)", __func__, *(uint8_t *)(p_elf_img + EI_CLASS));
		}
	} else {
		pr_err("%s, booting elf but no elf header found!", __func__);
	}

	clac();

	sw_kernel->kernel_entry_addr = elf_entry;

	if (elf_entry == NULL) {
		ret = -EFAULT;
	}

	return ret;
}

/**
 * @brief Locate the multiboot header in the ELF image.
 *
 * This function scans the first 8KB of the ELF image to detect the presence of a multiboot header,
 * which indicates that the image requires multiboot information. It checks for the multiboot header
 * magic number and verifies the checksum according to the multiboot specification 0.6.96, section 3.1.2.
 * If a valid multiboot header is found, it is returned.
 *
 * @param[in] vm Pointer to the `acrn_vm` structure representing the VM for which the multiboot header
 *            need to be found in kernel image.
 *
 * @return Pointer to the multiboot header if found, or NULL if not found.
 *
 * @pre vm != NULL
 *
 * @post N/A
 */
static struct multiboot_header *find_img_multiboot_header(struct acrn_vm *vm)
{
	uint16_t i, j;
	struct multiboot_header *ret = NULL;
	uint32_t *p = (uint32_t *)vm->sw.kernel_info.kernel_src_addr;

	/* Scan the first 8k to detect whether the elf needs multboot info prepared. */
	for (i = 0U; i <= (((MEM_4K * 2U) / sizeof(uint32_t)) - 3U); i++) {
		if (p[i] == MULTIBOOT_HEADER_MAGIC) {
			uint32_t sum = 0U;

			/* According to multiboot spec 0.6.96 sec 3.1.2.
			 * There are three u32:
			 *  offset   field
			 *    0      multiboot_header_magic
			 *    4      flags
			 *    8      checksum
			 * The sum of these three u32 should be u32 zero.
			 */
			for (j = 0U; j < 3U; j++) {
				sum += p[j + i];
			}

			if (0U == sum) {
				ret = (struct multiboot_header *)(p + i);
				break;
			}
		}
	}
	return ret;
}

/**
 * @brief ELF image bootloader for ACRN VM.
 *
 * This is the API of ELF image bootloader for ACRN pre-launched VM. The function loads an ELF image into
 * the VM's memory and sets up the necessary boot parameters, including multiboot information if applicable.
 * It initializes the primary virtual CPU in protected mode and prepares memory mappings, command line
 * arguments, and loader names according to the multiboot specification. The function supports both multiboot
 * and non-multiboot ELF images.
 *
 * @param[in] vm Pointer to the `acrn_vm` structure representing the VM for which the ELF kernel image
 *            will be loaded.
 *
 * @return 0 on success, or an error code if the ELF image or boot parameters are invalid.
 *
 * @retval -EFAULT if the ELF image is unsupported or invalid
 * @retval 0 on Success
 *
 * @pre vm != NULL
 *
 * @post N/A
 */
int32_t elf_loader(struct acrn_vm *vm)
{
	int32_t ret = 0;
	struct multiboot_header *mb_hdr;
	/* Get primary vcpu */
	struct acrn_vcpu *vcpu = vcpu_from_vid(vm, BSP_CPU_ID);
	/*
	 * Assuming the guest elf would not load content to GPA space under
	 * VIRT_RSDP_ADDR, and guest gpa load space is sure under address
	 * we prepared in ve820.c. In the future, need to check each
	 * ELF load entry according to ve820 if relocation is not supported.
	 */
	uint64_t load_params_gpa = find_space_from_ve820(vm, sizeof(struct elf_boot_para),
				   MEM_4K, VIRT_RSDP_ADDR);

	if (load_params_gpa != INVALID_GPA) {
		/* We boot ELF Image from protected mode directly */
		init_vcpu_protect_mode_regs(vcpu, load_params_gpa +
					    offsetof(struct elf_boot_para, init_gdt));
		stac();
		mb_hdr = find_img_multiboot_header(vm);
		clac();
		if (mb_hdr != NULL) {
			uint32_t mmap_length = 0U;
			struct multiboot_info mb_info = { 0 };

			stac();
			if ((mb_hdr->flags & MULTIBOOT_HEADER_NEED_MEMINFO) != 0U) {
				mmap_length = prepare_multiboot_mmap(vm, load_params_gpa +
						offsetof(struct elf_boot_para, mmap));
			}

			if (mmap_length != 0U) {
				mb_info.mi_flags |= MULTIBOOT_INFO_HAS_MMAP;
				mb_info.mi_mmap_addr = (uint32_t)(load_params_gpa +
						offsetof(struct elf_boot_para, mmap));
				mb_info.mi_mmap_length = mmap_length;
			}
			ret = prepare_elf_cmdline(vm, load_params_gpa +
						offsetof(struct elf_boot_para, cmdline));
			if (ret == 0) {
				mb_info.mi_flags |= MULTIBOOT_INFO_HAS_CMDLINE;
				mb_info.mi_cmdline = load_params_gpa +
						offsetof(struct elf_boot_para, cmdline);
				ret = prepare_loader_name(vm, load_params_gpa +
						offsetof(struct elf_boot_para, loader_name));
			}

			if (ret == 0) {
				mb_info.mi_flags |= MULTIBOOT_INFO_HAS_LOADER_NAME;
				mb_info.mi_loader_name = load_params_gpa +
						offsetof(struct elf_boot_para, loader_name);
				ret = copy_to_gpa(vm, (void *)&mb_info, load_params_gpa +
						offsetof(struct elf_boot_para, mb_info),
						sizeof(struct multiboot_info));
			}

			if (ret == 0) {
				vcpu_set_gpreg(vcpu, CPU_REG_RAX, MULTIBOOT_INFO_MAGIC);
				vcpu_set_gpreg(vcpu, CPU_REG_RBX, load_params_gpa +
						offsetof(struct elf_boot_para, mb_info));
				/* other vcpu regs should have satisfied multiboot requirement already. */
			}
			clac();
		}
		/*
		 * elf_loader need support non-multiboot header image
		 * at the same time.
		 */
		if (ret == 0) {
			ret = load_elf(vm);
		}
	}
	return ret;
}
/**
 * @}
 */
