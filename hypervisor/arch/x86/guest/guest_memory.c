/*
 * Copyright (C) 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <errno.h>
#include <asm/vmx.h>
#include <asm/guest/guest_memory.h>
#include <asm/guest/vcpu.h>
#include <asm/guest/vm.h>
#include <asm/guest/vmcs.h>
#include <asm/mmu.h>
#include <asm/guest/ept.h>
#include <logmsg.h>

/**
 * @defgroup vp-base_guest-mem vp-base.guest-mem Guest memory
 * @ingroup vp-base
 * @brief Guest memory implementation
 *
 * @{
 */

/**
 * @file
 * @brief Implementation of guest memory access.
 *
 * This file implements functions for translating guest virtual addresses to guest physical addresses,
 * and provide utilities for accessing guest memory from hypervisor.
 */

/**
 * @brief Structure to hold page walk information
 *
 * This structure holds the information required to walk the guest page table.
 */
struct page_walk_info {
	uint64_t top_entry;		/**< Top level paging structure entry */
	uint32_t level;			/**< Paging level */
	uint32_t width;			/**< Paging width */
	bool is_user_mode_access;	/**< User mode access flag */
	bool is_write_access;		/**< Write access flag */
	bool is_inst_fetch;		/**< Instruction fetch flag */
	bool pse;			/**< CR4.PSE for 32bit paging, true for PAE/4-level paging */
	bool wp;			/**< CR0.WP */
	bool nxe;			/**< MSR_IA32_EFER_NXE_BIT */

	bool is_smap_on;		/**< SMAP enabled flag */
	bool is_smep_on;		/**< SMEP enabled flag */
};

/**
 * @brief Get the paging mode of a vCPU
 *
 * This function determines the paging mode of the specified vCPU based on its
 * current state. The paging mode can be one of the following:
 * - PAGING_MODE_0_LEVEL: Non-paging mode.
 * - PAGING_MODE_2_LEVEL: 32-bit paging.
 * - PAGING_MODE_3_LEVEL: PAE (Physical Address Extension) paging.
 * - PAGING_MODE_4_LEVEL: 4-level paging (used in long mode).
 *
 * @param[in] vcpu Pointer to the vCPU structure.
 *
 * @return Paging mode of the vCPU.
 */
static enum vm_paging_mode get_vcpu_paging_mode(struct acrn_vcpu *vcpu)
{
	enum vm_paging_mode ret = PAGING_MODE_0_LEVEL;	/* non-paging */

	if (is_paging_enabled(vcpu)) {
		if (is_pae(vcpu)) {
			if (is_long_mode(vcpu)) {
				ret = PAGING_MODE_4_LEVEL;	/* 4-level paging */
			} else {
				ret = PAGING_MODE_3_LEVEL;	/* PAE paging */
			}
		} else {
			ret = PAGING_MODE_2_LEVEL;	/* 32-bit paging */
		}
	}

	return ret;
}

/**
 * @brief Translate Guest Virtual Address (GVA) to Guest Physical Address (GPA)
 *
 * This function translates a given Guest Virtual Address (GVA) to a Guest Physical Address (GPA) using the provided
 * page walk information to walk the guest page table.
 *
 *  The function performs the following checks during the translation:
 * - Ensures the page walk level is valid.
 * - Checks if the page table entry is present.
 * - Verifies read/write permissions.
 * - Checks for execution permissions (NX bit).
 * - Verifies user/supervisor mode access.
 * - Applies SMAP/SMEP checks if enabled.
 * If any of these checks fail, the function sets appropriate page fault error code and returns -EFAULT.
 *
 * @param[in]    vcpu     Pointer to the virtual CPU structure.
 * @param[in]    pw_info  Pointer to the page walk information structure.
 * @param[in]    gva      Guest Virtual Address to be translated.
 * @param[out]   gpa      Pointer to store the resulting Guest Physical Address.
 * @param[inout] err_code Pointer to the page fault error code.
 *
 * @return 0 on success, or error code if the translation fails.
 *
 * @retval 0       Success
 * @retval -EINVAL The page walk level is less than 1
 * @retval -EFAULT A page fault occurs during translation
 *
 * @pre vcpu != NULL && pw_info != NULL && gpa != NULL && err_code != NULL
 */
static int32_t local_gva2gpa_common(struct acrn_vcpu *vcpu, const struct page_walk_info *pw_info,
	uint64_t gva, uint64_t *gpa, uint32_t *err_code)
{
	uint32_t i;
	uint64_t index;
	uint32_t shift;
	void *base;
	uint64_t entry = 0U;
	uint64_t addr;
	uint64_t page_size = PAGE_SIZE_4K;
	int32_t ret = 0;
	int32_t fault = 0;
	bool is_user_mode_addr = true;

	if (pw_info->level < 1U) {
		ret = -EINVAL;
	} else {
		addr = pw_info->top_entry;
		i = pw_info->level;
		stac();

		while ((i != 0U) && (fault == 0)) {
			i--;

			addr = addr & IA32E_REF_MASK;
			base = gpa2hva(vcpu->vm, addr);
			if (base == NULL) {
				fault = 1;
			} else {
				shift = (i * pw_info->width) + 12U;
				index = (gva >> shift) & ((1UL << pw_info->width) - 1UL);
				page_size = 1UL << shift;

				if (pw_info->width == 10U) {
					uint32_t *base32 = (uint32_t *)base;
					/* 32bit entry */
					entry = (uint64_t)(*(base32 + index));
				} else {
					uint64_t *base64 = (uint64_t *)base;
					entry = *(base64 + index);
				}

				/* check if the entry present */
				if ((entry & PAGE_PRESENT) == 0U) {
					fault = 1;
				}

					/* check for R/W */
				if ((fault == 0) && ((entry & PAGE_RW) == 0U)) {
					if (pw_info->is_write_access  &&
					    (pw_info->is_user_mode_access || pw_info->wp)) {
						/* Case1: Supermode and wp is 1
						 * Case2: Usermode */
						fault = 1;
					}
				}
			}

			/* check for nx, since for 32-bit paging, the XD bit is
			 * reserved(0), use the same logic as PAE/4-level paging */
			if ((fault == 0) && pw_info->is_inst_fetch && pw_info->nxe &&
				    ((entry & PAGE_NX) != 0U)) {
				fault = 1;
			}

			/* check for U/S */
			if ((fault == 0) && ((entry & PAGE_USER) == 0U)) {
				is_user_mode_addr = false;

				if (pw_info->is_user_mode_access) {
					fault = 1;
				}
			}


			if ((fault == 0) && pw_info->pse &&
				((i > 0U) && ((entry & PAGE_PSE) != 0U))) {
					break;
			}
			addr = entry;
		}

		/* When SMAP/SMEP is on, we only need to apply check when address is
		 * user-mode address.
		 * Also SMAP/SMEP only impact the supervisor-mode access.
		 */
		/* if smap is enabled and supervisor-mode access */
		if ((fault == 0) && pw_info->is_smap_on && (!pw_info->is_user_mode_access) &&
			is_user_mode_addr && ((vcpu_get_rflags(vcpu) & RFLAGS_AC) == 0UL)) {
			fault = 1;
		}

		/* instruction fetch from user-mode address, smep on */
		if ((fault == 0) && pw_info->is_smep_on && (!pw_info->is_user_mode_access) &&
			is_user_mode_addr && pw_info->is_inst_fetch) {
			fault = 1;
		}

		if (fault == 0) {
			entry >>= shift;
			/* shift left 12bit more and back to clear XD/Prot Key/Ignored bits */
			entry <<= (shift + 12U);
			entry >>= 12U;
			*gpa = entry | (gva & (page_size - 1UL));
		}

		clac();
		if (fault != 0) {
			ret = -EFAULT;
			*err_code |= PAGE_FAULT_P_FLAG;
		}
	}
	return ret;
}

/**
 * @brief Translate Guest Virtual Address (GVA) to Guest Physical Address (GPA) using PAE paging
 *
 * This function translates a given GVA to a GPA for a specific vCPU using the Page Address Extension (PAE) paging
 * mechanism.
 *
 * @param[in]    vcpu     The virtual CPU for which the address translation is performed.
 * @param[in]    pw_info  Pointer to the page walk information structure.
 * @param[in]    gva      The guest virtual address to be translated.
 * @param[out]   gpa      Pointer to store the resulting guest physical address.
 * @param[inout] err_code Pointer to the page fault error code.
 *
 * @return 0 on success, or error code if `Convert Guest Physical Address (GPA) to Host Virtual Address (HVA)` or
 * `Translate Guest Virtual Address (GVA) to Guest Physical Address (GPA)` fails.
 *
 * @retval 0       Success
 * @retval -EFAULT A page fault occurs during translation
 *
 * @pre vcpu != NULL && pw_info != NULL && gpa != NULL && err_code != NULL
 */
static int32_t local_gva2gpa_pae(struct acrn_vcpu *vcpu, struct page_walk_info *pw_info,
	uint64_t gva, uint64_t *gpa, uint32_t *err_code)
{
	uint32_t index;
	uint64_t *base;
	uint64_t entry;
	uint64_t addr;
	int32_t ret = -EFAULT;

	addr = get_pae_pdpt_addr(pw_info->top_entry);
	base = (uint64_t *)gpa2hva(vcpu->vm, addr);
	if (base != NULL) {
		index = (uint32_t)gva >> 30U;
		stac();
		entry = base[index];
		clac();

		if ((entry & PAGE_PRESENT) != 0U) {
			pw_info->level = 2U;
			pw_info->top_entry = entry;
			ret = local_gva2gpa_common(vcpu, pw_info, gva, gpa, err_code);
		}
	}

	return ret;
}

/**
 * @brief Convert Guest Virtual Address (GVA) to Guest Physical Address (GPA)
 *
 * This function determines the paging mode on specified virtual CPU (vCPU), translates a given Guest Virtual Address
 * (GVA) to a Guest Physical Address (GPA) with appropriate translation, and sets corresponding page fault error code.
 *
 * If a protection violation detected during page walk, this function still will give the gpa translated,
 * it is up to caller to decide if it need to inject a page fault or not.
 *
 * @param[in]    vcpu     Pointer to the virtual CPU structure.
 * @param[in]    gva      The Guest Virtual Address to be translated.
 * @param[out]   gpa      Pointer to store the resulting Guest Physical Address.
 * @param[inout] err_code Pointer to the page fault error code.
 *
 * @return 0 on success, or error code if the translation fails.
 *
 * @retval 0       Success
 * @retval -EINVAL gpa or err_code is NULL
 * @retval -EFAULT A page fault occurs during translation
 *
 * @pre vcpu != NULL && gpa != NULL && err_code != NULL
 */
int32_t gva2gpa(struct acrn_vcpu *vcpu, uint64_t gva, uint64_t *gpa,
	uint32_t *err_code)
{
	enum vm_paging_mode pm = get_vcpu_paging_mode(vcpu);
	struct page_walk_info pw_info;
	int32_t ret = 0;

	if ((gpa == NULL) || (err_code == NULL)) {
		ret = -EINVAL;
	} else {
		*gpa = 0UL;

		pw_info.top_entry = exec_vmread(VMX_GUEST_CR3);
		pw_info.level = (uint32_t)pm;
		pw_info.is_write_access = ((*err_code & PAGE_FAULT_WR_FLAG) != 0U);
		pw_info.is_inst_fetch = ((*err_code & PAGE_FAULT_ID_FLAG) != 0U);

		/* SDM vol3 27.3.2
		 * If the segment register was unusable, the base, select and some
		 * bits of access rights are undefined. With the exception of
		 * DPL of SS
		 * and others.
		 * So we use DPL of SS access rights field for guest DPL.
		 */
		pw_info.is_user_mode_access = (((exec_vmread32(VMX_GUEST_SS_ATTR) >> 5U) & 0x3U) == 3U);
		pw_info.pse = true;
		pw_info.nxe = ((vcpu_get_efer(vcpu) & MSR_IA32_EFER_NXE_BIT) != 0UL);
		pw_info.wp = ((vcpu_get_cr0(vcpu) & CR0_WP) != 0UL);
		pw_info.is_smap_on = ((vcpu_get_cr4(vcpu) & CR4_SMAP) != 0UL);
		pw_info.is_smep_on = ((vcpu_get_cr4(vcpu) & CR4_SMEP) != 0UL);

		*err_code &=  ~PAGE_FAULT_P_FLAG;

		if (pm == PAGING_MODE_4_LEVEL) {
			pw_info.width = 9U;
			ret = local_gva2gpa_common(vcpu, &pw_info, gva, gpa, err_code);
		} else if (pm == PAGING_MODE_3_LEVEL) {
			pw_info.width = 9U;
			ret = local_gva2gpa_pae(vcpu, &pw_info, gva, gpa, err_code);
		} else if (pm == PAGING_MODE_2_LEVEL) {
			pw_info.width = 10U;
			pw_info.pse = ((vcpu_get_cr4(vcpu) & CR4_PSE) != 0UL);
			pw_info.nxe = false;
			ret = local_gva2gpa_common(vcpu, &pw_info, gva, gpa, err_code);
		} else {
			*gpa = gva;
		}

		if (ret == -EFAULT) {
			if (pw_info.is_user_mode_access) {
				*err_code |= PAGE_FAULT_US_FLAG;
			}
		}
	}

	return ret;
}

/**
 * @brief Copy memory between guest physical address (GPA) and hypervisor within a single page
 *
 * This function copies a specified size of memory between a guest physical address (GPA) and hypervisor within a
 * single page. It handles the translation from GPA to host physical address (HPA) and performs the memory copy
 * operation.
 *
 * @param[in]    vm          Pointer to the virtual machine structure.
 * @param[inout] h_ptr       Pointer to the hypervisor memory where data will be copied to or from.
 * @param[in]    gpa         Guest physical address from where data will be copied to or from.
 * @param[in]    size        Size of the memory to be copied.
 * @param[in]    fix_pg_size Fixed page size to be used for the copy operation. If 0, the actual page size will be used.
 * @param[in]    cp_from_vm  Boolean flag indicating the direction of the copy operation.
 *                           - true: Copy from guest memory to hypervisor memory.
 *                           - false: Copy from hypervisor memory to guest memory.
 *
 * @return Number of bytes copied, or 0 If the GPA is invalid or address translation fails.
 */
static inline uint32_t local_copy_gpa(struct acrn_vm *vm, void *h_ptr, uint64_t gpa,
	uint32_t size, uint32_t fix_pg_size, bool cp_from_vm)
{
	uint64_t hpa;
	uint32_t offset_in_pg, len, pg_size;
	void *g_ptr;

	hpa = local_gpa2hpa(vm, gpa, &pg_size);
	if (hpa == INVALID_HPA) {
		pr_err("%s,vm[%hu] gpa 0x%lx,GPA is unmapping",
			__func__, vm->vm_id, gpa);
		len = 0U;
	} else {

		if (fix_pg_size != 0U) {
			pg_size = fix_pg_size;
		}

		offset_in_pg = (uint32_t)gpa & (pg_size - 1U);
		len = (size > (pg_size - offset_in_pg)) ? (pg_size - offset_in_pg) : size;

		g_ptr = hpa2hva(hpa);

		stac();
		if (cp_from_vm) {
			(void)memcpy_s(h_ptr, len, g_ptr, len);
		} else {
			(void)memcpy_s(g_ptr, len, h_ptr, len);
		}
		clac();
	}

	return len;
}

/**
 * @brief Copy memory between host and guest physical address space.
 *
 * This function copies memory between the host and guest physical address space. The direction of the copy
 * is determined by the cp_from_vm parameter. Caller should make sure gpa is continuous.
 *
 * @param[in]    vm         Pointer to the VM structure.
 * @param[inout] h_ptr_arg  Pointer to the hypervisor memory where data will be copied to or from.
 * @param[in]    gpa_arg    Guest physical address.
 * @param[in]    size_arg   Size of the memory to copy.
 * @param[in]    cp_from_vm Boolean flag indicating the direction of the copy.
 *                          - true: Copy from guest to host.
 *                          - false: Copy from host to guest.
 *
 * @return 0 on success, or error code if `Copy memory between guest physical address (GPA) and hypervisor within a
 * single page` fails.
 *
 * @retval 0       Success
 * @retval -EINVAL An error occurs during the copy.
 */
static inline int32_t copy_gpa(struct acrn_vm *vm, void *h_ptr_arg, uint64_t gpa_arg,
	uint32_t size_arg, bool cp_from_vm)
{
	void *h_ptr = h_ptr_arg;
	uint32_t len;
	uint64_t gpa = gpa_arg;
	uint32_t size = size_arg;
	int32_t err = 0;

	while (size > 0U) {
		len = local_copy_gpa(vm, h_ptr, gpa, size, 0U, cp_from_vm);
		if (len == 0U) {
			err = -EINVAL;
			break;
		}
		gpa += len;
		h_ptr += len;
		size -= len;
	}

	return err;
}

/**
 * @brief Copy memory from Guest Physical Address (GPA) to hypervisor memory
 *
 * This function copies a specified amount of memory from a given GPA in a VM to hypervisor
 *
 * @param[in]  vm    Pointer to the VM structure.
 * @param[out] h_ptr Pointer to where the data will be copied to.
 * @param[in]  gpa   Guest Physical Address from which the memory will be copied.
 * @param[in]  size  Size of the memory to be copied in bytes.
 *
 * @return 0 on success, or error code if `Copy memory between host and guest physical address space` fails.
 *
 * @retval 0       Success
 * @retval -EINVAL Invalid parameters
 * @retval -EFAULT Failed to copy memory
 */
int32_t copy_from_gpa(struct acrn_vm *vm, void *h_ptr, uint64_t gpa, uint32_t size)
{
	int32_t ret = 0;

	ret = copy_gpa(vm, h_ptr, gpa, size, 1);
	if (ret != 0) {
		pr_err("Unable to copy GPA 0x%llx from VM%d to HPA 0x%llx\n", gpa, vm->vm_id, (uint64_t)h_ptr);
	}

	return ret;
}

/**
 * @brief Copy memory from hypervisor memory to given Guest Physical Address (GPA)
 *
 * This function copies a specified amount of memory from hypervisor to a given GPA in a VM
 *
 * @param[in]  vm    Pointer to the VM structure.
 * @param[in]  h_ptr Pointer to where the data will be copied from.
 * @param[in]  gpa   Guest Physical Address to which the memory will be copied.
 * @param[in]  size  Size of the memory to be copied in bytes.
 *
 * @return 0 on success, or error code if `Copy memory between host and guest physical address space` fails.
 *
 * @retval 0       Success
 * @retval -EINVAL Invalid parameters
 * @retval -EFAULT Failed to copy memory
 */
int32_t copy_to_gpa(struct acrn_vm *vm, void *h_ptr, uint64_t gpa, uint32_t size)
{
	int32_t ret = 0;

	ret = copy_gpa(vm, h_ptr, gpa, size, 0);
	if (ret != 0) {
		pr_err("Unable to copy HPA 0x%llx to GPA 0x%llx in VM%d\n", (uint64_t)h_ptr, gpa, vm->vm_id);
	}

	return ret;
}

/**
 * @brief Copy guest virtual address (GVA) to/from hypervisor memory.
 *
 * This function copies data between a guest virtual address (GVA) and hypervisor memory.
 * It translates the GVA to a guest physical address (GPA) and then performs the copy.
 *
 * @param[in]    vcpu        Pointer to the virtual CPU structure.
 * @param[inout] h_ptr_arg   Pointer to the hypervisor memory buffer.
 * @param[in]    gva_arg     Guest virtual address to copy from/to.
 * @param[in]    size_arg    Size of the data to copy.
 * @param[inout] err_code    Pointer to the page fault error code if page faults.
 * @param[out]   fault_addr  Pointer to store the page fault address.
 * @param[in]    cp_from_vm  Boolean flag indicating the direction of the copy.
 *                         - true: Copy from VM to hypervisor memory.
 *                         - false: Copy from hypervisor memory to VM.
 *
 * @return 0 on success, or error code if gva2gpa or `Copy memory between guest physical address (GPA) and hypervisor
 * within a single page` fails
 *
 * @retval 0       Success
 * @retval -EINVAL An error occurs during the copy.
 * @retval -EFAULT A page fault occurs during GVA->GPA translation.
 *
 * @pre vcpu != NULL && h_ptr_arg != NULL && err_code != NULL && fault_addr != NULL
 */
static inline int32_t copy_gva(struct acrn_vcpu *vcpu, void *h_ptr_arg, uint64_t gva_arg,
	uint32_t size_arg, uint32_t *err_code, uint64_t *fault_addr,
	bool cp_from_vm)
{
	void *h_ptr = h_ptr_arg;
	uint64_t gpa = 0UL;
	int32_t ret = 0;
	uint32_t len;
	uint64_t gva = gva_arg;
	uint32_t size = size_arg;

	while ((size > 0U) && (ret == 0)) {
		ret = gva2gpa(vcpu, gva, &gpa, err_code);
		if (ret >= 0) {
			len = local_copy_gpa(vcpu->vm, h_ptr, gpa, size, PAGE_SIZE_4K, cp_from_vm);
			if (len != 0U) {
				gva += len;
				h_ptr += len;
				size -= len;
			} else {
				ret =  -EINVAL;
			}
		} else {
			*fault_addr = gva;
			pr_err("error[%d] in GVA2GPA, err_code=0x%x", ret, *err_code);
		}
	}

	return ret;
}

/**
 * @brief Copy data from guest virtual address (GVA) to hypervisor memory.
 *
 * This function copies a specified amount of data from a guest virtual address (GVA) to hypervisor memory
 * in the context of a given virtual CPU.
 *
 * @param[in]    vcpu       Pointer to the vCPU structure.
 * @param[out]   h_ptr      Pointer to the destination in hypervisor memory.
 * @param[in]    gva        Guest virtual address where the data should be copied to.
 * @param[in]    size       Size of the data to be copied, in bytes.
 * @param[inout] err_code   Pointer to the page fault error code if page faults.
 * @param[out]   fault_addr Pointer to store the page fault address.
 *
 * @return 0 on success, or error code if `Copy guest virtual address (GVA) to/from hypervisor memory` fails.
 *
 * @retval 0       Success
 * @retval -EINVAL An error occurs during the copy.
 * @retval -EFAULT A page fault occurs during GVA->GPA translation.
 *
 * @pre vcpu != NULL && h_ptr != NULL && err_code != NULL && fault_addr != NULL
 */
int32_t copy_from_gva(struct acrn_vcpu *vcpu, void *h_ptr, uint64_t gva,
	uint32_t size, uint32_t *err_code, uint64_t *fault_addr)
{
	return copy_gva(vcpu, h_ptr, gva, size, err_code, fault_addr, 1);
}

/**
 * @brief Copy data from hypervisor memory to guest virtual address (GVA).
 *
 * This function copies a specified amount of data from hypervisor memory to a guest virtual address (GVA)
 * in the context of a given virtual CPU.
 *
 * @param[in]    vcpu       Pointer to the vCPU structure.
 * @param[in]    h_ptr      Pointer to the source in hypervisor memory.
 * @param[in]    gva        Guest virtual address where the data should be copied to.
 * @param[in]    size       Size of the data to be copied, in bytes.
 * @param[inout] err_code   Pointer to the page fault error code if page faults.
 * @param[out]   fault_addr Pointer to store the page fault address.
 *
 * @return 0 on success, or error code if `Copy guest virtual address (GVA) to/from hypervisor memory` fails.
 *
 * @retval 0       Success
 * @retval -EINVAL An error occurs during the copy.
 * @retval -EFAULT A page fault occurs during GVA->GPA translation.
 *
 * @pre vcpu != NULL && h_ptr != NULL && err_code != NULL && fault_addr != NULL
 */
int32_t copy_to_gva(struct acrn_vcpu *vcpu, void *h_ptr, uint64_t gva,
	uint32_t size, uint32_t *err_code, uint64_t *fault_addr)
{
	return copy_gva(vcpu, h_ptr, gva, size, err_code, fault_addr, false);
}

/**
 * @brief Convert Guest Physical Address (GPA) to Host Virtual Address (HVA)
 *
 * This function translates a given Guest Physical Address (GPA) to the corresponding
 * Host Virtual Address (HVA) for a specified virtual machine (VM).
 *
 * @param[in] vm Pointer to the virtual machine structure
 * @param[in] x  Guest Physical Address (GPA) to be translated
 *
 * @return Translated Host Virtual Address(HVA), or INVALID_HPA if the GPA is not mapped in the VM.
 */
void *gpa2hva(struct acrn_vm *vm, uint64_t x)
{
	uint64_t hpa = gpa2hpa(vm, x);
	return (hpa == INVALID_HPA) ? NULL : hpa2hva(hpa);
}

/**
 * @}
 */
