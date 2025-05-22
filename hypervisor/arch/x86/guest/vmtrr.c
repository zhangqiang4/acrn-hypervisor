/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <asm/guest/vmtrr.h>
#include <asm/msr.h>
#include <asm/mmu.h>
#include <asm/pgtable.h>
#include <asm/guest/ept.h>
#include <asm/guest/vcpu.h>
#include <asm/guest/vm.h>
#include <logmsg.h>
#include <vacpi.h>

/**
 * @addtogroup vp-base_vmtrr
 *
 * @{
 */

/**
 * @file
 * @brief Virtual Memory Type Range Register (MTRR) implementation
 *
 * This file provides functions to initialize and manage virtual MTRRs for VMs, handling MTRR related
 * MSR read and write operations.
 */

/**
 * @brief Check if the MSR is a IA32_MTRR_PHYSMASKn register.
 *
 * Determines if the given MSR address corresponds to one of the MTRR physical mask registers.
 * These registers are at addresses 0x201, 0x203, 0x205,..., 0x213 (odd-numbered addresses
 * in the range).
 *
 * @param[in] msr MSR address to check
 *
 * @return Whether the MSR is a physical mask register
 *
 * @retval true The MSR is a IA32_MTRR_PHYSMASKn register
 * @retval false The MSR is not a IA32_MTRR_PHYSMASKn register
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline bool is_mtrr_mask_register(uint32_t msr)
{
	return ((msr >= MSR_IA32_MTRR_PHYSMASK_0) && (msr <= MSR_IA32_MTRR_PHYSMASK_9) && ((msr % 2U) != 0U));
}

/**
 * @brief Check if the MSR is a IA32_MTRR_PHYSBASEn register.
 *
 * Determines if the given MSR address corresponds to one of the MTRR physical base registers.
 * These registers are at addresses 0x200, 0x202, 0x204,..., 0x212 (even-numbered addresses
 * in the range).
 *
 * @param[in] msr MSR address to check
 *
 * @return Whether the MSR is a physical base register
 *
 * @retval true The MSR is a IA32_MTRR_PHYSBASEn register
 * @retval false The MSR is not a IA32_MTRR_PHYSBASEn register
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline bool is_mtrr_base_register(uint32_t msr)
{
	return ((msr >= MSR_IA32_MTRR_PHYSBASE_0) && (msr <= MSR_IA32_MTRR_PHYSBASE_9) && ((msr % 2U) == 0U));
}

/**
 * @brief Get the index of the variable MTRR register.
 *
 * Calculates the index (0-9) of the variable MTRR register pair based on the MSR address.
 * Used to index into the variable_range array in the acrn_vmtrr structure.
 *
 * @param[in] msr MSR address of one variable MTRR register
 *
 * @return The index of the variable MTRR register
 *
 * @pre msr >= MSR_IA32_MTRR_PHYSBASE_0 && msr <= MSR_IA32_MTRR_PHYSMASK_9
 *
 * @post N/A
 */
static inline uint32_t get_variable_mtrr_index(uint32_t msr)
{
	return (msr - MSR_IA32_MTRR_PHYSBASE_0) / 2U;
}

/**
 * @brief Round up the variable MTRR size to the nearest valid size.
 *
 * Variable MTRR sizes must be powers of 2, and the base address alignment must be equal to or greater than
 * the size. This function rounds up the given size to the next power of 2 starting from 4KB.
 *
 * Refer to Chapter 12.11.4 Range Size and Alignment Requirement, Vol.3, SDM 325462-078 for more details.
 *
 * @param[in] val The size value to be rounded up
 *
 * @return The rounded up size (power of 2 value)
 *
 * @pre N/A
 *
 * @post N/A
 */
static uint64_t round_variable_mtrr_size_up(uint64_t val)
{
	/*
	* 12.11.4 Range Size and Alignment Requirement, SDM:
	*
	* The minimum range size is 4 KBytes and the base address of the range must be on at least a
	* 4-KByte boundary.
	*
	* For ranges greater than 4 KBytes, each range must be of length 2^n and its base address must be
	* aligned on a 2^n boundary, where n is a value equal to or greater than 12. The base-address
	* alignment value cannot be less than its length. For example, an 8-KByte range cannot be aligned
	* on a 4-KByte boundary. It must be aligned on at least an 8-KByte boundary.
	*/
	uint64_t size = MEM_4K;

	if (val > MEM_4K) {
		while (size < val) {
			size <<= 1U;
		}
	}

	if (val != size) {
		pr_info("MTRR size 0x%lx is not aligned, round up to 0x%lx", val, size);
	}
	return size;
}

/**
 * @brief Initialize the virtual MTRR for a VM.
 *
 * This function initializes the virtual MTRR for a VM. In vMTRR implementation, use \p vm->arch_vm.vmtrr to emulate
 * the MTRR related MSRs. This function sets proper value in this structure based on the VM type.
 *
 * For Service VM, exposes Variable MTRRs and default MTRR:
 * - Reads and caches the native MTRR MSRs(IA32_MTRR_CAP, IA32_MTRR_DEF_TYPE, IA32_MTRR_PHYSBASEn,
 *   IA32_MTRR_PHYSMASKn).
 * - Hide fixed MTRRs, SMRR, PRMRR by clearing the corresponding bits in \p vmtrr->cap and \p vmtrr->def_type.
 *
 * For Pre-launch VM and Post-launch VM, emulate Variables MTRRs and default MTRR:
 * - Initializes \p vmtrr->cap and \p vmtrr->def_type to zero to hide all MTRR features at first.
 * - Set \p vmtrr->def_type.bits.enable to 1U to enable the default MTRR.
 * - Set \p vmtrr->def_type.bits.type to MTRR_MEM_TYPE_WB to set the default memory type to WB.
 * - Add MMIO (2G-4G) and (256G-512G) to the variable MTRR with type UC.
 *
 * @param[inout] vm The pointer to the specified VM
 *
 * @return None
 *
 * @pre vm != NULL
 *
 * @post N/A
 */
void init_vmtrr(struct acrn_vm *vm)
{
	struct acrn_vmtrr *vmtrr = &vm->arch_vm.vmtrr;
	uint32_t i;

	/*
	 * Fixed MTRRs are hide.
	 * To SOS: expose native Variable MTRRs and default MTRR.
	 * To Pre & Post launched VM:
	 *  1. expose Variable MTRR as UC only for the memory parts which EPT mapping is UC.
	 *  2. expose default MTRR as WB.
	 * Only support read operation for Variable MTRR, write is ignore.
	 */

	if (is_service_vm(vm)) {
		/* expose native variable MTRR */
		vmtrr->cap.value = msr_read(MSR_IA32_MTRR_CAP);
		vmtrr->def_type.value = msr_read(MSR_IA32_MTRR_DEF_TYPE);
		/* Hide fixed MTRRs, SMRR, PRMRR for Service VM */
		vmtrr->cap.bits.fix = 0UL;
		vmtrr->def_type.bits.fixed_enable = 0UL;
		vmtrr->cap.bits.smrr = 0UL;
		vmtrr->cap.bits.prmrr = 0UL;

		for (i = 0U; i < vmtrr->cap.bits.vcnt; i++) {
			vmtrr->variable_range[i].physbase.value = msr_read(MSR_IA32_MTRR_PHYSBASE_0 + (2U * i));
			vmtrr->variable_range[i].physmask.value = msr_read(MSR_IA32_MTRR_PHYSMASK_0 + (2U * i));
			pr_dbg("vm%d variable-range MTRR[%u]: %16lx %16lx", vm->vm_id, i,
				vmtrr->variable_range[i].physbase.value,
				vmtrr->variable_range[i].physmask.value);
		}
	} else {
		/* Emulate Variale MTRRs for pre-launch and post-launch VMs */
		vmtrr->cap.value = 0UL;
		vmtrr->def_type.value = 0UL;
		vmtrr->def_type.bits.enable = 1UL;
		vmtrr->def_type.bits.type = MTRR_MEM_TYPE_WB;
		/*
		 * PCI hole (2G-3.5G): UC
		 *
		 * We will not put DRAM in [0xE0000000-0x100000000), set 2G-4G range to UC.
		 */
		add_variable_mtrr(vm, USER_VM_VIRT_PCI_MEMBASE32, MEM_2G, EPT_UNCACHED);

		/* PCI hole (256G-512G): UC */
		add_variable_mtrr(vm, USER_VM_VIRT_PCI_MEMBASE64,
			 (USER_VM_VIRT_PCI_MEMLIMIT64 - USER_VM_VIRT_PCI_MEMBASE64), EPT_UNCACHED);
	}

	pr_dbg("init vMTRR for vm%d", vm->vm_id);
}

/**
 * @brief Add one variable MTRR to the VM.
 *
 * This function adds one variable MTRR to the specified VM.
 *
 * A memory region must meet the follow requirements that will add to the variable MTRR:
 * - The VM must be in \p VM_CREATED or \p VM_POWERED_OFF state.
 * - The size of the region must be non-zero.
 * - The memory type must be EPT_UNCACHED.
 * - The size will be rounded up to the next power of 2 if needed.
 * - The current variable MTRR count must be less than the maximum count.
 *
 * @param[inout] vm The pointer to the VM to which the MTRR is added
 * @param[in] base The base address of the memory region
 * @param[in] size The size of the memory region
 * @param[in] type The type of the memory region
 *
 * @return None
 *
 * @pre vm != NULL
 *
 * @post N/A
 */
void add_variable_mtrr(struct acrn_vm *vm, uint64_t base, uint64_t size, uint64_t type)
{
	uint64_t idx;
	struct acrn_vmtrr *vmtrr = &vm->arch_vm.vmtrr;
	/* This function can be called only before VM_RUNNING. */
	if (is_created_vm(vm) || is_poweroff_vm(vm)) {
		/* TODO: Support more memory types */
		if ((size != 0UL) && (type == EPT_UNCACHED)) {
			/* vmtrr->cap.value[7:0] atomically increase 1 and get the index of PHYSBASEn/PHYSMASKn */
			idx = (atomic_incu64_return(&vmtrr->cap.value) & MTRR_VARIABLE_CNT) - 1UL;
			if (idx < VARIABLE_RANGE_MTRR_NUM_MAX) {

				vmtrr->variable_range[idx].physbase.value = base;
				vmtrr->variable_range[idx].physbase.bits.type = MTRR_MEM_TYPE_UC;

				vmtrr->variable_range[idx].physmask.value =
					~(round_variable_mtrr_size_up(size) - 1UL);
				vmtrr->variable_range[idx].physmask.bits.valid = 1UL;
				vmtrr->variable_range[idx].physmask.bits.res1 = 0UL;

				pr_dbg("vm%d variable-range MTRR[%u]: base:%16lx mask:%16lx", vm->vm_id, idx,
					vmtrr->variable_range[idx].physbase.value,
					vmtrr->variable_range[idx].physmask.value);

			} else {
				pr_err("vm%d add variable-range MTRR failed: base:0x%lx size:0x%lx type:0x%x",
					vm->vm_id, base, size, type);
			}
		}
	} else {
		pr_err("vm%d add variable-range MTRR fail: VM state is not VM_CREATED or VM_POWEREOFF", vm->vm_id);
	}
}

/**
 * @brief Write to the virtual MTRR MSR.
 *
 * This function writes to the virtual MTRR MSR. It only supports writing the default MTRR type.
 * - If writing to IA32_MTRR_PHYSBASEn or IA32_MTRR_PHYSMASKn, it will ignore the write.
 * - If writing to IA32_MTRR_DEF_TYPE register:
 *   - Returns -EACCES if the write attempts to modify reserved bits (Fixed-range MTRRs enable/disable (bit 10)
 *     is treated as reserved here to fit new platforms).
 *   - For default type UC or when MTRR is disabled: Overrides the guest's PAT register with \p PAT_ALL_UC_VALUE.
 *     This value sets configures all PAT entries to use UC type, effectively making all memory accesses uncached
 *     regardless of page table memory type settings.
 *   - For default type WB: Restores the guest's PAT register to its original PAT MSR value. This allows memory
 *     caching to occur according to the guest's intended PAT configurations, effectively enabling normal caching
 *     behavior.
 *   - Ignores writes that attempt to set other memory types (currently not supported by ACRN hypervisor).
 *
 * PAT modifications only occur when CR0.CD (cache disable) is clear. When CR0.CD is set, caching is
 * already globally disabled by the processor, so no PAT modifications are needed.
 *
 * These write operations are virtualized, meaning they do not affect the physical MTRR MSRs.
 *
 * Refer to Chapter 12.12 PAGE ATTRIBUTE TABLE (PAT), Volume 3, SDM 325462-078 for more details.
 *
 * @param[inout] vcpu The pointer to the vCPU accessing the MTRR MSR.
 * @param[in] msr The MSR address.
 * @param[in] value The value to be written to the MSR.
 *
 * @return error number, indicates the success or failure of the write operation.
 *
 * @retval 0 Success
 * @retval -EACCES Error caused by modifying reserved bits
 *
 * @pre vcpu != NULL
 * @pre msr == MSR_IA32_MTRR_DEF_TYPE || msr == MSR_IA32_MTRR_PHYSBASEn || msr == MSR_IA32_MTRR_PHYSMASKn
 *
 * @post N/A
 */
int32_t write_vmtrr(struct acrn_vcpu *vcpu, uint32_t msr, uint64_t value)
{
	/*
	 * Fixed MTRRs are disabled.
	 * Variable MTRRs are read only.
	 * So only default MTRR is able to write.
	 * Use PAT to set memory type if default MTRR is written.
	 *
	 *    MTRR    PAT   Effective Memory Type
	 *     UC     UC        UC
	 *     WB     WB        WB
	 *     UC     WB        UC
	 *     WB     UC        UC
	 */

	union mtrr_def_type_reg *def_type = &vcpu->arch.mtrr_def_type;
	int32_t err = 0;
	if (is_mtrr_base_register(msr) || is_mtrr_mask_register(msr)) {
		/* Ignore write to MSR_IA32_MTRR_PHYSBASEn, MSR_IA32_MTRR_PHYSMASKn */
		pr_err("Write to unexpected MTRR MSR: 0x%x", msr);

	} else if (def_type->value != value) {

		if ((value & MSR_IA32_MTRR_DEF_TYPE_RES) != 0UL) {
			/* check reserved bits */
			pr_err("Write to unexpected bits in MSR: 0x%x", msr);
			err = -EACCES;
		}

		if (((value & MSR_IA32_MTRR_DEF_TYPE_ENABLE) == 0UL) ||
			 ((value & MSR_IA32_MTRR_DEF_TYPE_TYPE) == MTRR_MEM_TYPE_UC)) {
			/* Set all memory type to UC */
			if ((vcpu_get_cr0(vcpu) & CR0_CD) == 0UL) {
				exec_vmwrite64(VMX_GUEST_IA32_PAT_FULL, PAT_ALL_UC_VALUE);
			}
			def_type->value = value;
		} else {
			if ((value & MSR_IA32_MTRR_DEF_TYPE_TYPE) == MTRR_MEM_TYPE_WB) {
				/* Restore IA32_PAT */
				if ((vcpu_get_cr0(vcpu) & CR0_CD) == 0UL) {
					exec_vmwrite64(VMX_GUEST_IA32_PAT_FULL, vcpu_get_guest_msr(vcpu, MSR_IA32_PAT));
				}
				def_type->value = value;
			} else {
				/* Other memory types are not supported */
				pr_err("Write unexpected default MTRR type to msr: 0x%x", msr);
			}
		}
	} else {
		/* MSR_IA32_MTRR_DEF_TYPE == value, do not need to write */
	}
	return err;
}

/**
 * @brief Read from the virtual MTRR MSR.
 *
 * This function handles reads from virtual MTRR MSRs, returning predefined values rather than reading from
 * physical MSRs. It supports reading:
 * - IA32_MTRR_CAP
 * - IA32_MTRR_DEF_TYPE
 * - IA32_MTRR_PHYSBASEn
 * - IA32_MTRR_PHYSMASKn
 *
 * @param[in] vcpu The pointer to the vCPU accessing the MTRR MSR.
 * @param[in] msr The MSR address to read.
 *
 * @return The value read from the emulated MTRR MSR.
 *
 * @pre vcpu != NULL
 * @pre msr == MSR_IA32_MTRR_CAP || msr == MSR_IA32_MTRR_DEF_TYPE ||
 *      msr == MSR_IA32_MTRR_PHYSBASEn || msr == MSR_IA32_MTRR_PHYSMASKn
 *
 * @post N/A
 */
uint64_t read_vmtrr(const struct acrn_vcpu *vcpu, uint32_t msr)
{
	const struct acrn_vmtrr *vmtrr = &vcpu->vm->arch_vm.vmtrr;
	const union mtrr_def_type_reg *def_type = &vcpu->arch.mtrr_def_type;
	uint64_t ret = 0UL;
	uint32_t index = get_variable_mtrr_index(msr);

	if (msr == MSR_IA32_MTRR_CAP) {
		ret = vmtrr->cap.value;
	} else if (msr == MSR_IA32_MTRR_DEF_TYPE) {
		ret = def_type->value;
	} else if (is_mtrr_base_register(msr)) {
		ret = vmtrr->variable_range[index].physbase.value;
	} else {
		/* mtrr mask register */
		ret = vmtrr->variable_range[index].physmask.value;
	}

	return ret;
}

/**
 * @}
 */