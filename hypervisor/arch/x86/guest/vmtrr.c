/*
 * Copyright (C) 2018-2022 Intel Corporation.
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

static inline bool is_mtrr_mask_register(uint64_t msr)
{
	return ((msr >= MSR_IA32_MTRR_PHYSMASK_0) && (msr <= MSR_IA32_MTRR_PHYSMASK_9) && ((msr % 2U) != 0UL));
}

static inline bool is_mtrr_base_register(uint64_t msr)
{
	return ((msr >= MSR_IA32_MTRR_PHYSBASE_0) && (msr <= MSR_IA32_MTRR_PHYSBASE_9) && ((msr % 2U) == 0UL));
}

static inline uint64_t get_variable_mtrr_index(uint64_t msr)
{
	return (msr - MSR_IA32_MTRR_PHYSBASE_0) / 2UL;
}

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

/* initialize virtual MTRR for a VM */
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
		vmtrr->cap.bits.fix = 0U;
		vmtrr->def_type.bits.fixed_enable = 0U;
		vmtrr->cap.bits.smrr = 0U;
		vmtrr->cap.bits.prmrr = 0U;

		for (i = 0U; i < vmtrr->cap.bits.vcnt; i++) {
			vmtrr->variable_range[i].physbase.value = msr_read(MSR_IA32_MTRR_PHYSBASE_0 + (2U * i));
			vmtrr->variable_range[i].physmask.value = msr_read(MSR_IA32_MTRR_PHYSMASK_0 + (2U * i));
			pr_dbg("vm%d variable-range MTRR[%u]: %16lx %16lx", vm->vm_id, i,
				vmtrr->variable_range[i].physbase.value,
				vmtrr->variable_range[i].physmask.value);
		}
	} else {
		/* Emulate Variale MTRRs for pre-launch and post-launch VMs */
		vmtrr->cap.value = 0U;
		vmtrr->def_type.value = 0U;
		vmtrr->def_type.bits.enable = 1U;
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

void add_variable_mtrr(struct acrn_vm *vm, uint64_t base, uint64_t size, uint8_t type)
{
	uint16_t idx;
	struct acrn_vmtrr *vmtrr = &vm->arch_vm.vmtrr;
	/* This function can be called only before VM_RUNNING. */
	if (is_created_vm(vm) || is_poweroff_vm(vm)) {
		/* TODO: Support more memory types */
		if ((size != 0UL) && (type == EPT_UNCACHED)) {
			/* vmtrr->cap.value[7:0] atomically increase 1 and get the index of PHYSBASEn/PHYSMASKn */
			idx = ((uint64_t)atomic_inc64_return((int64_t *)&vmtrr->cap.value) & MTRR_VARIABLE_CNT) - 1U;
			if (idx < VARIABLE_RANGE_MTRR_NUM_MAX) {

				vmtrr->variable_range[idx].physbase.value = base;
				vmtrr->variable_range[idx].physbase.bits.type = MTRR_MEM_TYPE_UC;

				vmtrr->variable_range[idx].physmask.value =
					~(round_variable_mtrr_size_up(size) - 1U);
				vmtrr->variable_range[idx].physmask.bits.valid = 1U;
				vmtrr->variable_range[idx].physmask.bits.res1 = 0U;

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

/*
 * virtual MTRR MSR write API.
 * Supports these MSRs:
 * 	MSR_IA32_MTRR_DEF_TYPE
 *	MSR_IA32_MTRR_PHYSBASEn
 *	MSR_IA32_MTRR_PHYSMASKn
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

		if (((value & MSR_IA32_MTRR_DEF_TYPE_ENABLE) == 0U) ||
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

/* virtual MTRR MSR read API */
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
