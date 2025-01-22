/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <errno.h>
#include <asm/lib/bits.h>
#include <asm/guest/vcpu.h>
#include <asm/guest/vm.h>
#include <asm/cpuid.h>
#include <asm/cpufeatures.h>
#include <asm/vmx.h>
#include <asm/sgx.h>
#include <asm/tsc.h>
#include <logmsg.h>
#include <asm/rdt.h>

/**
 * @defgroup vp-base_vcpuid vp-base.vcpuid
 *
 * @ingroup vp-base
 *
 * @brief Implement virtual CPUID(vCPUID) related interfaces and data structures
 *
 * This module provides interfaces to configure, initialize guest vCPUIDs, also includes functions to access
 * the vCPUIDs.
 *
 * @{
 */

/**
 * @file
 * @brief Define VM vCPUIDs and external vCPUID APIs
 *
 * This file implements virtual CPUID related functions, including functions to initialize vCPUIDs,
 * query vCPUID leaves and set/get vCPUIDs leaves etc.
 */

/**
 * @brief Data structure to hold per-CPU vCPUIDs
 *
 * Some CPUID leaves have different values across different logical CPUs within the system, these vCPUIDs include
 * CPUID_FEATURES, CPUID_EXTEND_TOPOLOGY, CPUID_XSAVE_FEATURES, CPUID_KEY_LOCKER, CPUID_V2_EXTEND_TOPOLOGY,
 * CPUID_EXTEND_FUNCTION_1, they are represented by this data structure.
 *
 * @consistency N/A
 * @alignment N/A
 */
struct percpu_cpuids {
	uint32_t leaf_nr; /**< Number of leaves in leaves array */
	uint32_t leaves[MAX_VM_VCPUID_ENTRIES]; /**< CPUID leaf IDs */
};

/**
 * @brief Variable to store per-CPU vCPUIDs
 *
 * This variable contains guest vCPUIDs that can vary across vCPUs.
 */
static struct percpu_cpuids pcpu_cpuids;

/**
 * @brief Search vCPUID entries for the CPUID leaf and subleaf
 *
 * This function iterates VM cached vCPUID array to find the entry identified by leaf and subleaf.
 * Note, the function's search algorithm assumes that VM's vcpuid entries in vcpuid_entries array have
 * been sorted with leaf value ascending order which is guaranteed by set_vcpuid_entries().
 *
 * @param[in] vcpu Target vCPU to find the vCPUID leaf
 * @param[in] leaf vCPUID leaf to search for
 * @param[in] subleaf vCPUID subleaf to search for
 *
 * @return Target vCPUID entry if found, otherwise NULL.
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static inline const struct vcpuid_entry *local_find_vcpuid_entry(const struct acrn_vcpu *vcpu,
					uint32_t leaf, uint32_t subleaf)
{
	uint32_t i = 0U, nr, half;
	const struct vcpuid_entry *found_entry = NULL;
	struct acrn_vm *vm = vcpu->vm;

	nr = vm->vcpuid_entry_nr;
	half = nr >> 1U;
	if (vm->vcpuid_entries[half].leaf < leaf) {
		i = half;
	}

	for (; i < nr; i++) {
		const struct vcpuid_entry *tmp = (const struct vcpuid_entry *)(&vm->vcpuid_entries[i]);

		if (tmp->leaf < leaf) {
			continue;
		} else if (tmp->leaf == leaf) {
			if (((tmp->flags & CPUID_CHECK_SUBLEAF) != 0U) && (tmp->subleaf != subleaf)) {
				continue;
			}
			found_entry = tmp;
			break;
		} else {
			/* tmp->leaf > leaf */
			break;
		}
	}

	return found_entry;
}

/**
 * @brief Find specific vCPUID entry with CPUID leaf and subleaf
 *
 * This is used to find specific vCPUID through local_find_vcpuid_entry(). If the target vCPUID
 * entry is not found, handle the returned vCPUID values as stated in SDM. Refer to CPUID—CPU
 * Identification Vol.2, SDM 325462-078US for details.
 *
 * @param[in] vcpu Target vCPU to find the vCPUID entry
 * @param[in] leaf_arg Target vCPUID leaf to search for
 * @param[in] subleaf Target vCPUID subleaf to search for
 *
 * @return The vCPUID entry identified by {leaf, subleaf} if the entry is found, or an entry stuffed with
 * highest leaf basic information if input leaf is higher than the supported maximum input value, or NULL
 * if the leaf is lower than the supported maximum input value but the entry referenced by {leaf, subleaf}
 * does not exist.
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static inline const struct vcpuid_entry *find_vcpuid_entry(const struct acrn_vcpu *vcpu,
					uint32_t leaf_arg, uint32_t subleaf)
{
	const struct vcpuid_entry *entry;
	uint32_t leaf = leaf_arg;

	entry = local_find_vcpuid_entry(vcpu, leaf, subleaf);
	if (entry == NULL) {
		uint32_t limit;
		struct acrn_vm *vm = vcpu->vm;

		if ((leaf & 0x80000000U) != 0U) {
			limit = vm->vcpuid_xlevel;
		} else {
			limit = vm->vcpuid_level;
		}

		if (leaf > limit) {
			/* Intel documentation states that invalid EAX input
			 * will return the same information as EAX=cpuid_level
			 * (Intel SDM Vol. 2A - Instruction Set Reference -
			 * CPUID)
			 */
			leaf = vm->vcpuid_level;
			entry = local_find_vcpuid_entry(vcpu, leaf, subleaf);
		}

	}

	return entry;
}

/**
 * @brief Set a vCPUID entry into vCPUID array
 *
 * This function copies vCPUID entry data to per VM cached vCPUID array and increases the cpuid entry total
 * number by 1. If vCPUID entry number exceeds MAX_VM_VCPUID_ENTRIES, print error message.
 *
 * @param[inout] vm Target VM to set the vCPUID entry
 * @param[in] entry Source vCPUID node holding the vCPUID entry data
 *
 * @return A integer value indicating the operation result
 *
 * @retval -ENOMEM vCPUID entry number overflows.
 * @retval 0 Set vCPUID entry successfully.
 *
 * @pre vm != NULL && entry != NULL
 *
 * @post N/A
 */
static inline int32_t set_vcpuid_entry(struct acrn_vm *vm,
				const struct vcpuid_entry *entry)
{
	struct vcpuid_entry *tmp;
	size_t entry_size = sizeof(struct vcpuid_entry);
	int32_t ret;

	if (vm->vcpuid_entry_nr == MAX_VM_VCPUID_ENTRIES) {
		pr_err("%s, vcpuid entry over MAX_VM_VCPUID_ENTRIES(%u)\n", __func__, MAX_VM_VCPUID_ENTRIES);
		ret = -ENOMEM;
	} else {
		tmp = &vm->vcpuid_entries[vm->vcpuid_entry_nr];
		vm->vcpuid_entry_nr++;
		(void)memcpy_s(tmp, entry_size, entry, entry_size);
		ret = 0;
	}
	return ret;
}

/**
 * @brief Initialize one vCPUID entry
 *
 * This function sets the initial values for the vCPUID identified by leaf, subleaf and flags. For HV emulated
 * virtual CPUID entries, the initial data is set by HV. For vCPUID entries supported by HW, the initial data
 * is retrieved by executing CPUID instruction with leaf/subleaf.
 * Refer to Section 6.3.3 CPUID Leaves of Software Architecture Design Specification for the detailed design that
 * specifies the expected behavior of initializing virtual CPUIDs.
 *
 * - HV emulated vCPUIDs:
 *   - Leaf 0x40000000, 0x40000001, 0x40000010.
 *   - Leaf 0x16: If the leaf is supported by HW, initialize the entry with native values,
 *   otherwise emulate the values.
 * - Other vCPUIDs:
 *   - Initialized the entries with native values identified with leaf/subleaf.
 *
 * @param[in] leaf vCPUID leaf to identify the entry
 * @param[in] subleaf vCPUID subleaf to identify the entry
 * @param[in] flags Whether to check subleaf
 * @param[inout] entry Target vCPUID entry to initialize
 *
 * @return None
 *
 * @pre entry != NULL
 *
 * @post N/A
 */
static void init_vcpuid_entry(uint32_t leaf, uint32_t subleaf,
			uint32_t flags, struct vcpuid_entry *entry)
{
	struct cpuinfo_x86 *cpu_info;

	entry->leaf = leaf;
	entry->subleaf = subleaf;
	entry->flags = flags;

	switch (leaf) {
	case CPUID_FREQ:
		cpu_info = get_pcpu_info();
		if (cpu_info->cpuid_level >= CPUID_FREQ) {
			/* call the cpuid when 0x16 is supported */
			cpuid_subleaf(leaf, subleaf, &entry->eax, &entry->ebx, &entry->ecx, &entry->edx);
		} else {
			/* Use the tsc to derive the emulated 0x16U cpuid. */
			entry->eax = (uint32_t) (get_tsc_khz() / 1000U);
			entry->ebx = entry->eax;
			/* Bus frequency: hard coded to 100M */
			entry->ecx = 100U;
			entry->edx = 0U;
		}
		break;

	/*
	 * Leaf 0x40000000
	 * This leaf returns the CPUID leaf range supported by the
	 * hypervisor and the hypervisor vendor signature.
	 *
	 * EAX: The maximum input value for CPUID supported by the
	 *	hypervisor.
	 * EBX, ECX, EDX: Hypervisor vendor ID signature.
	 */
	case 0x40000000U:
	{
		static const char sig[12] = "ACRNACRNACRN";
		const uint32_t *sigptr = (const uint32_t *)sig;

		entry->eax = 0x40000010U;
		entry->ebx = sigptr[0];
		entry->ecx = sigptr[1];
		entry->edx = sigptr[2];
		break;
	}

	/*
	 * Leaf 0x40000001 - ACRN extended information.
	 * This leaf returns the extended information of ACRN hypervisor.
	 *
	 * EAX: Guest capability flags
	 * EBX, ECX, EDX: RESERVED (reserved fields are set to zero).
	 */
	case 0x40000001U:
		entry->eax = 0U;
		entry->ebx = 0U;
		entry->ecx = 0U;
		entry->edx = 0U;
		break;

	/*
	 * Leaf 0x40000010 - Timing Information.
	 * This leaf returns the current TSC frequency and
	 * current Bus frequency in kHz.
	 *
	 * EAX: (Virtual) TSC frequency in kHz.
	 *      TSC frequency is calculated from PIT in ACRN
	 * EBX, ECX, EDX: RESERVED (reserved fields are set to zero).
	 */
	case 0x40000010U:
		entry->eax = get_tsc_khz();
		entry->ebx = 0U;
		entry->ecx = 0U;
		entry->edx = 0U;
		break;

	default:
		cpuid_subleaf(leaf, subleaf, &entry->eax, &entry->ebx, &entry->ecx, &entry->edx);
		break;
	}
}

#ifdef CONFIG_SGX_ENABLED
/**
 * @brief Set SGX related vCPUID entries
 *
 * This function initializes 3 SGX entries:
 * - CPUID.(EAX=12H,ECX=0H) is stuffed with HW supported SGX information.
 * - CPUID.(EAX=12H,ECX=01H) is stuffed with HW supported values except bit 3 and bit 4 are excluded in ECX.
 * - CPUID.(EAX=12H,ECX=02H) entry values are fully emulated according to SGX current settings.
 *
 * @param[inout] vm Target VM to set SGX vCPUID entry data
 *
 * @return A integer value to indicate the operation result
 *
 * @retval -ENOMEM vCPUID entry number overflows.
 * @retval 0 SGX vCPUID entries are set successfully.
 *
 * @pre vm != NULL
 *
 * @post N/A
 */
static int32_t set_vcpuid_sgx(struct acrn_vm *vm)
{
	int32_t result = 0;

	if (is_vsgx_supported(vm->vm_id)) {
		struct vcpuid_entry entry;
		struct epc_map *maps;
		uint32_t mid;
		uint64_t size = 0;
		/* init cpuid.12h.0h */
		init_vcpuid_entry(CPUID_SGX_LEAF, 0U, CPUID_CHECK_SUBLEAF, &entry);
		result = set_vcpuid_entry(vm, &entry);
		/* init cpuid.12h.1h */
		if (result == 0) {
			init_vcpuid_entry(CPUID_SGX_LEAF, 1U, CPUID_CHECK_SUBLEAF, &entry);
			/* MPX not present to guest */
			entry.ecx &= (uint32_t) ~XCR0_BNDREGS;
			entry.ecx &= (uint32_t) ~XCR0_BNDCSR;
			result = set_vcpuid_entry(vm, &entry);
		}
		if (result == 0) {
			maps = get_epc_mapping(vm->vm_id);
			/* init cpuid.12h.2h, only build one EPC section for a VM */
			for (mid = 0U; mid <= MAX_EPC_SECTIONS; mid++) {
				size += maps[mid].size;
			}
			entry.leaf = CPUID_SGX_LEAF;
			entry.subleaf = CPUID_SGX_EPC_SUBLEAF_BASE;
			entry.flags = CPUID_CHECK_SUBLEAF;
			entry.eax = CPUID_SGX_EPC_TYPE_VALID;
			entry.eax |= (uint32_t)maps[0].gpa & CPUID_SGX_EPC_LOW_MASK;
			entry.ebx = (uint32_t)(maps[0].gpa >> 32U) &  CPUID_SGX_EPC_HIGH_MASK;
			entry.ecx = (uint32_t)size & CPUID_SGX_EPC_LOW_MASK;
			entry.edx = (uint32_t)(size >> 32U) & CPUID_SGX_EPC_HIGH_MASK;
			result = set_vcpuid_entry(vm, &entry);
		}
	}

	return result;
}
#endif

/**
 * @brief Set cache related vCPUID entries
 *
 * This function initializes cache related vCPUIDs, i.e., CPUID.(EAX=04H,ECX=0H) and its subleaves. The data is
 * retrieved by executing CPUID instruction with the leaves and subleaves.
 *
 * @param[inout] vm Target VM to set cache vCPUID entry data
 *
 * @return A integer value to indicate the operation result
 *
 * @retval -ENOMEM vCPUID entry number overflows.
 * @retval 0 The vCPUID operation succeeded.
 *
 * @pre vm != NULL
 *
 * @post N/A
 */
static int32_t set_vcpuid_cache(struct acrn_vm *vm)
{
	int32_t result = 0;
	struct vcpuid_entry entry;
	uint32_t i;

	entry.leaf = CPUID_CACHE;
	entry.flags = CPUID_CHECK_SUBLEAF;
	for (i = 0U; ; i++) {
		entry.subleaf = i;
		cpuid_subleaf(CPUID_CACHE, i, &entry.eax, &entry.ebx, &entry.ecx, &entry.edx);
		if (entry.eax == 0U) {
			break;
		}

		result = set_vcpuid_entry(vm, &entry);
		if (result != 0) {
			/* wants to break out of switch */
			break;
		}
	}
	return result;
}

/**
 * @brief Set extended feature vCPUIDs
 *
 * This function initializes extended feature vCPUIDs, i.e., CPUID.(EAX=07H,ECX=0H) and
 * its subleaves. For CPUID.(EAX=07H,ECX=0H), it first stuffs the entry with results
 * returned by executing CPUID.(EAX=07H,ECX=0H) instruction, then filter out some unsupported
 * features, such as LA57, SGX, SGX_LC, MPX, Process Trace, CET, TME etc. Also filter out the
 * features according to current VM and CR4 configuration. The subleaves CPUID.(EAX=07H,ECX=i)
 * are stuffed with values returned by executing CPUID.(EAX=07H,ECX=i) instruction.
 * Refer to Section 6.3.3 CPUID Leaves of Software Architecture Design Specification for the detailed design that
 * specifies the expected behavior of initializing extended feature vCPUID leaves.
 *
 * @param[inout] vm Target VM to set extended feature vCPUID entry data
 *
 * @return A integer value to indicate the operation result
 *
 * @retval -ENOMEM vCPUID entry number overflows.
 * @retval 0 The vCPUID operation succeeded.
 *
 * @pre vm != NULL
 *
 * @post N/A
 */
static int32_t set_vcpuid_extfeat(struct acrn_vm *vm)
{
	uint64_t cr4_reserved_mask = get_cr4_reserved_bits(vm);
	int32_t result = 0;
	struct vcpuid_entry entry;
	uint32_t i, sub_leaves;

	/* cpuid.07h.0h */
	cpuid_subleaf(CPUID_EXTEND_FEATURE, 0U, &entry.eax, &entry.ebx, &entry.ecx, &entry.edx);

	entry.ebx &= ~(CPUID_EBX_PQM | CPUID_EBX_PQE);

	/* mask LA57 */
	entry.ecx &= ~CPUID_ECX_LA57;

	/* mask SGX and SGX_LC */
	entry.ebx &= ~CPUID_EBX_SGX;
	entry.ecx &= ~CPUID_ECX_SGX_LC;

	/* mask MPX */
	entry.ebx &= ~CPUID_EBX_MPX;

	/* mask Intel Processor Trace, since 14h is disabled */
	entry.ebx &= ~CPUID_EBX_PROC_TRC;

	/* mask CET shadow stack and indirect branch tracking */
	entry.ecx &= ~CPUID_ECX_CET_SS;
	entry.edx &= ~CPUID_EDX_CET_IBT;

	/* mask TME */
	entry.ecx &= ~CPUID_ECX_TME;
	entry.edx &= ~CPUID_EDX_PCONFIG;

	/* mask architectural LBRs */
	if (!vm_cpu_feature_enabled(vm, GUEST_CPU_FEATURE_LBR_PT)) {
		entry.edx &= ~CPUID_EDX_LBR;
	}

	if ((cr4_reserved_mask & CR4_FSGSBASE) != 0UL) {
		entry.ebx &= ~CPUID_EBX_FSGSBASE;
	}

	if ((cr4_reserved_mask & CR4_SMEP) != 0UL) {
		entry.ebx &= ~CPUID_EBX_SMEP;
	}

	if ((cr4_reserved_mask & CR4_SMAP) != 0UL) {
		entry.ebx &= ~CPUID_EBX_SMAP;
	}

	if ((cr4_reserved_mask & CR4_UMIP) != 0UL) {
		entry.ecx &= ~CPUID_ECX_UMIP;
	}

	if ((cr4_reserved_mask & CR4_PKE) != 0UL) {
		entry.ecx &= ~CPUID_ECX_PKE;
	}

	if ((cr4_reserved_mask & CR4_LA57) != 0UL) {
		entry.ecx &= ~CPUID_ECX_LA57;
	}

	if ((cr4_reserved_mask & CR4_PKS) != 0UL) {
		entry.ecx &= ~CPUID_ECX_PKS;
	}

#ifdef CONFIG_SGX_ENABLED
	if (is_vsgx_supported(vm->vm_id)) {
		entry.ebx |= CPUID_EBX_SGX;
	}
#endif

	entry.edx |= CPUID_EDX_ARCH_CAP;

	entry.leaf = CPUID_EXTEND_FEATURE;
	entry.subleaf = 0U;
	entry.flags = CPUID_CHECK_SUBLEAF;
	result = set_vcpuid_entry(vm, &entry);
	if (result == 0) {
		sub_leaves = entry.eax;
		for (i = 1U; i <= sub_leaves; i++) {
			cpuid_subleaf(CPUID_EXTEND_FEATURE, i, &entry.eax, &entry.ebx, &entry.ecx, &entry.edx);
			entry.subleaf = i;
			/* cpuid.07h.1h */
			if (i == 1) {
				/* Hypervisor doesn't support CET supervisor shadow stack, clear CPUID(7,1).EDX[18].*/
				entry.edx &= ~CPUID_EDX_CET_SSS;
				/* Hide HRESET as Thread Director is always hidden for the guest */
				entry.eax &= ~CPUID_EAX_HRESET;
			}
			result = set_vcpuid_entry(vm, &entry);
			if (result != 0) {
				break;
			}
		}
	}
	return result;
}

/**
 * @brief Get Monitor/MWait vCPUID
 *
 * This function sets Monitor/MWait vCPUID entry, i.e., CPUID.(EAX=05H).
 * The entry data is first retrieved by executing CPUID.(EAX=05H,ECX=0H), then enable
 * CPUID.(EAX=05H,ECX=0H).ECX[3] if below conditions are met:
 * - MSR_IA32_MISC_ENABLE_MONITOR_ENA in guest MSR_IA32_MISC_ENABLE is set.
 * - LAPIC is not passed through to VM.
 *
 * Otherwise, skip enabling the bit.
 *
 * @param[in] vcpu Target vCPU to set the vCPUID entry
 * @param[inout] eax Pointer to hold input CPUID leaf and output CPUID EAX value
 * @param[out] ebx Pointer to hold CPUID EBX value
 * @param[out] ecx Pointer to hold CPUID ECX value
 * @param[out] edx Pointer to hold CPUID EDX value

 * @return None
 *
 * @pre vcpu != NULL
 * @pre eax != NULL && ebx != NULL && ecx != NULL && edx != NULL
 *
 * @post N/A
 */
static void guest_cpuid_05h(struct acrn_vcpu *vcpu, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx)
{
	uint64_t guest_ia32_misc_enable = vcpu_get_guest_msr(vcpu, MSR_IA32_MISC_ENABLE);

	cpuid_subleaf(CPUID_MONITOR_MWAIT, *ecx, eax, ebx, ecx, edx);
	if (((guest_ia32_misc_enable & MSR_IA32_MISC_ENABLE_MONITOR_ENA) != 0UL) &&
			!is_lapic_pt_configured(vcpu->vm)) {
		*ecx |= CPUID_ECX_MONITOR_LESS;
	}
}

/**
 * @brief Get thermal and power vCPUID entry
 *
 * The function emulates CPUID.(EAX=06H) entry data. It first retrieves entry data by
 * executing CPUID.(EAX=06H,ECX=0H) then filters out the feature bits not supported by HV or
 * not supported by VM configuration.
 * - Bits not supported by HV:
 *   - CPUID_EAX_HWP_CTL, CPUID_EAX_HWP_PLR, CPUID_EAX_HWP_N, CPUID_EAX_HFI, CPUID_EAX_ITD
 *
 * - Bits affected by VM config:
 *   - CPUID_EAX_HWP, CPUID_EAX_HWP_AW, CPUID_EAX_HWP_EPP, CPUID_ECX_HCFC, CPUID_EAX_ECMD,
 *   - CPUID_EAX_HFN, CPUID_EAX_DTS, CPUID_EAX_PLN, CPUID_EAX_PTM
 *
 * @param[in] vm Target VM to get the CPUID data
 * @param[inout] eax Pointer to hold input CPUID leaf and output CPUID EAX value
 * @param[out] ebx Pointer to hold CPUID EBX value
 * @param[out] ecx Pointer to hold CPUID ECX value
 * @param[out] edx Pointer to hold CPUID EDX value
 *
 * @return None
 *
 * @pre vm != NULL
 * @pre eax != NULL && ebx != NULL && ecx != NULL && edx != NULL
 *
 * @post N/A
 */
static void guest_cpuid_06h(struct acrn_vm *vm, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx)
{
	cpuid_subleaf(CPUID_THERMAL_POWER, *ecx, eax, ebx, ecx, edx);

	/* Always hide package level HWP controls and HWP interrupt*/
	*eax &= ~(CPUID_EAX_HWP_CTL | CPUID_EAX_HWP_PLR | CPUID_EAX_HWP_N | CPUID_EAX_HWP_HPC | CPUID_EAX_HWP_PECI);
	/* Always hide Hardware Feedback Interface and Thread Director */
	*eax &= ~(CPUID_EAX_HFI | CPUID_EAX_ITD);
	/* Always hide Hardware Duty Cycling */
	*eax &= ~CPUID_EAX_HDC;
	/* Since Thread Director is hidden, hide the number of Thread Director classes */
	*ecx &= ~CPUID_ECX_ITD_NOC;
	/* Always hide Hardware Coordination Feedback Capability */
	*ecx &= ~CPUID_ECX_HCFC;
	/* Since HFI is hidden, hide the edx too */
	*edx = 0U;
	if (!vm_cpu_feature_enabled(vm, GUEST_CPU_FEATURE_VHWP)) {
		*eax &= ~(CPUID_EAX_HWP | CPUID_EAX_HWP_AW | CPUID_EAX_HWP_EPP | CPUID_EAX_HWP_FC | CPUID_EAX_FLEX_HWP |
			CPUID_EAX_HWP_FAM | CPUID_EAX_HWP_IILP);
	}
	if (!vm_cpu_feature_enabled(vm, GUEST_CPU_FEATURE_TM_PT)) {
		*eax &= ~(CPUID_EAX_ECMD | CPUID_EAX_HFN | CPUID_EAX_DTS | CPUID_EAX_PLN);
	}
	if (!vm_cpu_feature_enabled(vm, GUEST_CPU_FEATURE_PTM_PT)) {
		*eax &= ~CPUID_EAX_PTM;
	}
}

/**
 * @brief Set VM thermal and power vCPUID
 *
 * This function initializes VM thermal and power related vCPUID entry, i.e., CPUID.(EAX=06H,ECX=0H).
 * The entry data is retrieved by guest_cpuid_06h() and is set to the per-VM vCPUID entries array by
 * set_vcpuid_entry().
 *
 * @param[in] vm Target VM to set vCPUID entry
 *
 * @return A integer value to indicate the operation result
 *
 * @retval -ENOMEM vCPUID entry number overflows.
 * @retval 0 The vCPUID operation succeeded.
 *
 * @pre vm != NULL
 *
 * @post N/A
 */
static int32_t set_vcpuid_thermal_power(struct acrn_vm *vm)
{
	struct vcpuid_entry entry;

	entry.leaf = CPUID_THERMAL_POWER;
	entry.subleaf = 0;
	entry.ecx = 0;
	entry.flags = CPUID_CHECK_SUBLEAF;
	guest_cpuid_06h(vm, &entry.eax, &entry.ebx, &entry.ecx, &entry.edx);

	return set_vcpuid_entry(vm, &entry);
}

/**
 * @brief Set virtual and extended vCPUIDs
 *
 * This function initializes HV emulated CPUIDs and extended CPUIDs.
 * - HV emulated CPUIDs:
 *   - CPUID.(EAX=0x40000000H,ECX=0H) : Return the CPUID leaf range supported by HV and HV vendor signature.
 *   - CPUID.(EAX=0x40000001H,ECX=0H) : Return the extended information of ACRN hypervisor.
 *   - CPUID.(EAX=0x40000010H,ECX=0H) : Return the current TSC frequency and current Bus frequency in kHz.
 * - extended CPUIDs:
 *   - CPUID.(EAX=0x80000000H,ECX=0H), CPUID.(EAX=0x80000002H,ECX=0H) and other extended entries.
 *   Refer to CPUID—CPU Identification instruction, SDM Vol.2 325462-078US for details.
 *
 * @param[in] vm Target VM to set vCPUID entries
 *
 * @return A integer value to indicate the operation result
 *
 * @retval -ENOMEM vCPUID entry number overflows.
 * @retval 0 The vCPUID operation succeeded.
 *
 * @pre vm != NULL
 *
 * @post N/A
 */
static int32_t set_vcpuid_extended_function(struct acrn_vm *vm)
{
	uint32_t i, limit;
	struct vcpuid_entry entry;
	int32_t result;

	init_vcpuid_entry(0x40000000U, 0U, 0U, &entry);
	result = set_vcpuid_entry(vm, &entry);
	if (result == 0) {
		init_vcpuid_entry(0x40000001U, 0U, 0U, &entry);
		/* EAX: Guest capability flags (e.g. whether it is a privilege VM) */
		if (is_service_vm(vm)) {
			entry.eax |= GUEST_CAPS_PRIVILEGE_VM;
		}
		result = set_vcpuid_entry(vm, &entry);
	}

	if (result == 0) {
		init_vcpuid_entry(0x40000010U, 0U, 0U, &entry);
		result = set_vcpuid_entry(vm, &entry);
	}

	if (result == 0) {
		init_vcpuid_entry(0x80000000U, 0U, 0U, &entry);
		result = set_vcpuid_entry(vm, &entry);
	}

	if (result == 0) {
		limit = entry.eax;
		vm->vcpuid_xlevel = limit;
		for (i = 0x80000002U; i <= limit; i++) {
			init_vcpuid_entry(i, 0U, 0U, &entry);
			result = set_vcpuid_entry(vm, &entry);
			if (result != 0) {
				break;
			}
		}
	}

	return result;
}

/**
 * @brief Check whether the vCPUID leaf is per-cpu leaf
 *
 * This function iterates pcpu_cpuids array to check whether the target vCPUID leaf belongs to
 * per-cpu leaf.
 *
 * @param[in] leaf Target vCPUID leaf to check
 *
 * @return true if the leaf is per-cpu vCPUID leaf, false otherwise.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline bool is_percpu_related(uint32_t leaf)
{
	uint32_t i;
	bool ret = false;

	for (i = 0; i < pcpu_cpuids.leaf_nr; i++) {
		if (leaf == pcpu_cpuids.leaves[i]) {
			ret = true;
			break;
		}
	}
	return ret;
}

/**
 * @brief Set per-cpu type vCPUID leaves
 *
 * This function initializes per-cpu type vCPUID leaves. These leaves include CPUID_FEATURES,
 * CPUID_EXTEND_TOPOLOGY, CPUID_XSAVE_FEATURES, CPUID_KEY_LOCKER, CPUID_V2_EXTEND_TOPOLOGY and
 * CPUID_EXTEND_FUNCTION_1. And if the processor is identified as a hybrid part, also include
 * these CPUID leaves: CPUID_TLB, CPUID_CACHE, CPUID_THERMAL_POWER, CPUID_TRACE, CPUID_FREQ,
 * CPUID_ADDR_TRANS, CPUID_MODEL_ID, CPUID_LAST_BRANCH_RECORD and CPUID_EXTEND_CACHE. Per-cpu
 * vCPUID leaf values may be emulated for different vCPUs but per-VM CPUID leaf values are
 * unified across one VM's vCPUs.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline void percpu_cpuid_init(void)
{
	/* 0x1U, 0xBU, 0xDU, 0x19U, 0x1FU, 0x80000001U */
	uint32_t percpu_leaves[] = {CPUID_FEATURES, CPUID_EXTEND_TOPOLOGY,
		CPUID_XSAVE_FEATURES, CPUID_KEY_LOCKER,
		CPUID_V2_EXTEND_TOPOLOGY, CPUID_EXTEND_FUNCTION_1};

	pcpu_cpuids.leaf_nr = sizeof(percpu_leaves)/sizeof(uint32_t);
	memcpy_s(pcpu_cpuids.leaves, sizeof(percpu_leaves),
		 percpu_leaves, sizeof(percpu_leaves));

	/* hybrid related percpu leaves*/
	if (pcpu_has_cap(X86_FEATURE_HYBRID)) {
		/* 0x2U, 0x4U, 0x6U, 0x14U, 0x16U, 0x18U, 0x1A, 0x1C, 0x80000006U */
		uint32_t hybrid_leaves[] = {CPUID_TLB, CPUID_CACHE, CPUID_MONITOR_MWAIT,
			CPUID_THERMAL_POWER, CPUID_TRACE, CPUID_FREQ,
			CPUID_ADDR_TRANS, CPUID_MODEL_ID, CPUID_LAST_BRANCH_RECORD,
			CPUID_EXTEND_CACHE};
		memcpy_s(pcpu_cpuids.leaves + pcpu_cpuids.leaf_nr,
			 sizeof(hybrid_leaves), hybrid_leaves, sizeof(hybrid_leaves));
		pcpu_cpuids.leaf_nr += sizeof(hybrid_leaves)/sizeof(uint32_t);
	}
}

/**
 * @brief Set one VM's vCPUID entries
 *
 * This function is used to initialize vCPUID entries of one VM, it's called during VM creation.
 * The origin CPUID leaf data are retrieved from results of executing CPUID instruction with
 * the leaf/subleaf, HV shall filter out features/feature bits from vCPUID entries if they're
 * not supported. HV virtualized leaves, such as CPUID.(EAX=0x40000000H,ECX=0H),
 * CPUID.(EAX=0x40000001H,ECX=0H) and CPUID.(EAX=0x40000010H, ECX=0H) are initialized with fixed data.
 * Refer to Section 6.3.3 CPUID Leaves of Software Architecture Design Specification for the detailed design that
 * specifies the expected behavior of initializing virtual CPUIDs.
 *
 * Normal case behaviors:
 * - Succeed to set up all vCPUID entries of a VM
 *
 * Error case behaviors:
 * - vCPUID entry number overflows and returns an error code
 *
 * @param[inout] vm Target VM to set vCPUID entries
 *
 * @return A integer value to indicate operation result
 *
 * @retval -ENOMEM vCPUID entry number overflows.
 * @retval 0 The vCPUID operation succeeded.
 *
 * @pre vm != NULL
 *
 * @post N/A
 */
int32_t set_vcpuid_entries(struct acrn_vm *vm)
{
	int32_t result;
	struct vcpuid_entry entry;
	uint32_t limit;
	uint32_t i;
	struct cpuinfo_x86 *cpu_info = get_pcpu_info();

	init_vcpuid_entry(0U, 0U, 0U, &entry);
	if (cpu_info->cpuid_level < 0x16U) {
		/* The cpuid with zero leaf returns the max level. Emulate that the 0x16U is supported */
		entry.eax = 0x16U;
	}
	result = set_vcpuid_entry(vm, &entry);
	if (result == 0) {
		percpu_cpuid_init();

		limit = entry.eax;
		vm->vcpuid_level = limit;

		for (i = 1U; i <= limit; i++) {
			if (is_percpu_related(i)) {
				continue;
			}

			switch (i) {
			/* 0x4U */
			case CPUID_CACHE:
				result = set_vcpuid_cache(vm);
				break;
			/* 0x06U */
			case CPUID_THERMAL_POWER:
				result = set_vcpuid_thermal_power(vm);
				break;
			 /* 0x07U */
			case CPUID_EXTEND_FEATURE:
				result = set_vcpuid_extfeat(vm);
				break;
#ifdef CONFIG_SGX_ENABLED
			/* 0x12U */
			case CPUID_SGX_CAP:
				result = set_vcpuid_sgx(vm);
				break;
#endif
			/* These features are disabled */
			/* PMU is not supported except for core partition VM, like RTVM */
			/* 0x0aU */
			case CPUID_ARCH_PERF_MON:
				if (is_pmu_pt_configured(vm)) {
					init_vcpuid_entry(i, 0U, 0U, &entry);
					result = set_vcpuid_entry(vm, &entry);
				}
				break;

			/* 0xFU, Intel RDT */
			case CPUID_RDT_MONITOR:
				break;
			/* 0x10U, Intel RDT */
			case CPUID_RDT_ALLOCATION:
				break;

			/* 0x14U, Intel Processor Trace */
			case CPUID_TRACE:
			/* 0x1BU, PCONFIG */
			case CPUID_PCONFIG:
			/* 0x20U, HRESET */
			case CPUID_HRESET:
				break;
			/* 0x1CU, LBR */
			case CPUID_LAST_BRANCH_RECORD:
				if (vm_cpu_feature_enabled(vm, GUEST_CPU_FEATURE_LBR_PT)) {
					init_vcpuid_entry(i, 0U, 0U, &entry);
					result = set_vcpuid_entry(vm, &entry);
				}
				break;
			default:
				init_vcpuid_entry(i, 0U, 0U, &entry);
				result = set_vcpuid_entry(vm, &entry);
				break;
			}

			/* WARNING: do nothing between break out of switch and before this check */
			if (result != 0) {
				/* break out of for */
				break;
			}
		}

		if (result == 0) {
			result = set_vcpuid_extended_function(vm);
		}
	}

	return result;
}

/**
 * @brief Get vCPU CPUID.(EAX=01H,ECX=0H) leaf data
 *
 * This function emulates CPUID.(EAX=01H,ECX=0H) leaf data. It first executes CPUID.(EAX=01H,ECX=0H) to retrieve
 * platform CPUID data, then synthesize the CPUID data.
 * Refer to Section 6.3.3 CPUID Leaves of Software Architecture Design Specification for the detailed design that
 * specifies the expected behavior of vCPUID(EAX=01H,ECX=0H).
 *
 * @param[in] vcpu Target vCPU to get the vCPUID data
 * @param[inout] eax Pointer to hold input CPUID leaf and output CPUID EAX value
 * @param[out] ebx Pointer to a variable to hold EBX
 * @param[out] ecx Pointer to a variable to hold ECX
 * @param[out] edx Pointer to a variable to hold EDX
 *
 * @return None
 *
 * @pre vcpu != NULL
 * @pre eax != NULL && ebx != NULL && ecx != NULL && edx != NULL
 *
 * @post N/A
 */
static void guest_cpuid_01h(struct acrn_vcpu *vcpu, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx)
{
	uint32_t apicid = vlapic_get_apicid(vcpu_vlapic(vcpu));
	uint64_t guest_ia32_misc_enable = vcpu_get_guest_msr(vcpu, MSR_IA32_MISC_ENABLE);
	uint64_t cr4_reserved_mask = get_cr4_reserved_bits(vcpu->vm);

	cpuid_subleaf(CPUID_FEATURES, 0x0U, eax, ebx, ecx, edx);
	/* Patching initial APIC ID */
	*ebx &= ~APIC_ID_MASK;
	*ebx |= (apicid <<  APIC_ID_SHIFT);

	if (!is_mc_pt_enabled(vcpu)) {
		*edx &= ~CPUID_EDX_MCE;
		*edx &= ~CPUID_EDX_MCA;
	}

	/* mask Safer Mode Extension */
	*ecx &= ~CPUID_ECX_SMX;

	*ecx &= ~CPUID_ECX_EST;

	/* mask SDBG for silicon debug */
	*ecx &= ~CPUID_ECX_SDBG;

	/* mask VMX to guest OS */
	*ecx &= ~CPUID_ECX_VMX;

	/* mask xTPR Update Control */
	*ecx &= ~CPUID_ECX_ETPRD;

	/* set Hypervisor Present Bit */
	*ecx |= CPUID_ECX_HV;

	if ((cr4_reserved_mask & CR4_PCIDE) != 0UL) {
		*ecx &= ~CPUID_ECX_PCID;
	}

	/* guest monitor/mwait is supported only if MSR_IA32_MISC_ENABLE_MONITOR_ENA bit
	 * of guest MSR_IA32_MISC_ENABLE is set,
	 * else clear cpuid.01h[3].
	 */
	*ecx &= ~CPUID_ECX_MONITOR;
	if ((guest_ia32_misc_enable & MSR_IA32_MISC_ENABLE_MONITOR_ENA) != 0UL) {
		*ecx |= CPUID_ECX_MONITOR;
	}

	*ecx &= ~CPUID_ECX_OSXSAVE;
	if ((*ecx & CPUID_ECX_XSAVE) != 0U) {
		uint64_t cr4;
		/*read guest CR4*/
		cr4 = vcpu_get_cr4(vcpu);
		if ((cr4 & CR4_OSXSAVE) != 0UL) {
			*ecx |= CPUID_ECX_OSXSAVE;
		}
	}

	if ((cr4_reserved_mask & CR4_VME) != 0UL) {
		*edx &= ~CPUID_EDX_VME;
	}

	if ((cr4_reserved_mask & CR4_DE) != 0UL) {
		*edx &= ~CPUID_EDX_DE;
	}

	if ((cr4_reserved_mask & CR4_PSE) != 0UL) {
		*edx &= ~CPUID_EDX_PSE;
	}

	if ((cr4_reserved_mask & CR4_PAE) != 0UL) {
		*edx &= ~CPUID_EDX_PAE;
	}

	if ((cr4_reserved_mask & CR4_PGE) != 0UL) {
		*edx &= ~CPUID_EDX_PGE;
	}

	if ((cr4_reserved_mask & CR4_OSFXSR) != 0UL) {
		*edx &= ~CPUID_EDX_FXSR;
	}

	/* DS/PEBS is not supported except for core partition VM, like RTVM */
	if (!is_pmu_pt_configured(vcpu->vm)) {
		/* mask Debug Store feature */
		*ecx &= ~(CPUID_ECX_DTES64 | CPUID_ECX_DS_CPL);

		/* mask PDCM: Perfmon and Debug Capability */
		*ecx &= ~CPUID_ECX_PDCM;

		/* mask Debug Store feature */
		*edx &= ~CPUID_EDX_DTES;
	}
	if (!vm_cpu_feature_enabled(vcpu->vm, GUEST_CPU_FEATURE_TM_PT)) {
		*ecx &= ~CPUID_ECX_TM2;
		*edx &= ~(CPUID_EDX_TM1 | CPUID_EDX_ACPI);
	}
}

/**
 * @brief Get vCPU CPUID.(EAX=0BH,ECX>=0) leaf data
 *
 * This function emulates CPUID.(EAX=0BH,ECX>=0) leaf data. It first executes CPUID.(EAX=0BH,ECX>=0) to retrieve
 * platform CPUID data, then overwrite EDX value with vCPU's x2APIC ID.
 *
 * @param[in] vcpu Target vCPU to get the vCPUID data
 * @param[inout] eax Pointer to hold input CPUID leaf and output CPUID EAX value
 * @param[out] ebx Pointer to a variable to hold EBX
 * @param[inout] ecx Pointer to hold input CPUID subleaf and output CPUID ECX value
 * @param[out] edx Pointer to a variable to hold EDX
 *
 * @return None
 *
 * @pre vcpu != NULL
 * @pre eax != NULL && ebx != NULL && ecx != NULL && edx != NULL
 *
 * @post N/A
 */
static void guest_cpuid_0bh(struct acrn_vcpu *vcpu, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx)
{
	/* Forward host cpu topology to the guest, guest will know the native platform information such as host cpu topology here */
	cpuid_subleaf(CPUID_EXTEND_TOPOLOGY, *ecx, eax, ebx, ecx, edx);

	/* Patching X2APIC */
	*edx = vlapic_get_apicid(vcpu_vlapic(vcpu));
}

/**
 * @brief Get vCPU CPUID.(EAX=0DH,ECX>=0) leaf data
 *
 * This function emulates CPUID.(EAX=0DH,ECX>=0) and sub-leaf data. If pCPU doesn't support X86_FEATURE_OSXSAVE,
 * then resets the entry data, otherwise it executes CPUID.(EAX=0DH,ECX>=0) to retrieve platform CPUID data, then
 * filters out MPX bits for CPUID.(EAX=0DH,ECX=0H) entry and CET bits for CPUID.(EAX=0DH,ECX=01H) entry. It also
 * resets CPUID.(EAX=0DH,ECX=0BH) entry and CPUID.(EAX=0DH,ECX=0CH) entry as HV doesn't support CET feature.
 *
 * @param[in] vcpu Target vCPU to get the vCPUID data
 * @param[inout] eax Pointer to hold input CPUID leaf and output CPUID EAX value
 * @param[out] ebx Pointer to a variable to hold EBX
 * @param[inout] ecx Pointer to hold input CPUID subleaf and output CPUID ECX value
 * @param[out] edx Pointer to a variable to hold EDX
 *
 * @return None
 *
 * @pre vcpu != NULL
 * @pre eax != NULL && ebx != NULL && ecx != NULL && edx != NULL
 *
 * @post N/A
 */
static void guest_cpuid_0dh(__unused struct acrn_vcpu *vcpu, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx)
{
	uint32_t subleaf = *ecx;

	if (!pcpu_has_cap(X86_FEATURE_OSXSAVE)) {
				*eax = 0U;
				*ebx = 0U;
				*ecx = 0U;
				*edx = 0U;
	} else {
		cpuid_subleaf(CPUID_XSAVE_FEATURES, subleaf, eax, ebx, ecx, edx);
		switch (subleaf) {
		case 0U:
			/* SDM Vol.1 17-2, On processors that do not support Intel MPX,
			 * CPUID.(EAX=0DH,ECX=0H):EAX[3] and
			 * CPUID.(EAX=0DH,ECX=0H):EAX[4] will both be 0 */
			*eax &= ~(CPUID_EAX_XCR0_BNDREGS | CPUID_EAX_XCR0_BNDCSR);
			break;
		case 1U:
			*ecx &= ~(CPUID_ECX_CET_U_STATE | CPUID_ECX_CET_S_STATE);
			break;
		case 11U:
		case 12U:
			*eax = 0U;
			*ebx = 0U;
			*ecx = 0U;
			*edx = 0U;
			break;
		default:
			/* No emulation for other leaves */
			break;
		}
	}
}

/**
 * @brief Get vCPU CPUID.(EAX=19H,ECX=0H) leaf data
 *
 * This function emulates CPUID.(EAX=19H,ECX=0H) leaf data. If pCPU supports X86_FEATURE_KEYLOCKER,
 * then it executes CPUID.(EAX=19H,ECX=0H) to retrieve platform CPUID data, then filters out
 * CPUID_EBX_KL_AES_EN bit if guest doesn't set CR4.KL bit, and also filters out CPUID_ECX_KL_NOBACKUP
 * and CPUID_ECX_KL_RANDOM_KS. Otherwise reset entry data for the vCPUID leaf.
 *
 * @param[in] vcpu Target vCPU to get the vCPUID data
 * @param[inout] eax Pointer to hold input CPUID leaf and output CPUID EAX value
 * @param[out] ebx Pointer to a variable to hold EBX
 * @param[out] ecx Pointer to a variable to hold ECX
 * @param[out] edx Pointer to a variable to hold EDX
 *
 * @return None
 *
 * @pre vcpu != NULL
 * @pre eax != NULL && ebx != NULL && ecx != NULL && edx != NULL
 *
 * @post N/A
 */
static void guest_cpuid_19h(struct acrn_vcpu *vcpu, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx)
{
	if (pcpu_has_cap(X86_FEATURE_KEYLOCKER)) {
		/* Host CR4.KL should be enabled at boot time */
		cpuid_subleaf(CPUID_KEY_LOCKER, 0U, eax, ebx, ecx, edx);
		/* Guest CR4.KL determines KL_AES_ENABLED */
		*ebx &= ~(vcpu->arch.cr4_kl_enabled ? 0U : CPUID_EBX_KL_AES_EN);
		/* Don't support nobackup and randomization parameter of LOADIWKEY */
		*ecx &= ~(CPUID_ECX_KL_NOBACKUP | CPUID_ECX_KL_RANDOM_KS);
	} else {
		*eax = 0U;
		*ebx = 0U;
		*ecx = 0U;
		*edx = 0U;
	}
}

/**
 * @brief Get vCPU CPUID.(EAX=1FH,ECX>=0) leaf data
 *
 * This function emulates CPUID.(EAX=1FH,ECX>=0) leaf data. It first executes CPUID.(EAX=1FH,ECX>=0)
 * to retrieve platform CPUID data, then overwrite EDX value with vCPU's x2APIC ID.
 *
 * @param[in] vcpu Target vCPU to get the vCPUID data
 * @param[inout] eax Pointer to hold input CPUID leaf and output CPUID EAX value
 * @param[out] ebx Pointer to a variable to hold EBX
 * @param[inout] ecx Pointer to hold input CPUID subleaf and output CPUID ECX value
 * @param[out] edx Pointer to a variable to hold EDX
 *
 * @return None
 *
 * @pre vcpu != NULL
 * @pre eax != NULL && ebx != NULL && ecx != NULL && edx != NULL
 *
 * @post N/A
 */
static void guest_cpuid_1fh(struct acrn_vcpu *vcpu, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx)
{
	cpuid_subleaf(CPUID_V2_EXTEND_TOPOLOGY, *ecx, eax, ebx, ecx, edx);

	/* Patching X2APIC */
	*edx = vlapic_get_apicid(vcpu_vlapic(vcpu));
}

/**
 * @brief Get vCPU CPUID.(EAX=80000001H,ECX=0H) leaf data
 *
 * This function emulates CPUID.(EAX=80000001H,ECX=0H) leaf data. It first checks Max. input
 * value for extended function CPUID, if the entry is there and 80000001H leaf is supported,
 * then it executes CPUID.(EAX=80000001H,ECX=0H) to retrieve platform data, then if guest sets
 * IA32_MISC_ENABLE_XD_DISABLE, it filters out CPUID_EDX_XD_BIT_AVIL from EDX. Otherwise reset
 * the leaf entry.
 *
 * @param[in] vcpu Target vCPU to get the vCPUID data
 * @param[inout] eax Pointer to hold input CPUID leaf and output CPUID EAX value
 * @param[out] ebx Pointer to a variable to hold EBX
 * @param[out] ecx Pointer to a variable to hold ECX
 * @param[out] edx Pointer to a variable to hold EDX
 *
 * @return None
 *
 * @pre vcpu != NULL
 * @pre eax != NULL && ebx != NULL && ecx != NULL && edx != NULL
 *
 * @post N/A
 */
static void guest_cpuid_80000001h(const struct acrn_vcpu *vcpu,
	uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx)
{
	const struct vcpuid_entry *entry_check = find_vcpuid_entry(vcpu, CPUID_MAX_EXTENDED_FUNCTION, 0);
	uint64_t guest_ia32_misc_enable = vcpu_get_guest_msr(vcpu, MSR_IA32_MISC_ENABLE);
	uint32_t leaf = CPUID_EXTEND_FUNCTION_1;

	if ((entry_check != NULL) && (entry_check->eax >= leaf)) {
		cpuid_subleaf(leaf, 0x0U, eax, ebx, ecx, edx);
		/* SDM Vol4 2.1, XD Bit Disable of MSR_IA32_MISC_ENABLE
		 * When set to 1, the Execute Disable Bit feature (XD Bit) is disabled and the XD Bit
		 * extended feature flag will be clear (CPUID.80000001H: EDX[20]=0)
		 */
		if ((guest_ia32_misc_enable & MSR_IA32_MISC_ENABLE_XD_DISABLE) != 0UL) {
			*edx = *edx & ~CPUID_EDX_XD_BIT_AVIL;
		}
	} else {
		*eax = 0U;
		*ebx = 0U;
		*ecx = 0U;
		*edx = 0U;
	}
}

/**
 * @brief Get guest CPUID data identified by leaf and subleaf
 *
 * This function retrieves guest vCPUID data from VM cached vCPUID array if it's not a per-cpu leaf.
 * Otherwise it gets data through vCPUID helpers for specific leaves, i.e., CPUID_FEATURES,
 * CPUID_THERMAL_POWER, CPUID_EXTEND_TOPOLOGY, CPUID_XSAVE_FEATURES, CPUID_KEY_LOCKER,
 * CPUID_V2_EXTEND_TOPOLOGY and CPUID_EXTEND_FUNCTION_1. For other per-cpu leaves except CPUID_TRACE,
 * CPUID_PCONFIG, it retrieves CPUID leaf data directly from platform CPUID execution results.
 * CPUID_TRACE, CPUID_PCONFIG and CPUID_HRESET are not supported so reset CPUID entry data.
 *
 * Normal case behaviors:
 * - Get the vCPUID data identified by leaf/subleaf in eax/ecx
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[in] vcpu Target vCPU to get the vCPUID data
 * @param[inout] eax Pointer to hold input CPUID leaf and output CPUID EAX value
 * @param[out] ebx Pointer to a variable to hold EBX
 * @param[inout] ecx Pointer to hold input CPUID subleaf and output CPUID ECX value
 * @param[out] edx Pointer to a variable to hold EDX
 *
 * @return None
 *
 * @pre vcpu != NULL
 * @pre eax != NULL && ebx != NULL && ecx != NULL && edx != NULL
 *
 * @post N/A
 */
void guest_cpuid(struct acrn_vcpu *vcpu, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx)
{
	uint32_t leaf = *eax;
	uint32_t subleaf = *ecx;

	/* vm related */
	if (!is_percpu_related(leaf)) {
		const struct vcpuid_entry *entry = find_vcpuid_entry(vcpu, leaf, subleaf);

		if (entry != NULL) {
			*eax = entry->eax;
			*ebx = entry->ebx;
			*ecx = entry->ecx;
			*edx = entry->edx;
		} else {
			*eax = 0U;
			*ebx = 0U;
			*ecx = 0U;
			*edx = 0U;
		}
	} else {
		/* percpu related */
		switch (leaf) {
		/* 0x01U */
		case CPUID_FEATURES:
			guest_cpuid_01h(vcpu, eax, ebx, ecx, edx);
			break;

		/* 0x05U for MONITOR MWAIT */
		case CPUID_MONITOR_MWAIT:
			guest_cpuid_05h(vcpu, eax, ebx, ecx, edx);
			break;

		/* 0x06U for hybrid arch */
		case CPUID_THERMAL_POWER:
			guest_cpuid_06h(vcpu->vm, eax, ebx, ecx, edx);
			break;

		/* 0x0BU */
		case CPUID_EXTEND_TOPOLOGY:
			guest_cpuid_0bh(vcpu, eax, ebx, ecx, edx);
			break;

		/* 0x0dU */
		case CPUID_XSAVE_FEATURES:
			guest_cpuid_0dh(vcpu, eax, ebx, ecx, edx);
			break;

		/* 0x19U */
		case CPUID_KEY_LOCKER:
			guest_cpuid_19h(vcpu, eax, ebx, ecx, edx);
			break;

		/* 0x1fU */
		case CPUID_V2_EXTEND_TOPOLOGY:
			guest_cpuid_1fh(vcpu, eax, ebx, ecx, edx);
			break;

		/* 0x80000001U */
		case CPUID_EXTEND_FUNCTION_1:
			guest_cpuid_80000001h(vcpu, eax, ebx, ecx, edx);
			break;

		/* 0x14U for hybrid arch */
		case CPUID_TRACE:
			/* FALLTHROUGH */
		/* 0x1BU for PCONFIG*/
		case CPUID_PCONFIG:
		/* 0x20U for HRESET */
		case CPUID_HRESET:
			*eax = 0U;
			*ebx = 0U;
			*ecx = 0U;
			*edx = 0U;
			break;
		default:
			/*
			 * In this switch statement, leaf 0x01/0x04/0x06/0x0b/0x0d/0x19/0x1f/0x80000001
			 * shall be handled specifically. All the other cases
			 * just return physical value.
			 */
			cpuid_subleaf(leaf, *ecx, eax, ebx, ecx, edx);
			break;
		}
	}

}

/**
 * @}
 */
