/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <asm/guest/vm.h>
#include <vboot.h>
#include <errno.h>
#include <logmsg.h>

/**
 * @addtogroup vp-base_vboot
 *
 * @{
 */

/**
 * @file
 * @brief Functions to prepare OS image and load corresponding software modules.
 *
 * The file provides functions to prepare OS image and load software modules. It supports loading images
 * in different kernel types, including bzImage, raw image, and ELF, based on the configuration. It uses the
 * appropriate loader function for each kernel type.
 */

/**
 * @brief Load software module into guest memory
 *
 * This function loads a software module into guest memory by copying it to the specified GPA. It checks
 * the validity of the software module's size and load address before performing the copy operation.
 *
 * @param[in] vm A pointer to the `acrn_vm` structure representing the guest which the software module
 *            will be loaded into.
 * @param[in] sw_module A pointer to the `sw_module_info` structure containing information about the
 *            software module to be loaded, including its size, source address, and load address.
 *
 * @return None
 *
 * @pre vm != NULL
 * @pre sw_module != NULL
 *
 * @post N/A
 */
void load_sw_module(struct acrn_vm *vm, struct sw_module_info *sw_module)
{
	if ((sw_module->size != 0) && (sw_module->load_addr != NULL)) {
		(void)copy_to_gpa(vm, sw_module->src_addr, (uint64_t)sw_module->load_addr, sw_module->size);
	}
}

/**
 * @brief Prepare OS image for ACRN guest
 *
 * This function prepares the operating system image for ACRN pre-launched VM or Service VM by selecting
 * the appropriate loader based on the kernel type, loading the OS image, and setting the entry point for the
 * primary VCPU. It also copies the Guest OS ACPI information to its designated load location.
 *
 * @param[in] vm A pointer to the `acrn_vm` structure representing the guest for which the OS image
 *         	  is being prepared.
 *
 * @return Returns 0 on successful preparation of the OS image. Returns -EINVAL if the kernel type
 *         is not recognized or if loading fails.
 *
 * @retval -EINVAL if the kernel type is not recognized or if loading fails.
 * @retval 0 on successful preparation of the OS image.
 *
 * @pre vm != NULL
 *
 * @post N/A
 */
int32_t prepare_os_image(struct acrn_vm *vm)
{
	int32_t ret = -EINVAL;
	/* get primary vcpu */
	struct acrn_vcpu *vcpu = vcpu_from_vid(vm, BSP_CPU_ID);
	struct sw_module_info *acpi_info = &(vm->sw.acpi_info);

	switch (vm->sw.kernel_type) {
#ifdef CONFIG_GUEST_KERNEL_BZIMAGE
	case KERNEL_BZIMAGE:
		ret = bzimage_loader(vm);
		break;
#endif
#ifdef CONFIG_GUEST_KERNEL_RAWIMAGE
	case KERNEL_RAWIMAGE:
		ret = rawimage_loader(vm);
		break;
#endif
#ifdef CONFIG_GUEST_KERNEL_ELF
	case KERNEL_ELF:
		ret = elf_loader(vm);
		break;
#endif
	default:
		ret = -EINVAL;
		break;
	}

	if (ret == 0) {
		/* Copy Guest OS ACPI to its load location */
		load_sw_module(vm, acpi_info);
		/* Set VCPU entry point to kernel entry */
		vcpu_set_rip(vcpu, (uint64_t)vm->sw.kernel_info.kernel_entry_addr);
		pr_info("%s, VM %hu VCPU %hu Entry: 0x%p ", __func__, vm->vm_id, vcpu->vcpu_id,
			vm->sw.kernel_info.kernel_entry_addr);
	}

	return ret;
}
/**
 * @}
 */
