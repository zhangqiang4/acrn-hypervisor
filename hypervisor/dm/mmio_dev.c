/*
 * Copyright (C) 2020-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>
#include <util.h>
#include <acrn_hv_defs.h>
#include <asm/pgtable.h>
#include <asm/guest/vm.h>
#include <asm/guest/ept.h>
#include <debug/logmsg.h>

/**
 * @addtogroup vp-dm_mmio-dev
 *
 * @{
 */

/**
 * @file
 * @brief Functions to assign or deassign a MMIO device to/from a vm.
 *
 * This file contains the implementation of functions that manage the assignment and deassignment of MMIO devices
 * to a VM. These operations involve mapping and unmapping the MMIO device's physical memory regions into the VM's
 * address space through the EPT. The functions ensure proper alignment and validity of the memory regions before
 * performing the operations.
 */

/**
 * @brief Assign a MMIO device to a VM.
 *
 * This function performs MMIO device passthrough by mapping the MMIO device's physical memory regions into the
 * address space of a VM. It ensures that the guest physical address, host physical address and the size of the
 * MMIO region are page-aligned. If the alignment checks pass, the MMIO memory region is added to the VM's EPT.
 * The function returns 0 on the success or -EINVAL if any alignment checks fail.
 *
 * @param[inout] vm A pointer to VM data structure to which the MMIO device will be assigned.
 * @param[in] mmiodev A pointer to MMIO device structure that will be assigned to the VM.
 *
 * @return An int32_t value indicating whether the MMIO device was successfully assigned to the VM.
 *
 * @retval 0 MMIO device assign success.
 * @retval -EINVAL Alignment checks fail.
 *
 * @pre vm != NULL
 * @pre mmiodev != NULL
 *
 * @remark On success, the MMIO regions are mapped into VM's EPT.
 */

int32_t assign_mmio_dev(struct acrn_vm *vm, const struct acrn_mmiodev *mmiodev)
{
	int32_t i, ret = 0;
	const struct acrn_mmiores *res;

	for (i = 0; i < MMIODEV_RES_NUM; i++) {
		res = &mmiodev->res[i];
		if (mem_aligned_check(res->user_vm_pa, PAGE_SIZE) &&
			mem_aligned_check(res->host_pa, PAGE_SIZE) &&
			mem_aligned_check(res->size, PAGE_SIZE)) {
			ept_add_mr(vm, (uint64_t *)vm->arch_vm.nworld_eptp, res->host_pa,
				is_service_vm(vm) ? res->host_pa : res->user_vm_pa,
				res->size, EPT_RWX | (res->mem_type & EPT_MT_MASK));
		} else {
			pr_err("%s invalid mmio res[%d] gpa:0x%lx hpa:0x%lx size:0x%lx",
				__FUNCTION__, i, res->user_vm_pa, res->host_pa, res->size);
			ret = -EINVAL;
			break;
		}
	}

	return ret;
}

/**
 * @brief Deassign a MMIO device to a VM.
 *
 * This function reverses the operation performed by assign_mmio_dev. It removes the mappings of the MMIO device's
 * physical memory regions from the VM's EPT. It checks that the guest physical address and the size of the MMIO
 * region are page-aligned and that the memory region is valid before removing it. The function returns 0 on
 * success or -EINVAL if any alignment checks fail.
 *
 * @param[inout] vm A pointer to the VM data structure from which the MMIO device will be deassigned.
 * @param[in] mmiodev A pointer to the MMIO device structure that will be deassigned from the VM.
 *
 * @return An int32_t value indicating whether the MMIO device was successfully deassigned from the VM.
 *
 * @retval 0 Deassign a MMIO device to a VM success, or if the memory region of the MMIO device is not mapped to
 *           the given VM.
 * @retval -EINVAL Alignment check fail.
 *
 * @pre vm != NULL
 * @pre mmiodev != NULL
 *
 * @remark On success, the MMIO regions are unmapped from the VM's EPT.
 */
int32_t deassign_mmio_dev(struct acrn_vm *vm, const struct acrn_mmiodev *mmiodev)
{
	int32_t i, ret = 0;
	uint64_t gpa;
	const struct acrn_mmiores *res;

	for (i = 0; i < MMIODEV_RES_NUM; i++) {
		res = &mmiodev->res[i];
		gpa = is_service_vm(vm) ? res->host_pa : res->user_vm_pa;
		if (ept_is_valid_mr(vm, gpa, res->size)) {
			if (mem_aligned_check(gpa, PAGE_SIZE) &&
				mem_aligned_check(res->size, PAGE_SIZE)) {
				ept_del_mr(vm, (uint64_t *)vm->arch_vm.nworld_eptp, gpa, res->size);
			} else {
				pr_err("%s invalid mmio res[%d] gpa:0x%lx hpa:0x%lx size:0x%lx",
					__FUNCTION__, i, res->user_vm_pa, res->host_pa, res->size);
				ret = -EINVAL;
				break;
			}
		}
	}

	return ret;
}

/**
 * @}
 */
