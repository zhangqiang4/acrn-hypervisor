/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "acrn_common.h"
#include <types.h>
#include <errno.h>
#include <asm/guest/vcpu.h>
#include <asm/guest/virq.h>
#include <asm/lib/bits.h>
#include <asm/vmx.h>
#include <logmsg.h>
#include <asm/cpufeatures.h>
#include <asm/cpu_caps.h>
#include <asm/per_cpu.h>
#include <asm/init.h>
#include <asm/guest/vm.h>
#include <asm/guest/vmcs.h>
#include <asm/mmu.h>
#include <lib/sprintf.h>
#include <asm/lapic.h>
#include <asm/irq.h>
#ifdef HV_DEBUG
#include <console.h>
#endif
#include <hv_main.h>

/**
 * @addtogroup vp-base_vcpu
 *
 * @{
 */

/*
 * @file
 * @brief Implement vCPU related functions
 *
 * This file implements functions on vCPU operations such as prepare, create and run etc., also implement
 * vCPU register operations such as read/write general purpose registers and control registers etc.
 *
 */

/**
 * @file arch/x86/guest/vmx_asm.S
 * @brief Define the entry to run guest code in non-root mode
 *
 * The file defines an execution entry for hypervisor to start guest code. After hypervisor configures
 * the vCPU states for the guest, it transfers execution flow here to swap host/guest GPRs and gets
 * into non-root mode by executing vmlaunch or vmresume instruction. The guest vCPU execution context
 * is built or resumed from guest VMCS fields with a successful execution of the instruction, then guest
 * starts or continues execution from the last trapped/interrupted point. Root mode execution flow resumes
 * if a normal VM-Exit or a VM-Entry failure happens. A VM-Exit is followed by swapping host/guest
 * GPRs and mitigation work depending on mitigation type, but in VM-Entry failure case, skip mitigation
 * work because guest code hasn't started yet. And finally the execution flow returns to hypervisor to
 * continue handling normal VM-Exit case or VM-Entry failure case.
 */

/*
 * @brief Data struct to store vCPU GPRs that should be swapped during vCPU scheduling
 *
 * This struct is used in arch_switch_to() to store previous vCPU registers and restore next vCPU's registers.
 *
 * @consistency N/A
 * @alignment N/A
 */
struct stack_frame {
	uint64_t rdi;	/**< vCPU rdi register */
	uint64_t r15;	/**< vCPU r15 register */
	uint64_t r14;	/**< vCPU r14 register */
	uint64_t r13;	/**< vCPU r13 register */
	uint64_t r12;	/**< vCPU r12 register */
	uint64_t rbp;	/**< vCPU rbp register */
	uint64_t rbx;	/**< vCPU rbx register */
	uint64_t rflag;	/**< vCPU rflags register */
	uint64_t rip;	/**< vCPU rip register */
	uint64_t magic;	/**< magic number for stack */
};

/**
 * @brief Get vCPU general purpose register
 *
 * This function is used to read general purpose register value from vCPU run_context.
 *
 * Normal case behaviors:
 * - Return the cached data for specific register
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[in] vcpu Target vCPU of the GP register
 * @param[in] reg The cache index of the GP register
 *
 * @return The GP register value
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 *
 * @remark reg is defined in enum cpu_reg_name.
 */
uint64_t vcpu_get_gpreg(const struct acrn_vcpu *vcpu, uint32_t reg)
{
	const struct run_context *ctx = &vcpu->arch.contexts.run_ctx;

	return ctx->cpu_regs.longs[reg];
}

/**
 * @brief Store vCPU general purpose register
 *
 * This function is used to write general purpose register value to vCPU run_context.
 *
 * Normal case behaviors:
 * - Store the register value to memory cache indexed by reg
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[inout] vcpu Target vCPU to save register
 * @param[in] reg The register index
 * @param[in] val The register value to store
 *
 * @return None
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 *
 * @remark reg is defined in enum cpu_reg_name.
 */
void vcpu_set_gpreg(struct acrn_vcpu *vcpu, uint32_t reg, uint64_t val)
{
	struct run_context *ctx = &vcpu->arch.contexts.run_ctx;

	ctx->cpu_regs.longs[reg] = val;
}

/**
 * @brief Get vCPU RIP
 *
 * This function is used to get vCPU RIP. VM-Exit doesn't cache vCPU RIP, if HV doesn't update vCPU RIP,
 * this function needs to read RIP from VMX_GUEST_RIP.
 *
 * Normal case behaviors:
 * - Return RIP of the target vCPU
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[in] vcpu Target vCPU to get the RIP
 *
 * @return The RIP of the vCPU
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
uint64_t vcpu_get_rip(struct acrn_vcpu *vcpu)
{
	struct run_context *ctx = &vcpu->arch.contexts.run_ctx;

	if (!bitmap_test(CPU_REG_RIP, &vcpu->reg_updated) &&
		!bitmap_test_and_set_nolock(CPU_REG_RIP, &vcpu->reg_cached)) {
		ctx->rip = exec_vmread(VMX_GUEST_RIP);
	}
	return ctx->rip;
}

/**
 * @brief Set vCPU RIP
 *
 * The RIP value is written to memory cache directly and set the RIP bit in bitmap reg_updated to indicate
 * the VMX_GUEST_RIP needs update.
 *
 * Normal case behaviors:
 * - Store RIP value to memory cache.
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[inout] vcpu Target vCPU to store the RIP
 * @param[in] val The RIP value to store
 *
 * @return None
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
void vcpu_set_rip(struct acrn_vcpu *vcpu, uint64_t val)
{
	vcpu->arch.contexts.run_ctx.rip = val;
	bitmap_set_nolock(CPU_REG_RIP, &vcpu->reg_updated);
}

/**
 * @brief Get vCPU RSP
 *
 * This function is used to get vCPU RSP. VM-Exit doesn't cache vCPU RSP, if HV doesn't update vCPU RSP,
 * this function needs to read RSP from VMX_GUEST_RSP.
 *
 * Normal case behaviors:
 * - Return RSP of the target vCPU
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[in] vcpu Target vCPU to get the RIP
 *
 * @return The RSP of the vCPU
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
uint64_t vcpu_get_rsp(struct acrn_vcpu *vcpu)
{
	struct run_context *ctx = &vcpu->arch.contexts.run_ctx;

	if (!bitmap_test(CPU_REG_RSP, &vcpu->reg_updated) &&
		!bitmap_test_and_set_nolock(CPU_REG_RSP, &vcpu->reg_cached)) {
		ctx->cpu_regs.regs.rsp = exec_vmread(VMX_GUEST_RSP);
	}
	return ctx->cpu_regs.regs.rsp;
}

/**
 * @brief Set vCPU RSP
 *
 * This function is used to store RSP to memory cache. The RSP value is written to cache directly and
 * RSP bit is set in bitmap reg_updated to indicate the CPU_REG_RSP needs update.
 *
 * Normal case behaviors:
 * - Store RSP value to memory cache.
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[inout] vcpu The target vCPU to store the RSP.
 * @param[in] val The RSP value to store.
 *
 * @return None
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
void vcpu_set_rsp(struct acrn_vcpu *vcpu, uint64_t val)
{
	struct run_context *ctx = &vcpu->arch.contexts.run_ctx;

	ctx->cpu_regs.regs.rsp = val;
	bitmap_set_nolock(CPU_REG_RSP, &vcpu->reg_updated);
}

/**
 * @brief Get vCPU IA32_EFER MSR
 *
 * The function is used to get EFER MSR data from memory cache.
 *
 * Normal case behaviors:
 * - Return the cached EFER MSR data of the vCPU
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[in] vcpu Target vCPU to get the MSR
 *
 * @return EFER MSR data of the vCPU
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
uint64_t vcpu_get_efer(struct acrn_vcpu *vcpu)
{
	struct run_context *ctx = &vcpu->arch.contexts.run_ctx;

	return ctx->ia32_efer;
}

/**
 * @brief Set vCPU IA32_EFER MSR
 *
 * The MSR value is written to cache directly and toggle the EFER bit in bitmap reg_updated to indicate
 * IA32_EFER MSR needs update. When the MSR value is equal to the data in current HW, no need to
 * overwrite the MSR thus clear LOAD_EFER bit in VMX_{ENTRY,EXIT}_CTLS, otherwise set the bit in VMX_
 * {ENTRY,EXIT}_CTLS so that EFER MSR will be loaded from {guest, host} VMCS field on VM-{ENTRY,EXIT}.
 *
 * Normal case behaviors:
 * - Store EFER MSR value to memory cache
 * - Clear or set the EFER bit in VMX_{ENTRY,EXIT}_CTLS
 * - Set EFER bit in vcpu->reg_updated
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[inout] vcpu Target vCPU to store the EFER MSR value
 * @param[in] val EFER value to store
 *
 * @return None
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
void vcpu_set_efer(struct acrn_vcpu *vcpu, uint64_t val)
{
	vcpu->arch.contexts.run_ctx.ia32_efer = val;

	if (val == msr_read(MSR_IA32_EFER)) {
		clear_vmcs_bit(VMX_ENTRY_CONTROLS, VMX_ENTRY_CTLS_LOAD_EFER);
		clear_vmcs_bit(VMX_EXIT_CONTROLS, VMX_EXIT_CTLS_LOAD_EFER);
	} else {
		set_vmcs_bit(VMX_ENTRY_CONTROLS, VMX_ENTRY_CTLS_LOAD_EFER);
		set_vmcs_bit(VMX_EXIT_CONTROLS, VMX_EXIT_CTLS_LOAD_EFER);
	}

	/* Write the new value to VMCS in either case */
	bitmap_set_nolock(CPU_REG_EFER, &vcpu->reg_updated);
}

/**
 * @brief Get vCPU RFLAGS
 *
 * This function is used to get vCPU RFLAGS register value. VM-Exit doesn't cache vCPU RFLAGS,
 * if HV doesn't update vCPU RFLAGS, this function needs to read RSP from VMX_GUEST_RFLAGS.
 *
 * Normal case behaviors:
 * - Return RFLAGS of target vCPU
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[in] vcpu Target vCPU to get the RFLAGS
 *
 * @return RFLAGS of the vCPU
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
uint64_t vcpu_get_rflags(struct acrn_vcpu *vcpu)
{
	struct run_context *ctx = &vcpu->arch.contexts.run_ctx;

	if (!bitmap_test(CPU_REG_RFLAGS, &vcpu->reg_updated) &&
		!bitmap_test_and_set_nolock(CPU_REG_RFLAGS, &vcpu->reg_cached) && vcpu->launched) {
		ctx->rflags = exec_vmread(VMX_GUEST_RFLAGS);
	}
	return ctx->rflags;
}

/**
 * @brief Store vCPU RFLAGS
 *
 * This function is used to store RFLAGS register to memory cache. The RSP value is written to cache
 * directly and toggle the RFLAGS bit in bitmap vcpu->reg_updated to indicate the cached data is
 * up-to-date.
 *
 * Normal case behaviors:
 * - Store RFLAGS value to memory cache
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[inout] vcpu Target vCPU to store RFLAGS
 * @param[in] val RFLAGS value to store
 *
 * @return None
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
void vcpu_set_rflags(struct acrn_vcpu *vcpu, uint64_t val)
{
	vcpu->arch.contexts.run_ctx.rflags = val;
	bitmap_set_nolock(CPU_REG_RFLAGS, &vcpu->reg_updated);
}

/**
 * @brief Get MSR listed in emulated_guest_msrs
 *
 * These MSRs are retrieved from cached MSR array with the index mapped from MSR encoding. If the given
 * MSR is not found in the emulated MSR array (refer to vmsr_get_guest_msr_index() for details), the
 * default return value is 0.
 *
 * Normal case behaviors:
 * - Get MSR value from guest_msrs array
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[in] vcpu Target vCPU to get the MSR
 * @param[in] msr MSR encoding
 *
 * @return MSR value indicated by msr
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
uint64_t vcpu_get_guest_msr(const struct acrn_vcpu *vcpu, uint32_t msr)
{
	uint32_t index = vmsr_get_guest_msr_index(msr);
	uint64_t val = 0UL;

	if (index < NUM_EMULATED_MSRS) {
		val = vcpu->arch.guest_msrs[index];
	}

	return val;
}

/**
 * @brief Set emulated MSR value to memory cache
 *
 * The function is used to store emulated MSR value, i.e., found in vmsr_get_guest_msr_index(),
 * to MSR cache array. The array index is mapped from MSR encoding.
 *
 * Normal case behaviors:
 * - Set MSR value to guest_msrs array
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[inout] vcpu Target vCPU to get the MSR
 * @param[in] msr MSR encoding
 * @param[in] val The MSR value to store
 *
 * @return None
 *
 * @pre vcpu != NULL.
 *
 * @post N/A
 */
void vcpu_set_guest_msr(struct acrn_vcpu *vcpu, uint32_t msr, uint64_t val)
{
	uint32_t index = vmsr_get_guest_msr_index(msr);

	if (index < NUM_EMULATED_MSRS) {
		vcpu->arch.guest_msrs[index] = val;
	}
}

/**
 * @brief Update eoi_exit_bitmaps to VMCS fields.
 *
 * This function is used to update VMCS VMX_EOI_EXITx_FULL fields with cached eoi_exit_bitmaps.
 *
 * Normal case behaviors:
 * - Write vCPU eoi_exit_bitmaps to corresponding VMCS fields.
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[in] vcpu Target vCPU to update the eoi_exit_bitmaps.
 *
 * @return None
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
void vcpu_set_vmcs_eoi_exit(const struct acrn_vcpu *vcpu)
{
	exec_vmwrite64(VMX_EOI_EXIT0_FULL, vcpu->arch.eoi_exit_bitmap[0]);
	exec_vmwrite64(VMX_EOI_EXIT1_FULL, vcpu->arch.eoi_exit_bitmap[1]);
	exec_vmwrite64(VMX_EOI_EXIT2_FULL, vcpu->arch.eoi_exit_bitmap[2]);
	exec_vmwrite64(VMX_EOI_EXIT3_FULL, vcpu->arch.eoi_exit_bitmap[3]);
}

/**
 * @brief Set bit in vCPU EOI bitmaps
 *
 * This function sets vector relevant bit in vCPU eoi_exit_bitmap. If the bit was clear,
 * send vCPU ACRN_REQUEST_EOI_EXIT_BITMAP_UPDATE request to update the VMCS EOI bitmap fields.
 *
 * Normal case behaviors:
 * - Set vector relevant bit in vCPU eoi_exit_bitmap.
 * - Send vCPU request to update VMCS EOI bitmap fields if the bit was clear.
 *
 * Error case behaviors:
 *  - N/A
 *
 * @param[inout] vcpu Target vCPU to set the eoi_exit_bitmaps
 * @param[in] vector The interrupt vector for which to set the bit
 *
 * @return None
 *
 * @pre vcpu != NULL && vector <= 255U
 *
 * @post N/A
 */
void vcpu_set_eoi_exit_bitmap(struct acrn_vcpu *vcpu, uint32_t vector)
{
	pr_dbg("%s", __func__);

	if (!bitmap_test_and_set_lock((uint16_t)(vector & 0x3fU),
			&(vcpu->arch.eoi_exit_bitmap[(vector & 0xffU) >> 6U]))) {
		vcpu_make_request(vcpu, ACRN_REQUEST_EOI_EXIT_BITMAP_UPDATE);
	}
}

/**
 * @brief Clear bit in vCPU EOI bitmap
 *
 * This function clears vector relevant bit in vCPU eoi_exit_bitmap. If the bit was set,
 * send vCPU ACRN_REQUEST_EOI_EXIT_BITMAP_UPDATE request to update the VMCS EOI bitmap fields.
 *
 * Normal case behaviors:
 * - Clear vector relevant bit in vCPU eoi_exit_bitmap.
 * - Send vCPU request to update VMCS EOI bitmap fields if the bit was set.
 *
 * Error case behaviors:
 *  - N/A
 *
 * @param[inout] vcpu Target vCPU to set the eoi_exit_bitmaps
 * @param[in] vector The interrupt vector for which to clear the bit
 *
 * @return None
 *
 * @pre vcpu != NULL && vector <= 255U
 *
 * @post N/A
 */
void vcpu_clear_eoi_exit_bitmap(struct acrn_vcpu *vcpu, uint32_t vector)
{
	pr_dbg("%s", __func__);

	if (bitmap_test_and_clear_lock((uint16_t)(vector & 0x3fU),
			&(vcpu->arch.eoi_exit_bitmap[(vector & 0xffU) >> 6U]))) {
		vcpu_make_request(vcpu, ACRN_REQUEST_EOI_EXIT_BITMAP_UPDATE);
	}
}

/**
 * @brief Reset all eoi_exit_bitmaps
 *
 * This function is used to reset all bits in eoi_exit_bitmaps when vLAPIC is being reset. It sends
 * vCPU ACRN_REQUEST_EOI_EXIT_BITMAP_UPDATE request after resets eoi_exit_bitmaps. HV checks the request
 * and updates VMX_EOI_EXIT_FULL VMCS fields with reset bitmap values at next VM-Entry.
 *
 * Normal case behaviors:
 * - Reset all bits of vCPU eoi_exit_bitmaps.
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[inout] vcpu Target vCPU to reset the eoi_exit_bitmaps
 *
 * @return None
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
void vcpu_reset_eoi_exit_bitmaps(struct acrn_vcpu *vcpu)
{
	pr_dbg("%s", __func__);

	(void)memset((void *)(vcpu->arch.eoi_exit_bitmap), 0U, sizeof(vcpu->arch.eoi_exit_bitmap));
	vcpu_make_request(vcpu, ACRN_REQUEST_EOI_EXIT_BITMAP_UPDATE);
}

/**
 * @brief Initialize IWKeys with random values
 *
 * This function uses random value to initialize IWKeys while vCPU is being reset. Guest can overwrite the initial
 * IWKeys by executing LOADIWKEY instruction.
 *
 * Normal case behaviors:
 * - The IWKeys are filled with random key values.
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[inout] vcpu Target vCPU to initialize the IWKeys.
 *
 * @return None
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static void init_iwkey(struct acrn_vcpu *vcpu)
{
	/* Initialize a random iwkey */
	if (pcpu_has_cap(X86_FEATURE_KEYLOCKER)) {
		vcpu->arch.IWKey.integrity_key[0] = get_random_value();
		vcpu->arch.IWKey.integrity_key[1] = get_random_value();
		vcpu->arch.IWKey.encryption_key[0] = get_random_value();
		vcpu->arch.IWKey.encryption_key[1] = get_random_value();
		vcpu->arch.IWKey.encryption_key[2] = get_random_value();
		vcpu->arch.IWKey.encryption_key[3] = get_random_value();
		/* It's always safe to clear whose_iwkey */
		per_cpu(whose_iwkey, pcpuid_from_vcpu(vcpu)) = NULL;
	}
}

/**
 * @brief Load Key Locker IWKey
 *
 * This function is used to load Internal Wrapping Key(IWKey) when below conditions are met:
 * - Physical CPU supports KeyLocker feature.
 * - vcpu->arch.cr4_kl_enabled is true.
 * - IWKey current owner is not the target vCPU.
 *
 * The load-IWKey process includes below steps:
 * - Saves XMM0, XMM1, and XMM2 registers to local variables.
 * - Writes these registers with IWKey's integrity_key and encryption_key.
 * - Load the IWKey.
 * - Restore local values to XMM0, XMM1, and XMM2 registers.
 * - Set the target vCPU as IWKey current owner.
 *
 * Normal case behaviors:
 * - The IWKey is loaded and the target vCPU is set as current owner of the key.
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[in] vcpu Target vCPU to load the IWKey.
 *
 * @return None
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
void load_iwkey(struct acrn_vcpu *vcpu)
{
	uint64_t xmm_save[6];

	/* Only load IWKey with vCPU CR4 keylocker bit enabled */
	if (pcpu_has_cap(X86_FEATURE_KEYLOCKER) && vcpu->arch.cr4_kl_enabled &&
	    (get_cpu_var(whose_iwkey) != vcpu)) {
		/* Save/restore xmm0/xmm1/xmm2 during the process */
		read_xmm_0_2(&xmm_save[0], &xmm_save[2], &xmm_save[4]);
		write_xmm_0_2(&vcpu->arch.IWKey.integrity_key[0], &vcpu->arch.IWKey.encryption_key[0],
						&vcpu->arch.IWKey.encryption_key[2]);
		asm_loadiwkey(0);
		write_xmm_0_2(&xmm_save[0], &xmm_save[2], &xmm_save[4]);
		get_cpu_var(whose_iwkey) = vcpu;
	}
}

static void reset_vcpu_regs(struct acrn_vcpu *vcpu);
static uint64_t build_stack_frame(struct acrn_vcpu *vcpu);
static void vcpu_reset_mtrr_def_type(struct acrn_vcpu *vcpu);

/**
 * @brief Reset vCPU to initial states
 *
 * This function performs below set up to reset/initialize vCPU states:
 * - Reset vCPU flags/VMCS/running context/MTRR default type to default values.
 * - Call clear_va_vmcs to Clear VMCS data structure
 * - Initillize interrupt mode to kill vCPU out of non-root mode
 * - Call set_vcpu_regs to Reset vCPU to initial states
 * - Rebuild vCPU thread stack if pCPU state is offline.
 * - Call reset_event to Reset all the vCPU schedule event
 * - Initialize IWKeys with random values
 *
 * Normal case behaviors:
 * - Reset and initialize vCPU to initial states.
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[in] pcpu_id The physical CPU ID that runs the vCPU reset.
 * @param[inout] vcpu The vCPU to be reset.
 * @param[in] mode The reset type for this vCPU.
 *
 * @return None
 *
 * @pre 0 <= pcpu_id && pcpu_id <= MAX_PCPU_NUM - 1
 * @pre vcpu != NULL
 *
 * @post N/A
 *
 * @remark 1)DO NOT touch any vcpu transient states in this function as they're not finalized.
 * 2) mode is the pre-defined value in vcpu_reset_mode.
 */
static void vcpu_reset_internal(uint16_t pcpu_id, struct acrn_vcpu *vcpu, enum vcpu_reset_mode mode)
{
	int32_t i;
	uint64_t vmx_rev_id;

	vcpu->arch.contexts.ext_ctx.reset_mode = mode;
	vcpu->launched = false;
	vcpu->frozen = false;
	vcpu->arch.nr_sipi = 0U;

	vcpu->arch.exception_info.exception = VECTOR_INVALID;
	vcpu->arch.irq_window_enabled = false;
	vcpu->arch.emulating_lock = false;

	/* vcpu init reset won't change local apic state */
	if (mode != VCPU_INIT_RESET) {
		(void)memset((void *)vcpu->arch.vmcs, 0U, PAGE_SIZE);
		/*
		 * Obtain the VM Rev ID from HW and populate VMCS page with it so that VMCS can be
		 * loaded when scheduling to the vCPU thread.
		 */
		vmx_rev_id = msr_read(MSR_IA32_VMX_BASIC);
		(void)memcpy_s(vcpu->arch.vmcs, 4U, (void *)&vmx_rev_id, 4U);

		per_cpu(mode_to_idle, pcpu_id) = IDLE_MODE_CX;

#ifdef HV_DEBUG
		if (is_using_init_ipi()) {
			per_cpu(mode_to_kick_pcpu, pcpu_id) = DEL_MODE_INIT;
			pr_info("pcpu=%d, kick-mode=%d, use_init_flag=%d", pcpu_id,
				per_cpu(mode_to_kick_pcpu, pcpu_id), is_using_init_ipi());
		} else
#endif
		{
			per_cpu(mode_to_kick_pcpu, pcpu_id) = DEL_MODE_IPI;
		}
	}
	/*
	 * Execute VMCLEAR VMCS to initialize parts of the VMCS region (e.g. it sets the
	 * launch state of the VMCS to clear).
	 */
	clear_va_vmcs(vcpu->arch.vmcs);

	(void)memset((void *)(&vcpu->arch.contexts), 0U,
			sizeof(struct run_context));

	/* reset vcpu default memory type */
	vcpu_reset_mtrr_def_type(vcpu);

	reset_vcpu_regs(vcpu);

	/* Need to rebuild vcpu thread stack if pcpu offline */
	if (pcpu_get_current_state(pcpuid_from_vcpu(vcpu)) == PCPU_STATE_DEAD) {
		vcpu->thread_obj.host_sp = build_stack_frame(vcpu);
	}

	for (i = 0; i < VCPU_EVENT_NUM; i++) {
		reset_event(&vcpu->events[i]);
	}

	init_iwkey(vcpu);
	vcpu->arch.iwkey_copy_status = 0UL;
}

/**
 * @brief Get running vCPU from thread_object data
 *
 * This function is used to get the running vCPU according to thread_object of the target physical CPU.
 * If an idle thread is running on the physical CPU, then return NULL to caller.
 *
 * Normal case behaviors:
 * - Return the running vCPU pointer.
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[in] pcpu_id The physical CPU ID to get running vCPU.
 *
 * @return The running vCPU pointer
 *
 * @pre 0 <= pcpu_id && pcpu_id <= MAX_PCPU_NUM - 1
 *
 * @post N/A
 */
struct acrn_vcpu *get_running_vcpu(uint16_t pcpu_id)
{
	struct thread_object *curr = sched_get_current(pcpu_id);
	struct acrn_vcpu *vcpu = NULL;

	if ((curr != NULL) && (!is_idle_thread(curr))) {
		vcpu = container_of(curr, struct acrn_vcpu, thread_obj);
	}

	return vcpu;
}

/**
 * @brief Get running vCPU from per-CPU ever_run_vcpu data
 *
 * This function is used to get the running vCPU from per-CPU ever_run_vcpu data of the target physical CPU.
 *
 * Normal case behaviors:
 * - Return the active vCPU on the physical CPU.
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[in] pcpu_id The physical CPU ID to get active vCPU
 *
 * @return pointer to the active vCPU
 *
 * @pre @pre 0 <= pcpu_id && pcpu_id <= MAX_PCPU_NUM - 1
 *
 * @post N/A
 */
struct acrn_vcpu *get_ever_run_vcpu(uint16_t pcpu_id)
{
	return per_cpu(ever_run_vcpu, pcpu_id);
}

/**
 * @brief Set the vCPU in specific mode
 *
 * This function is used to set vCPU in one of the mode listed in vm_cpu_mode, i.e., Real-Address mode,
 * Protected mode, IA32-e compatibility mode and 64-bit mode. The mode is set according to EFER MSR, CS
 * segment attributes and CR0 settings. Refer to 2.2 Modes of Operation, SDM Vol.3 SDM 325462-078US
 * for details.
 *
 * @param[inout] vcpu Target vCPU to set CPU mode
 * @param[in] cs_attr CS segment attributes
 * @param[in] ia32_efer vCPU EFER MSR value
 * @param[in] cr0 vCPU CR0 value
 *
 * @return None
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static void set_vcpu_mode(struct acrn_vcpu *vcpu, uint32_t cs_attr, uint64_t ia32_efer,
		uint64_t cr0)
{
	if ((ia32_efer & MSR_IA32_EFER_LMA_BIT) != 0UL) {
		if ((cs_attr & 0x2000U) != 0U) {
			/* CS.L = 1 */
			vcpu->arch.cpu_mode = CPU_MODE_64BIT;
		} else {
			vcpu->arch.cpu_mode = CPU_MODE_COMPATIBILITY;
		}
	} else if ((cr0 & CR0_PE) != 0UL) {
		vcpu->arch.cpu_mode = CPU_MODE_PROTECTED;
	} else {
		vcpu->arch.cpu_mode = CPU_MODE_REAL;
	}
}

/**
 * @brief Initialize XSAVE relevant settings
 *
 * This function is used to initialize XSAVE related settings after vCPU is created.
 * It performs below initialization if physical CPU supports XSAVE feature:
 * - Set vCPU ext_context's XCR0 with XSAVE_FPU;
 * - Reset xsave_area.
 * - Set xsave_header's XCOMP_BV[63] to indicate the compacted format is used for XSAVE data storage.
 *
 * @param[inout] vcpu Target vCPU to do XSAVE initialization
 *
 * @return None
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static void init_xsave(struct acrn_vcpu *vcpu)
{
	struct ext_context *ectx = &(vcpu->arch.contexts.ext_ctx);
	struct xsave_area *area = &ectx->xs_area;

	/* if the HW has this cap, we need to prepare the buffer for potential save/restore.
	 *  Guest may or may not enable XSAVE -- it doesn't matter.
	 */
	if (pcpu_has_cap(X86_FEATURE_XSAVE)) {
		ectx->xcr0 = XSAVE_FPU;
		(void)memset((void *)area, 0U, XSAVE_STATE_AREA_SIZE);

		/* xsaves only support compacted format, so set it in xcomp_bv[63],
		 * keep the reset area in header area as zero.
		 */
		ectx->xs_area.xsave_hdr.hdr.xcomp_bv |= XSAVE_COMPACTED_FORMAT;
	}
}

/**
 * @brief Set vCPU to target mode states
 *
 * This function performs below setup for the vCPU:
 * - Set up initial segment attr/limit fields according to CR0.PE state.
 * - Initialize base/attr/limit/selector fields for {CS, SS, DS, ES, FS, GS} segments.
 * - Override ext_context CS segment attr/base/limit with the data in vcpu_regs.
 * - Set GDTR and IDTR base/limit fields with the data in vcpu_regs.
 * - Set LDTR and TR selector field with the data in vcpu_regs.
 * - Set LDTR and TR base/limit/attr fields with default values.
 * - Initialize general purpose registers with corresponding data in vcpu_regs.
 * - Set RIP/RSP with the data in vcpu_regs.
 * - Set/Clear VMX_{ENTRY,EXIT}_CTLS_LOAD_EFER in VMX_{ENTRY,EXIT}_CONTROLS fields according to vcpu_regs->ia32_efer.
 * - If vcpu_regs->rflags == 0UL, set RFLAGs to 0x02UL, otherwise set it to vcpu_regs->rflags & ~(0x8d5UL).
 * - Set CR0/CR3/CR4 to the values in vcpu_regs.
 * - Set vCPU mode according to vcpu_regs {cs_ar, ia32_efer, cr0}.
 *
 * Normal case behaviors:
 * - Update vCPU GP registers, CS/SS/DS/ES/FS/GS/GDTR/LDTR/TR registers, RIP/RSP/RFLAGS registers, CR0/CR3/CR4 registers.
 * - Update VMX_{ENTRY, EXIT}_CTLS_LOAD_EFER bits in VMCS VMX_ENTRY_CTLS/VMX_EXIT_CTLS VMCS fields;
 * - Update vCPU mode to the target mode.
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[inout] vcpu Target vCPU to update the states
 * @param[in] vcpu_regs Target register states to set
 *
 * @return None
 *
 * @pre vcpu != NULL && vcpu_regs != NULL
 *
 * @post N/A
 */
void set_vcpu_regs(struct acrn_vcpu *vcpu, struct acrn_regs *vcpu_regs)
{
	struct ext_context *ectx;
	struct run_context *ctx;
	uint16_t *sel = &(vcpu_regs->cs_sel);
	struct segment_sel *seg;
	uint32_t limit, attr;

	ectx = &(vcpu->arch.contexts.ext_ctx);
	ctx = &(vcpu->arch.contexts.run_ctx);

	/* NOTE:
	 * This is to set the attr and limit to default value.
	 * If the set_vcpu_regs is used not only for vcpu state
	 * initialization, this part of code needs be revised.
	 */
	if ((vcpu_regs->cr0 & CR0_PE) != 0UL) {
		attr = PROTECTED_MODE_DATA_SEG_AR;
		limit = PROTECTED_MODE_SEG_LIMIT;
	} else {
		attr = REAL_MODE_DATA_SEG_AR;
		limit = REAL_MODE_SEG_LIMIT;
	}

	for (seg = &(ectx->cs); seg <= &(ectx->gs); seg++) {
		seg->base     = 0UL;
		seg->limit    = limit;
		seg->attr     = attr;
		seg->selector = *sel;
		sel++;
	}

	/* override cs attr/base/limit */
	ectx->cs.attr = vcpu_regs->cs_ar;
	ectx->cs.base = vcpu_regs->cs_base;
	ectx->cs.limit = vcpu_regs->cs_limit;

	ectx->gdtr.base = vcpu_regs->gdt.base;
	ectx->gdtr.limit = vcpu_regs->gdt.limit;

	ectx->idtr.base = vcpu_regs->idt.base;
	ectx->idtr.limit = vcpu_regs->idt.limit;

	ectx->ldtr.selector = vcpu_regs->ldt_sel;
	ectx->tr.selector = vcpu_regs->tr_sel;

	/* NOTE:
	 * This is to set the ldtr and tr to default value.
	 * If the set_vcpu_regs is used not only for vcpu state
	 * initialization, this part of code needs be revised.
	 */
	ectx->ldtr.base = 0UL;
	ectx->tr.base = 0UL;
	ectx->ldtr.limit = 0xFFFFU;
	ectx->tr.limit = 0xFFFFU;
	ectx->ldtr.attr = LDTR_AR;
	ectx->tr.attr = TR_AR;

	(void)memcpy_s((void *)&(ctx->cpu_regs), sizeof(struct acrn_gp_regs),
			(void *)&(vcpu_regs->gprs), sizeof(struct acrn_gp_regs));

	vcpu_set_rip(vcpu, vcpu_regs->rip);
	vcpu_set_efer(vcpu, vcpu_regs->ia32_efer);
	vcpu_set_rsp(vcpu, vcpu_regs->gprs.rsp);

	if (vcpu_regs->rflags == 0UL) {
		vcpu_set_rflags(vcpu, 0x02UL);
	} else {
		vcpu_set_rflags(vcpu, vcpu_regs->rflags & ~(0x8d5UL));
	}

	/* cr0, cr3 and cr4 needs be set without using API.
	 * The real cr0/cr3/cr4 writing will be delayed to
	 * init_vmcs
	 */
	ctx->cr0 = vcpu_regs->cr0;
	ectx->cr3 = vcpu_regs->cr3;
	ctx->cr4 = vcpu_regs->cr4;

	set_vcpu_mode(vcpu, vcpu_regs->cs_ar, vcpu_regs->ia32_efer,
			vcpu_regs->cr0);
}

/**
 * @brief Variable to hold initial real-mode CPU register values
 *
 * This struct variable holds initial values for real-mode GDT/IDT, CS segment, RIP and CR0/CR3/CR4 registers.
 */
static struct acrn_regs realmode_init_vregs = {
	.gdt = {
		.limit = 0xFFFFU,
		.base = 0UL,
	},
	.idt = {
		.limit = 0xFFFFU,
		.base = 0UL,
	},
	.cs_ar = REAL_MODE_CODE_SEG_AR,
	.cs_sel = REAL_MODE_BSP_INIT_CODE_SEL,
	.cs_base = 0xFFFF0000UL,
	.cs_limit = 0xFFFFU,
	.rip = 0xFFF0UL,
	.cr0 = CR0_ET | CR0_NE,
	.cr3 = 0UL,
	.cr4 = 0UL,
};

/**
 * @brief Variable to hold guest protected-mode GDT
 *
 * This struct variable holds guest protected-mode GDT contents after vCPU is configured to protected-mode.
 */
static uint64_t init_vgdt[] = {
	0x0UL,
	0x0UL,
	0x00CF9B000000FFFFUL,   /* Linear Code */
	0x00CF93000000FFFFUL,   /* Linear Data */
};

/**
 * @brief Variable to hold initial protected-mode CPU register values
 *
 * This struct variable holds initial values for protected-mode GDT/IDT, CS/DS/SS/ES segments and CR0.
 */
static struct acrn_regs protect_mode_init_vregs = {
	.gdt = {
		.limit = 0xFFFFU,
		.base = 0UL,
	},
	.idt = {
		.limit = 0xFFFFU,
		.base = 0UL,
	},
	.cs_ar = PROTECTED_MODE_CODE_SEG_AR,
	.cs_limit = PROTECTED_MODE_SEG_LIMIT,
	.cs_sel = 0x10U,
	.cr0 = CR0_ET | CR0_NE | CR0_PE,
	.ds_sel = 0x18U,
	.ss_sel = 0x18U,
	.es_sel = 0x18U,
};

/**
 * @brief Check VM real and protected mode CR0/CR4 values
 *
 * This function calls is_valid_cr0_cr4() to check validity of VM initial CR0/CR4 values.
 * If it fails to pass check, print error message together with original CR0/CR4 values.
 *
 * Normal case behaviors:
 * - Return true to indicate valid CR0/CR4 values, otherwise false.
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[in] vm Target VM to check CR0/CR4 register
 *
 * @return A boolean value
 *
 * @pre N/A
 *
 * @post N/A
 */
bool sanitize_cr0_cr4_pattern(struct acrn_vm *vm)
{
	bool ret = false;

	if (is_valid_cr0_cr4(vm, realmode_init_vregs.cr0, realmode_init_vregs.cr4) &&
			is_valid_cr0_cr4(vm, protect_mode_init_vregs.cr0, protect_mode_init_vregs.cr4)) {
		ret = true;
	} else {
		pr_err("Wrong CR0/CR4 pattern: real %lx %lx; protected %lx %lx\n", realmode_init_vregs.cr0,
			realmode_init_vregs.cr4, protect_mode_init_vregs.cr0, protect_mode_init_vregs.cr4);
	}
	return ret;
}

/**
 * @brief Reset vCPU registers to real-mode state
 *
 * This function calls set_vcpu_regs() to reset vCPU registers to real-mode default CPU state.
 * Reset tsc_aux flag if the target reset mode is not INIT_RESET.
 *
 * @param[inout] vcpu Target vCPU to reset
 *
 * @return None
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static void reset_vcpu_regs(struct acrn_vcpu *vcpu)
{
	set_vcpu_regs(vcpu, &realmode_init_vregs);

	/*
	 * According to SDM Vol3 "Table 9-1. IA-32 and Intel 64 Processor States Following Power-up, Reset, or INIT",
	 * for some registers, the state following INIT is different from the state following Power-up, Reset.
	 * (For all registers mentioned in Table 9-1, the state following Power-up and following Reset are same.)
	 *
	 * To distinguish this kind of case for vCPU:
	 *  - If the state following INIT is same as the state following Power-up/Reset, handle it in
	 *    set_vcpu_regs above.
	 *  - Otherwise, handle it below.
	 */
	if (vcpu->arch.contexts.ext_ctx.reset_mode != VCPU_INIT_RESET) {
		/* IA32_TSC_AUX: 0 following Power-up/Reset, unchanged following INIT */
		vcpu->arch.contexts.ext_ctx.tsc_aux = 0UL;
	}
}

/**
 * @brief Initialize protected-mode vCPU registers
 *
 * This function copies default vCPU register values from protect_mode_init_vregs and override
 * virtual gdt base/limit with the data passed by caller, then calls set_vcpu_regs() to initialize
 * vCPU protected mode registers and flags.
 *
 * Normal case behaviors:
 * - Initialize the vCPU states to protected-mode settings.
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[inout] vcpu Target vCPU to initialize
 * @param[in] vgdt_base_gpa Guest gdt table base address
 *
 * @return None
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 *
 * @remark vgdt_base_gpa is valid GPA.
 */
void init_vcpu_protect_mode_regs(struct acrn_vcpu *vcpu, uint64_t vgdt_base_gpa)
{
	struct acrn_regs vcpu_regs;

	(void)memcpy_s((void *)&vcpu_regs, sizeof(struct acrn_regs),
		(void *)&protect_mode_init_vregs, sizeof(struct acrn_regs));

	vcpu_regs.gdt.base = vgdt_base_gpa;
	vcpu_regs.gdt.limit = sizeof(init_vgdt) - 1U;
	(void)copy_to_gpa(vcpu->vm, &init_vgdt, vgdt_base_gpa, sizeof(init_vgdt));

	set_vcpu_regs(vcpu, &vcpu_regs);
}

/**
 * @brief Set vCPU startup entry
 *
 * This function is used to set up guest code entry. It sets the entry as below:
 * - vCPU CS.selector = (entry >> 4U) & 0xFFFFU
 * - vCPU CS.base = CS.selector << 4U
 * - vCPU RIP = 0UL
 *
 * Normal case behaviors:
 * - Set guest code execution entry.
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[inout] vcpu Target vCPU to set up entry
 * @param[in] entry The code entry for vCPU to run
 *
 * @return None
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 *
 * @remark The entry should contain valid CS selector and base.
 */
void set_vcpu_startup_entry(struct acrn_vcpu *vcpu, uint64_t entry)
{
	struct ext_context *ectx;

	ectx = &(vcpu->arch.contexts.ext_ctx);
	ectx->cs.selector = (uint16_t)((entry >> 4U) & 0xFFFFU);
	ectx->cs.base = (uint64_t)ectx->cs.selector << 4U;

	vcpu_set_rip(vcpu, 0UL);
}

static void vcpu_set_state(struct acrn_vcpu *vcpu, enum vcpu_state new_state);

/**
 * @brief Reset vCPU's IA32_MTRR_DEF_TYPE MSR value
 *
 * This function resets vCPU's IA32_MTRR_DEF_TYPE MSR to VM settings for the MSR. When vCPU is in reset process,
 * this function is called to reset vCPU's IA32_MTRR_DEF_TYPE settings.
 *
 * @param[inout] vcpu Target vCPU to reset the MSR value
 *
 * @return None
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static void vcpu_reset_mtrr_def_type(struct acrn_vcpu *vcpu)
{
	union mtrr_def_type_reg *def_type = &vcpu->arch.mtrr_def_type;

	def_type->value = vcpu->vm->arch_vm.vmtrr.def_type.value;
}

/**
 * @brief Create a virtual CPU for a VM
 *
 * This function is used to create a vCPU on specific physical CPU for a VM.
 * It performs below vCPU setup if new vCPU ID is less than MAX_VCPUS_PER_VM:
 * - Allocate a vCPU slot from VM's static memory with new vCPU ID and reset the vCPU slot memory.
 * - Initialize vCPU flags such as vCPU ID, VPID, Posted-Interrupt notification vector and destination pCPU ID etc.
 * - Call vlapic_create() to create vLAPIC for the vCPU.
 * - Reset XSAVE memory and initialize XSAVE flags.
 * - Reset vCPU flags/VMCS/running context/MTRR default type to default values.
 * - Call clear_va_vmcs to Clear VMCS data structure
 * - Initillize interrupt mode to kill vCPU out of non-root mode
 * - Call set_vcpu_regs to Reset vCPU to initial states
 * - Rebuild vCPU thread stack if pCPU state is offline.
 * - Call reset_event to Reset all the vCPU schedule event
 * - Initialize IWKeys with random values
 * - Set vCPU state to VCPU_INIT.
 * - Reset io_request for the vCPU.
 * - Increase created_vcpus for virtual machine the vCPU belongs to.
 *
 * Otherwise print error message and return an error.
 *
 * @param[in] pcpu_id Physical CPU ID on which the vCPU is created.
 * @param[inout] vm The virtual machine the vCPU belongs to.
 * @param[inout] rtn_vcpu_handle Pointer to caller supplied local vCPU pointer variable.
 *
 * @return A integer value indicates creating result.
 *
 * @retval 0 The vCPU is created successfully.
 * @retval -EINVAL The created vCPU number per VM exceeds MAX_VCPUS_PER_VM.
 *
 * @pre vm != NULL && rtn_vcpu_handle != NULL
 *
 * @post N/A
 */
static int32_t create_vcpu(uint16_t pcpu_id, struct acrn_vm *vm, struct acrn_vcpu **rtn_vcpu_handle)
{
	struct acrn_vcpu *vcpu;
	uint16_t vcpu_id;
	int32_t ret;

	pr_info("Creating VCPU working on PCPU%hu", pcpu_id);

	/*
	 * vcpu->vcpu_id = vm->hw.created_vcpus;
	 * vm->hw.created_vcpus++;
	 */
	vcpu_id = vm->hw.created_vcpus;
	if (vcpu_id < MAX_VCPUS_PER_VM) {
		/* Allocate memory for VCPU */
		vcpu = &(vm->hw.vcpu_array[vcpu_id]);
		(void)memset((void *)vcpu, 0U, sizeof(struct acrn_vcpu));

		/* Initialize CPU ID for this VCPU */
		vcpu->vcpu_id = vcpu_id;
		per_cpu(ever_run_vcpu, pcpu_id) = vcpu;

		/* Initialize the parent VM reference */
		vcpu->vm = vm;

		/* Initialize the virtual ID for this VCPU */
		/* FIXME:
		 * We assume that we always destroys vcpus in one
		 * shot (like when vm is destroyed). If we need to support
		 * specific vcpu destroy on the fly, this vcpu_id assignment
		 * needs revise.
		 */

		pr_info("Create VM%d-VCPU%d, Role: %s",
				vcpu->vm->vm_id, vcpu->vcpu_id,
				is_vcpu_bsp(vcpu) ? "PRIMARY" : "SECONDARY");

		/*
		 * If the logical processor is in VMX non-root operation and
		 * the "enable VPID" VM-execution control is 1, the current VPID
		 * is the value of the VPID VM-execution control field in the VMCS.
		 *
		 * This assignment guarantees a unique non-zero per vcpu vpid at runtime.
		 */
		vcpu->arch.vpid = ALLOCATED_MIN_L1_VPID + (vm->vm_id * MAX_VCPUS_PER_VM) + vcpu->vcpu_id;

		/*
		 * There are two locally independent writing operations, namely the
		 * assignment of vcpu->vm and vcpu_array[]. Compilers may optimize
		 * and reorder writing operations while users of vcpu_array[] may
		 * assume the presence of vcpu->vm. A compiler barrier is added here
		 * to prevent compiler reordering, ensuring that assignments to
		 * vcpu->vm precede vcpu_array[].
		 */
		cpu_compiler_barrier();

		/*
		 * ACRN uses the following approach to manage VT-d PI notification vectors:
		 * Allocate unique Activation Notification Vectors (ANV) for each vCPU that
		 * belongs to the same pCPU, the ANVs need only be unique within each pCPU,
		 * not across all vCPUs. The max numbers of vCPUs may be running on top of
		 * a pCPU is CONFIG_MAX_VM_NUM, since ACRN does not support 2 vCPUs of the same
		 * VM running on top of the same pCPU. This reduces # of pre-allocated ANVs for
		 * posted interrupts to CONFIG_MAX_VM_NUM, and enables ACRN to avoid switching
		 * between active and wake-up vector values in the posted interrupt descriptor
		 * on vCPU scheduling state changes.
		 *
		 * We maintain a per-pCPU array of vCPUs, and use vm_id as the index to the
		 * vCPU array
		 */
		per_cpu(vcpu_array, pcpu_id)[vm->vm_id] = vcpu;

		/*
		 * Use vm_id as the index to indicate the posted interrupt IRQ/vector pair that are
		 * assigned to this vCPU:
		 * 0: first posted interrupt IRQs/vector pair (POSTED_INTR_IRQ/POSTED_INTR_VECTOR)
		 * ...
		 * CONFIG_MAX_VM_NUM-1: last posted interrupt IRQs/vector pair
		 * ((POSTED_INTR_IRQ + CONFIG_MAX_VM_NUM - 1U)/(POSTED_INTR_VECTOR + CONFIG_MAX_VM_NUM - 1U)
		 */
		vcpu->arch.pid.control.bits.nv = POSTED_INTR_VECTOR + vm->vm_id;

		/* ACRN does not support vCPU migration, one vCPU always runs on
		 * the same pCPU, so PI's ndst is never changed after startup.
		 */
		vcpu->arch.pid.control.bits.ndst = per_cpu(lapic_id, pcpu_id);

		/* Create per vcpu vlapic */
		vlapic_create(vcpu, pcpu_id);

		/* Populate the return handle */
		*rtn_vcpu_handle = vcpu;
		vcpu_set_state(vcpu, VCPU_INIT);

		init_xsave(vcpu);
		vcpu_reset_internal(pcpu_id, vcpu, VCPU_POWER_UP);
		(void)memset((void *)&vcpu->req, 0U, sizeof(struct io_request));
		vm->hw.created_vcpus++;
		ret = 0;
	} else {
		pr_err("%s, vcpu id is invalid!\n", __func__);
		ret = -EINVAL;
	}

	return ret;
}

/**
 * @brief Start to execute guest code in non-root mode
 *
 * This function first flushes L1 data cache if hypervisor enabled L1D flush on VM-Entry. Then it calls
 * cpu_internal_buffers_clear() to mitigate CPU vulnerability. Finally it prepares guest registers
 * and tries to execute guest code in non-root mode.
 *
 * @param[inout] ctx The vCPU context state
 * @param[in] launch_type vCPU entry type, launch or resume
 * @param[in] ibrs_type Type of speculation control
 *
 * @return A value to indicate VM-Entry status
 *
 * @pre ctx != NULL
 *
 * @post N/A
 */
static inline int32_t exec_vmentry(struct run_context *ctx, int32_t launch_type, int32_t ibrs_type)
{
#ifdef CONFIG_L1D_FLUSH_VMENTRY_ENABLED
	cpu_l1d_flush();
#endif

	/* Mitigation for MDS vulnerability, overwrite CPU internal buffers */
	cpu_internal_buffers_clear();

	return vmx_vmrun(ctx, launch_type, ibrs_type);
}

/**
 * @brief Write VMCS fields with cached values
 *
 * This function is used to update RIP/RSP/EFER/RFLAGS/CR0/CR4 VMCS fields. If the register relevant bit
 * is set in vCPU reg_updated, clear the bit and write corresponding VMCS field with the cached register
 * value.
 *
 * @param[inout] vcpu Target vCPU to write the register VMCS fields
 *
 * @return N/A
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static void write_cached_registers(struct acrn_vcpu *vcpu)
{
	struct run_context *ctx = &vcpu->arch.contexts.run_ctx;

	if (bitmap_test_and_clear_nolock(CPU_REG_RIP, &vcpu->reg_updated)) {
		exec_vmwrite(VMX_GUEST_RIP, ctx->rip);
	}
	if (bitmap_test_and_clear_nolock(CPU_REG_RSP, &vcpu->reg_updated)) {
		exec_vmwrite(VMX_GUEST_RSP, ctx->cpu_regs.regs.rsp);
	}
	if (bitmap_test_and_clear_nolock(CPU_REG_EFER, &vcpu->reg_updated)) {
		exec_vmwrite64(VMX_GUEST_IA32_EFER_FULL, ctx->ia32_efer);
	}
	if (bitmap_test_and_clear_nolock(CPU_REG_RFLAGS, &vcpu->reg_updated)) {
		exec_vmwrite(VMX_GUEST_RFLAGS, ctx->rflags);
	}

	/*
	 * Currently, updating CR0/CR4 here is only designed for world
	 * switching. There should be no other module requesting updates.
	 * CR0/CR4 here.
	 */
	if (bitmap_test_and_clear_nolock(CPU_REG_CR0, &vcpu->reg_updated)) {
		vcpu_set_cr0(vcpu, ctx->cr0);
	}

	if (bitmap_test_and_clear_nolock(CPU_REG_CR4, &vcpu->reg_updated)) {
		vcpu_set_cr4(vcpu, ctx->cr4);
	}
}

/**
 * @brief Set up VMCS and run guest code
 *
 * This function performs below operations to run guest code:
 * - Check whether cached guest RIP/RSP/EFER/RFLAGS/CR0/CR4 is updated, if it is, write the new data to VMCS field.
 * - If it's the first time to launch vCPU then:
 *   - Write vCPU VPID to corresponding VMCS filed if it's valid.
 *   - Call flush_vpid_global() to flush caches.
 *   - Set vCPU state to launched.
 *   - If IBRS type is IBRS_RAW, set PRED_SET_IBPB in MSR_IA32_PRED_CMD MSR.
 *
 * Otherwise skip the last emulated instruction.
 * Then do following things:
 *   - If Hypervisor is configured to flush L1 data cached at VM-Entry, do it before run guest code.
 *   - Overwrite CPU internal buffers to mitigate MDS vulnerability if it's required to do so.
 *   - Launch or resume vCPU execution according to launch status.
 *
 * After vCPU VM-Exit or VM-Entry failure, the function does following clean-up work:
 * - Set vCPU mode according to guest CS attribute, EFER MSR and CR0.
 * - Read VMCS VMX_EXIT_INSTR_LEN and VMX_EXIT_REASON fields to cache the data for later HV handling.
 * - If VM-Entry failed, it prints error message and related information.
 *
 * Normal case behaviors:
 * - Prepare guest execution environment and run guest code.
 *
 * Error case behaviors:
 * - Fail to run guest code.
 *
 * @param[inout] vcpu Target vCPU to run guest code
 *
 * @return A integer to indicate VM-Entry result
 *
 * @retval 0 Succeeded in running guest code
 * @retval non-zero Failed to run guest code
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
int32_t run_vcpu(struct acrn_vcpu *vcpu)
{
	uint32_t cs_attr;
	uint64_t ia32_efer, cr0;
	struct run_context *ctx = &vcpu->arch.contexts.run_ctx;
	int32_t status = 0;
	int32_t ibrs_type = get_ibrs_type();

	if (vcpu->reg_updated != 0UL) {
		write_cached_registers(vcpu);
	}

	/* If this VCPU is not already launched, launch it */
	if (!vcpu->launched) {
		pr_info("VM %d Starting VCPU %hu",
				vcpu->vm->vm_id, vcpu->vcpu_id);

		if (vcpu->arch.vpid != 0U) {
			exec_vmwrite16(VMX_VPID, vcpu->arch.vpid);
		}

		/*
		 * A power-up or a reset invalidates all linear mappings,
		 * guest-physical mappings, and combined mappings
		 */
		flush_vpid_global();

#ifdef CONFIG_HYPERV_ENABLED
		if (is_vcpu_bsp(vcpu)) {
			hyperv_init_time(vcpu->vm);
		}
#endif

		/* Set vcpu launched */
		vcpu->launched = true;

		/* avoid VMCS recycling RSB usage, set IBPB.
		 * NOTE: this should be done for any time vmcs got switch
		 * currently, there is no other place to do vmcs switch
		 * Please add IBPB set for future vmcs switch case
		 */
		if (ibrs_type == IBRS_RAW) {
			msr_write(MSR_IA32_PRED_CMD, PRED_SET_IBPB);
		}

		/* Launch the VM */
		status = exec_vmentry(ctx, VM_LAUNCH, ibrs_type);

		/* See if VM launched successfully */
		if (status == 0) {
			if (is_vcpu_bsp(vcpu)) {
				pr_info("VM %d VCPU %hu successfully launched",
					vcpu->vm->vm_id, vcpu->vcpu_id);
			}
		}
	} else {
		/* This VCPU was already launched, check if the last guest
		 * instruction needs to be repeated and resume VCPU accordingly
		 */
		if (vcpu->arch.inst_len != 0U) {
			exec_vmwrite(VMX_GUEST_RIP, vcpu_get_rip(vcpu) + vcpu->arch.inst_len);
		}

		/* Resume the VM */
		status = exec_vmentry(ctx, VM_RESUME, ibrs_type);
	}

	cs_attr = exec_vmread32(VMX_GUEST_CS_ATTR);
	ia32_efer = vcpu_get_efer(vcpu);
	cr0 = vcpu_get_cr0(vcpu);
	set_vcpu_mode(vcpu, cs_attr, ia32_efer, cr0);

	vcpu->reg_cached = 0UL;

	/* Obtain current VCPU instruction length */
	vcpu->arch.inst_len = exec_vmread32(VMX_EXIT_INSTR_LEN);

	/* Obtain VM exit reason */
	vcpu->arch.exit_reason = exec_vmread32(VMX_EXIT_REASON);

	if (status != 0) {
		/* refer to 64-ia32 spec section 24.9.1 volume#3 */
		if ((vcpu->arch.exit_reason & VMX_VMENTRY_FAIL) != 0U) {
			pr_fatal("vmentry fail reason=%x", vcpu->arch.exit_reason);
		} else {
			pr_fatal("vmexit fail err_inst=%x", exec_vmread32(VMX_INSTR_ERROR));
		}

		ASSERT(status == 0, "vm fail");
	}

	return status;
}

/**
 * @brief Put vCPU to offline status and do cleanup
 *
 * This function is used to put vCPU into offline status. It does following clean-up work:
 * - Call vlapic_free() to release active timer.
 * - Reset per-CPU variable ever_run_vcpu to NULL.
 * - Reset vCPU slot in per-CPU variable vcpu_array to NULL.
 * - Set vCPU state to VCPU_OFFLINE.
 *
 * Normal case behaviors:
 * - Clean up vCPU states before make it offline.
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[inout] vcpu Target vCPU to put into offline state
 *
 * @return N/A
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
void offline_vcpu(struct acrn_vcpu *vcpu)
{
	vlapic_free(vcpu);
	per_cpu(ever_run_vcpu, pcpuid_from_vcpu(vcpu)) = NULL;

	/* This operation must be atomic to avoid contention with posted interrupt handler */
	per_cpu(vcpu_array, pcpuid_from_vcpu(vcpu))[vcpu->vm->vm_id] = NULL;

	vcpu_set_state(vcpu, VCPU_OFFLINE);
}

void online_vcpu(struct acrn_vcpu *vcpu)
{
	vlapic_create(vcpu, pcpuid_from_vcpu(vcpu));
	per_cpu(ever_run_vcpu, pcpuid_from_vcpu(vcpu)) = vcpu;

	/* This operation must be atomic to avoid contention with posted interrupt handler */
	per_cpu(vcpu_array, pcpuid_from_vcpu(vcpu))[vcpu->vm->vm_id] = vcpu;

	reset_vcpu(vcpu, VCPU_COLD_RESET);
}

/**
 * @brief Kick vCPU out of non-root mode
 *
 * This function calls kick_pcpu() to kick the target vCPU out of non-root mode when below conditions are met:
 * - Current physical CPU is not the one running the vCPU.
 * - Current VMCS region on target physical CPU matches the vCPU's VMCS region.
 *
 * Normal case behaviors:
 * - Kick target vCPU out of non-root mode.
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[in] vcpu Target vCPU to be kicked off.
 *
 * @return N/A
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
void kick_vcpu(struct acrn_vcpu *vcpu)
{
	uint16_t pcpu_id = pcpuid_from_vcpu(vcpu);

	if ((get_pcpu_id() != pcpu_id) && (per_cpu(vmcs_run, pcpu_id) == vcpu->arch.vmcs)) {
		kick_pcpu(pcpu_id);
	}
}

/**
 * @brief Create a stack frame for target vCPU's stack states
 *
 * This function is to create a stack frame to hold the vCPU GPRs, the stack frame is used when vCPU is
 * scheduled in and out. The previous vCPU GPRs are stored in it's frame, and the next vCPU's GPRs are
 * loaded from it's stack frame.
 *
 * @param[inout] vcpu Target vCPU to build stack frame for
 *
 * @return Start address of the stack frame
 *
 * @pre vcpu != NULL
 * @pre (&vcpu->stack[CONFIG_STACK_SIZE] & (CPU_STACK_ALIGN - 1UL)) == 0
 *
 * @post N/A
 */
static uint64_t build_stack_frame(struct acrn_vcpu *vcpu)
{
	uint64_t stacktop = (uint64_t)&vcpu->stack[CONFIG_STACK_SIZE];
	struct stack_frame *frame;
	uint64_t *ret;

	frame = (struct stack_frame *)stacktop;
	frame -= 1;

	frame->magic = SP_BOTTOM_MAGIC;
	frame->rip = (uint64_t)vcpu->thread_obj.thread_entry; /*return address*/
	frame->rflag = 0UL;
	frame->rbx = 0UL;
	frame->rbp = 0UL;
	frame->r12 = 0UL;
	frame->r13 = 0UL;
	frame->r14 = 0UL;
	frame->r15 = 0UL;
	frame->rdi = (uint64_t)&vcpu->thread_obj;

	ret = &frame->rdi;

	return (uint64_t) ret;
}

/**
 * @brief Reset vCPU states per reset mode
 *
 * This function performs following operations to reset vCPU states:
 * - Reset vCPU flags/VMCS/running context/MTRR default type to default values.
 * - Call clear_va_vmcs to Clear VMCS data structure
 * - Initillize interrupt mode to kill vCPU out of non-root mode
 * - Call set_vcpu_regs to Reset vCPU to initial states
 * - Rebuild vCPU thread stack if pCPU state is offline.
 * - Call reset_event to Reset all the vCPU schedule event
 * - Initialize IWKeys with random values
 * - Set vCPU state to VCPU_INIT.
 *
 * Normal case behaviors:
 * - Reset vCPU internal states according to reset mode.
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[inout] vcpu Target vCPU to reset
 * @param[in] mode Target reset mode
 *
 * @return N/A
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 *
 * @remark a)The VM this vCPU belongs to should be paused before call this function. b) mode is a valid defined value.
 */
void reset_vcpu(struct acrn_vcpu *vcpu, enum vcpu_reset_mode mode)
{
	pr_dbg("vcpu%hu reset", vcpu->vcpu_id);

	vcpu_reset_internal(pcpuid_from_vcpu(vcpu), vcpu, mode);
	vcpu_set_state(vcpu, VCPU_INIT);
}

/**
 * @brief Set vCPU state to VCPU_ZOMBIE
 *
 * If current vCPU state is VCPU_RUNNING or VCPU_INIT, this function sets vCPU state to VCPU_ZOMBIE.
 * Then it does below clean-up work if the vCPU was in VCPU_RUNNING state:
 * - If the vCPU is running on current pCPU, call sleep_thread() to make the vCPU thread in blocked status.
 * - Otherwise call sleep_thread_sync() to wait for the target vCPU thread until it's in blocked status.
 *
 * Normal case behaviors:
 * - The target vCPU is set to VCPU_ZOMBIE state and is blocked.
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[inout] vcpu Target vCPU to set VCPU_ZOMBIE state
 *
 * @return None
 *
 * @pre vcpu != NULL
 *
 * @post The target vCPU state is VCPU_ZOMBIE and vCPU thread is blocked.
 */
void zombie_vcpu(struct acrn_vcpu *vcpu)
{
	enum vcpu_state prev_state;
	uint16_t pcpu_id = pcpuid_from_vcpu(vcpu);

	pr_dbg("vcpu%hu paused, new state: %d",	vcpu->vcpu_id, VCPU_ZOMBIE);

	if ((vcpu->state == VCPU_RUNNING) || (vcpu->state == VCPU_INIT)) {
		prev_state = vcpu->state;
		vcpu_set_state(vcpu, VCPU_ZOMBIE);

		if (prev_state == VCPU_RUNNING) {
			if (pcpu_id == get_pcpu_id()) {
				sleep_thread(&vcpu->thread_obj);
			} else {
				sleep_thread_sync(&vcpu->thread_obj);
			}
		}
	}
}

/**
 * @brief Save XSAVE-managed feature components to XSAVE area
 *
 * This function performs below things if physical CPU supports XSAVES feature:
 * - Read current XCR(0) from physical CPU, write it back with XSAVE_SSE set.
 * - Execute XSAVES instruction to save the features corresponding bits set in (XCR0 | IA32_XSS) to XSAVE area.
 *
 * @param[inout] vcpu Target vCPU to store the feature components
 * @param[in] ectx vCPU's extended context
 *
 * @return None
 *
 * @pre vcpu != NULL && ectx != NULL
 *
 * @post N/A
 */
static void save_xsave_area(__unused struct acrn_vcpu *vcpu, struct ext_context *ectx)
{
	if (pcpu_has_cap(X86_FEATURE_XSAVES)) {
		ectx->xcr0 = read_xcr(0);
		write_xcr(0, ectx->xcr0 | XSAVE_SSE);
		xsaves(&ectx->xs_area, UINT64_MAX);
	}
}

/**
 * @brief Restore XSAVE-managed feature components from XSAVE area to registers
 *
 * This function performs below things if physical CPU supports XSAVES feature:
 * - Write physical XCR(0) with cached XCR0 and XSAVE_SSE bit.
 * - Write physical IA32_XSS MSR with guest IA32_XSS MSR value.
 * - Execute XRSTORS to restore registers for features corresponding to bits set in (XCR0 | IA32_XSS) from XSAVE area.
 * - Write physical XCR(0) with cached guest XCR0.
 *
 * @param[in] vcpu Target vCPU to restore the feature components
 * @param[in] ectx vCPU's extended context
 *
 * @return None
 *
 * @pre vcpu != NULL && ectx != NULL
 *
 * @post N/A
 */
static void rstore_xsave_area(const struct acrn_vcpu *vcpu, const struct ext_context *ectx)
{
	if (pcpu_has_cap(X86_FEATURE_XSAVES)) {
		/*
		 * Restore XSAVE area if any of the following conditions is met:
		 * 1. "vcpu->launched" is false (state initialization for guest)
		 * 2. "vcpu->arch.xsave_enabled" is true (state restoring for guest)
		 *
		 * Before vCPU is launched, condition 1 is satisfied.
		 * After vCPU is launched, condition 2 is satisfied because
		 * that "vcpu->arch.xsave_enabled" is consistent with pcpu_has_cap(X86_FEATURE_XSAVES).
		 *
		 * Therefore, the check against "vcpu->launched" and "vcpu->arch.xsave_enabled" can be eliminated here.
		 */
		write_xcr(0, ectx->xcr0 | XSAVE_SSE);
		msr_write(MSR_IA32_XSS, vcpu_get_guest_msr(vcpu, MSR_IA32_XSS));
		xrstors(&ectx->xs_area, UINT64_MAX);
		write_xcr(0, ectx->xcr0);
	}
}

/* TODO:
 * Now we have switch_out and switch_in callbacks for each thread_object, and schedule
 * will call them every thread switch. We can implement lazy context switch , which
 * only does context switch when really need.
 */

/**
 * @brief Save vCPU relevant registers to extended context to prepare switch out the context
 *
 * This function saves current vCPU passthrough MSRs, i.e., MSR_IA32_STAR, MSR_IA32_CSTAR, MSR_IA32_LSTAR,
 * MSR_IA32_FMASK, MSR_IA32_KERNEL_GS_BASE, MSR_IA32_TSC_AUX and XSAVE-managed components to vCPU extended
 * context.
 *
 * Normal case behaviors:
 * - Save current vCPU passthrough MSRs and XSAVE-managed components to extended context.
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[in] prev Thread object to be scheduled out
 *
 * @return None
 *
 * @pre prev != NULL
 *
 * @post N/A
 */
static void context_switch_out(struct thread_object *prev)
{
	struct acrn_vcpu *vcpu = container_of(prev, struct acrn_vcpu, thread_obj);
	struct ext_context *ectx = &(vcpu->arch.contexts.ext_ctx);

	/* We don't flush TLB as we assume each vcpu has different vpid */
	ectx->ia32_star = msr_read(MSR_IA32_STAR);
	ectx->ia32_cstar = msr_read(MSR_IA32_CSTAR);
	ectx->ia32_lstar = msr_read(MSR_IA32_LSTAR);
	ectx->ia32_fmask = msr_read(MSR_IA32_FMASK);
	ectx->ia32_kernel_gs_base = msr_read(MSR_IA32_KERNEL_GS_BASE);
	ectx->tsc_aux = msr_read(MSR_IA32_TSC_AUX);
	save_xsave_area(vcpu, ectx);
}

/**
 * @brief Load vCPU relevant registers from extended context to prepare switch in the context
 *
 * This function performs below things:
 * - Call load_vmcs() to load vCPU VMCS memory region as current VMCS memory region.
 * - Load vCPU passthrough MSRs, i.e., MSR_IA32_STAR, MSR_IA32_CSTAR, MSR_IA32_LSTAR, MSR_IA32_FMASK,
 *   MSR_IA32_KERNEL_GS_BASE and MSR_IA32_TSC_AUX from extended context.
 * - If physical CPU supports WAITPKG feature and guest/host MSR_IA32_UMWAIT_CONTROL MSR values are different,
 *   update MSR_IA32_UMWAIT_CONTROL MSR with guest value.
 * - Load IWKey to registers for the target vCPU.
 * - Restore XSAVE-managed components from extended context to feature registers.
 *
 * Normal case behaviors:
 * - Load passthrough MSRs, XSAVE-managed components, VMCS memory region and IWKey for the vCPU.
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[in] next Thread object to be scheduled in
 *
 * @return None
 *
 * @pre next != NULL
 *
 * @post N/A
 */
static void context_switch_in(struct thread_object *next)
{
	struct acrn_vcpu *vcpu = container_of(next, struct acrn_vcpu, thread_obj);
	struct ext_context *ectx = &(vcpu->arch.contexts.ext_ctx);
	uint64_t vmsr_val;

	load_vmcs(vcpu);

	msr_write(MSR_IA32_STAR, ectx->ia32_star);
	msr_write(MSR_IA32_CSTAR, ectx->ia32_cstar);
	msr_write(MSR_IA32_LSTAR, ectx->ia32_lstar);
	msr_write(MSR_IA32_FMASK, ectx->ia32_fmask);
	msr_write(MSR_IA32_KERNEL_GS_BASE, ectx->ia32_kernel_gs_base);
	msr_write(MSR_IA32_TSC_AUX, ectx->tsc_aux);

	if (pcpu_has_cap(X86_FEATURE_WAITPKG)) {
		vmsr_val = vcpu_get_guest_msr(vcpu, MSR_IA32_UMWAIT_CONTROL);
		if (vmsr_val != msr_read(MSR_IA32_UMWAIT_CONTROL)) {
			msr_write(MSR_IA32_UMWAIT_CONTROL, vmsr_val);
		}
	}

	load_iwkey(vcpu);

	rstore_xsave_area(vcpu, ectx);
}


/**
 * @brief Launch a vCPU
 *
 * This function is used to launch a vCPU thread to be scheduled as a running vCPU object.
 * It first sets the vCPU state to VCPU_RUNNING then calls wake_thread() to make the vCPU thread
 * ready for scheduling.
 *
 * Normal case behaviors:
 * - Launch a vCPU thread to be running thread.
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[inout] vcpu Target vCPU to launch
 *
 * @return None
 *
 * @pre vcpu != NULL
 * @pre vcpu->state == VCPU_INIT
 *
 * @post N/A
 */
void launch_vcpu(struct acrn_vcpu *vcpu)
{
	uint16_t pcpu_id = pcpuid_from_vcpu(vcpu);

	pr_dbg("vcpu%hu scheduled on pcpu%hu", vcpu->vcpu_id, pcpu_id);
	vcpu_set_state(vcpu, VCPU_RUNNING);
	wake_thread(&vcpu->thread_obj);

}

/**
 * @brief Create a vCPU and initialize vCPU thread object
 *
 * This function is used to create a vCPU for a VM before launch the VM. It first creates a vCPU then initializes the
 * schedule thread object and calls init_event() to initialize all schedule events for this vCPU.
 * The function creates a vCPU as below if new vCPU ID is less than MAX_VCPUS_PER_VM:
 * - Allocate a vCPU slot from VM's static memory with new vCPU ID and reset the vCPU slot memory.
 * - Initialize vCPU flags such as vCPU ID, VPID, Posted-Interrupt notification vector and destination pCPU ID etc.
 * - Call vlapic_create() to create vLAPIC for the vCPU.
 * - Reset XSAVE memory and initialize XSAVE flags.
 * - Reset vCPU flags/VMCS/running context/MTRR default type to default values.
 * - Call clear_va_vmcs to Clear VMCS data structure
 * - Initillize interrupt mode to kill vCPU out of non-root mode
 * - Call set_vcpu_regs to Reset vCPU to initial states
 * - Rebuild vCPU thread stack if pCPU state is offline.
 * - Call reset_event to Reset all the vCPU schedule event
 * - Initialize IWKeys with random values
 * - Set vCPU state to VCPU_INIT.
 *
 * Normal case behaviors:
 * - Create a vCPU and set up its thread object attributes
 *
 * Error case behaviors:
 * - Don't set up vCPU thread object and return error code.
 *
 * @param[inout] vm The owner VM of this vCPU
 * @param[in] pcpu_id The physical CPU ID of the affinited vCPU
 *
 * @return A integer indicating creation result
 *
 * @retval 0 Succeeded in creating a vCPU.
 * @retval non-zero Failed to created a vCPU.
 *
 * @pre vm != NULL
 * @pre 0 <= pcpu_id && pcpu_id <= MAX_PCPU_NUM - 1
 *
 * @post N/A
 */
int32_t prepare_vcpu(struct acrn_vm *vm, uint16_t pcpu_id)
{
	int32_t ret, i;
	struct acrn_vcpu *vcpu = NULL;
	char thread_name[16];

	ret = create_vcpu(pcpu_id, vm, &vcpu);
	if (ret == 0) {
		(void)snprintf(thread_name, 16U, "vm%hu:vcpu%hu", vm->vm_id, vcpu->vcpu_id);
		(void)strncpy_s(vcpu->thread_obj.name, 16U, thread_name, 16U);
		vcpu->thread_obj.sched_ctl = &per_cpu(sched_ctl, pcpu_id);
		vcpu->thread_obj.thread_entry = vcpu_thread;
		vcpu->thread_obj.pcpu_id = pcpu_id;
		vcpu->thread_obj.host_sp = build_stack_frame(vcpu);
		vcpu->thread_obj.switch_out = context_switch_out;
		vcpu->thread_obj.switch_in = context_switch_in;
		init_thread_data(&vcpu->thread_obj, &get_vm_config(vm->vm_id)->sched_params);
		for (i = 0; i < VCPU_EVENT_NUM; i++) {
			init_event(&vcpu->events[i]);
		}
	}

	return ret;
}

/**
 * @brief Get physical CPU ID from vCPU object
 *
 * This function is used to get affinited physical CPU ID from vCPU thread object's cache.
 *
 * Normal case behaviors:
 * - Return affinited physical CPU ID
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[in] vcpu Target vCPU to get pCPU ID from
 *
 * @return pCPU ID of the vCPU thread
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
uint16_t pcpuid_from_vcpu(const struct acrn_vcpu *vcpu)
{
	return sched_get_pcpuid(&vcpu->thread_obj);
}

/**
 * @brief Fetch vCPU mask from corresponding pCPU mask
 *
 * This function is used to get pCPU mask from a VM's vCPU mask. The pCPU mask is calculated from vCPU/pCPU
 *  mapping in vCPU thread object.
 *
 * Normal case behaviors:
 * - Return pCPU mask of the input vCPU mask
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[in] vm Target VM from which it gets pCPU mask
 * @param[in] vdmask vCPU mask to search for
 *
 * @return pCPU mask corresponding to the vCPU mask
 *
 * @pre vm!= NULL
 *
 * @post N/A
 *
 * @remark Input vdmask contains valid vCPU mask of the VM.
 */
uint64_t vcpumask2pcpumask(struct acrn_vm *vm, uint64_t vdmask)
{
	uint16_t vcpu_id;
	uint64_t dmask = 0UL;
	struct acrn_vcpu *vcpu;

	for (vcpu_id = 0U; vcpu_id < vm->hw.created_vcpus; vcpu_id++) {
		if ((vdmask & (1UL << vcpu_id)) != 0UL) {
			vcpu = vcpu_from_vid(vm, vcpu_id);
			bitmap_set_nolock(pcpuid_from_vcpu(vcpu), &dmask);
		}
	}

	return dmask;
}

/**
 * @brief Handle Posted-Interrupt
 *
 * This function is the interrupt handler for Posted-Interrupt request(PIR). If the vCPU of the owner VM
 * is scheduled out before a PIR is received on physical CPU, HV needs to post the request to the target
 * vCPU through this handler. The target vCPU is firstly waken up by scheduler, then HV triggers a virtual
 * self-IPI with the PIR notification vector, thus the pending interrupts in Posted-Interrupt descriptor
 * (PID) are synced to vIRR at the VM-Entry.
 *
 * The function performs below steps if target vCPU ID is existing:
 * - Test Posted-Interrupt Descriptor's POSTED_INTR_ON bit, if it set, send vCPU ACRN_REQUEST_EVENT request,
 *   then call signal_event() to signal VCPU_EVENT_VIRTUAL_INTERRUPT event for the target thread.
 *
 * shutdown_vm() would unregister the devices before offline_vcpu is called, so spinlock is not needed to protect
 * access to vcpu_array and vCPU.
 *
 * Normal case behaviors:
 * - Send ACRN_REQUEST_EVENT request to target vCPU
 * - Signal VCPU_EVENT_VIRTUAL_INTERRUPT event to wake up target vCPU thread
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[in] vcpu_index VM ID for this PIR to get the vCPU slot
 *
 * @return None
 *
 * @pre vcpu_index >= 0 && vcpu_index <= CONFIG_MAX_VM_NUM - 1
 *
 * @post N/A
 */
void vcpu_handle_pi_notification(uint32_t vcpu_index)
{
	struct acrn_vcpu *vcpu = get_cpu_var(vcpu_array)[vcpu_index];

	ASSERT(vcpu_index < CONFIG_MAX_VM_NUM, "");

	if (vcpu != NULL) {
		struct pi_desc *pid = get_pi_desc(vcpu);

		if (bitmap_test(POSTED_INTR_ON, &(pid->control.value))) {
			/*
			 * Perform same as vlapic_accept_intr():
			 * Wake up the waiting thread, set the NEED_RESCHEDULE flag,
			 * at a point schedule() will be called to make scheduling decisions.
			 *
			 * Record this request as ACRN_REQUEST_EVENT,
			 * so that vlapic_inject_intr() will sync PIR to vIRR
			 */
			vcpu_make_request(vcpu, ACRN_REQUEST_EVENT);
			signal_event(&vcpu->events[VCPU_EVENT_VIRTUAL_INTERRUPT]);
		}
	}
}

/**
 * @brief Set vCPU state
 *
 * This function is used to set target vCPU state to new_state. HV can transition vCPU state between
 * VCPU_OFFLINE, VCPU_INIT, VCPU_RUNNING, VCPU_OFFLINE and VCPU_ZOMBIE per vCPU running condition.
 *
 * @param[inout] vcpu Target vCPU to set state
 * @param[in] new_state The new state vCPU to transition to
 *
 * @return None
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 *
 * @remark Input new_state is in vcpu_state.
 */
static void vcpu_set_state(struct acrn_vcpu *vcpu, enum vcpu_state new_state)
{
	vcpu->state = new_state;
}

/**
 * @}
 */
