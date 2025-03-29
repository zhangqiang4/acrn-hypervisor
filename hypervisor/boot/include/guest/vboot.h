/*
 * Copyright (C) 2019-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @defgroup vp-base_vboot vp-base.vboot
 * @ingroup vp-base
 * @brief API definitions and functions for booting ACRN Service VM or pre-launched VM.
 *
 * The component provides methods for the hypervisor to load and boot guest VM images in Linux bzImage format,
 * standard ELF format, or self-defined RAW format.
 *
 * @{
 */

/**
 * @file
 * @brief Interface definitions for booting ACRN guest VMs.
 *
 * The file provides API for ACRN hypervisor to boot ACRN Service VM or pre-launched VM.
 */

#ifndef VBOOT_H

#define VBOOT_H

int32_t init_vm_boot_info(struct acrn_vm *vm);
void load_sw_module(struct acrn_vm *vm, struct sw_module_info *sw_module);
int32_t prepare_os_image(struct acrn_vm *vm);

#ifdef CONFIG_GUEST_KERNEL_BZIMAGE
int32_t bzimage_loader(struct acrn_vm *vm);
#endif
#ifdef CONFIG_GUEST_KERNEL_RAWIMAGE
int32_t rawimage_loader(struct acrn_vm *vm);
#endif
#ifdef CONFIG_GUEST_KERNEL_ELF
int32_t elf_loader(struct acrn_vm *vm);
#endif

#endif /* end of include guard: VBOOT_H */
/**
 * @}
 */
