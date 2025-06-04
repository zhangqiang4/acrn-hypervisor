/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * this file contains vmcs operations which is vcpu related
 */

#include <types.h>
#include <errno.h>
#include <asm/lib/bits.h>
#include <asm/guest/virq.h>
#include <asm/mmu.h>
#include <asm/guest/vcpu.h>
#include <asm/guest/vm.h>
#include <asm/vmx.h>
#include <asm/vtd.h>
#include <asm/guest/vmexit.h>
#include <asm/pgtable.h>
#include <asm/cpufeatures.h>
#include <trace.h>
#include <logmsg.h>

/**
 * @defgroup vp-base_vcr vp-base.vcr
 *
 * @ingroup vp-base
 *
 * @brief Implement vCR related functions and bits definitions
 *
 * This module provides external APIs to check, set/get CR{0,2,4} related virtual control registers.
 *
 * @{
 */

/**
 * @file
 * @brief Implement functions to initialize, check and get/set control registers.
 *
 * When handle guest CR4 access, the function follows below rules:
 * Guet CR4 bits in VMX operation can be either flexible or fixed. All the guest
 * reserved bits shall be trapped and emulated by HV to inject a \#GP to guest.
 *
 * For guest operatable bits, it may be:
 * - CR4_PASSTHRU_BITS:
 *	Bits that may be passed through to guest. The actual pass-through bits shall
 *	be masked by flexible bits.
 *
 * - CR4_TRAP_AND_PASSTHRU_BITS:
 *	The bits are trapped by HV and HV emulation shall eventually write the guest
 *	value to VMCS GUEST_CR4 field. The actual bits shall be masked by flexible bits.
 *
 * - CR4_TRAP_AND_EMULATE_BITS:
 *	The bits are trapped by HV and emulated, but HV updates virtual CR4 only
 *	(no update to VMCS GUEST_CR4 field), i.e. pure software emulation.
 *
 * - CR4_EMULATED_RESERVE_BITS:
 *	The bits are trapped, but are emulated by injecting a \#GP.
 *
 * NOTE: Above bits should not overlap.
 *
 * CR0 follows the same rule of CR4, except it won't inject \#GP for reserved bits violation
 * for the low 32 bits. Instead it ignores software write to those reserved bits. Attempting
 * to set any reserved bits in CR0[63:32] results in a general-protection exception, \#GP(0).
 */

/**
 * @brief Macro to define CR4 bits that can be passed through to guest.
 *
 * The actual passthrough bits shall be masked by flexible bits.
 */
#define CR4_PASSTHRU_BITS	(CR4_VME | CR4_PVI | CR4_TSD | CR4_DE | \
				CR4_PGE | CR4_PCE | CR4_OSFXSR | CR4_PCIDE | \
				CR4_OSXSAVE | CR4_FSGSBASE | CR4_OSXMMEXCPT | \
				CR4_UMIP | CR4_LASS | CR4_LAM_SUP)

/**
 * @brief Variable to hold CR4 bits that can be written through to guest CR4 register without VM-Exit
 */
static uint64_t cr4_passthru_mask = CR4_PASSTHRU_BITS;

/**
 * @brief Macro to define CR4 bits that can be written through to guest CR4 register but need VM-Exit
 */
#define CR4_TRAP_AND_PASSTHRU_BITS	(CR4_PSE | CR4_PAE | CR4_SMEP | CR4_SMAP | CR4_KL)

/**
 * @brief Variable to hold CR4 bits that can be written through to guest CR4 by HV with VM-Exit.
 */
static uint64_t	cr4_trap_and_passthru_mask = CR4_TRAP_AND_PASSTHRU_BITS; /* bound to flexible bits */

/**
 * @brief Macro to hold CR4 bits that are trapped and are always emulated by HV.
 *
 * For CR4.MCE, we follow below policy:
 * CR4.MCE should always be enabled in guest CR4, regardless of values of vCR4. Physically turning off CR4.MCE will
 * suppress \#MC and this will result in logical processor to enter shutdown state when the machine check event
 * happens. Refer to 28.8 Machine-Check Events During VM Exit, SDM Vol.3 325462-078US for details.
 */
#define CR4_TRAP_AND_EMULATE_BITS	CR4_MCE

/**
 * @brief CR4 feature bits that are always set in guest CR4
 */
#define CR4_EM_BITS_INITIAL_PHYS_VALUE	CR4_MCE

/**
 * @brief CR4 feature bits that are hidden by HV as reserved bits
 */
#define CR4_EMULATED_RESERVE_BITS	(CR4_VMXE | CR4_CET | CR4_SMXE | CR4_PKE | CR4_PKS | CR4_LA57)

/**
 * @brief CR4 emulated feature bits set to guest CR4
 */
#define CR4_EMRSV_BITS_PHYS_VALUE	CR4_VMXE

/**
 * @brief Initial value or reset value of GUEST_CR4, i.e. initial guest value.
 */
static uint64_t initial_guest_cr4;

/**
 * @brief Bits not in cr4_passthru_mask/cr4_trap_and_passthru_mask/CR4_TRAP_AND_EMULATE_BITS
 * are reserved bits.
 */
static uint64_t cr4_reserved_bits_mask;

/**
 * @brief Macro to define CR0 bits that can be passed through to guest
 *
 * CR0 follows the same rule of CR4, except it won't inject \#GP for reserved bits violation
 * for the low 32 bits. Instead, it ignores software write to those reserved bits.
 */
#define CR0_PASSTHRU_BITS	(CR0_MP | CR0_EM | CR0_TS | CR0_ET | CR0_NE | CR0_AM)

/**
 * @brief Variable to hold CR0 bits that can be passed through to guest.
 */
static uint64_t cr0_passthru_mask = CR0_PASSTHRU_BITS;

/**
 * @brief Macro to define CR0 bits that can be written through to guest CR0 register but need VM-Exit.
 */
#define CR0_TRAP_AND_PASSTHRU_BITS	(CR0_PE | CR0_PG | CR0_WP)

/**
 * @brief Variable to hold CR0 bits that can be written through to guest CR0 by HV with VM-Exit.
 */
static uint64_t	cr0_trap_and_passthru_mask = CR0_TRAP_AND_PASSTHRU_BITS;

/**
 @brief Macro to hold CR4 bits that are trapped and are always emulated by HV.
*/
#define CR0_TRAP_AND_EMULATE_BITS	(CR0_CD | CR0_NW)

/**
 * @brief CR0 feature bits that are emulated by HV
 */
#define CR0_EMULATED_RESERVE_BITS	0UL

/**
 * @brief CR0 emulated feature bits set to guest CR0
 */
#define	CR0_EMRSV_BITS_PHYS_VALUE	0UL

/**
 * @brief Initial value or reset value of GUEST_CR0, i.e. initial guest value.
 */
static uint64_t initial_guest_cr0;

/**
 * @brief Bits not in cr0_passthru_mask/cr0_trap_and_passthru_mask/CR0_TRAP_AND_EMULATE_BITS
 * are reserved bits.
 */
static uint64_t cr0_reserved_bits_mask;

/**
 * @brief PAE PDPTE reserved bits[2:1], bits[8:5].
 */
#define PAE_PDPTE_FIXED_RESVD_BITS	0x00000000000001E6UL

/**
 * @brief Check whether CR0 value is valid
 *
 * This function checks whether there's any reserved bit set in CR0 value, if no reserved bit is set,
 * it's a valid value, otherwise, invalid value.
 *
 * @param[in] cr0 Target CR0 value to check
 *
 * @return true if target CR0 value is valid, false otherwise.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline bool is_valid_cr0(uint64_t cr0)
{
	return (cr0 & cr0_reserved_bits_mask) == 0UL;
}

/**
 * @brief Check whether guest CR0 write value is valid.
 *
 * This function checks below invalid cases:
 * - The value includes bits in its high 32 bits.
 * - CR0.PG == 1, CR4.PAE == 0 and IA32_EFER.LME == 1.
 * - CR0.PE == 0 and CR0.PG == 1.
 * - CR0.CD == 0 and CR0.NW == 1.
 * - CR0.PG == 0 and cpu_mode == CPU_MODE_64BIT.
 * - CR0.PG == 0 and CR4.PCIDE == 1
 * - Previously CR0.PG == 0, new CR0.PG == 1, CR4.PAE == 1, IA32_EFER.LME == 1 and guest CS has the L-bit set.
 * - Previously CR0.PG == 0, new CR0.PG == 1, CR4.PAE == 1, IA32_EFER.LME == 1 and guest TR is a 16-bit TSS.
 *
 * If these invalid cases are not detected, then the write value is valid, otherwise invalid.
 *
 * @param[in] vcpu Target vCPU to check the CR0 value
 * @param[in] cr0 Target CR0 value to check
 *
 * @return true if the value is valid, otherwise false.
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static bool is_cr0_write_valid(struct acrn_vcpu *vcpu, uint64_t cr0)
{
	bool ret = true;
	uint32_t cs_attr;
	uint32_t tr_attr;
	/*
	 * Set 1 in high 32 bits (part of reserved bits) leads to #GP.
	 */
	if ((cr0 >> 32UL) != 0UL) {
		ret = false;
	} else {
		/* SDM 25.3 "Changes to instruction behavior in VMX non-root"
		 *
		 * We always require "unrestricted guest" control enabled. So
		 *
		 * CR0.PG = 1, CR4.PAE = 0 and IA32_EFER.LME = 1 is invalid.
		 * CR0.PE = 0 and CR0.PG = 1 is invalid.
		 *
		 * SDM 10.8.5 Initializing IA-32e mode:
		 *
		 * We check if the current CS has the L-bit set
		 * or the TR contains a 16-bit TSS on an attempt to activate IA-32e mode.
		 * If so, this write is invalid.
		 */
		if (((cr0 & CR0_PG) != 0UL) && ((exec_vmread32(VMX_GUEST_CR0) & CR0_PG) == 0UL)
			 && ((vcpu_get_efer(vcpu) & MSR_IA32_EFER_LME_BIT) != 0UL)) {
			if (!is_pae(vcpu)) {
				ret = false;
			} else {
				cs_attr = exec_vmread32(VMX_GUEST_CS_ATTR);
				tr_attr = exec_vmread32(VMX_GUEST_TR_ATTR);
				/*
				 * SDM Table 3-2 System-Segment and Gate-Descriptor Types
				 * In 32-bit Mode:
				 * Type 1-16-bit TSS (Available)
				 * Type 3-16-bit TSS (Busy)
				 *
				 * SDM 25.4.1 Guest Register State - Table 25-2 Format of Access Rights
				 * Bit[3:0] Segment type
				 * Bit[13] L-bit 64-bit mode active (for CS only)
				 */
				if (((cs_attr & 0x2000U) != 0U) || ((tr_attr & 0xFU) == 0x1U)
					 || ((tr_attr & 0xFU) == 0x3U)) {
					/* Current CR0.PG = 0, and CS.L = 1 or TR contains a 16-bit TSS */
					ret = false;
				}
			}
		} else {
			if (((cr0 & CR0_PE) == 0UL) && ((cr0 & CR0_PG) != 0UL)) {
				ret = false;
			} else {
				/* SDM 6.15 "Exception and Interrupt Reference" GP Exception
				 *
				 * Loading CR0 register with a set NW flag and a clear CD flag
				 * is invalid
				 */
				if (((cr0 & CR0_CD) == 0UL) && ((cr0 & CR0_NW) != 0UL)) {
					ret = false;
				}

				if ((cr0 & CR0_PG) == 0UL) {
					if (vcpu->arch.cpu_mode == CPU_MODE_64BIT) {
						/* SDM Vol.2 "MOV-Move to/from Control Registers"
						 *
						 * In 64-Bit mode, clearing CR0.PG causes #GP(0).
						 */
						ret = false;
					} else if ((vcpu_get_cr4(vcpu) & CR4_PCIDE) != 0UL) {
						/* SDM 4.10.1 "Process-Context Identifiers"
						 *
						 * MOV to CR0 causes a general-protection exception if it would
						 * clear CR0.PG to 0 while CR4.PCIDE = 1 and in
						 * compatibility mode.
						 */
						ret = false;
					} else {
						/* CR0.PG is allowed to be 0 in compatibility/protected
						 * mode with CR4.PCIDE = 0, and real-address mode.
						 */
					}
				}
			}
		}
	}

	return ret;
}

/**
 * @brief Emulate write to guest CR0
 *
 * Guest CR0 bits shall be treated as below:
 *   - PE(0): Trapped to track cpu mode.
 *   - MP(1), EM(2), TS(3), ET(4), AM(18): Flexible bits to guest.
 *   - CD(3), NW(29): Trapped to emulate cache disable situation.
 *   - NE(5): Fixed-1 bit, always set by HV.
 *   - WP(16): Trapped to get if it inhibits supervisor level procedures to write into ro-pages.
 *   - PG(31): Trapped to track cpu/paging mode.
 *   - Other bits are reserved to guest.
 *
 * This function emulates guest CR0 write in below steps:
 * - Check whether the target CR0 value is valid, if not, inject \#GP to guest.
 * - Otherwise check paging mode as below:
 *   - If CR0.PG == 1 and MSR EFER.LME_BIT == 1, enable long-mode paging.
 *   - If CR0.PG == 1 and MSR EFER.LME_BIT == 0, inject \#GP to guest as PAE paging mode is not supported.
 *   - If CR0.PG == 0 and MSR EFER.LME_BIT == 1, disable long-mode paging.
 *
 * - Try to reload guest PAT if CR0.CD is changed. If CR0.CD == 1, set guest PAT VMCS field with all UC entries,
 *   otherwise set PAT field with guest value.
 * - If guest PG/WP/CD is to be changed, issue vCPU request to flush EPT cache.
 * - Re-calculate and set values for guest CR0 and shadow CR0 in VMCS.
 * - Finally clear CPU_REG_CR0 bit in vcpu->reg_cached.
 *
 * @param[inout] vcpu Target vCPU to write the CR0 value
 * @param[in] value Target CR0 value
 *
 * @return None
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static void vmx_write_cr0(struct acrn_vcpu *vcpu, uint64_t value)
{
	bool err_found = false;
	/*
	 * For reserved bits of CR0, SDM states:
	 * attempts to set them have no impact, while set to high 32 bits lead to #GP.
	 */

	if (!is_cr0_write_valid(vcpu, value)) {
		pr_err("Invalid cr0 write operation from guest");
		vcpu_inject_gp(vcpu, 0U);
	} else {
		uint64_t effective_cr0 = (value & ~cr0_reserved_bits_mask);
		uint64_t mask, tmp;
		uint32_t entry_ctrls;
		uint64_t cr0_changed_bits = vcpu_get_cr0(vcpu) ^ effective_cr0;

		if ((cr0_changed_bits & CR0_PG) != 0UL) {
			/* PG bit changes */
			if ((effective_cr0 & CR0_PG) != 0UL) {
				/* Enable paging */
				if ((vcpu_get_efer(vcpu) & MSR_IA32_EFER_LME_BIT) != 0UL) {
					/* Enable long mode */
					pr_dbg("VMM: Enable long mode");
					entry_ctrls = exec_vmread32(VMX_ENTRY_CONTROLS);
					entry_ctrls |= VMX_ENTRY_CTLS_IA32E_MODE;
					exec_vmwrite32(VMX_ENTRY_CONTROLS, entry_ctrls);

					vcpu_set_efer(vcpu, vcpu_get_efer(vcpu) | MSR_IA32_EFER_LMA_BIT);
				} else {
					pr_dbg("VMM: NOT Enable long mode");
					if (is_pae(vcpu)) {
						pr_err("%s: fail to set CR0.PG, PAE paging mode is not supported",__func__);
						err_found = true;
						vcpu_inject_gp(vcpu, 0U);
					}
				}
			} else  {
				/* Disable paging */
				pr_dbg("disable paginge");
				if ((vcpu_get_efer(vcpu) & MSR_IA32_EFER_LME_BIT) != 0UL) {
					/* Disable long mode */
					pr_dbg("VMM: Disable long mode");
					entry_ctrls = exec_vmread32(VMX_ENTRY_CONTROLS);
					entry_ctrls &= ~VMX_ENTRY_CTLS_IA32E_MODE;
					exec_vmwrite32(VMX_ENTRY_CONTROLS, entry_ctrls);

					vcpu_set_efer(vcpu, vcpu_get_efer(vcpu) & ~MSR_IA32_EFER_LMA_BIT);
				}
			}
		}

		if (!err_found) {
			/* If CR0.CD or CR0.NW get cr0_changed_bits */
			if ((cr0_changed_bits & CR0_TRAP_AND_EMULATE_BITS) != 0UL) {
				/* No action if only CR0.NW is changed */
				if ((cr0_changed_bits & CR0_CD) != 0UL) {
					if ((effective_cr0 & CR0_CD) != 0UL) {
						/*
						 * When the guest requests to set CR0.CD, we don't allow
						 * guest's CR0.CD to be actually set, instead, we write guest
						 * IA32_PAT with all-UC entries to emulate the cache
						 * disabled behavior
						 */
						exec_vmwrite64(VMX_GUEST_IA32_PAT_FULL, PAT_ALL_UC_VALUE);
					} else {
						/* Restore IA32_PAT to enable cache again */
						exec_vmwrite64(VMX_GUEST_IA32_PAT_FULL,
							vcpu_get_guest_msr(vcpu, MSR_IA32_PAT));
					}
				}
			}

			if ((cr0_changed_bits & (CR0_PG | CR0_WP | CR0_CD)) != 0UL) {
				vcpu_make_request(vcpu, ACRN_REQUEST_EPT_FLUSH);
			}

			mask = cr0_trap_and_passthru_mask | cr0_passthru_mask;
			tmp = (initial_guest_cr0 & ~mask) | (effective_cr0 & mask);

			exec_vmwrite(VMX_GUEST_CR0, tmp);
			exec_vmwrite(VMX_CR0_READ_SHADOW, effective_cr0);

			/* clear read cache, next time read should from VMCS */
			bitmap_clear_nolock(CPU_REG_CR0, &vcpu->reg_cached);

			pr_dbg("VMM: Try to write %016lx, allow to write 0x%016lx to CR0", effective_cr0, tmp);
		}
	}
}

/**
 * @brief Check whether target CR4 value is valid
 *
 * This function checks whether there's any CR4 reserved bit is set for target VM. If no reserved bit is set,
 * it's a valid value, otherwise invalid value.
 *
 * @param[in] vm Target VM to check CR4
 * @param[in] cr4 Target CR4 value to check
 *
 * @return true if target CR4 value is valid, false otherwise.
 *
 * @pre vm != NULL
 *
 * @post N/A
 */
static inline bool is_valid_cr4(struct acrn_vm *vm, uint64_t cr4)
{
	return (cr4 & vm->cfg.cr4_reserved_bits_mask) == 0UL;
}

/**
 * @brief Check whether both CR0 and CR4 values are valid
 *
 * This function checks whether any the reserved bit of CR0/CR4 register is set, if not, valid
 * register value, otherwise, invalid value.
 *
 * Normal case behaviors:
 * - Return a boolean value to indicate whether both register values are valid.
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[in] vm Target VM to check CR0/CR4
 * @param[in] cr0 Target CR0 value to check
 * @param[in] cr4 Target CR4 value to check
 *
 * @return true if both register values are valid, false if either of the values is invalid.
 *
 * @pre N/A
 *
 * @post N/A
 */
bool is_valid_cr0_cr4(struct acrn_vm *vm, uint64_t cr0, uint64_t cr4)
{
	return is_valid_cr4(vm, cr4) & is_valid_cr0(cr0);
}

/**
 * @brief Check whether guest CR4 write value is valid
 *
 * This function checks below invalid cases:
 * - Any guest CR4 reserved bit is set.
 * - EFER.MSR_IA32_EFER_LMA_BIT is set but CR4.PAE is not.
 * - CR4.MCE is set but Machine-Check is not passed through to guest.
 *
 * If these invalid cases are not detected, the target write value is valid, otherwise invalid.
 *
 * @param[in] vcpu Target vCPU to check CR4 value
 * @param[in] cr4 Target CR4 value to check
 *
 * @return true if the CR4 value is valid, false otherwise.
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static bool is_cr4_write_valid(struct acrn_vcpu *vcpu, uint64_t cr4)
{
	bool ret = true;

	if (!is_valid_cr4(vcpu->vm, cr4) || (is_long_mode(vcpu) && ((cr4 & CR4_PAE) == 0UL))) {
		ret = false;
	}

	if (!is_mc_pt_enabled(vcpu) && ((cr4 & CR4_MCE) != 0UL)) {
		ret = false;
	}

	return ret;
}

/**
 * @brief Emulate write to guest CR4
 *
 * Guest CR4 bits shall be treated as below:
 *   - VME(0), PVI(1), TSD(2), DE(3), PGE(7), PCE(8), OSFXSR(9), OSXMMEXCPT(10), UMIP(11),
 *     FSGSBASE(16), PCIDE(17), OSXSAVE(18), LASS(27), LAM_SUP(28): Flexible bits to guest.
 *   - PSE(4), SMEP(20), SMAP(21): Trapped to flush EPT cache.
 *   - PAE(5): Trapped to enable guest PAE paging and flush EPT cache.
 *   - MCE(6): Always set for guest.
 *   - VMXE(13): Fixed-1 bit, always set by HV.
 *   - KL(19): Trapped to flush EPT cache and load IWKey for vCPU if set.
 *   - Other bits are reserved to guest.
 *
 * This function first checks if the target CR4 value is valid, if not, inject \#GP to guest. Otherwise
 * it performs below checks:
 * - If guest PSE/PAE/SMEP/SMAP/KL is to be changed:
 *   - If guest PAE paging is enabled, inject \#GP to guest as it is not supported.
 *   - Send vCPU request to flush EPT cache.
 * - If CR4.PCIDE == 1, check whether guest MSR EFER.LMA and CR3[11:0] are properly set, if not, inject \#GP. Refer to
 *   4.10.1 Process-Context Identifiers (PCIDs), SDM Vol.3 325462-078US for details.
 *
 * If it passes above checks, then the function performs below steps:
 * - If CR4.KL == 1, set arch.cr4_kl_enabled to true and load Key-Lock IWKEY, otherwise set the flag to false.
 * - Re-calculate and set guest CR4 and shadow CR4 in VMCS.
 * - Clear CPU_REG_CR4 bit flag in vcpu->reg_cached.
 *
 * @param[inout] vcpu Target vCPU to write the CR4 value
 * @param[in] cr4 Target CR4 value
 *
 * @return None
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static void vmx_write_cr4(struct acrn_vcpu *vcpu, uint64_t cr4)
{
	bool err_found = false;

	if (!is_cr4_write_valid(vcpu, cr4)) {
		pr_err("Invalid cr4 write operation from guest");
		vcpu_inject_gp(vcpu, 0U);
	} else {
		uint64_t mask, tmp;
		uint64_t cr4_changed_bits = vcpu_get_cr4(vcpu) ^ cr4;

		if ((cr4_changed_bits & CR4_TRAP_AND_PASSTHRU_BITS) != 0UL) {
			if (((cr4 & CR4_PAE) != 0UL) && (is_paging_enabled(vcpu)) && (!is_long_mode(vcpu))) {
				err_found = true;
				pr_err("%s: fail to set CR4.PAE, PAE paging mode is not supported",__func__);
				vcpu_inject_gp(vcpu, 0U);
			}
			vcpu_make_request(vcpu, ACRN_REQUEST_EPT_FLUSH);
		}

		if (!err_found && ((cr4_changed_bits & CR4_PCIDE) != 0UL)) {
			/* MOV to CR4 causes a general-protection exception (#GP) if it would change
			 * CR4.PCIDE from 0 to 1 and either IA32_EFER.LMA = 0 or CR3[11:0] != 000H
			 */
			if ((cr4 & CR4_PCIDE) != 0UL) {
				uint64_t guest_cr3 = exec_vmread(VMX_GUEST_CR3);

				if ((!is_long_mode(vcpu)) || ((guest_cr3 & 0xFFFUL) != 0UL)) {
					pr_dbg("Failed to enable CR4.PCID, cr4:0x%lx, cr4_changed_bits:0x%lx,vcpu_cr4:0x%lx cr3:0x%lx",
						cr4, cr4_changed_bits, vcpu_get_cr4(vcpu), guest_cr3);

					err_found = true;
					vcpu_inject_gp(vcpu, 0U);
				}
			}
		}

		if (!err_found && ((cr4_changed_bits & CR4_KL) != 0UL)) {
			if ((cr4 & CR4_KL) != 0UL) {
				vcpu->arch.cr4_kl_enabled = true;
				load_iwkey(vcpu);
			} else {
				vcpu->arch.cr4_kl_enabled = false;
			}
		}

		if (!err_found) {
			/*
			 * Update the passthru bits.
			 */
			mask = cr4_trap_and_passthru_mask | vcpu->vm->cfg.cr4_passthru_mask;
			/*
			 * CR4 fixed bits can't be changed. Otherwise, VM-entry will fail due to invalid guest state.
			 */
			tmp = (initial_guest_cr4 & ~mask) | (cr4 & mask);

			/*
			 * For all reserved bits (including CR4_EMULATED_RESERVE_BITS), we came here because
			 * the guest is not changing them.
			 */
			exec_vmwrite(VMX_GUEST_CR4, tmp);
			exec_vmwrite(VMX_CR4_READ_SHADOW, cr4);

			/* clear read cache, next time read should from VMCS */
			bitmap_clear_nolock(CPU_REG_CR4, &vcpu->reg_cached);

			pr_dbg("VMM: Try to write %016lx, allow to write 0x%016lx to CR4", cr4, tmp);
		}
	}
}

/**
 * @brief Initialize CR0 and CR4 related guest masks
 *
 * This function uses CR0/CR4 fixed-1 bits, flexible bits and pre-defined macros to initialize global CR0/CR4
 * passthrough and trap_and_passthrough masks, initial guest CR0/CR4 values and CR0/CR4 reserved bit masks.
 * passthrough and trap_and_passtrough masks are filtered by flexible bit mask. Initial guest CR0/CR4
 * values include the fixed-1 bits and HV managed bit, but exclude emulated reserved feature bits.
 * Reserved bit masks include all bits not in passthrough, trap_and_passthrough and trap_and_emulated masks.
 *
 * Normal case behaviors:
 * - Set the initial values for guest masks.
 *
 * Error case behaviors:
 * - N/A
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
void init_cr0_cr4_flexible_bits(void)
{
	uint64_t cr0_flexible_bits, cr0_fixed1_bits;
	uint64_t cr4_flexible_bits, cr4_fixed1_bits;
	uint64_t fixed0, fixed1;

	/* make sure following MACROs don't have any overlapped set bit.
	 */
	ASSERT(((CR0_PASSTHRU_BITS ^ CR0_TRAP_AND_PASSTHRU_BITS) ^ CR0_TRAP_AND_EMULATE_BITS) ==
			(CR0_PASSTHRU_BITS | CR0_TRAP_AND_PASSTHRU_BITS | CR0_TRAP_AND_EMULATE_BITS));

	ASSERT(((CR4_PASSTHRU_BITS ^ CR4_TRAP_AND_PASSTHRU_BITS) ^ CR4_TRAP_AND_EMULATE_BITS) ==
			(CR4_PASSTHRU_BITS | CR4_TRAP_AND_PASSTHRU_BITS | CR4_TRAP_AND_EMULATE_BITS));

	/* Read the CR0 fixed0 / fixed1 MSR registers */
	fixed0 = msr_read(MSR_IA32_VMX_CR0_FIXED0);
	fixed1 = msr_read(MSR_IA32_VMX_CR0_FIXED1);

	pr_dbg("%s:cr0 fixed0 = 0x%016lx, fixed1 = 0x%016lx\n", __func__, fixed0, fixed1);
	cr0_flexible_bits = (fixed0 ^ fixed1);
	/*
	 * HW reports fixed bits for CR0_PG & CR0_PE, but do not check the violation.
	 * ACRN needs to set them for (unrestricted) guest, and therefore view them as
	 * flexible bits.
	 */
	cr0_flexible_bits |= (CR0_PE | CR0_PG);
	cr0_fixed1_bits = (fixed0 & ~cr0_flexible_bits);

	/*
	 * We only pass-through flexible feature bits to guest and the fixed-1 feature bits are managed
	 * by hypervisor. The fixed-1 bits are not reserved bits, they should not be set in the
	 * cr0_reserved_bits_mask.
	 */
	cr0_reserved_bits_mask = ~(cr0_passthru_mask | cr0_trap_and_passthru_mask | CR0_TRAP_AND_EMULATE_BITS);
	cr0_passthru_mask &= cr0_flexible_bits;
	cr0_trap_and_passthru_mask &= cr0_flexible_bits;

	initial_guest_cr0 = (cr0_fixed1_bits & ~CR0_EMULATED_RESERVE_BITS) | CR0_EMRSV_BITS_PHYS_VALUE;

	pr_dbg("cr0_flexible_bits:0x%lx, cr0_passthru_mask:0x%lx, cr0_trap_and_passthru_mask:0x%lx.\n",
		cr0_flexible_bits, cr0_passthru_mask, cr0_trap_and_passthru_mask);
	pr_dbg("cr0_reserved_bits_mask:0x%lx, initial_guest_cr0:0x%lx.\n",
		cr0_reserved_bits_mask, initial_guest_cr0);

	/* Read the CR4 fixed0 / fixed1 MSR registers */
	fixed0 = msr_read(MSR_IA32_VMX_CR4_FIXED0);
	fixed1 = msr_read(MSR_IA32_VMX_CR4_FIXED1);

	pr_dbg("%s:cr4 fixed0 = 0x%016lx, fixed1 = 0x%016lx\n", __func__, fixed0, fixed1);
	cr4_flexible_bits = (fixed0 ^ fixed1);
	cr4_passthru_mask &= cr4_flexible_bits;
	cr4_trap_and_passthru_mask &= cr4_flexible_bits;

	/*
	 * vcpuid should always consult cr4_reserved_bits_mask when reporting capability.
	 *
	 * The guest value of reserved bits are likely identical to fixed bits, but certain
	 * exceptions may be applied, i.e. for CR4_EMULATED_RESERVE_BITS.
	 */
	cr4_reserved_bits_mask = ~(cr4_passthru_mask | cr4_trap_and_passthru_mask | CR4_TRAP_AND_EMULATE_BITS);

	cr4_fixed1_bits = (fixed0 & ~cr4_flexible_bits);

	initial_guest_cr4 = (cr4_fixed1_bits & ~CR4_EMULATED_RESERVE_BITS) | CR4_EMRSV_BITS_PHYS_VALUE \
						| CR4_EM_BITS_INITIAL_PHYS_VALUE;

	pr_dbg("cr4_flexible_bits:0x%lx, cr4_passthru_mask:0x%lx, cr4_trap_and_passthru_mask:0x%lx.\n",
		cr4_flexible_bits, cr4_passthru_mask, cr4_trap_and_passthru_mask);
	pr_dbg("cr4_reserved_bits_mask:0x%lx, initial_guest_cr4:0x%lx.\n", cr4_reserved_bits_mask, initial_guest_cr4);
}

/**
 * @brief Fix up VM CR0/CR4 passthrough and reserved bit masks
 *
 * This function is used to fix up VM cr0/cr4 reserved bit mask and passthrough bit mask according to VM feature
 * configuration. If PMU is not passed through to VM, add CR4.PCE bit as VM CR4 reserved bit mask and remove it
 * from VM CR4 pass-through bit mask. Otherwise reuse the initialized guest CR0/CR4 masks for target VM.
 *
 * Normal case behaviors:
 * - Fix up VM CR0/CR4 reserved bit mask and passthrough bit mask.
 *
 * Error case behaviors:
 * - N/A
 *
 * @return None
 *
 * @pre vm != NULL
 *
 * @post N/A
 */
void fixup_cr0_cr4_guest_mask(struct acrn_vm *vm)
{
	if (is_pmu_pt_configured(vm)) {
		vm->cfg.cr4_reserved_bits_mask = cr4_reserved_bits_mask;
		vm->cfg.cr4_passthru_mask = cr4_passthru_mask;
	} else {
		vm->cfg.cr4_reserved_bits_mask = cr4_reserved_bits_mask | CR4_PCE;
		vm->cfg.cr4_passthru_mask = cr4_passthru_mask & ~CR4_PCE;
	}
}

/**
 * @brief Set CR0/CR4 guest/host masks
 *
 * This function uses cr0_passthru_mask and cr4_passthru_mask for a VM to set CR0/CR4 guest_host masks in
 * VMCS fields. Refer to 25.6.6 Guest/Host Masks and Read Shadows for CR0 and CR4, SDM Vol.3 325462-078US
 * for details.
 *
 * Normal case behaviors:
 * - Set the initial values for VMCS fields.
 *
 * Error case behaviors:
 * - N/A
 *
 * @return None
 *
 * @pre vm != NULL
 *
 * @post N/A
 */
void init_cr0_cr4_host_guest_mask(struct acrn_vm *vm)
{
	/*
	 * "1" means the bit is trapped by host, and "0" means passthru to guest..
	 */
	exec_vmwrite(VMX_CR0_GUEST_HOST_MASK, ~cr0_passthru_mask); /* all bits except passthrubits are trapped */
	pr_dbg("CR0 guest-host mask value: 0x%016lx", ~cr0_passthru_mask);

	/* VM may reserve some bits access due to VM configuration. */
	exec_vmwrite(VMX_CR4_GUEST_HOST_MASK, ~vm->cfg.cr4_passthru_mask); /* all bits except passthru bits are trapped */
	pr_dbg("CR4 guest-host mask value: 0x%016lx", ~vm->cfg.cr4_passthru_mask);
}

/**
 * @brief Get vCPU CR0
 *
 * This function returns vCPU CR0 value. VM-Exit doesn't cache vCPU CR0, this function needs to read
 * related VMCS fields and synthesize a vCPU CR0 value, then set CR0 bit in vcpu->reg_cached so that
 * following read to vCPU CR0 can return the cached data directly.
 *
 * Normal case behaviors:
 * - Read VMCS fields and synthesize a CR0 value or get the value from memory cache.
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[inout] vcpu Target vCPU to get the CR0 value
 *
 * @return Target vCPU CR0 value
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
uint64_t vcpu_get_cr0(struct acrn_vcpu *vcpu)
{
	struct run_context *ctx = &vcpu->arch.contexts.run_ctx;

	if (bitmap_test_and_set_nolock(CPU_REG_CR0, &vcpu->reg_cached) == 0) {
		ctx->cr0 = (exec_vmread(VMX_CR0_READ_SHADOW) & ~cr0_passthru_mask) |
			(exec_vmread(VMX_GUEST_CR0) & cr0_passthru_mask);
	}
	return ctx->cr0;
}

/**
 * @brief Set vCPU CR0
 *
 * This function writes CR0 value to corresponding VMCS field. It's called in below cases:
 * - The cached CR0 is updated before VM-Entry.
 * - Guest CR0 write is trapped and HV tries to update VMCS with guest value.
 * - HV gets ACRN_REQUEST_INIT_VMCS request to initialize guest VMCS fields.
 *
 * Normal case behaviors:
 * - Update CR0 VMCS field with target value
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[inout] vcpu Target vCPU to set the CR0 value
 * @param[in] val The CR0 value to set
 *
 * @return None
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
void vcpu_set_cr0(struct acrn_vcpu *vcpu, uint64_t val)
{
	pr_dbg("%s, value: 0x%016lx rip: %016lx", __func__, val, vcpu_get_rip(vcpu));
	vmx_write_cr0(vcpu, val);
}

/**
 * @brief Get vCPU CR2
 *
 * This function returns cached vCPU CR2 value from vCPU run-context.
 *
 * Normal case behaviors:
 * - Return cached CR2 value
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[in] vcpu Target vCPU to get the CR2 value
 *
 * @return Cached CR2 value
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
uint64_t vcpu_get_cr2(const struct acrn_vcpu *vcpu)
{
	return vcpu->arch.contexts.run_ctx.cr2;
}

/**
 * @brief Set vCPU CR2
 *
 * This function sets CR2 value to vCPU run-context.
 *
 * Normal case behaviors:
 * - Set CR2 value to vCPU run-context
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[inout] vcpu Target vCPU to set the CR2 value
 * @param[in] val Target CR2 value to set
 *
 * @return None
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
void vcpu_set_cr2(struct acrn_vcpu *vcpu, uint64_t val)
{
	vcpu->arch.contexts.run_ctx.cr2 = val;
}

/**
 * @brief Get vCPU CR4
 *
 * This function returns vCPU CR4 value. VM-Exit doesn't cache vCPU CR4, this function needs to read
 * related VMCS fields and synthesize a vCPU CR4 value, then set CR4 bit in vcpu->reg_cached so that
 * following read to vCPU CR4 can return the cached data directly.
 *
 * Normal case behaviors:
 * - Read VMCS fields and synthesize a CR4 value or get the value from memory cache directly.
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[inout] vcpu Target vCPU to get the CR4 value
 *
 * @return Target vCPU CR4 value
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
uint64_t vcpu_get_cr4(struct acrn_vcpu *vcpu)
{
	struct run_context *ctx = &vcpu->arch.contexts.run_ctx;
	uint64_t pt_mask = cr4_trap_and_passthru_mask | vcpu->vm->cfg.cr4_passthru_mask;

	if (bitmap_test_and_set_nolock(CPU_REG_CR4, &vcpu->reg_cached) == 0) {
		ctx->cr4 = (exec_vmread(VMX_CR4_READ_SHADOW) & ~pt_mask) | (exec_vmread(VMX_GUEST_CR4) & pt_mask);
	}
	return ctx->cr4;
}

/**
 * @brief Set vCPU CR4
 *
 * This function writes CR4 value to corresponding VMCS field. It's called in below cases:
 * - The cached CR4 is updated before VM-Entry.
 * - Guest CR4 write is trapped and HV tries to update VMCS with guest value.
 * - HV gets ACRN_REQUEST_INIT_VMCS request to initialize guest VMCS fields.
 *
 * Normal case behaviors:
 * - Update CR4 VMCS field with target value
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[inout] vcpu Target vCPU to set the CR4 value
 * @param[in] val The CR4 value to set
 *
 * @return None
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
void vcpu_set_cr4(struct acrn_vcpu *vcpu, uint64_t val)
{
	pr_dbg("%s, value: 0x%016lx rip: %016lx", __func__, val, vcpu_get_rip(vcpu));
	vmx_write_cr4(vcpu, val);
}

/**
 * @brief Handler for control register access induced VM-Exit
 *
 * This function handles VM-Exit induced by guest access to control registers. It parses target control register
 * index and value from VM-Exit qualification and set the value to target control register. CR0 and CR4 are the
 * only valid target control registers for this handler, other access targets are invalid.
 * Refer to 26.1.3 Instructions That Cause VM Exits Conditionally, SDM Vol.3 325462-078US for details.
 *
 * Normal case behaviors:
 * - Emulate guest CR0/CR4 write.
 *
 * Error case behaviors:
 * - Return an error for invalid control register access.
 *
 * @param[inout] vcpu Target vCPU to handle the VM-Exit
 *
 * @return A integer value to indicate the handling result
 *
 * @retval -EINVAL Access to an invalid control register.
 * @retval 0 Write to guest CR0/CR4 is handled successfully.
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
int32_t cr_access_vmexit_handler(struct acrn_vcpu *vcpu)
{
	uint64_t reg;
	uint32_t idx;
	uint64_t exit_qual;
	int32_t ret = 0;

	exit_qual = vcpu->arch.exit_qualification;
	idx = (uint32_t)vm_exit_cr_access_reg_idx(exit_qual);

	ASSERT((idx <= 15U), "index out of range");
	reg = vcpu_get_gpreg(vcpu, idx);

	switch ((vm_exit_cr_access_type(exit_qual) << 4U) | vm_exit_cr_access_cr_num(exit_qual)) {
	case 0x00UL:
		/* mov to cr0 */
		vcpu_set_cr0(vcpu, reg);
		break;

	case 0x04UL:
		/* mov to cr4 */
		vcpu_set_cr4(vcpu, reg);
		break;
	default:
		ASSERT(false, "Unhandled CR access");
		ret = -EINVAL;
		break;
	}

	TRACE_2L(TRACE_VMEXIT_CR_ACCESS, vm_exit_cr_access_type(exit_qual),
			vm_exit_cr_access_cr_num(exit_qual));

	return ret;
}

/**
 * @brief Get VM CR4 reserved bit mask
 *
 * Normal case behaviors:
 * - Return a uint64_t value containing VM CR4 reserved bit mask.
 *
 * Error case behaviors:
 * - N/A
 *
 * @return VM CR4 reserved bit mask
 *
 * @pre vm != NULL
 *
 * @post N/A
 */
uint64_t get_cr4_reserved_bits(struct acrn_vm *vm)
{
	return vm->cfg.cr4_reserved_bits_mask;
}

/**
 * @}
 */
