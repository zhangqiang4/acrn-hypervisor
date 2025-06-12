/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * this file contains vmcs operations which is vcpu related
 */

#include <types.h>
#include <asm/guest/vmcs.h>
#include <asm/guest/vcpu.h>
#include <asm/guest/vm.h>
#include <asm/vmx.h>
#include <asm/gdt.h>
#include <asm/pgtable.h>
#include <asm/per_cpu.h>
#include <asm/cpu_caps.h>
#include <asm/cpufeatures.h>
#include <asm/guest/vmexit.h>
#include <logmsg.h>

/**
 * @addtogroup vp-base_hv-main
 *
 * @{
 */

/**
 * @file
 * @brief Virtual Machine Control Structure (VMCS)
 *
 * This file contains functions for initializing and managing the Virtual Machine Control Structure (VMCS)
 * for vCPUs in the ACRN hypervisor. It includes functions for initializing VMCS fields, loading VMCS, and
 * manipulating VMCS fields.
 */

/**
 * @brief Initialize the guest-area for a given vCPU.
 *
 * This function sets up the initial state of the fields contained in the guest-state area of the VMCS for a specified
 * vCPU. It includes setting up guest register state.
 *
 * Guest register state contains:
 * - Control registers CR0, CR3 and CR4.
 * - Debug register DR7.
 * - RSP, RIP, and RFLAGS.
 * - CS, SS, DS, ES, FS, GS, LDTR, and TR.
 * - GDTR and IDTR.
 * - MSRs: IA32_SYSENTER_CS, IA32_SYSENTER_ESP, IA32_SYSENTER_EIP, IA32_DEBUGCTL, IA32_PAT, IA32_PERF_CTL,
 *   and IA32_MISC_ENABLE.
 *
 * rip, rsp, ia32_efer and rflags are written to VMCS in start_vcpu().
 *
 * @param[inout] vcpu The pointer to the specified vCPU.
 * @param[in] cr0 The value of the CR0 register.
 * @param[in] cr3 The value of the CR3 register.
 * @param[in] cr4 The value of the CR4 register.
 *
 * @return None
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static void init_guest_vmx(struct acrn_vcpu *vcpu, uint64_t cr0, uint64_t cr3,
	uint64_t cr4)
{
	struct guest_cpu_context *ctx = &vcpu->arch.contexts;
	struct ext_context *ectx = &ctx->ext_ctx;
	/* As CPUID.01H:ECX[7] is removed from guests, guests should not see EIST enable bit. */
	uint64_t misc_en = msr_read(MSR_IA32_MISC_ENABLE) & ~MSR_IA32_MISC_ENABLE_EIST;

	pr_dbg("%s,cr0:0x%lx, cr4:0x%lx.", __func__, cr0, cr4);

	vcpu_set_cr4(vcpu, cr4);
	vcpu_set_cr0(vcpu, cr0);
	exec_vmwrite(VMX_GUEST_CR3, cr3);

	exec_vmwrite(VMX_GUEST_GDTR_BASE, ectx->gdtr.base);
	pr_dbg("VMX_GUEST_GDTR_BASE: 0x%016lx", ectx->gdtr.base);
	exec_vmwrite32(VMX_GUEST_GDTR_LIMIT, ectx->gdtr.limit);
	pr_dbg("VMX_GUEST_GDTR_LIMIT: 0x%08x", ectx->gdtr.limit);

	exec_vmwrite(VMX_GUEST_IDTR_BASE, ectx->idtr.base);
	pr_dbg("VMX_GUEST_IDTR_BASE: 0x%016lx", ectx->idtr.base);
	exec_vmwrite32(VMX_GUEST_IDTR_LIMIT, ectx->idtr.limit);
	pr_dbg("VMX_GUEST_IDTR_LIMIT: 0x%08x", ectx->idtr.limit);

	/* init segment selectors: es, cs, ss, ds, fs, gs, ldtr, tr */
	load_segment(ectx->cs, VMX_GUEST_CS);
	load_segment(ectx->ss, VMX_GUEST_SS);
	load_segment(ectx->ds, VMX_GUEST_DS);
	load_segment(ectx->es, VMX_GUEST_ES);
	load_segment(ectx->fs, VMX_GUEST_FS);
	load_segment(ectx->gs, VMX_GUEST_GS);
	load_segment(ectx->tr, VMX_GUEST_TR);
	load_segment(ectx->ldtr, VMX_GUEST_LDTR);

	/* init guest ia32_misc_enable value for guest read */
	if (!is_pmu_pt_configured(vcpu->vm)) {
		/* DS is not supported, set BTS and PEBS unavailable */
		misc_en |= (MSR_IA32_MISC_ENABLE_BTS_UNAVAIL | MSR_IA32_MISC_ENABLE_PEBS_UNAVAIL);
		/* PMA disabled */
		misc_en &= ~MSR_IA32_MISC_ENABLE_PMA;
	}
	vcpu_set_guest_msr(vcpu, MSR_IA32_MISC_ENABLE, misc_en);

	vcpu_set_guest_msr(vcpu, MSR_IA32_PERF_CTL, msr_read(MSR_IA32_PERF_CTL));

	/* Set to default for Power-up/Reset events, not for INIT event */
	if (ectx->reset_mode != VCPU_INIT_RESET) {
		exec_vmwrite32(VMX_GUEST_IA32_SYSENTER_CS, 0U);
		exec_vmwrite(VMX_GUEST_IA32_SYSENTER_ESP, 0UL);
		exec_vmwrite(VMX_GUEST_IA32_SYSENTER_EIP, 0UL);
	}

	/* fixed values */
	exec_vmwrite(VMX_GUEST_PENDING_DEBUG_EXCEPT, 0UL);
	exec_vmwrite(VMX_GUEST_IA32_DEBUGCTL_FULL, 0UL);
	exec_vmwrite32(VMX_GUEST_INTERRUPTIBILITY_INFO, 0U);
	exec_vmwrite32(VMX_GUEST_ACTIVITY_STATE, 0U);
	exec_vmwrite32(VMX_GUEST_SMBASE, 0U);
	vcpu_set_guest_msr(vcpu, MSR_IA32_PAT, PAT_POWER_ON_VALUE);
	exec_vmwrite(VMX_GUEST_IA32_PAT_FULL, PAT_POWER_ON_VALUE);
	exec_vmwrite(VMX_GUEST_DR7, DR7_INIT_VALUE);
}

/**
 * @brief Initialize fields contained in the guest-state area of the VMCS for a given vCPU.
 *
 * This function initializes the fields contained in the guest-state area of the VMCS for a specified vCPU by
 * calling init_guest_vmx function with the control register values in the guest CPU context.
 *
 * @param[inout] vcpu The pointer to the specified vCPU.
 *
 * @return None
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static void init_guest_state(struct acrn_vcpu *vcpu)
{
	struct guest_cpu_context *ctx = &vcpu->arch.contexts;

	pr_dbg("%s, cr0:0x%lx, cr4:0x%lx.\n", __func__,
	ctx->run_ctx.cr0, ctx->run_ctx.cr4);

	init_guest_vmx(vcpu, ctx->run_ctx.cr0, ctx->ext_ctx.cr3,
			ctx->run_ctx.cr4 & ~(CR4_VMXE | CR4_SMXE | CR4_MCE));
}

/**
 * @brief Initialize host-state area.
 *
 * This function initializes the fields contained in the host-state area of the VMCS.
 *
 * Initialization in the host-state area correspond to processor registers:
 * - CR0, CR3, and CR4.
 * - RSP and RIP.
 * - Selector fields for the segment registers CS, SS, DS, ES, FS, GS, and TR.
 * - Base-address fields for the FS, GS, TR, GDTR and IDTR.
 * - MSRs: IA32_SYSENTER_CS, IA32_SYSENTER_ESP, IA32_SYSENTER_EIP, IA32_PAT, and IA32_EFER.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
static void init_host_state(void)
{
	uint16_t value16;
	uint64_t value64;
	uint64_t value;
	uint64_t tss_addr;
	uint64_t gdt_base;
	uint64_t idt_base;

	pr_dbg("Initialize host state");

	/***************************************************
	 * 16 - Bit fields
	 * Move the current ES, CS, SS, DS, FS, GS, TR, LDTR * values to the
	 * corresponding 16-bit host * segment selection (ES, CS, SS, DS, FS,
	 * GS), * Task Register (TR), * Local Descriptor Table Register (LDTR)
	 *
	 ***************************************************/
	CPU_SEG_READ(es, &value16);
	exec_vmwrite16(VMX_HOST_ES_SEL, value16);
	pr_dbg("VMX_HOST_ES_SEL: 0x%hx ", value16);

	CPU_SEG_READ(cs, &value16);
	exec_vmwrite16(VMX_HOST_CS_SEL, value16);
	pr_dbg("VMX_HOST_CS_SEL: 0x%hx ", value16);

	CPU_SEG_READ(ss, &value16);
	exec_vmwrite16(VMX_HOST_SS_SEL, value16);
	pr_dbg("VMX_HOST_SS_SEL: 0x%hx ", value16);

	CPU_SEG_READ(ds, &value16);
	exec_vmwrite16(VMX_HOST_DS_SEL, value16);
	pr_dbg("VMX_HOST_DS_SEL: 0x%hx ", value16);

	CPU_SEG_READ(fs, &value16);
	exec_vmwrite16(VMX_HOST_FS_SEL, value16);
	pr_dbg("VMX_HOST_FS_SEL: 0x%hx ", value16);

	CPU_SEG_READ(gs, &value16);
	exec_vmwrite16(VMX_HOST_GS_SEL, value16);
	pr_dbg("VMX_HOST_GS_SEL: 0x%hx ", value16);

	exec_vmwrite16(VMX_HOST_TR_SEL, HOST_GDT_RING0_CPU_TSS_SEL);
	pr_dbg("VMX_HOST_TR_SEL: 0x%hx ", HOST_GDT_RING0_CPU_TSS_SEL);

	/******************************************************
	 * 32-bit fields
	 * Set up the 32 bit host state fields - pg 3418 B.3.3 * Set limit for
	 * ES, CS, DD, DS, FS, GS, LDTR, Guest TR, * GDTR, and IDTR
	 ******************************************************/

	/* TODO: Should guest GDTB point to host GDTB ? */
	/* Obtain the current global descriptor table base */
	gdt_base = sgdt();

	if (((gdt_base >> 47U) & 0x1UL) != 0UL) {
	        gdt_base |= 0xffff000000000000UL;
	}

	/* Set up the guest and host GDTB base fields with current GDTB base */
	exec_vmwrite(VMX_HOST_GDTR_BASE, gdt_base);
	pr_dbg("VMX_HOST_GDTR_BASE: 0x%lx ", gdt_base);

	tss_addr = hva2hpa((void *)&get_cpu_var(tss));
	/* Set up host TR base fields */
	exec_vmwrite(VMX_HOST_TR_BASE, tss_addr);
	pr_dbg("VMX_HOST_TR_BASE: 0x%016lx ", tss_addr);

	/* Obtain the current interrupt descriptor table base */
	idt_base = sidt();
	/* base */
	if (((idt_base >> 47U) & 0x1UL) != 0UL) {
		idt_base |= 0xffff000000000000UL;
	}

	exec_vmwrite(VMX_HOST_IDTR_BASE, idt_base);
	pr_dbg("VMX_HOST_IDTR_BASE: 0x%lx ", idt_base);

	/**************************************************/
	/* 64-bit fields */
	pr_dbg("64-bit********");

	value64 = msr_read(MSR_IA32_PAT);
	exec_vmwrite64(VMX_HOST_IA32_PAT_FULL, value64);
	pr_dbg("VMX_HOST_IA32_PAT: 0x%016lx ", value64);

	value64 = msr_read(MSR_IA32_EFER);
	exec_vmwrite64(VMX_HOST_IA32_EFER_FULL, value64);
	pr_dbg("VMX_HOST_IA32_EFER: 0x%016lx ",
			value64);

	/**************************************************/
	/* Natural width fields */
	pr_dbg("Natural-width********");
	/* Set up host CR0 field */
	CPU_CR_READ(cr0, &value);
	exec_vmwrite(VMX_HOST_CR0, value);
	pr_dbg("VMX_HOST_CR0: 0x%016lx ", value);

	/* Set up host CR3 field */
	CPU_CR_READ(cr3, &value);
	exec_vmwrite(VMX_HOST_CR3, value);
	pr_dbg("VMX_HOST_CR3: 0x%016lx ", value);

	/* Set up host CR4 field */
	CPU_CR_READ(cr4, &value);
	exec_vmwrite(VMX_HOST_CR4, value);
	pr_dbg("VMX_HOST_CR4: 0x%016lx ", value);

	/* Set up host and guest FS base address */
	value = msr_read(MSR_IA32_FS_BASE);
	exec_vmwrite(VMX_HOST_FS_BASE, value);
	pr_dbg("VMX_HOST_FS_BASE: 0x%016lx ", value);
	value = msr_read(MSR_IA32_GS_BASE);
	exec_vmwrite(VMX_HOST_GS_BASE, value);
	pr_dbg("VMX_HOST_GS_BASE: 0x%016lx ", value);

	/* Set up host instruction pointer on VM Exit */
	value64 = (uint64_t)&vm_exit;
	pr_dbg("HOST RIP on VMExit %016lx ", value64);
	exec_vmwrite(VMX_HOST_RIP, value64);
	pr_dbg("vm exit return address = %016lx ", value64);

	/* As a type I hypervisor, just init sysenter fields to 0 */
	exec_vmwrite(VMX_HOST_IA32_SYSENTER_ESP, 0UL);
	exec_vmwrite(VMX_HOST_IA32_SYSENTER_EIP, 0UL);

	/* We use IA32_SYSENTER_CS MSR to cache pCPU ID. */
	exec_vmwrite32(VMX_HOST_IA32_SYSENTER_CS, msr_read(ACRN_PSEUDO_PCPUID_MSR));
}

/**
 * @brief Check VM-execution control for request capabilities.
 *
 * This function checks VM-execution control capability MSRs for pin-based VM-execution controls, the primary
 * processor-based VM-execution controls and the secondary processor-based VM-execution controls. It performs a bitwise
 * AND operation with the high 32 bits of the MSR value and the requested control settings, and a bitwise OR operation
 * with the low 32 bits. This ensures that only the supported control bits are set. If the requested control settings
 * are not fully supported, an informational message is logged.
 *
 * @param[in] msr The MSR address to read the VMX capabilities from.
 * @param[in] ctrl_req The requested VMX control settings.
 *
 * @return The validated control value that can be set to the VMX control fields.
 *
 * @pre N/A
 *
 * @post N/A
 */
static uint32_t check_vmx_ctrl(uint32_t msr, uint32_t ctrl_req)
{
	uint64_t vmx_msr;
	uint32_t vmx_msr_low, vmx_msr_high;
	uint32_t ctrl = ctrl_req;

	vmx_msr = msr_read(msr);
	vmx_msr_low  = (uint32_t)vmx_msr;
	vmx_msr_high = (uint32_t)(vmx_msr >> 32U);
	pr_dbg("VMX_PIN_VM_EXEC_CONTROLS:low=0x%x, high=0x%x\n",
			vmx_msr_low, vmx_msr_high);

	/* high 32b: must 0 setting
	 * low 32b:  must 1 setting
	 */
	ctrl &= vmx_msr_high;
	ctrl |= vmx_msr_low;

	if ((ctrl_req & ~ctrl) != 0U) {
		pr_info("VMX ctrl 0x%x not fully enabled: current capabilities are 0x%x (full capabilities are 0x%x)\n",
						msr, ctrl, ctrl_req);
	}

	return ctrl;

}

/**
 * @brief Initialize VM-execution controls for a given vCPU.
 *
 * The VM-execution control fields govern VMX non-root operation.
 *
 * This initialization includes:
 * - Pin-based VM-execution controls (Refer to Table 25-5 Definitions of Pin-Based VM-Execution Controls,
 *   Vol. 3, SDM 325462-078):
 *   - External-interrupt exiting (bit 0)
 *   - Process posted interrupts (bit 7)
 * - Primary Processor-Based VM-execution controls (Refer to Table 25-6 Definitions of Primary Processor-Based
 *   VM-Execution Controls, Vol. 3, SDM 325462-078):
 *   - Use TSC offsetting (bit 3)
 *   - HLT exiting (bit 7)
 *   - Disable INVLPG exiting (bit 9)
 *   - MWAIT exiting (bit 10)
 *   - Disable CR3-load exiting (bit 15)
 *   - Disable CR3-store exiting (bit 16)
 *   - Activate tertiary controls (bit 17)
 *   - Disable CR8-load exiting (bit 19)
 *   - Disable CR8-store exiting (bit 20)
 *   - Use TPR shadow (bit 21)
 *   - Use I/O bitmaps (bit 25)
 *   - Use I/O MST bitmaps (bit 28)
 *   - MONITOR exiting (bit 29)
 *   - Activate secondary controls (bit 31)
 * - Secondary Processor-Based VM-execution controls (Refer to Table 25-7 Definitions of Secondary Processor-Based
 *   VM-Execution Controls, Vol. 3, SDM 325462-078):
 *   - Virtualize APIC accesses (bit 0)
 *   - Enable EPT (bit 1)
 *   - Enable RDTSCP (bit 3)
 *   - Enable VPID (bit 5)
 *   - WBINVD exiting (bit 6)
 *   - Unrestricted guest (bit 7)
 *   - APIC-register virtualization (bit 8)
 *   - Virtual-interrupt delivery (bit 9)
 *   - PAUSE-loop exiting (bit 10)
 *   - Enable INVPCID (bit 12)
 *   - Enable XSAVES/XRSTORS (bit 20)
 *   - Enable user wait and pause (bit 26)
 *   - Disable PCONFIG (bit 27)
 * - Tertiary Processor-Based VM-execution controls (Refer to Table 25-8 Definitions of Tertiary Processor-Based
 *  VM-Execution Controls, Vol. 3, SDM 325462-078):
 *   - LOADIWKEY exiting (bit 0)
 * - Exception bitmap
 * - I/O-bitmap addresses
 * - Time-stamp counter offset and multiplier
 * - Guest/host masks and read shadows for CR0 and CR4
 * - CR3-target controls:
 *   - CR3-target values
 *   - CR3-target count
 * - Controls for APIC virtualization:
 *   - APIC-access address
 *   - Virtual-APIC address
 *   - TPR threshold
 *   - EOI-exit bitmap
 *   - Posted-interrupt notification vector
 *   - Posted-interrupt descriptor address
 *   - PID-pointer table address
 *   - Last PID-pointer index
 * - MSR-bitmap address
 *   - Read bitmap for low MSRs
 *   - Read bitmap for high MSRs
 *   - Write bitmap for low MSRs
 *   - Write bitmap for high MSRs
 * - Executive-VMCS pointer
 * - Extended-Page-Table pointer(EPTP)
 * - Controls for PAUSE-loop exiting
 *   - PLE_Gap
 *   - PLE_Window

 *
 * @param[inout] vcpu The pointer to the specified vCPU.
 *
 * @return None
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static void init_exec_ctrl(struct acrn_vcpu *vcpu)
{
	uint32_t value32;
	uint64_t value64;
	struct acrn_vm *vm = vcpu->vm;

	/* Log messages to show initializing VMX execution controls */
	pr_dbg("Initialize execution control ");

	/* Set up VM Execution control to enable Set VM-exits on external
	 * interrupts preemption timer - pg 3950 25.6.1
	 */
	/* enable external interrupt VM Exit */
	value32 = check_vmx_ctrl(MSR_IA32_VMX_PINBASED_CTLS, VMX_PINBASED_CTLS_IRQ_EXIT);

	value32 |= VMX_PINBASED_CTLS_POST_IRQ;

	exec_vmwrite32(VMX_PIN_VM_EXEC_CONTROLS, value32);
	pr_dbg("VMX_PIN_VM_EXEC_CONTROLS: 0x%x ", value32);

	/* Set up primary processor based VM execution controls - pg 3950
	 * 25.6.2. Set up for:
	 * Enable TSC offsetting
	 * Enable TSC exiting
	 * guest access to IO bit-mapped ports causes VM exit
	 * guest access to MSR causes VM exit
	 * Activate secondary controls
	 * Activate tertiary controls
	 */
	/* These are bits 1,4-6,8,13-16, and 26, the corresponding bits of
	 * the IA32_VMX_PROCBASED_CTRLS MSR are always read as 1 --- A.3.2
	 */
	value32 = check_vmx_ctrl(MSR_IA32_VMX_PROCBASED_CTLS,
			 VMX_PROCBASED_CTLS_TSC_OFF | VMX_PROCBASED_CTLS_TPR_SHADOW |
			 VMX_PROCBASED_CTLS_IO_BITMAP | VMX_PROCBASED_CTLS_MSR_BITMAP |
			 VMX_PROCBASED_CTLS_HLT | VMX_PROCBASED_CTLS_SECONDARY | VMX_PROCBASED_CTLS_TERTIARY |
			 VMX_PROCBASED_CTLS_MWAIT | VMX_PROCBASED_CTLS_MONITOR);

	/*Disable VM_EXIT for CR3 access*/
	value32 &= ~(VMX_PROCBASED_CTLS_CR3_LOAD | VMX_PROCBASED_CTLS_CR3_STORE);
	value32 &= ~(VMX_PROCBASED_CTLS_CR8_LOAD | VMX_PROCBASED_CTLS_CR8_STORE);

	/*
	 * Disable VM_EXIT for invlpg execution.
	 */
	value32 &= ~VMX_PROCBASED_CTLS_INVLPG;

	/*
	 * Enable VM_EXIT for rdpmc execution except core partition VM, like RTVM
	 */
	if (!is_pmu_pt_configured(vcpu->vm)) {
		value32 |= VMX_PROCBASED_CTLS_RDPMC;
	}

	vcpu->arch.proc_vm_exec_ctrls = value32;
	exec_vmwrite32(VMX_PROC_VM_EXEC_CONTROLS, value32);
	pr_dbg("VMX_PROC_VM_EXEC_CONTROLS: 0x%x ", value32);

	/* Set up secondary processor based VM execution controls - pg 3952
	 * 25.6.2. Set up for: * Enable EPT * Enable VPID * Enable RDTSCP *
	 * Enable Unrestricted guest (optional)
	 */
	value32 = check_vmx_ctrl(MSR_IA32_VMX_PROCBASED_CTLS2,
			VMX_PROCBASED_CTLS2_VAPIC | VMX_PROCBASED_CTLS2_EPT |VMX_PROCBASED_CTLS2_VPID |
			VMX_PROCBASED_CTLS2_RDTSCP | VMX_PROCBASED_CTLS2_UNRESTRICT | VMX_PROCBASED_CTLS2_XSVE_XRSTR |
			VMX_PROCBASED_CTLS2_PAUSE_LOOP | VMX_PROCBASED_CTLS2_UWAIT_PAUSE);

	/* SDM Vol3, 26.3,  setting "enable INVPCID" VM-execution to 1 with "INVLPG exiting" disabled,
	 * passes-through INVPCID instruction to guest if the instruction is supported.
	 */
	if (pcpu_has_cap(X86_FEATURE_INVPCID)) {
		value32 |= VMX_PROCBASED_CTLS2_INVPCID;
	} else {
		value32 &= ~VMX_PROCBASED_CTLS2_INVPCID;
	}

	value32 |= VMX_PROCBASED_CTLS2_VIRQ;
	value32 |= VMX_PROCBASED_CTLS2_VAPIC_REGS;

	if ((value32 & VMX_PROCBASED_CTLS2_XSVE_XRSTR) != 0U) {
		exec_vmwrite64(VMX_XSS_EXITING_BITMAP_FULL, 0UL);
		vcpu->arch.xsave_enabled = true;
	} else {
		value32 &= ~VMX_PROCBASED_CTLS2_XSVE_XRSTR;
	}

	value32 |= VMX_PROCBASED_CTLS2_WBINVD;

	/* Disable PCONFIG */
	value32 &= ~VMX_PROCBASED_CTLS2_PCONFIG;

	exec_vmwrite32(VMX_PROC_VM_EXEC_CONTROLS2, value32);
	pr_dbg("VMX_PROC_VM_EXEC_CONTROLS2: 0x%x ", value32);

	/* Set up tertiary processor based VM execution controls */
	if ((exec_vmread32(VMX_PROC_VM_EXEC_CONTROLS) & VMX_PROCBASED_CTLS_TERTIARY) != 0U) {
		/* Enable KeyLocker if support */
		value64 = check_vmx_ctrl_64(MSR_IA32_VMX_PROCBASED_CTLS3, VMX_PROCBASED_CTLS3_LOADIWKEY);

		exec_vmwrite64(VMX_PROC_VM_EXEC_CONTROLS3_FULL, value64);
		pr_dbg("VMX_PROC_VM_EXEC_CONTROLS3: 0x%llx ", value64);
	}

	/*APIC-v, config APIC-access address*/
	value64 = vlapic_apicv_get_apic_access_addr();
	exec_vmwrite64(VMX_APIC_ACCESS_ADDR_FULL, value64);

	/*APIC-v, config APIC virtualized page address*/
	value64 = vlapic_apicv_get_apic_page_addr(vcpu_vlapic(vcpu));
	exec_vmwrite64(VMX_VIRTUAL_APIC_PAGE_ADDR_FULL, value64);

	/* Assume advanced APICs feature is supported, disable all
	 * EOI VMEXIT by default and clear RVI and SVI.
	 */
	exec_vmwrite64(VMX_EOI_EXIT0_FULL, 0UL);
	exec_vmwrite64(VMX_EOI_EXIT1_FULL, 0UL);
	exec_vmwrite64(VMX_EOI_EXIT2_FULL, 0UL);
	exec_vmwrite64(VMX_EOI_EXIT3_FULL, 0UL);

	exec_vmwrite16(VMX_POSTED_INTR_VECTOR, (uint16_t)vcpu->arch.pid.control.bits.nv);
	exec_vmwrite64(VMX_PIR_DESC_ADDR_FULL, hva2hpa(get_pi_desc(vcpu)));

	/* Load EPTP execution control
	 * TODO: introduce API to make this data driven based
	 * on VMX_EPT_VPID_CAP
	 */
	value64 = hva2hpa(vm->arch_vm.nworld_eptp) | (3UL << 3U) | 6UL;
	exec_vmwrite64(VMX_EPT_POINTER_FULL, value64);
	pr_dbg("VMX_EPT_POINTER: 0x%016lx ", value64);

	/* Set up page fault error code mask - second paragraph * pg 3953
	 * 25.6.3 - guest page fault exception causing * vmexit is governed by
	 * both VMX_EXCEPTION_BITMAP and * VMX_PF_ERROR_CODE_MASK
	 */
	exec_vmwrite32(VMX_PF_ERROR_CODE_MASK, 0U);

	/* Set up page fault error code match - second paragraph * pg 3953
	 * 25.6.3 - guest page fault exception causing * vmexit is governed by
	 * both VMX_EXCEPTION_BITMAP and * VMX_PF_ERROR_CODE_MATCH
	 */
	exec_vmwrite32(VMX_PF_ERROR_CODE_MATCH, 0U);

	/* Set up CR3 target count - An execution of mov to CR3 * by guest
	 * causes HW to evaluate operand match with * one of N CR3-Target Value
	 * registers. The CR3 target * count values tells the number of
	 * target-value regs to evaluate
	 */
	exec_vmwrite32(VMX_CR3_TARGET_COUNT, 0U);

	/* Set up IO bitmap register A and B - pg 3954 25.6.4 */
	value64 = hva2hpa(vm->arch_vm.io_bitmap);
	exec_vmwrite64(VMX_IO_BITMAP_A_FULL, value64);
	pr_dbg("VMX_IO_BITMAP_A: 0x%016lx ", value64);
	value64 = hva2hpa((void *)&(vm->arch_vm.io_bitmap[PAGE_SIZE]));
	exec_vmwrite64(VMX_IO_BITMAP_B_FULL, value64);
	pr_dbg("VMX_IO_BITMAP_B: 0x%016lx ", value64);

	init_msr_emulation(vcpu);

	/* Set up guest exception mask bitmap setting a bit * causes a VM exit
	 * on corresponding guest * exception - pg 3953 25.6.3
	 * enable VM exit on MC for VMs that does NOT enable MC PT
	 * enable AC for split-lock emulation when split-lock detection is enabled on physical platform.
	 */
	value32 = is_mc_pt_enabled(vcpu) ? 0U : (1U << IDT_MC);
	if (is_ac_enabled()) {
		value32 = (value32 | (1U << IDT_AC));
	}
	if (is_gp_enabled()) {
		value32 = (value32 | (1U << IDT_GP));
	}
	exec_vmwrite32(VMX_EXCEPTION_BITMAP, value32);

	/* Set up executive VMCS pointer - pg 3956 25.6.10 */
	exec_vmwrite64(VMX_EXECUTIVE_VMCS_PTR_FULL, 0UL);

	/* Setup Time stamp counter offset - pg 3954 25.6.5
	 * VMCS.OFFSET = vAdjust - pAdjust
	 */
	value64 = vcpu_get_guest_msr(vcpu, MSR_IA32_TSC_ADJUST) - cpu_msr_read(MSR_IA32_TSC_ADJUST);
	exec_vmwrite64(VMX_TSC_OFFSET_FULL, value64);

	/* Set up the link pointer */
	exec_vmwrite64(VMX_VMS_LINK_PTR_FULL, 0xFFFFFFFFFFFFFFFFUL);

	/* Natural-width */
	pr_dbg("Natural-width*********");

	init_cr0_cr4_host_guest_mask(vm);

	/* The CR3 target registers work in concert with VMX_CR3_TARGET_COUNT
	 * field. Using these registers guest CR3 access can be managed. i.e.,
	 * if operand does not match one of these register values a VM exit
	 * would occur
	 */
	exec_vmwrite(VMX_CR3_TARGET_0, 0UL);
	exec_vmwrite(VMX_CR3_TARGET_1, 0UL);
	exec_vmwrite(VMX_CR3_TARGET_2, 0UL);
	exec_vmwrite(VMX_CR3_TARGET_3, 0UL);

	/* Setup PAUSE-loop exiting - 25.6.13 */
	exec_vmwrite(VMX_PLE_GAP, 128U);
	exec_vmwrite(VMX_PLE_WINDOW, 4096U);
}

/**
 * @brief Initialize VM-entry controls for a given vCPU.
 *
 * VM-entry control fields govern the behavior of VM entries. These controls include:
 *
 * - A 32-bit vector that governs the basic operations of VM entries. (Refer to Chapter 25.8.1 VM-Entry Controls,
 *   Vol. 3, SDM 325462-078):
 *   - IA-32e mode guest (bit 9)
 *   - Load IA32_PAT (bit 14)
 *
 * - VM-entry controls for MSRs. (Refer to Chapter 25.8.2 VM-Entry Controls for MSRs, Vol. 3, SDM 325462-078):
 *   - VM-entry MSR-load count.
 *   - VM-entry MSR_load address.
 *
 * - VM-Entry controls for event injection. (Refer to Chapter 25.8.3 VM-Entry Controls for Event Injection,
 *   Vol. 3, SDM 325462-078):
 *   - VM-entry interrupt information field.
 *   - VM-entry exception error code.
 *   - VM-entry instruction length.
 *
 * @param[in] vcpu The pointer to the specified vCPU.
 *
 * @return None
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static void init_entry_ctrl(const struct acrn_vcpu *vcpu)
{
	uint32_t value32;

	/* Log messages to show initializing VMX entry controls */
	pr_dbg("Initialize Entry control ");

	/* Set up VMX entry controls - ISDM 25.8.1 */
	value32 = VMX_ENTRY_CTLS_LOAD_PAT;

	if (get_vcpu_mode(vcpu) == CPU_MODE_64BIT) {
		value32 |= (VMX_ENTRY_CTLS_IA32E_MODE);
	}

	value32 = check_vmx_ctrl(MSR_IA32_VMX_ENTRY_CTLS, value32);

	exec_vmwrite32(VMX_ENTRY_CONTROLS, value32);
	pr_dbg("VMX_ENTRY_CONTROLS: 0x%x ", value32);

	/* Set up VMX entry MSR load count - pg 3964 25.8.2 Tells the number of
	 * MSRs on load from memory on VM entry from mem address provided by
	 * VM-entry MSR load address field
	 */
	exec_vmwrite32(VMX_ENTRY_MSR_LOAD_COUNT, vcpu->arch.msr_area.count);
	exec_vmwrite64(VMX_ENTRY_MSR_LOAD_ADDR_FULL, hva2hpa((const void *)vcpu->arch.msr_area.guest));

	/* Set up VM entry interrupt information field pg 3964 25.8.3 */
	exec_vmwrite32(VMX_ENTRY_INT_INFO_FIELD, 0U);

	/* Set up VM entry exception error code - pg 3965 25.8.3 */
	exec_vmwrite32(VMX_ENTRY_EXCEPTION_ERROR_CODE, 0U);

	/* Set up VM entry instruction length - pg 3965 25.8.3 */
	exec_vmwrite32(VMX_ENTRY_INSTR_LENGTH, 0U);
}

/**
 * @brief Initialize VM-exit controls for a given vCPU.
 *
 * VM-exit control fields govern the behavior of VM exits. These controls include:
 * - VM-exit controls:
 *   - Host address-space size (bit 9)
 *   - Acknowledge interrupt on exit (bit 15)
 *   - Save IA32_PAT (bit 18)
 *   - Load IA32_PAT (bit 19)
 * - VM-exit controls for MSRs:
 *   - VM-exit MSR-store count
 *   - VM-exit MSR-store address
 *   - VM-exit MSR-load count
 *   - VM-exit MSR-load address
 *
 * @param[in] vcpu The pointer to the specified vCPU.
 *
 * @return None
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static void init_exit_ctrl(const struct acrn_vcpu *vcpu)
{
	uint32_t value32;

	/* Log messages to show initializing VMX entry controls */
	pr_dbg("Initialize Exit control ");

	/* Set up VM exit controls - pg 3960 25.7.1 for: Host address space
	 * size is 64 bit Set up to acknowledge interrupt on exit, if 1 the HW
	 * acks the interrupt in VMX non-root and saves the interrupt vector to
	 * the relevant VM exit field for further processing by Hypervisor
	 * Enable saving and loading IA32_PAT on VMEXIT
	 */
	value32 = check_vmx_ctrl(MSR_IA32_VMX_EXIT_CTLS,
			 VMX_EXIT_CTLS_ACK_IRQ | VMX_EXIT_CTLS_SAVE_PAT |
			 VMX_EXIT_CTLS_LOAD_PAT | VMX_EXIT_CTLS_HOST_ADDR64);

	exec_vmwrite32(VMX_EXIT_CONTROLS, value32);
	pr_dbg("VMX_EXIT_CONTROL: 0x%x ", value32);

	/* Set up VM exit MSR store and load counts pg 3962 25.7.2 - tells the
	 * HW number of MSRs to stored to mem and loaded from mem on VM exit.
	 * The 64 bit VM-exit MSR store and load address fields provide the
	 * corresponding addresses
	 */
	exec_vmwrite32(VMX_EXIT_MSR_STORE_COUNT, vcpu->arch.msr_area.count);
	exec_vmwrite32(VMX_EXIT_MSR_LOAD_COUNT, vcpu->arch.msr_area.count);
	exec_vmwrite64(VMX_EXIT_MSR_STORE_ADDR_FULL, hva2hpa((const void *)vcpu->arch.msr_area.guest));
	exec_vmwrite64(VMX_EXIT_MSR_LOAD_ADDR_FULL, hva2hpa((const void *)vcpu->arch.msr_area.host));
}

static void switch_apicv_mode_x2apic(struct acrn_vcpu *vcpu);

/**
 * @brief VMCS initialization for a given vCPU.
 *
 * This function initializes the VMCS for the given vCPU. It sets up the host and guest states, execution controls,
 * entry controls, and exit controls. Now ACRN Hypervisor locks guest in x2apic mode, so would use virtualize x2APIC
 * mode instead of virtualize APIC accesses in secondary processor-based VM-execution controls for default.
 *
 * @param[inout] vcpu The pointer to the specified vCPU.
 *
 * @return None
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
void init_vmcs(struct acrn_vcpu *vcpu)
{
	void **vmcs_ptr = &get_cpu_var(vmcs_run);
	/* Log message */
	pr_dbg("Initializing VMCS");
	/*
	 * Execute VMCLEAR VMCS to initialize parts of the VMCS region (e.g. it sets the
	 * launch state of the VMCS to clear).
	 */
	clear_va_vmcs(vcpu->arch.vmcs);
	load_va_vmcs(vcpu->arch.vmcs);
	*vmcs_ptr = (void *)vcpu->arch.vmcs;

	/* Initialize the Virtual Machine Control Structure (VMCS) */
	init_host_state();
	/* init exec_ctrl needs to run before init_guest_state */
	init_exec_ctrl(vcpu);
	init_guest_state(vcpu);
	init_entry_ctrl(vcpu);
	init_exit_ctrl(vcpu);

	/* TODO: remove switch_apicv_mode_x2apic */
	switch_apicv_mode_x2apic(vcpu);
}

/**
 * @brief Load VMCS for a given vCPU.
 *
 * This function is to load the VMCS of the given vCPU on the current physical CPU. It checks if the vCPU is launched
 * and if the current VMCS pointer is different from the vCPU's VMCS. If so, it calls load_va_vmcs() function to
 * load the target VMCS to current VMCS and updates the VMCS pointer. Otherwise, it does nothing.
 *
 * @param[in] vcpu The pointer to the specified vCPU.
 *
 * @return None
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
void load_vmcs(const struct acrn_vcpu *vcpu)
{
	const void **vmcs_ptr = (const void **)&get_cpu_var(vmcs_run);

	if (*vmcs_ptr != (const void *)vcpu->arch.vmcs) {
		load_va_vmcs((const uint8_t *)vcpu->arch.vmcs);
		*vmcs_ptr = (const void *)vcpu->arch.vmcs;
	}
}

/**
 * @brief Switch APICv mode to x2APIC for a given vCPU.
 *
 * This function switches the APICv mode to x2APIC. Step by step:
 * - If the LAPIC passthrough is configured:
 *   - Disable external interrupt exiting.
 *   - Disable posted interrupt processing if APICv advanced features are supported.
 *   - Disable IRQ acknowledgment on VM exits.
 *   - Disable TPR shadow, HLT and MONITOR/MWAIT exiting.
 *   - Set TPR threshold to zero.
 *   - Disable pause loop exiting and virtual APIC.
 *   - Disable Register Virtualization and virtual interrupt delivery if APICv advanced features are supported.
 *
 * - If the LAPIC passthrough is not configured:
 *   - Enable x2APIC mode in the VMX_PROC_VM_EXEC_CONTROLS2.
 *   - If IPI virtualization can be enabled, set up tertiary processor based VM execution controls.
 *
 * @param[inout] vcpu The pointer to the specified vCPU.
 *
 * @return None
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static void switch_apicv_mode_x2apic(struct acrn_vcpu *vcpu)
{
	uint32_t value32;
	uint64_t value64;
	if (is_lapic_pt_configured(vcpu->vm)) {
		dev_dbg(DBG_LEVEL_LAPICPT, "%s: switching to x2apic and passthru", __func__);

		/* Disable external interrupt exiting */
		value32 = exec_vmread32(VMX_PIN_VM_EXEC_CONTROLS);
		value32 &= ~VMX_PINBASED_CTLS_IRQ_EXIT;

		/* Disable posted interrupt processing */
		value32 &= ~VMX_PINBASED_CTLS_POST_IRQ;

		exec_vmwrite32(VMX_PIN_VM_EXEC_CONTROLS, value32);

		/* Disable IRQ acknowledge on VM exits */
		value32 = exec_vmread32(VMX_EXIT_CONTROLS);
		value32 &= ~VMX_EXIT_CTLS_ACK_IRQ;
		exec_vmwrite32(VMX_EXIT_CONTROLS, value32);

		/* Disable TPR shadow and HLT exiting */
		vcpu->arch.proc_vm_exec_ctrls &= ~VMX_PROCBASED_CTLS_TPR_SHADOW;
		vcpu->arch.proc_vm_exec_ctrls &= ~VMX_PROCBASED_CTLS_HLT;
		vcpu->arch.proc_vm_exec_ctrls &= ~VMX_PROCBASED_CTLS_MWAIT;
		vcpu->arch.proc_vm_exec_ctrls &= ~VMX_PROCBASED_CTLS_MONITOR;
		exec_vmwrite32(VMX_PROC_VM_EXEC_CONTROLS, vcpu->arch.proc_vm_exec_ctrls);
		/* Set TPR threshold to zero */
		exec_vmwrite32(VMX_TPR_THRESHOLD, 0U);

		/* Disable pause loop exiting and virtual APIC */
		value32 = exec_vmread32(VMX_PROC_VM_EXEC_CONTROLS2);
		value32 &= ~VMX_PROCBASED_CTLS2_PAUSE_LOOP;
		value32 &= ~VMX_PROCBASED_CTLS2_VAPIC;

		/* Disable Register Virtualization and virtual interrupt delivery */
		value32 &= ~VMX_PROCBASED_CTLS2_VIRQ;
		value32 &= ~VMX_PROCBASED_CTLS2_VAPIC_REGS;

		exec_vmwrite32(VMX_PROC_VM_EXEC_CONTROLS2, value32);
	} else {
		/* Enable x2apic mode */
		value32 = exec_vmread32(VMX_PROC_VM_EXEC_CONTROLS2);
		value32 &= ~VMX_PROCBASED_CTLS2_VAPIC;
		value32 |= VMX_PROCBASED_CTLS2_VX2APIC;
		exec_vmwrite32(VMX_PROC_VM_EXEC_CONTROLS2, value32);

		if (can_ipiv_enabled(vcpu->vm)) {
			/* Set up tertiary processor based VM execution controls */
			if ((exec_vmread32(VMX_PROC_VM_EXEC_CONTROLS) & VMX_PROCBASED_CTLS_TERTIARY) != 0U) {
				pr_info("%s enable IPIv\n", __func__);
				value64 = exec_vmread64(VMX_PROC_VM_EXEC_CONTROLS3_FULL);;
				exec_vmwrite64(VMX_PROC_VM_EXEC_CONTROLS3_FULL, value64 | VMX_PROCBASED_CTLS3_IPI_VIRT);

				exec_vmwrite64(VMX_PID_POINTER_TABLE_ADDR_FULL, hva2hpa(vcpu->vm->arch_vm.pid_table));
				exec_vmwrite16(VMX_LAST_PID_POINTER_IDX, vcpu->vm->arch_vm.max_lapic_id);
			}
		}
	}
}

/**
 * @}
 */
