/*
 * Copyright (C) 2021-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <asm/guest/vm.h>
#include <asm/e820.h>
#include <asm/zeropage.h>
#include <asm/guest/ept.h>
#include <asm/mmu.h>
#include <boot.h>
#include <vboot.h>
#include <vacpi.h>
#include <efi_mmap.h>
#include <errno.h>
#include <logmsg.h>

/**
 * @addtogroup vp-base_vboot
 *
 * @{
 */

/**
 * @file
 * @brief Functions to load Bzimage for ACRN VM
 *
 * The file provides functions to load and boot image in Linux bzimage format for ACRN pre-launched VM and Service VM.
 */

/**
 * @brief The debug level of the Linux guest VM boot process.
 */
#define DBG_LEVEL_VM_BZIMAGE	6U

/**
 * @brief Represents boot parameters for loading a bzImage kernel.
 *
 * The `bzimage_boot_para` structure contains various boot parameters required for
 * loading and booting a bzImage kernel. It includes fields for the initial Global
 * Descriptor Table (GDT), the zero page, boot command line arguments, and reserved
 * space for trampoline code. Additionally, it may include an EFI memory map if
 * multiboot2 is configured.
 * Please note in Linux OS, the last 8KB space below MEM_1M is for trampoline code.
 * Thus the struct reserves this space to avoid the trampoline corruption.
 *
 * @alignment 8
 *
 * @remark This structure shall be packed.
 */
struct bzimage_boot_para {
	char init_gdt[MEM_1K];                /**< Initial Global Descriptor Table (GDT) entries. */
	struct zero_page zeropage;            /**< Zero page containing boot parameters and memory map. */
	char cmdline[MAX_BOOTARGS_SIZE];      /**< Boot command line arguments. */
#ifdef CONFIG_MULTIBOOT2
	struct efi_memory_desc efi_mmap[MAX_EFI_MMAP_ENTRIES]; /**< EFI memory map descriptors. */
#endif
	char reserved_trampoline[MEM_4K * 2]; /**< Reserved space for trampoline code. */
} __packed __aligned(8);

/* The value is referenced from Linux boot protocal for old kernels, it should ideally be configurable
 * to accommodate different operating systems and their specific requirements.
 * TODO: Make this value configurable for different OS environments.
 */
/**
 * @brief Define the default maximum guest physical address for the ramdisk.
 *
 * This macro sets the default maximum guest physical address for the ramdisk,
 * referenced from the Linux boot protocol for older kernels. It represents the
 * upper limit of the address space where the ramdisk can be loaded in the guest
 * environment. This value should ideally be configurable to accommodate different
 * operating systems and their specific requirements.
 */
#define DEFAULT_RAMDISK_GPA_MAX		0x37ffffffUL

/**
 * @brief Determine the load address for the initrd (ramdisk) in the virtual machine.
 *
 * This function calculates the appropriate guest physical address (GPA) to load the
 * initrd (ramdisk) for a given virtual machine, based on the kernel's start address
 * and alignment requirements. It considers the Linux boot protocol specifications,
 * including the kernel's initialization size and maximum ramdisk address. The function
 * attempts to place the ramdisk after the kernel, and if unsuccessful, it searches for
 * space before the kernel. For service VMs, it ensures the ramdisk is placed after the
 * hypervisor image to avoid log memory corruption.
 *
 * @param[in] vm Pointer to the `acrn_vm` structure representing the VM for which the
 *             initrd image need to be loaded.
 * @param[in] kernel_start The starting address of the kernel in guest physical memory.
 *
 * @return Pointer to the load address for the ramdisk, or NULL if no valid space is found.
 *
 * @pre vm != NULL
 *
 * @post N/A
 */
static void *get_initrd_load_addr(struct acrn_vm *vm, uint64_t kernel_start)
{
	uint64_t ramdisk_load_gpa = INVALID_GPA;
	uint64_t ramdisk_gpa_max = DEFAULT_RAMDISK_GPA_MAX;
	struct zero_page *zeropage = (struct zero_page *)vm->sw.kernel_info.kernel_src_addr;
	uint32_t kernel_init_size, kernel_align, initrd_addr_max;
	uint64_t kernel_end;

	/* Per Linux boot protocol, the Kernel need a size of contiguous
	 * memory(i.e. init_size field in zeropage) from its extract address to boot,
	 * and initrd_addr_max field specifies the maximum address of the ramdisk.
	 * Per kernel src head_64.S, decompressed kernel start at 2M aligned to the
	 * compressed kernel load address.
	 */
	stac();
	kernel_init_size = zeropage->hdr.init_size;
	kernel_align = zeropage->hdr.kernel_alignment;
	initrd_addr_max = zeropage->hdr.initrd_addr_max;
	clac();
	kernel_end = roundup(kernel_start, kernel_align) + kernel_init_size;

	if (initrd_addr_max != 0U) {
		ramdisk_gpa_max = (uint64_t)initrd_addr_max;
	}

	/* Let's try to place ramdisk after kernel first, if failed then try to find place before kernel.
	 * Especially for Service VM, we need to put ramdisk after HV image to avoid log memory corruption.
	 */
	ramdisk_load_gpa = find_space_from_ve820(vm, vm->sw.ramdisk_info.size, kernel_end, ramdisk_gpa_max);

	if (ramdisk_load_gpa == INVALID_GPA) {
		uint64_t find_from = MEM_1M;

		if (is_service_vm(vm)) {
			find_from = get_hv_image_base();
		}

		ramdisk_load_gpa = find_space_from_ve820(vm, vm->sw.ramdisk_info.size,
						find_from, min(kernel_start, ramdisk_gpa_max));
	}

	if (ramdisk_load_gpa == INVALID_GPA) {
		pr_err("no space in guest memory to load VM %d ramdisk", vm->vm_id);
		vm->sw.ramdisk_info.size = 0U;
	}
	dev_dbg(DBG_LEVEL_VM_BZIMAGE, "VM%d ramdisk load_addr: 0x%lx", vm->vm_id, ramdisk_load_gpa);

	return (ramdisk_load_gpa == INVALID_GPA) ? NULL : (void *)ramdisk_load_gpa;
}

/**
 * @brief Determine the load address for the bzImage kernel in the virtual machine.
 *
 * This function calculates the appropriate guest physical address (GPA) to load the
 * bzImage kernel for a given ACRN VM. It considers whether the kernel is relocatable
 * and attempts to load it at the preferred address if possible. For the service VM it
 * handles scenarios where the load address should not conflict with the hypervisor's
 * reserved log memory, which is located before the hypervisor image.
 *
 * @param[in] vm Pointer to the `acrn_vm` structure representing the VM for which the bzImage
 *            kernel need to be loaded.
 *
 * @return Pointer to the load address for the kernel, or NULL if no valid space is found.
 *
 * @pre vm != NULL
 *
 * @post N/A
 */
static void *get_bzimage_kernel_load_addr(struct acrn_vm *vm)
{
	void *load_addr = NULL;
	uint32_t kernel_init_size;
	uint64_t kernel_pref_addr;
	uint64_t hv_start = get_hv_image_base();
	struct vm_sw_info *sw_info = &vm->sw;
	struct zero_page *zeropage;

	/* According to the explaination for pref_address
	 * in Documentation/x86/boot.txt, a relocating
	 * bootloader should attempt to load kernel at pref_address
	 * if possible. A non-relocatable kernel will unconditionally
	 * move itself and to run at this address.
	 */
	zeropage = (struct zero_page *)sw_info->kernel_info.kernel_src_addr;

	stac();
	kernel_pref_addr = zeropage->hdr.pref_addr;
	kernel_init_size = zeropage->hdr.init_size;

	/* Only if the address of pref_address and (pref_address + kernel_init_size) are in the same
	 * vE820 RAM entry, the pref_addess could be valid to load kernel.
	 */
	if (ve820_mapped_all(vm, kernel_pref_addr, (kernel_pref_addr + (uint64_t)kernel_init_size), E820_TYPE_RAM)) {
		load_addr = (void *)kernel_pref_addr;

		/* The HV image is loaded by bootloader under HPA 4GB space per multiboot/multiboot2 protocol.
		 * Given the GPA and HPA are identical mapping in Service VM, if the GPA of Service VM kernel's
		 * load address is located before the HPA of HV image, the Service VM kernel might corrupt the
		 * memory which reserved for HV and VM log(or memory dump).
		 *
		 * So for Service VM we will:
		 * 1) for relocatable kernel, reset the load address so that we can try relocation in next step;
		 * 2) for non-relocatable kernel, give hint for possible memory corruption;
		 *
		 * For Pre-launched VMs we don't have such limitation since their HPA space will be beyond HPA 4GB
		 * per config tool, there is no memory conflict on HV log area.
		 */
		if (is_service_vm(vm) && (load_addr < (void *)hv_start)) {
			if (zeropage->hdr.relocatable_kernel != 0U) {
				load_addr = NULL;
			} else {
				/* The non-relocatable Service VM kernel might corrupt log/dump area. */
				pr_err("Non-relocatable kernel found, possible corruption if hvlog exists!");
			}
		}
	}

	/* If load address is still invalid, let's try to relocate kernel if kernel image supports. */
	if ((load_addr == NULL) && (zeropage->hdr.relocatable_kernel != 0U)) {
		uint64_t kernel_load_gpa = INVALID_GPA;
		uint64_t relocate_from = MEM_1M;
		uint32_t kernel_align = zeropage->hdr.kernel_alignment;
		/* Because the kernel load address need to be up aligned to kernel_align size
		 * whereas find_space_from_ve820() can only return page aligned address,
		 * we enlarge the needed size to (kernel_init_size + kernel_align).
		 */
		uint32_t kernel_size = kernel_init_size + kernel_align;

		/* For Service VM, we will relocate kernel after HV image to avoid the corruption of HV logs.*/
		if (is_service_vm(vm)) {
			relocate_from = hv_start;
		}

		kernel_load_gpa = find_space_from_ve820(vm, kernel_size, relocate_from, MEM_4G);

		if (kernel_load_gpa != INVALID_GPA) {
			load_addr = (void *)roundup((uint64_t)kernel_load_gpa, kernel_align);
		}
	}
	clac();

	if (load_addr == NULL) {
		pr_err("Could not get kernel load addr of VM %d .", vm->vm_id);
	}

	dev_dbg(DBG_LEVEL_VM_BZIMAGE, "VM%d kernel load_addr: 0x%p", vm->vm_id, load_addr);
	return load_addr;
}

#ifdef CONFIG_MULTIBOOT2
/**
 * @pre vm != NULL && efi_mmap_desc != NULL
 */
static uint16_t create_service_vm_efi_mmap_desc(struct acrn_vm *vm, struct efi_memory_desc *efi_mmap_desc)
{
	uint16_t i, desc_idx = 0U;
	const struct efi_memory_desc *hv_efi_mmap_desc = get_efi_mmap_entry();

	for (i = 0U; i < (uint16_t)get_efi_mmap_entries_count(); i++) {
		/* Below efi mmap desc types in native should be kept as original for Service VM */
		if ((hv_efi_mmap_desc[i].type == EFI_RESERVED_MEMORYTYPE)
				|| (hv_efi_mmap_desc[i].type == EFI_UNUSABLE_MEMORY)
				|| (hv_efi_mmap_desc[i].type == EFI_ACPI_RECLAIM_MEMORY)
				|| (hv_efi_mmap_desc[i].type == EFI_ACPI_MEMORY_NVS)
				|| (hv_efi_mmap_desc[i].type == EFI_BOOT_SERVICES_CODE)
				|| (hv_efi_mmap_desc[i].type == EFI_BOOT_SERVICES_DATA)
				|| (hv_efi_mmap_desc[i].type == EFI_RUNTIME_SERVICES_CODE)
				|| (hv_efi_mmap_desc[i].type == EFI_RUNTIME_SERVICES_DATA)
				|| (hv_efi_mmap_desc[i].type == EFI_MEMORYMAPPED_IO)
				|| (hv_efi_mmap_desc[i].type == EFI_MEMORYMAPPED_IOPORTSPACE)
				|| (hv_efi_mmap_desc[i].type == EFI_PALCODE)
				|| (hv_efi_mmap_desc[i].type == EFI_PERSISTENT_MEMORY)) {

			efi_mmap_desc[desc_idx] = hv_efi_mmap_desc[i];
			desc_idx++;
		}
	}

	for (i = 0U; i < (uint16_t)vm->e820_entry_num; i++) {
		/* The memory region with e820 type of RAM could be acted as EFI_CONVENTIONAL_MEMORY
		 * for Service VM, the region which occupied by HV and pre-launched VM has been filtered
		 * already, so it is safe for Service VM.
		 * As Service VM start to run after efi call ExitBootService(), the type of EFI_LOADER_CODE
		 * and EFI_LOADER_DATA which have been mapped to E820_TYPE_RAM are not needed.
		 */
		if (vm->e820_entries[i].type == E820_TYPE_RAM) {
			efi_mmap_desc[desc_idx].type = EFI_CONVENTIONAL_MEMORY;
			efi_mmap_desc[desc_idx].phys_addr = vm->e820_entries[i].baseaddr;
			efi_mmap_desc[desc_idx].virt_addr = vm->e820_entries[i].baseaddr;
			efi_mmap_desc[desc_idx].num_pages = vm->e820_entries[i].length / PAGE_SIZE;
			efi_mmap_desc[desc_idx].attribute = EFI_MEMORY_WB;
			desc_idx++;
		}
	}

	for (i = 0U; i < desc_idx; i++) {
		pr_dbg("Service VM efi mmap desc[%d]: addr: 0x%lx, len: 0x%lx, type: %d", i,
			efi_mmap_desc[i].phys_addr, efi_mmap_desc[i].num_pages * PAGE_SIZE, efi_mmap_desc[i].type);
	}

	return desc_idx;
}
#endif

/**
 * @brief Create a zeropage E820 memory map for the virtual machine.
 *
 * This function copies the E820 memory map entries from the virtual machine's
 * configuration into the zeropage structure. The E820 memory map defines the
 * physical memory layout, including usable RAM and reserved areas. The function
 * ensures that the number of entries does not exceed the maximum allowed and
 * handles errors if the entries are invalid or exceed the limit.
 *
 * @param[inout] zp Pointer to the `zero_page` structure where the E820 entries will be copied.
 * @param[in] vm Pointer to the `acrn_vm` structure containing the E820 entries.
 *
 * @return The number of E820 entries successfully copied, or 0 if an error occurred.
 *
 * @pre zp != NULL
 * @pre vm != NULL
 *
 * @post retval <= E820_MAX_ENTRIES
 */
static uint32_t create_zeropage_e820(struct zero_page *zp, const struct acrn_vm *vm)
{
	uint32_t entry_num = vm->e820_entry_num;
	struct e820_entry *zp_e820 = zp->entries;
	const struct e820_entry *vm_e820 = vm->e820_entries;

	if ((vm_e820 == NULL) || (entry_num == 0U) || (entry_num > E820_MAX_ENTRIES)) {
		pr_err("e820 create error");
		entry_num = 0U;
	} else {
		(void)memcpy_s((void *)zp_e820, entry_num * sizeof(struct e820_entry),
			(const void *)vm_e820, entry_num * sizeof(struct e820_entry));
	}
	return entry_num;
}

/**
 * @brief Create and initialize the zero page for the virtual machine.
 *
 * This function sets up the zero page, which is a data structure used by the Linux kernel
 * during boot. It initializes various fields in the zero page, including boot parameters,
 * RAM disk information, boot arguments, and the E820 memory map. For service VM, it may
 * also include EFI information if booting from UEFI. The function ensures that the zero
 * page is properly cleared and populated with the necessary data for the kernel to boot.
 *
 * @param[inout] vm Pointer to the `acrn_vm` structure representing the VM for which the zeropage
 *               is being created.
 * @param[in] load_params_gpa Guest physical address where the load parameters are stored.
 *
 * @return Guest physical address of the initialized zero page.
 *
 * @pre vm != NULL
 *
 * @post N/A
 */
static uint64_t create_zero_page(struct acrn_vm *vm, uint64_t load_params_gpa)
{
	struct zero_page *zeropage, *hva;
	struct sw_kernel_info *sw_kernel = &(vm->sw.kernel_info);
	struct sw_module_info *bootargs_info = &(vm->sw.bootargs_info);
	struct sw_module_info *ramdisk_info = &(vm->sw.ramdisk_info);
	uint64_t gpa, addr;

	gpa = load_params_gpa + offsetof(struct bzimage_boot_para, zeropage);
	hva = (struct zero_page *)gpa2hva(vm, gpa);
	zeropage = hva;

	stac();
	/* clear the zeropage */
	(void)memset(zeropage, 0U, sizeof(struct zero_page));

#ifdef CONFIG_MULTIBOOT2
	if (is_service_vm(vm)) {
		struct acrn_boot_info *abi = get_acrn_boot_info();

		if (boot_from_uefi(abi)) {
			struct efi_info *service_vm_efi_info = &zeropage->boot_efi_info;
			uint64_t efi_mmap_gpa = load_params_gpa + offsetof(struct bzimage_boot_para, efi_mmap);
			struct efi_memory_desc *efi_mmap_desc = (struct efi_memory_desc *)gpa2hva(vm, efi_mmap_gpa);
			uint16_t efi_mmap_desc_nr = create_service_vm_efi_mmap_desc(vm, efi_mmap_desc);

			service_vm_efi_info->loader_signature = 0x34364c45; /* "EL64" */
			service_vm_efi_info->memdesc_version = abi->uefi_info.memdesc_version;
			service_vm_efi_info->memdesc_size = sizeof(struct efi_memory_desc);
			service_vm_efi_info->memmap_size = efi_mmap_desc_nr * sizeof(struct efi_memory_desc);
			service_vm_efi_info->memmap = (uint32_t)efi_mmap_gpa;
			service_vm_efi_info->memmap_hi = (uint32_t)(efi_mmap_gpa >> 32U);
			service_vm_efi_info->systab = abi->uefi_info.systab;
			service_vm_efi_info->systab_hi = abi->uefi_info.systab_hi;
		}
	}
#endif
	/* copy part of the header into the zero page */
	hva = (struct zero_page *)sw_kernel->kernel_src_addr;
	(void)memcpy_s(&(zeropage->hdr), sizeof(zeropage->hdr),
				&(hva->hdr), sizeof(hva->hdr));

	/* See if kernel has a RAM disk */
	if (ramdisk_info->src_addr != NULL) {
		/* Copy ramdisk load_addr and size in zeropage header structure
		 */
		addr = (uint64_t)ramdisk_info->load_addr;
		zeropage->hdr.ramdisk_addr = (uint32_t)addr;
		zeropage->hdr.ramdisk_size = (uint32_t)ramdisk_info->size;
	}

	/* Copy bootargs load_addr in zeropage header structure */
	addr = (uint64_t)bootargs_info->load_addr;
	zeropage->hdr.bootargs_addr = (uint32_t)addr;

	/* set constant arguments in zero page */
	zeropage->hdr.loader_type = 0xffU;
	zeropage->hdr.load_flags |= (1U << 5U);	/* quiet */

	/* Create/add e820 table entries in zeropage */
	zeropage->e820_nentries = (uint8_t)create_zeropage_e820(zeropage, vm);
	clac();

	/* Return Physical Base Address of zeropage */
	return gpa;
}

/**
 * @brief Load the bzImage kernel into the virtual machine's memory.
 *
 * This function loads the bzImage kernel, which consists of boot parameters, real mode setup code,
 * and compressed protected mode code, into the virtual machine's memory. It focuses on loading the
 * protected mode code to its runtime location. The function also handles the loading of the ramdisk
 * and boot arguments, setting up the virtual CPU registers according to the Linux boot protocol.
 *
 * @param[inout] vm Pointer to the `acrn_vm` structure representing the VM for which the bzImage
 *               kernel is being loaded.
 * @param[inout] vcpu Pointer to the `acrn_vcpu` structure representing the BSP of the VM.
 * @param[in] load_params_gpa Guest physical address for the load parameters.
 * @param[in] kernel_load_gpa Guest physical address for the kernel load.
 *
 * @return None
 *
 * @pre vm != NULL
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static void load_bzimage(struct acrn_vm *vm, struct acrn_vcpu *vcpu,
						uint64_t load_params_gpa, uint64_t kernel_load_gpa)
{
	uint32_t i;
	uint32_t prot_code_offset, prot_code_size, kernel_entry_offset;
	uint8_t setup_sectors;
	const struct acrn_vm_config *vm_config = get_vm_config(vm->vm_id);
	struct sw_kernel_info *sw_kernel = &(vm->sw.kernel_info);
	struct sw_module_info *bootargs_info = &(vm->sw.bootargs_info);
	struct sw_module_info *ramdisk_info = &(vm->sw.ramdisk_info);
	struct zero_page *zeropage = (struct zero_page *)sw_kernel->kernel_src_addr;

	/* The bzImage file consists of three parts:
	 * boot_params(i.e. zero page) + real mode setup code + compressed protected mode code
	 * The compressed proteced mode code start at offset (setup_sectors + 1U) * 512U of bzImage.
	 * Only protected mode code need to be loaded.
	 */
	stac();
	setup_sectors = (zeropage->hdr.setup_sects == 0U) ? 4U : zeropage->hdr.setup_sects;
	clac();
	prot_code_offset = (uint32_t)(setup_sectors + 1U) * 512U;
	prot_code_size = (sw_kernel->kernel_size > prot_code_offset) ?
				(sw_kernel->kernel_size - prot_code_offset) : 0U;

	/* Copy the protected mode part kernel code to its run-time location */
	(void)copy_to_gpa(vm, (sw_kernel->kernel_src_addr + prot_code_offset), kernel_load_gpa, prot_code_size);

	if (vm->sw.ramdisk_info.size > 0U) {
		/* Use customer specified ramdisk load addr if it is configured in VM configuration,
		 * otherwise use allocated address calculated by HV.
		 */
		if (vm_config->os_config.kernel_ramdisk_addr != 0UL) {
			vm->sw.ramdisk_info.load_addr = (void *)vm_config->os_config.kernel_ramdisk_addr;
		} else {
			vm->sw.ramdisk_info.load_addr = (void *)get_initrd_load_addr(vm, kernel_load_gpa);
			if (vm->sw.ramdisk_info.load_addr == NULL) {
				pr_err("failed to load initrd for VM%d !", vm->vm_id);
			}
		}

		load_sw_module(vm, ramdisk_info);
	}

	bootargs_info->load_addr = (void *)(load_params_gpa + offsetof(struct bzimage_boot_para, cmdline));

	load_sw_module(vm, bootargs_info);

	/* 32bit kernel entry is at where protected mode code loaded */
	kernel_entry_offset = 0U;
	if (vcpu->arch.cpu_mode == CPU_MODE_64BIT) {
		/* 64bit entry is the 512bytes after the start */
		kernel_entry_offset += 512U;
	}

	sw_kernel->kernel_entry_addr = (void *)(kernel_load_gpa + kernel_entry_offset);

	/* Documentation states: ebx=0, edi=0, ebp=0, esi=ptr to
	 * zeropage
	 */
	for (i = 0U; i < NUM_GPRS; i++) {
		vcpu_set_gpreg(vcpu, i, 0UL);
	}

	/* Create Zeropage and copy Physical Base Address of Zeropage
	 * in RSI
	 */
	vcpu_set_gpreg(vcpu, CPU_REG_RSI, create_zero_page(vm, load_params_gpa));
	pr_info("%s, RSI pointing to zero page for VM %d at GPA %lx",
			__func__, vm->vm_id, vcpu_get_gpreg(vcpu, CPU_REG_RSI));
}

/**
 * @brief Load the bzImage kernel into the virtual machine and prepare for boot.
 *
 * This function attempts to load the bzImage kernel into the virtual machine's memory
 * and prepare the virtual CPU for booting from protected mode. It finds suitable memory
 * space from ve820 for the load parameters and kernel, initializes the virtual CPU registers, and
 * calls the `load_bzimage` function to perform the actual loading process. The function
 * returns an error code if memory allocation fails or if the kernel load address is invalid.
 *
 * @param[inout] vm Pointer to the `acrn_vm` structure representing the VM for which the bzImage kernel
 *               will be loaded.
 *
 * @return 0 on success, or -ENOMEM if no memory available to load bzImage.
 *
 * @retval -ENOMEM if no memory available to load bzImage
 * @retval 0 on Success
 *
 * @pre vm != NULL
 *
 * @post N/A
 */
int32_t bzimage_loader(struct acrn_vm *vm)
{
	int32_t ret = -ENOMEM;
	/* get primary vcpu */
	struct acrn_vcpu *vcpu = vcpu_from_vid(vm, BSP_CPU_ID);
	uint64_t load_params_gpa = find_space_from_ve820(vm, sizeof(struct bzimage_boot_para), MEM_4K, MEM_1M);

	if (load_params_gpa != INVALID_GPA) {
		uint64_t kernel_load_gpa = (uint64_t)get_bzimage_kernel_load_addr(vm);

		if (kernel_load_gpa != 0UL) {
			/* We boot bzImage from protected mode directly */
			init_vcpu_protect_mode_regs(vcpu, (load_params_gpa +
					offsetof(struct bzimage_boot_para, init_gdt)));

			load_bzimage(vm, vcpu, load_params_gpa, kernel_load_gpa);

			ret = 0;
		}
	}

	return ret;
}
/**
 * @}
 */
