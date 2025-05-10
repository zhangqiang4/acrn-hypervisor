/*
 * Copyright (C) 2019-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <rtl.h>
#include <errno.h>
#include <asm/per_cpu.h>
#include <asm/irq.h>
#include <boot.h>
#include <asm/pgtable.h>
#include <asm/zeropage.h>
#include <asm/mmu.h>
#include <asm/guest/vm.h>
#include <asm/guest/ept.h>
#include <logmsg.h>
#include <vacpi.h>

/**
 * @addtogroup vp-base_vboot
 *
 * @{
 */

/**
 * @file
 * @brief Functions to initialize software modules info of VM for its boot.
 *
 * The file provides functions to initialize software modules information of ACRN pre-launched VM and Service VM
 * for its load and boot process. The software modules cover kernel, ramdisk, ACPI, boot arguments, etc.
 */

/**
 * @brief The debug level of the guest VM boot process.
 */
#define DBG_LEVEL_BOOT	6U

/**
 * @brief Initialize ramdisk information for ACRN VM
 *
 * This function initializes the ramdisk information for OS of ACRN pre-launched VM and Service VM using the
 * provided module information. It sets the source address and size of the ramdisk based on the module's
 * data, allowing the VM to access the ramdisk during its operation.
 *
 * @param[inout] vm A pointer to the `acrn_vm` structure representing the VM for which the ramdisk information
 *               is being initialized.
 * @param[in] mod A pointer to the `abi_module` structure containing information about the ramdisk module,
 *            including its start address and size.
 *
 * @return None
 *
 * @pre vm != NULL
 * @pre mod != NULL
 *
 * @post N/A
 */
static void init_vm_ramdisk_info(struct acrn_vm *vm, const struct abi_module *mod)
{
	if (mod->start != NULL) {
		vm->sw.ramdisk_info.src_addr = mod->start;
		vm->sw.ramdisk_info.size = mod->size;
	}

	dev_dbg(DBG_LEVEL_BOOT, "ramdisk mod start=0x%p, size=0x%x", mod->start, mod->size);
}

/**
 * @brief Initialize ACPI Information for ACRN VM
 *
 * This function initializes the ACPI information for OS of ACRN pre-launched VM and Service VM using the
 * provided module information. It sets the source address, load address, and size of the ACPI data, allowing
 * the VM to access and utilize ACPI tables during its operation.
 *
 * @param[inout] vm A pointer to the `acrn_vm` structure representing the VM for which the ACPI information
 *               is being initialized.
 * @param[in] mod A pointer to the `abi_module` structure containing information about the ACPI module,
 *            including its start address.
 *
 * @return None
 *
 * @pre vm != NULL
 * @pre mod != NULL
 *
 * @post N/A
 *
 * @remark The function sets the ACPI load address to a predefined virtual address
 *       (`VIRT_ACPI_DATA_ADDR`) and the size to a fixed value (`ACPI_MODULE_SIZE`).
 *       This ensures that the ACPI data is correctly positioned and sized for the VM.
 */
static void init_vm_acpi_info(struct acrn_vm *vm, const struct abi_module *mod)
{
	vm->sw.acpi_info.src_addr = mod->start;
	vm->sw.acpi_info.load_addr = (void *)VIRT_ACPI_DATA_ADDR;
	vm->sw.acpi_info.size = ACPI_MODULE_SIZE;
}

/**
 * @brief Initialize kernel information for ACRN VM
 *
 * This function initializes the kernel information for OS of ACRN pre-launched VM and Service VM using the
 * provided module information. It sets the source address, size, and type of the kernel, ensuring that the
 * VM can correctly load and execute the kernel.
 *
 * @param[inout] vm A pointer to the `acrn_vm` structure representing the VM for which the kernel information
 *               is being initialized.
 * @param[in] mod A pointer to the `abi_module` structure containing information about the kernel module,
 *            including its start address and size.
 *
 * @return Returns 0 on successful initialization of the kernel information. Returns -EINVAL if the kernel type
 *         is unsupported or if the module information is invalid.
 *
 * @retval -EINVAL if the kernel type is unsupported or if the module information is invalid.
 * @retval 0 on successful initialization of the kernel information.
 *
 * @pre vm != NULL
 * @pre mod != NULL
 *
 * @post N/A
 *
 * @remark The function retrieves the kernel type from the VM's configuration and checks its validity.
 */
static int32_t init_vm_kernel_info(struct acrn_vm *vm, const struct abi_module *mod)
{
	int32_t ret = -EINVAL;
	struct acrn_vm_config *vm_config = get_vm_config(vm->vm_id);

	dev_dbg(DBG_LEVEL_BOOT, "kernel mod start=0x%p, size=0x%x",
			mod->start, mod->size);

	vm->sw.kernel_type = vm_config->os_config.kernel_type;
	if ((mod->start != NULL) && (mod->size != 0U)) {
		vm->sw.kernel_info.kernel_src_addr = mod->start;
		vm->sw.kernel_info.kernel_size = mod->size;
		if ((vm->sw.kernel_type > 0) && (vm->sw.kernel_type < KERNEL_UNKNOWN)) {
			ret = 0;
		} else {
			pr_err("Unsupported Kernel type.");
		}
	}

	return ret;
}

/**
 * @brief Kernel module string array for ACRN VM
 *
 * This array stores kernel module string associated with ACRN pre-launched VM and service VM. Each element
 * in the array corresponds to a specific VM, providing space for storing boot arguments that can be used during
 * the VM's initialization and operation.
 */
static char kernel_mod_string[PRE_VM_NUM + SERVICE_VM_NUM][MAX_BOOTARGS_SIZE] = { 0 };

/**
 * @brief Initialize boot arguments information for ACRN VM
 *
 * This function initializes the boot arguments information for OS of ACRN pre-launched VM and Service VM.
 * It sets the source address and size of the boot arguments, allowing for dynamic configuration based on
 * module strings and bootloader input. The function provides flexibility to override pre-configured boot
 * arguments with runtime input from the bootloader, such as GRUB menu options. It also handles the merging
 * of additional command-line arguments for service VM, ensuring that runtime modifications can be applied.
 *
 * @param[inout] vm A pointer to the `acrn_vm` structure representing the VM for which the boot arguments
 *               information is being initialized.
 * @param[in] abi A pointer to the `acrn_boot_info` structure containing boot information, including
 *            command-line arguments.
 *
 * @return None
 *
 * @pre vm != NULL
 * @pre abi != NULL
 *
 * @post N/A
 */
static void init_vm_bootargs_info(struct acrn_vm *vm, const struct acrn_boot_info *abi)
{
	struct acrn_vm_config *vm_config = get_vm_config(vm->vm_id);

	vm->sw.bootargs_info.src_addr = vm_config->os_config.bootargs;
	/* If module string of the kernel module exists, it would OVERRIDE the pre-configured build-in VM bootargs,
	 * which means we give user a chance to re-configure VM bootargs at bootloader runtime. e.g. GRUB menu
	 */
	if (kernel_mod_string[vm->vm_id][0] != '\0') {
		vm->sw.bootargs_info.src_addr = &kernel_mod_string[vm->vm_id][0];
	}

	if (vm_config->load_order == SERVICE_VM) {
		if (strncat_s((char *)vm->sw.bootargs_info.src_addr, MAX_BOOTARGS_SIZE, " ", 1U) == 0) {

			/* If there is cmdline from abi->cmdline, merge it with configured Service VM bootargs.
			 * This is very helpful when one of configured bootargs need to be revised at GRUB runtime
			 * (e.g. "root="), since the later one would override the previous one if multiple bootargs exist.
			 */
			if (abi->cmdline[0] != '\0') {
				if (strncat_s((char *)vm->sw.bootargs_info.src_addr, MAX_BOOTARGS_SIZE,
						abi->cmdline, (MAX_BOOTARGS_SIZE - 1U)) != 0) {
					pr_err("failed to merge mbi cmdline to Service VM bootargs!");
				}
			}
		} else {
			pr_err("no space to append Service VM bootargs!");
		}

	}

	vm->sw.bootargs_info.size = strnlen_s((const char *)vm->sw.bootargs_info.src_addr, (MAX_BOOTARGS_SIZE - 1U)) + 1U;

}

/**
 * @brief Initialize software load of ACRN VM
 *
 * This function initializes the software loading process for ACRN pre-launched VM and Service VM. It identifies
 * and loads the kernel, boot arguments, ramdisk, and ACPI modules based on the configuration and boot
 * information provided. It also handles the merging of cmdline arguments if specified in the kernel module string.
 * The function ensures that the VM is correctly set up with the necessary software modules for its operation.
 *
 * @param[inout] vm A pointer to the `acrn_vm` structure representing the VM for which the software loading is
 *               being initialized.
 * @param[in] abi A pointer to the `acrn_boot_info` structure containing boot information, including the list of
 *            modules.
 *
 * @return Returns 0 on successful initialization of the software loading process. Returns -EINVAL if the
 *         kernel module cannot be loaded.
 *
 * @retval -EINVAL if the initialization process of the VM kernel information failed.
 * @retval 0 on successful initialization of the software loading process.
 *
 * @pre vm != NULL
 * @pre abi != NULL
 *
 * @post N/A
 */
static int32_t init_vm_sw_load(struct acrn_vm *vm, const struct acrn_boot_info *abi)
{
	struct acrn_vm_config *vm_config = get_vm_config(vm->vm_id);
	struct abi_module *mod;
	int32_t ret = -EINVAL;

	dev_dbg(DBG_LEVEL_BOOT, "mod counts=%d\n", abi->mods_count);

	/* find kernel module first */
	mod = get_mod_by_tag(abi, vm_config->os_config.kernel_mod_tag);
	if (mod != NULL) {
		const char *string = (char *)hpa2hva((uint64_t)mod->string);
		uint32_t str_len = strnlen_s(string, MAX_BOOTARGS_SIZE);
		uint32_t tag_len = strnlen_s(vm_config->os_config.kernel_mod_tag, MAX_MOD_TAG_LEN);
		const char *p_chr = string + tag_len + 1; /* point to the possible start of cmdline */

		/* check whether there is a cmdline configured in kernel module string */
		if (((str_len > (tag_len + 1U))) && (is_space(*(p_chr - 1))) && (!is_eol(*p_chr))) {
			(void)strncpy_s(&kernel_mod_string[vm->vm_id][0], MAX_BOOTARGS_SIZE,
					p_chr, (MAX_BOOTARGS_SIZE - 1U));
		}

		ret = init_vm_kernel_info(vm, mod);
	}

	if (ret == 0) {
		init_vm_bootargs_info(vm, abi);

		/* check whether there is a ramdisk module */
		mod = get_mod_by_tag(abi, vm_config->os_config.ramdisk_mod_tag);
		if (mod != NULL) {
			init_vm_ramdisk_info(vm, mod);
		}

		if (is_prelaunched_vm(vm)) {
			mod = get_mod_by_tag(abi, vm_config->acpi_config.acpi_mod_tag);
			if ((mod != NULL) && (mod->size == ACPI_MODULE_SIZE)) {
				init_vm_acpi_info(vm, mod);
			} else {
				pr_err("failed to load VM %d acpi module", vm->vm_id);
			}
		}

	} else {
		pr_err("failed to load VM %d kernel module", vm->vm_id);
	}
	return ret;
}

/**
 * @brief Initialize boot information of ACRN VM
 *
 * This function initializes the boot information for OS of ACRN pre-launched VM and Service VM by setting up
 * the VM's software loading process using the ACRN boot information.
 *
 * @param[inout] vm A pointer to the `acrn_vm` structure representing the VM for which the boot information is
 *               being initialized.
 *
 * @return Returns 0 on successful initialization of the boot information. Returns -EINVAL if the software
 *         loading process fails.
 *
 * @retval -EINVAL if the software loading process fails.
 * @retval 0 on successful initialization of the boot information.
 *
 * @pre vm != NULL
 *
 * @post N/A
 *
 * @remark The function uses `stac` and `clac` to temporarily enable and disable
 *       access to user space memory, ensuring safe access to the boot information.
 */
int32_t init_vm_boot_info(struct acrn_vm *vm)
{
	struct acrn_boot_info *abi = get_acrn_boot_info();
	int32_t ret = -EINVAL;

	stac();
	ret = init_vm_sw_load(vm, abi);
	clac();

	return ret;
}
/**
 * @}
 */
