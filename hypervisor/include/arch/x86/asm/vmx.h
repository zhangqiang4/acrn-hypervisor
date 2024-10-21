/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef VMX_H_
#define VMX_H_

/**
 * @defgroup hwmgmt_vmx hwmgmt.vmx
 * @ingroup hwmgmt
 * @brief Implement interfaces to VMX operation and VMCS structure
 *
 * This module provides basic control interfaces to enter/leave VMX operation, and interfaces
 * to load/clear VMCS for running a vCPU. It also provides read/write interfaces to VMCS fields.
 *
 * The logical processor needs to be put in VMX operation with vmxon instruction before executes
 * VMX specific operations and executes vmxoff to leave VMX operation. Every vCPU requires a current
 * VMCS to hold guest/host registers and control fields. VMM executes vmptrld to load a VMCS as
 * the current VMCS and executes vmclear to flush any cached data to VMCS memory.
 *
 * VMCS contains 16bit/32bit/64bit field values and the module provides 3 types of read/write
 * functions to access the data.
 *
 * @{
 */

/**
 * @file
 * @brief Declare VMX external APIs and defines VMCS control fields.
 *
 * This file defines VMCS control fields, related feature bits and data structures, also includes
 * external function prototypes and VMX exit reasons.
 */

/*
 * VMCS 16-Bit Control Fields
 * Refer to B.1.1 16-Bit Control Fields, Vol. 3, SDM 325462-078US for details.
 */
#define VMX_VPID			0x00000000U /**< Virtual-processor identifier */
#define VMX_POSTED_INTR_VECTOR		0x00000002U /**< Posted-interrupt notification vector */
#define VMX_LAST_PID_POINTER_IDX	0x00000008U /**< Last PID-pointer index */

/*
 * VMCS 16-Bit Guest-State Fields
 * Refer to B.1.2 16-Bit Guest-State Fields, Vol. 3, SDM 325462-078US for details.
 */
#define VMX_GUEST_ES_SEL	0x00000800U /**< Guest EC selector */
#define VMX_GUEST_CS_SEL	0x00000802U /**< Guest CS selector */
#define VMX_GUEST_SS_SEL	0x00000804U /**< Guest SS selector */
#define VMX_GUEST_DS_SEL	0x00000806U /**< Guest DS selector */
#define VMX_GUEST_FS_SEL	0x00000808U /**< Guest FS selector */
#define VMX_GUEST_GS_SEL	0x0000080aU /**< Guest GS selector */
#define VMX_GUEST_LDTR_SEL	0x0000080cU /**< Guest LDTR selector */
#define VMX_GUEST_TR_SEL	0x0000080eU /**< Guest task register selector */
#define VMX_GUEST_INTR_STATUS	0x00000810U /**< Guest interrupt status */
#define VMX_GUEST_PML_INDEX	0x00000812U /**< PML index */


/*
 * VMCS 16-Bit Host-State Fields
 * Refer to B.1.3 16-Bit Host-State Fields, Vol. 3, SDM 325462-078US for details.
 */
#define VMX_HOST_ES_SEL 	0x00000c00U /**< Host ES selector */
#define VMX_HOST_CS_SEL 	0x00000c02U /**< Host CS selector */
#define VMX_HOST_SS_SEL 	0x00000c04U /**< Host SS selector */
#define VMX_HOST_DS_SEL 	0x00000c06U /**< Host DS selector */
#define VMX_HOST_FS_SEL 	0x00000c08U /**< Host FS selector */
#define VMX_HOST_GS_SEL 	0x00000c0aU /**< Host GS selector */
#define VMX_HOST_TR_SEL 	0x00000c0cU /**< Host TR selector */


/*
 * VMCS 64-Bit Control Fields
 * Refer to B.2.1 64-Bit Control Fields, Vol. 3, SDM 325462-078US for details.
 */
#define VMX_IO_BITMAP_A_FULL	0x00002000U /**< Address of I/O bitmap A (full) */
#define VMX_IO_BITMAP_A_HIGH	0x00002001U /**< Address of I/O bitmap A (high) */
#define VMX_IO_BITMAP_B_FULL	0x00002002U /**< Address of I/O bitmap B (full) */
#define VMX_IO_BITMAP_B_HIGH	0x00002003U /**< Address of I/O bitmap B (high) */
#define VMX_MSR_BITMAP_FULL	0x00002004U /**< Address of MSR bitmaps (full) */
#define VMX_MSR_BITMAP_HIGH	0x00002005U /**< Address of MSR bitmaps (high) */
#define VMX_EXIT_MSR_STORE_ADDR_FULL	0x00002006U /**< VM-exit MSR-store address (full) */
#define VMX_EXIT_MSR_STORE_ADDR_HIGH	0x00002007U /**< VM-exit MSR-store address (high) */
#define VMX_EXIT_MSR_LOAD_ADDR_FULL	0x00002008U /**< VM-exit MSR-load address (full) */
#define VMX_EXIT_MSR_LOAD_ADDR_HIGH	0x00002009U /**< VM-exit MSR-load address (high) */
#define VMX_ENTRY_MSR_LOAD_ADDR_FULL	0x0000200aU /**< VM-entry MSR-load address (full) */
#define VMX_ENTRY_MSR_LOAD_ADDR_HIGH	0x0000200bU /**< VM-entry MSR-load address (high) */
#define VMX_EXECUTIVE_VMCS_PTR_FULL	0x0000200cU /**< Executive-VMCS pointer (full) */
#define VMX_EXECUTIVE_VMCS_PTR_HIGH	0x0000200dU /**< Executive-VMCS pointer (high) */
#define VMX_PML_ADDR_FULL		0x0000200EU /**< PML address (full) */
#define VMX_TSC_OFFSET_FULL		0x00002010U /**< TSC offset (full) */
#define VMX_TSC_OFFSET_HIGH		0x00002011U /**< TSC offset (high) */
#define VMX_VIRTUAL_APIC_PAGE_ADDR_FULL 0x00002012U /**< Virtual-APIC address (full) */
#define VMX_VIRTUAL_APIC_PAGE_ADDR_HIGH 0x00002013U /**< Virtual-APIC address (high) */
#define VMX_APIC_ACCESS_ADDR_FULL	0x00002014U /**< APIC-access address (full) */
#define VMX_APIC_ACCESS_ADDR_HIGH	0x00002015U /**< APIC-access address (high) */
#define VMX_PIR_DESC_ADDR_FULL		0x00002016U /**< Posted-interrupt descriptor address (full) */
#define VMX_PIR_DESC_ADDR_HIGH		0x00002017U /**< Posted-interrupt descriptor address (high) */
#define VMX_VM_FUNCTION_CTL_FULL	0x00002018U /**< VM-function controls (full) */
#define VMX_EPT_POINTER_FULL		0x0000201AU /**< EPT pointer (EPTP; full) */
#define VMX_EPT_POINTER_HIGH		0x0000201BU /**< EPT pointer (EPTP; high) */
#define VMX_EOI_EXIT0_FULL		0x0000201CU /**< EOI-exit bitmap 0 (EOI_EXIT0; full) */
#define VMX_EOI_EXIT0_HIGH		0x0000201DU /**< EOI-exit bitmap 0 (EOI_EXIT0; high) */
#define VMX_EOI_EXIT1_FULL		0x0000201EU /**< EOI-exit bitmap 1 (EOI_EXIT1; full) */
#define VMX_EOI_EXIT1_HIGH		0x0000201FU /**< EOI-exit bitmap 1 (EOI_EXIT1; high) */
#define VMX_EOI_EXIT2_FULL		0x00002020U /**< EOI-exit bitmap 2 (EOI_EXIT2; full) */
#define VMX_EOI_EXIT2_HIGH		0x00002021U /**< EOI-exit bitmap 2 (EOI_EXIT2; high) */
#define VMX_EOI_EXIT3_FULL		0x00002022U /**< EOI-exit bitmap 3 (EOI_EXIT3; full) */
#define VMX_EOI_EXIT3_HIGH		0x00002023U /**< EOI-exit bitmap 3 (EOI_EXIT3; high) */
#define VMX_EPT_LIST_ADDR_FULL		0x00002024U /**< EPTP-list address (full) */
#define VMX_VMREAD_BITMAP_FULL		0x00002026U /**< VMREAD-bitmap address (full) */
#define VMX_VMREAD_BITMAP_HIGH		0x00002027U /**< VMREAD-bitmap address (high) */
#define VMX_VMWRITE_BITMAP_FULL 	0x00002028U /**< VMWRITE-bitmap address (full) */
#define VMX_VMWRITE_BITMAP_HIGH 	0x00002029U /**< VMWRITE-bitmap address (high) */
#define VMX_VIR_EXCEPTION_INFO_FULL	0x0000202AU /**< Virtualization-exception information address (full) */
#define VMX_XSS_EXITING_BITMAP_FULL	0x0000202CU /**< XSS-exiting bitmap (full) */
#define VMX_XSS_EXITING_BITMAP_HIGH	0x0000202DU /**< XSS-exiting bitmap (high) */
#define VMX_TSC_MULTIPLIER_FULL 	0x00002032U /**< TSC multiplier (full) */
#define VMX_PROC_VM_EXEC_CONTROLS3_FULL 0x00002034U /**< Tertiary processor-based VM-execution controls (full) */
#define VMX_PROC_VM_EXEC_CONTROLS3_HIGH 0x00002035U /**< Tertiary processor-based VM-execution controls (high) */
#define VMX_PID_POINTER_TABLE_ADDR_FULL	0x00002042U /**< PID-pointer table address (full) */

/*
 * VMCS 64-Bit Read-Only Data Fields
 * Refer to B.2.2 64-Bit Read-Only Data Field, Vol. 3, SDM 325462-078US for details
 */
#define VMX_GUEST_PHYSICAL_ADDR_FULL    0x00002400U /**< Guest-physical address (full) */
#define VMX_GUEST_PHYSICAL_ADDR_HIGH    0x00002401U /**< Guest-physical address (high) */

/*
 * 64-Bit Guest-State Fields
 * Refer to B.2.3 64-Bit Guest-State Fields, Vol. 3, SDM 325462-078US for details
 */
#define VMX_VMS_LINK_PTR_FULL		0x00002800U /**< VMCS link pointer (full) */
#define VMX_VMS_LINK_PTR_HIGH		0x00002801U /**< VMCS link pointer (high) */
#define VMX_GUEST_IA32_DEBUGCTL_FULL	0x00002802U /**< Guest IA32_DEBUGCTL (full) */
#define VMX_GUEST_IA32_DEBUGCTL_HIGH	0x00002803U /**< Guest IA32_DEBUGCTL (high) */
#define VMX_GUEST_IA32_PAT_FULL 	0x00002804U /**< Guest IA32_PAT (full) */
#define VMX_GUEST_IA32_PAT_HIGH 	0x00002805U /**< Guest IA32_PAT (high) */
#define VMX_GUEST_IA32_EFER_FULL	0x00002806U /**< Guest IA32_EFER (full) */
#define VMX_GUEST_IA32_EFER_HIGH	0x00002807U /**< Guest IA32_EFER (high) */
#define VMX_GUEST_IA32_PERF_CTL_FULL	0x00002808U /**< Guest IA32_PERF_GLOBAL_CTRL (full) */
#define VMX_GUEST_IA32_PERF_CTL_HIGH	0x00002809U /**< Guest IA32_PERF_GLOBAL_CTRL (high) */
#define VMX_GUEST_PDPTE0_FULL		0x0000280AU /**< Guest PDPTE0 (full) */
#define VMX_GUEST_PDPTE0_HIGH		0x0000280BU /**< Guest PDPTE0 (high) */
#define VMX_GUEST_PDPTE1_FULL		0x0000280CU /**< Guest PDPTE1 (full) */
#define VMX_GUEST_PDPTE1_HIGH		0x0000280DU /**< Guest PDPTE1 (high) */
#define VMX_GUEST_PDPTE2_FULL		0x0000280EU /**< Guest PDPTE2 (full) */
#define VMX_GUEST_PDPTE2_HIGH		0x0000280FU /**< Guest PDPTE2 (high) */
#define VMX_GUEST_PDPTE3_FULL		0x00002810U /**< Guest PDPTE3 (full) */
#define VMX_GUEST_PDPTE3_HIGH		0x00002811U /**< Guest PDPTE3 (high) */
#define VMX_GUEST_IA32_BNDCFGS_FULL	0x00002812U /**< Guest IA32_BNDCFGS (full) */
#define VMX_GUEST_IA32_BNDCFGS_HIGH	0x00002813U /**< Guest IA32_BNDCFGS (high) */

/*
 * VMCS 64-Bit Host-State Fields
 * Refer to B.2.4 64-Bit Host-State Fields, Vol. 3, SDM 325462-078US for details.
 */
#define VMX_HOST_IA32_PAT_FULL		0x00002C00U /**< Host IA32_PAT (full) */
#define VMX_HOST_IA32_PAT_HIGH		0x00002C01U /**< Host IA32_PAT (high) */
#define VMX_HOST_IA32_EFER_FULL 	0x00002C02U /**< Host IA32_EFER (full) */
#define VMX_HOST_IA32_EFER_HIGH 	0x00002C03U /**< Host IA32_EFER (high) */
#define VMX_HOST_IA32_PERF_CTL_FULL	0x00002C04U /**< Host IA32_PERF_GLOBAL_CTRL (full) */
#define VMX_HOST_IA32_PERF_CTL_HIGH	0x00002C05U /**< Host IA32_PERF_GLOBAL_CTRL (high) */

/*
 * VMCS 32-Bit Control Fields
 * Refer to B.3.1 32-Bit Control Fields, Vol. 3, SDM 325462-078US for details.
 */
#define VMX_PIN_VM_EXEC_CONTROLS	0x00004000U /**< Pin-based VM-execution controls */
#define VMX_PROC_VM_EXEC_CONTROLS	0x00004002U /**< Primary processor-based VM-execution controls */
#define VMX_EXCEPTION_BITMAP		0x00004004U /**< Exception bitmap */
#define VMX_PF_ERROR_CODE_MASK		0x00004006U /**< Page-fault error-code mask */
#define VMX_PF_ERROR_CODE_MATCH 	0x00004008U /**< Page-fault error-code match */
#define VMX_CR3_TARGET_COUNT		0x0000400aU /**< CR3-target count */
#define VMX_EXIT_CONTROLS		0x0000400cU /**< Primary VM-exit controls */
#define VMX_EXIT_MSR_STORE_COUNT	0x0000400eU /**< VM-exit MSR-store count */
#define VMX_EXIT_MSR_LOAD_COUNT 	0x00004010U /**< VM-exit MSR-load count */
#define VMX_ENTRY_CONTROLS		0x00004012U /**< VM-entry controls */
#define VMX_ENTRY_MSR_LOAD_COUNT	0x00004014U /**< VM-entry MSR-load count */
#define VMX_ENTRY_INT_INFO_FIELD	0x00004016U /**< VM-entry interruption-information field */
#define VMX_ENTRY_EXCEPTION_ERROR_CODE	0x00004018U /**< VM-entry exception error code */
#define VMX_ENTRY_INSTR_LENGTH		0x0000401aU /**< VM-entry instruction length */
#define VMX_TPR_THRESHOLD		0x0000401cU /**< TPR threshold */
#define VMX_PROC_VM_EXEC_CONTROLS2	0x0000401EU /**< Secondary processor-based VM-execution controls */
#define VMX_PLE_GAP			0x00004020U /**< PLE_Gap */
#define VMX_PLE_WINDOW			0x00004022U /**< PLE_Window */

/*
 * VMCS 32-Bit Read-Only Data Fields
 * Refer to B.3.2 32-Bit Read-Only Data Fields, Vol. 3, SDM 325462-078US for details.
 */
#define VMX_INSTR_ERROR 		0x00004400U /**< VM-instruction error */
#define VMX_EXIT_REASON 		0x00004402U /**< Exit reason */
#define VMX_EXIT_INT_INFO		0x00004404U /**< VM-exit interruption information */
#define VMX_EXIT_INT_ERROR_CODE 	0x00004406U /**< VM-exit interruption error code */
#define VMX_IDT_VEC_INFO_FIELD		0x00004408U /**< IDT-vectoring information field */
#define VMX_IDT_VEC_ERROR_CODE		0x0000440aU /**< IDT-vectoring error code */
#define VMX_EXIT_INSTR_LEN		0x0000440cU /**< VM-exit instruction length */
#define VMX_INSTR_INFO			0x0000440eU /**< VM-exit instruction information */

/*
 * VMCS 32-Bit Guest-State Fields
 * Refer to B.3.3 32-Bit Guest-State Fields, Vol. 3, SDM 325462-078US for details.
 */
#define VMX_GUEST_ES_LIMIT		0x00004800U /**< Guest ES limit */
#define VMX_GUEST_CS_LIMIT		0x00004802U /**< Guest CS limit */
#define VMX_GUEST_SS_LIMIT		0x00004804U /**< Guest SS limit */
#define VMX_GUEST_DS_LIMIT		0x00004806U /**< Guest DS limit */
#define VMX_GUEST_FS_LIMIT		0x00004808U /**< Guest FS limit */
#define VMX_GUEST_GS_LIMIT		0x0000480aU /**< Guest GS limit */
#define VMX_GUEST_LDTR_LIMIT		0x0000480cU /**< Guest LDTR limit */
#define VMX_GUEST_TR_LIMIT		0x0000480eU /**< Guest TR limit */
#define VMX_GUEST_GDTR_LIMIT		0x00004810U /**< Guest GDTR limit */
#define VMX_GUEST_IDTR_LIMIT		0x00004812U /**< Guest IDTR limit */
#define VMX_GUEST_ES_ATTR		0x00004814U /**< Guest ES access rights */
#define VMX_GUEST_CS_ATTR		0x00004816U /**< Guest CS access rights */
#define VMX_GUEST_SS_ATTR		0x00004818U /**< Guest SS access rights */
#define VMX_GUEST_DS_ATTR		0x0000481aU /**< Guest DS access rights */
#define VMX_GUEST_FS_ATTR		0x0000481cU /**< Guest FS access rights */
#define VMX_GUEST_GS_ATTR		0x0000481eU /**< Guest GS access rights */
#define VMX_GUEST_LDTR_ATTR		0x00004820U /**< Guest LDTR access rights */
#define VMX_GUEST_TR_ATTR		0x00004822U /**< Guest TR access rights */
#define VMX_GUEST_INTERRUPTIBILITY_INFO 0x00004824U /**< Guest interruptibility state */
#define VMX_GUEST_ACTIVITY_STATE	0x00004826U /**< Guest activity state */
#define VMX_GUEST_SMBASE		0x00004828U /**< Guest SMBASE */
#define VMX_GUEST_IA32_SYSENTER_CS	0x0000482aU /**< Guest IA32_SYSENTER_CS */
#define VMX_GUEST_TIMER 		0x0000482EU /**< VMX-preemption timer value */

/*
 * 32-Bit Host-State Fields
 * Refer to B.3.4 32-Bit Host-State Field, Vol. 3, SDM 325462-078US for details.
 */
#define VMX_HOST_IA32_SYSENTER_CS	0x00004c00U /**< Host IA32_SYSENTER_CS */

/*
 * Natural-Width Control Fields
 * Refer to B.4.1 Natural-Width Control Fields, Vol. 3, SDM 325462-078US for details.
 */
#define VMX_CR0_GUEST_HOST_MASK 	0x00006000U /**< CR0 guest/host mask */
#define VMX_CR4_GUEST_HOST_MASK 	0x00006002U /**< CR4 guest/host mask */
#define VMX_CR0_READ_SHADOW		0x00006004U /**< CR0 read shadow */
#define VMX_CR4_READ_SHADOW		0x00006006U /**< CR4 read shadow */
#define VMX_CR3_TARGET_0		0x00006008U /**< CR3-target value 0 */
#define VMX_CR3_TARGET_1		0x0000600aU /**< CR3-target value 1 */
#define VMX_CR3_TARGET_2		0x0000600cU /**< CR3-target value 2 */
#define VMX_CR3_TARGET_3		0x0000600eU /**< CR3-target value 3 */


/*
 * Natural-Width Read-Only Data Fields
 * Refer to B.4.2 Natural-Width Read-Only Data Fields, Vol. 3, SDM 325462-078US for details.
 */
#define VMX_EXIT_QUALIFICATION		0x00006400U /**< Exit qualification */
#define VMX_IO_RCX			0x00006402U /**< I/O RCX */
#define VMX_IO_RSI			0x00006404U /**< I/O RSI */
#define VMX_IO_RDI			0x00006406U /**< I/O RDI */
#define VMX_IO_RIP			0x00006408U /**< I/O RIP */
#define VMX_GUEST_LINEAR_ADDR		0x0000640aU /**< Guest-linear address */

/*
 * Natural-Width Guest-State Fields
 * Refer to B.4.3 Natural-Width Guest-State Fields, Vol. 3, SDM 325462-078US for details.
 */
#define VMX_GUEST_CR0		0x00006800U /**< Guest CR0 */
#define VMX_GUEST_CR3		0x00006802U /**< Guest CR3 */
#define VMX_GUEST_CR4		0x00006804U /**< Guest CR4 */
#define VMX_GUEST_ES_BASE	0x00006806U /**< Guest ES base */
#define VMX_GUEST_CS_BASE	0x00006808U /**< Guest CS base */
#define VMX_GUEST_SS_BASE	0x0000680aU /**< Guest SS base */
#define VMX_GUEST_DS_BASE	0x0000680cU /**< Guest DS base */
#define VMX_GUEST_FS_BASE	0x0000680eU /**< Guest FS base */
#define VMX_GUEST_GS_BASE	0x00006810U /**< Guest GS base */
#define VMX_GUEST_LDTR_BASE	0x00006812U /**< Guest LDTR base */
#define VMX_GUEST_TR_BASE	0x00006814U /**< Guest TR base */
#define VMX_GUEST_GDTR_BASE	0x00006816U /**< Guest GDTR base */
#define VMX_GUEST_IDTR_BASE	0x00006818U /**< Guest IDTR base */
#define VMX_GUEST_DR7		0x0000681aU /**< Guest DR7 */
#define VMX_GUEST_RSP		0x0000681cU /**< Guest RSP */
#define VMX_GUEST_RIP		0x0000681eU /**< Guest RIP */
#define VMX_GUEST_RFLAGS	0x00006820U /**< Guest RFLAGS */
#define VMX_GUEST_PENDING_DEBUG_EXCEPT	0x00006822U /**< Guest pending debug exceptions */
#define VMX_GUEST_IA32_SYSENTER_ESP	0x00006824U /**< Guest IA32_SYSENTER_ESP */
#define VMX_GUEST_IA32_SYSENTER_EIP	0x00006826U /**< Guest IA32_SYSENTER_EIP */

/*
 * Natural-Width Host-State Fields
 * Refer to B.4.4 Natural-Width Host-State Fields, Vol. 3, SDM 325462-078US for details.
 */
#define VMX_HOST_CR0		0x00006c00U /**< Host CR0 */
#define VMX_HOST_CR3		0x00006c02U /**< Host CR3 */
#define VMX_HOST_CR4		0x00006c04U /**< Host CR4 */
#define VMX_HOST_FS_BASE	0x00006c06U /**< Host FS base */
#define VMX_HOST_GS_BASE	0x00006c08U /**< Host GS base */
#define VMX_HOST_TR_BASE	0x00006c0aU /**< Host TR base */
#define VMX_HOST_GDTR_BASE	0x00006c0cU /**< Host GDTR base */
#define VMX_HOST_IDTR_BASE	0x00006c0eU /**< Host IDTR base */
#define VMX_HOST_IA32_SYSENTER_ESP 0x00006c10U /**< Host IA32_SYSENTER_ESP */
#define VMX_HOST_IA32_SYSENTER_EIP 0x00006c12U /**< Host IA32_SYSENTER_EIP */
#define VMX_HOST_RSP		0x00006c14U /**< Host RSP */
#define VMX_HOST_RIP		0x00006c16U /**< Host RIP */

/*
 * Basic VM exit reasons
 * Refer to APPENDIX C VMX BASIC EXIT REASONS, Vol. 3, SDM 325462-078US for details.
 */
#define VMX_EXIT_REASON_EXCEPTION_OR_NMI	0x00000000U	/**< Exception or non-maskable interrupt (NMI) */
#define VMX_EXIT_REASON_EXTERNAL_INTERRUPT	0x00000001U	/**< External interrupt */
#define VMX_EXIT_REASON_TRIPLE_FAULT		0x00000002U	/**< Triple fault */
#define VMX_EXIT_REASON_INIT_SIGNAL		0x00000003U	/**< INIT signal */
#define VMX_EXIT_REASON_STARTUP_IPI		0x00000004U	/**< Start-up IPI (SIPI) */
#define VMX_EXIT_REASON_IO_SMI			0x00000005U	/**< I/O system-management interrupt (SMI) */
#define VMX_EXIT_REASON_OTHER_SMI		0x00000006U	/**< Other SMI */
#define VMX_EXIT_REASON_INTERRUPT_WINDOW	0x00000007U	/**< Interrupt window */
#define VMX_EXIT_REASON_NMI_WINDOW		0x00000008U	/**< NMI window */
#define VMX_EXIT_REASON_TASK_SWITCH		0x00000009U	/**< Task switch */
#define VMX_EXIT_REASON_CPUID			0x0000000AU	/**< CPUID */
#define VMX_EXIT_REASON_GETSEC			0x0000000BU	/**< GETSEC */
#define VMX_EXIT_REASON_HLT			0x0000000CU	/**< HLT */
#define VMX_EXIT_REASON_INVD			0x0000000DU	/**< INVD */
#define VMX_EXIT_REASON_INVLPG			0x0000000EU	/**< INVLPG */
#define VMX_EXIT_REASON_RDPMC			0x0000000FU	/**< RDPMC */
#define VMX_EXIT_REASON_RDTSC			0x00000010U	/**< RDTSC */
#define VMX_EXIT_REASON_RSM			0x00000011U	/**< RSM */
#define VMX_EXIT_REASON_VMCALL			0x00000012U	/**< VMCALL */
#define VMX_EXIT_REASON_VMCLEAR 		0x00000013U	/**< VMCLEAR */
#define VMX_EXIT_REASON_VMLAUNCH		0x00000014U	/**< VMLAUNCH */
#define VMX_EXIT_REASON_VMPTRLD 		0x00000015U	/**< VMPTRLD */
#define VMX_EXIT_REASON_VMPTRST 		0x00000016U	/**< VMPTRST */
#define VMX_EXIT_REASON_VMREAD			0x00000017U	/**< VMREAD */
#define VMX_EXIT_REASON_VMRESUME		0x00000018U	/**< VMRESUME */
#define VMX_EXIT_REASON_VMWRITE 		0x00000019U	/**< VMWRITE */
#define VMX_EXIT_REASON_VMXOFF			0x0000001AU	/**< VMXOFF */
#define VMX_EXIT_REASON_VMXON			0x0000001BU	/**< VMXON */
#define VMX_EXIT_REASON_CR_ACCESS		0x0000001CU	/**< Control-register accesses */
#define VMX_EXIT_REASON_DR_ACCESS		0x0000001DU	/**< MOV DR */
#define VMX_EXIT_REASON_IO_INSTRUCTION		0x0000001EU	/**< I/O instruction */
#define VMX_EXIT_REASON_RDMSR			0x0000001FU	/**< RDMSR */
#define VMX_EXIT_REASON_WRMSR			0x00000020U	/**< WRMSR */
#define VMX_EXIT_REASON_ENTRY_FAILURE_INVALID_GUEST_STATE 0x00000021U	/**< VM-entry failure due to invalid state */
#define VMX_EXIT_REASON_ENTRY_FAILURE_MSR_LOADING 0x00000022U	/**< VM-entry failure due to MSR loading */
#define VMX_EXIT_REASON_MWAIT			0x00000024U	/**< MWAIT */
#define VMX_EXIT_REASON_MONITOR_TRAP		0x00000025U	/**< Monitor trap flag */
#define VMX_EXIT_REASON_MONITOR 		0x00000027U	/**< MONITOR */
#define VMX_EXIT_REASON_PAUSE			0x00000028U	/**< PAUSE */
#define VMX_EXIT_REASON_ENTRY_FAILURE_MACHINE_CHECK 0x00000029U	/**< VM-entry failure due to machine-check event */
#define VMX_EXIT_REASON_TPR_BELOW_THRESHOLD	0x0000002BU	/**< TPR below threshold */
#define VMX_EXIT_REASON_APIC_ACCESS		0x0000002CU	/**< APIC access */
#define VMX_EXIT_REASON_VIRTUALIZED_EOI 	0x0000002DU	/**< Virtualized EOI */
#define VMX_EXIT_REASON_GDTR_IDTR_ACCESS	0x0000002EU	/**< Access to GDTR or IDTR */
#define VMX_EXIT_REASON_LDTR_TR_ACCESS		0x0000002FU	/**< Access to LDTR or TR */
#define VMX_EXIT_REASON_EPT_VIOLATION		0x00000030U	/**< EPT violation */
#define VMX_EXIT_REASON_EPT_MISCONFIGURATION	0x00000031U	/**< EPT misconfiguration */
#define VMX_EXIT_REASON_INVEPT			0x00000032U	/**< INVEPT */
#define VMX_EXIT_REASON_RDTSCP			0x00000033U	/**< RDTSCP */
#define VMX_EXIT_REASON_VMX_PREEMPTION_TIMER_EXPIRED 0x00000034U /**< VMX-preemption timer expired */
#define VMX_EXIT_REASON_INVVPID 		0x00000035U	/**< INVVPID */
#define VMX_EXIT_REASON_WBINVD			0x00000036U	/**< WBINVD or WBNOINVD */
#define VMX_EXIT_REASON_XSETBV			0x00000037U	/**< XSETBV */
#define VMX_EXIT_REASON_APIC_WRITE		0x00000038U	/**< APIC write */
#define VMX_EXIT_REASON_RDRAND			0x00000039U	/**< RDRAND */
#define VMX_EXIT_REASON_INVPCID 		0x0000003AU	/**< INVPCID */
#define VMX_EXIT_REASON_VMFUNC			0x0000003BU	/**< VMFUNC */
#define VMX_EXIT_REASON_ENCLS			0x0000003CU	/**< ENCLS */
#define VMX_EXIT_REASON_RDSEED			0x0000003DU	/**< RDSEED */
#define VMX_EXIT_REASON_PAGE_MODIFICATION_LOG_FULL 0x0000003EU	/**< Page-modification log full */
#define VMX_EXIT_REASON_XSAVES			0x0000003FU	/**< XSAVES */
#define VMX_EXIT_REASON_XRSTORS 		0x00000040U	/**< XRSTORS */
#define VMX_EXIT_REASON_LOADIWKEY		0x00000045U	/**< LOADIWKEY */
#define NR_VMX_EXIT_REASONS			70U		/**< Maximum Exit Reasons supported. */

/*
 * VMX execution control bits (pin based)
 * Refer to 25.6.1 Pin-Based VM-Execution Controls, Vol. 3, SDM 325462-078US for details.
 */
#define VMX_PINBASED_CTLS_IRQ_EXIT	(1U<<0U)	/**< External-interrupt exiting */
#define VMX_PINBASED_CTLS_NMI_EXIT	(1U<<3U)	/**< NMI exiting */
#define VMX_PINBASED_CTLS_VIRT_NMI	(1U<<5U)	/**< Virtual NMIs */
#define VMX_PINBASED_CTLS_ENABLE_PTMR	(1U<<6U)	/**< Activate VMX-preemption timer */
#define VMX_PINBASED_CTLS_POST_IRQ	(1U<<7U)	/**< Process posted interrupts */

/*
 * VMX execution control bits (processor based)
 * Refer to 25.6.2 Processor-Based VM-Execution Controls, Vol. 3, SDM 325462-078US for details.
 */
#define VMX_PROCBASED_CTLS_IRQ_WIN	(1U<<2U)	/**< Interrupt-window exiting */
#define VMX_PROCBASED_CTLS_TSC_OFF	(1U<<3U)	/**< Use TSC offsetting */
#define VMX_PROCBASED_CTLS_HLT		(1U<<7U)	/**< HLT exiting */
#define VMX_PROCBASED_CTLS_INVLPG	(1U<<9U)	/**< INVLPG exiting */
#define VMX_PROCBASED_CTLS_MWAIT	(1U<<10U)	/**< MWAIT exiting */
#define VMX_PROCBASED_CTLS_RDPMC	(1U<<11U)	/**< RDPMC exiting */
#define VMX_PROCBASED_CTLS_RDTSC	(1U<<12U)	/**< RDTSC exiting */
#define VMX_PROCBASED_CTLS_CR3_LOAD	(1U<<15U)	/**< CR3-load exiting */
#define VMX_PROCBASED_CTLS_CR3_STORE	(1U<<16U)	/**< CR3-store exiting */
#define VMX_PROCBASED_CTLS_TERTIARY	(1U<<17U)	/**< Activate tertiary controls */
#define VMX_PROCBASED_CTLS_CR8_LOAD	(1U<<19U)	/**< CR8-load exiting */
#define VMX_PROCBASED_CTLS_CR8_STORE	(1U<<20U)	/**< CR8-store exiting */
#define VMX_PROCBASED_CTLS_TPR_SHADOW	(1U<<21U)	/**< Use TPR shadow */
#define VMX_PROCBASED_CTLS_NMI_WINEXIT	(1U<<22U)	/**< NMI-window exiting */
#define VMX_PROCBASED_CTLS_MOV_DR	(1U<<23U)	/**< MOV-DR exiting */
#define VMX_PROCBASED_CTLS_UNCOND_IO	(1U<<24U)	/**< Unconditional I/O exiting */
#define VMX_PROCBASED_CTLS_IO_BITMAP	(1U<<25U)	/**< Use I/O bitmaps */
#define VMX_PROCBASED_CTLS_MON_TRAP	(1U<<27U)	/**< Monitor trap flag */
#define VMX_PROCBASED_CTLS_MSR_BITMAP	(1U<<28U)	/**< Use MSR bitmaps */
#define VMX_PROCBASED_CTLS_MONITOR	(1U<<29U)	/**< MONITOR exiting */
#define VMX_PROCBASED_CTLS_PAUSE	(1U<<30U)	/**< PAUSE exiting */
#define VMX_PROCBASED_CTLS_SECONDARY	(1U<<31U)	/**< Activate secondary controls */

/*
 * VMX execution control bits (secondary processor-based)
 * Refer to 25.6.2 Processor-Based VM-Execution Controls, Vol. 3, SDM 325462-078US for details.
 */
#define VMX_PROCBASED_CTLS2_VAPIC	(1U<<0U)	/**< Virtualize APIC accesses */
#define VMX_PROCBASED_CTLS2_EPT 	(1U<<1U)	/**< Enable EPT */
#define VMX_PROCBASED_CTLS2_DESC_TABLE	(1U<<2U)	/**< Descriptor-table exiting */
#define VMX_PROCBASED_CTLS2_RDTSCP	(1U<<3U)	/**< Enable RDTSCP */
#define VMX_PROCBASED_CTLS2_VX2APIC	(1U<<4U)	/**< Virtualize x2APIC mode */
#define VMX_PROCBASED_CTLS2_VPID	(1U<<5U)	/**< Enable VPID */
#define VMX_PROCBASED_CTLS2_WBINVD	(1U<<6U)	/**< WBINVD exiting */
#define VMX_PROCBASED_CTLS2_UNRESTRICT	(1U<<7U)	/**< Unrestricted guest */
#define VMX_PROCBASED_CTLS2_VAPIC_REGS	(1U<<8U)	/**< APIC-register virtualization */
#define VMX_PROCBASED_CTLS2_VIRQ	(1U<<9U)	/**< Virtual-interrupt delivery */
#define VMX_PROCBASED_CTLS2_PAUSE_LOOP	(1U<<10U)	/**< PAUSE-loop exiting */
#define VMX_PROCBASED_CTLS2_RDRAND	(1U<<11U)	/**< RDRAND exiting */
#define VMX_PROCBASED_CTLS2_INVPCID	(1U<<12U)	/**< Enable INVPCID */
#define VMX_PROCBASED_CTLS2_VM_FUNCS	(1U<<13U)	/**< Enable VM functions */
#define VMX_PROCBASED_CTLS2_VMCS_SHADW	(1U<<14U)	/**< VMCS shadowing */
#define VMX_PROCBASED_CTLS2_ENCLS_EXIT	(1U<<15U)	/**< Enable ENCLS exiting */
#define VMX_PROCBASED_CTLS2_RDSEED	(1U<<16U)	/**< RDSEED exiting */
#define VMX_PROCBASED_CTLS2_PML 	(1U<<17U)	/**< Enable PML */
#define VMX_PROCBASED_CTLS2_EPT_VE	(1U<<18U)	/**< EPT-violation \#VE */
#define VMX_PROCBASED_CTLS2_XSVE_XRSTR	(1U<<20U)	/**< Enable XSAVES/XRSTORS */
#define VMX_PROCBASED_CTLS2_PT_USE_GPA	(1U<<22U)	/**< Mode-based execute control for EPT */
#define VMX_PROCBASED_CTLS2_TSC_SCALING (1U<<25U)	/**< Use TSC scaling */
#define VMX_PROCBASED_CTLS2_UWAIT_PAUSE (1U<<26U)	/**< Enable user wait and pause */
#define VMX_PROCBASED_CTLS2_PCONFIG	(1U<<27U)	/**< Enable PCONFIG */
#define VMX_PROCBASED_CTLS2_ENCLV_EXIT	(1U<<28U)	/**< Enable ENCLV exiting */

/*
 * VMX execution control bits (tertiary processor-based)
 * Refer to 25.6.2 Processor-Based VM-Execution Controls, Vol. 3, SDM 325462-078US for details.
 */
#define VMX_PROCBASED_CTLS3_LOADIWKEY	(1U<<0U)	/**< LOADIWKEY exiting */
#define VMX_PROCBASED_CTLS3_IPI_VIRT	(1U<<4U)	/**< IPI virtualization */

/*
 * Bits for MSR_IA32_VMX_EPT_VPID_CAP MSR
 * Refer to A.10 VPID AND EPT CAPABILITIES, Vol. 3, SDM 325462-078US for details.
 */
#define VMX_EPT_EXECUTE_ONLY		(1UL << 0U)	/**< Support execute-only translations by EPT */
#define VMX_EPT_PAGE_WALK_4		(1UL << 6U)	/**< Support for a page-walk length of 4 */
#define VMX_EPT_PAGE_WALK_5		(1UL << 7U)	/**< Support for a page-walk length of 5 */
#define VMX_EPTP_UC			(1UL << 8U)	/**< Support UC EPT paging-structure memory type */
#define VMX_EPTP_WB			(1UL << 14U)	/**< Support WB EPT paging-structure memory type */
#define VMX_EPT_2MB_PAGE		(1UL << 16U)	/**< Support EPT PDE to map a 2-Mbyte page */
#define VMX_EPT_1GB_PAGE		(1UL << 17U)	/**< Support EPT PDPTE to map a 1-Gbyte page */
#define VMX_EPT_INVEPT  		(1UL << 20U)	/**< Support INVEPT instruction */
#define VMX_EPT_AD			(1UL << 21U)	/**< Support EPT accessed and dirty flags */
#define VMX_EPT_INVEPT_SINGLE_CONTEXT	(1UL << 25U)	/**< Support single-context INVEPT type */
#define VMX_EPT_INVEPT_GLOBAL_CONTEXT	(1UL << 26U)	/**< Support all-context INVEPT type */
#define VMX_VPID_INVVPID			(1UL << 32U)	/**< Support INVVPID instruction */
#define VMX_VPID_INVVPID_INDIVIDUAL_ADDR	(1UL << 40U)	/**< Support individual-address INVVPID type */
#define VMX_VPID_INVVPID_SINGLE_CONTEXT 	(1UL << 41U)	/**< Support single-context INVVPID type */
#define VMX_VPID_INVVPID_GLOBAL_CONTEXT 	(1UL << 42U)	/**< Support all-context INVVPID type */
#define VMX_VPID_INVVPID_SINGLE_NON_GLOBAL	(1UL << 43U)	/**< Support single-context-non-global type */

/*
 * INVPCID types
 * Refer to INVPCID—Invalidate Process-Context Identifier, Vol. 2, SDM 325462-078US for details.
 */
#define VMX_VPID_TYPE_INDIVIDUAL_ADDR	0UL	/**< Individual-address INVVPID type */
#define VMX_VPID_TYPE_SINGLE_CONTEXT	1UL	/**< Single-context INVVPID type */
#define VMX_VPID_TYPE_ALL_CONTEXT	2UL	/**< All-context INVVPID type */
#define VMX_VPID_TYPE_SINGLE_NON_GLOBAL	3UL	/**< Single-context-non-globals INVVPID type */

/*
 * Macros for EPT/EPTP configuration
 * Refer to 25.6.11 Extended-Page-Table Pointer (EPTP) and 29.3.2 EPT Translation Mechanism,
 * Vol. 3, SDM 325462-078US for details.
 */
#define VMX_EPT_MT_EPTE_SHIFT		3U		/**< EPT leaf entry memory type shift */
#define VMX_EPTP_PWL_MASK		0x38UL		/**< EPTP page-walk field mask */
#define VMX_EPTP_PWL_4  		0x18UL		/**< 4-level setting value for EPTP */
#define VMX_EPTP_PWL_5  		0x20UL		/**< 5-level setting value for EPTP */
#define VMX_EPTP_AD_ENABLE_BIT  	(1UL << 6U)	/**< Accessed and dirty flag shift for EPTP */
#define VMX_EPTP_MT_MASK		0x7UL		/**< EPT leaf entry memory type mask */
#define VMX_EPTP_MT_WB  		0x6UL		/**< WB memory type */
#define VMX_EPTP_MT_UC  		0x0UL		/**< UC memory type */

/*
 * VMX Exit Controls
 * Refer to 25.7.1 VM-Exit Controls, Vol. 3, SDM 325462-078US for details.
 */
#define VMX_EXIT_CTLS_SAVE_DBG		(1U<<2U)	/**< Save debug controls */
#define VMX_EXIT_CTLS_HOST_ADDR64	(1U<<9U)	/**< Host address-space size */
#define VMX_EXIT_CTLS_LOAD_PERF 	(1U<<12U)	/**< Load IA32_PERF_GLOBAL_CTRL */
#define VMX_EXIT_CTLS_ACK_IRQ		(1U<<15U)	/**< Acknowledge interrupt on exit */
#define VMX_EXIT_CTLS_SAVE_PAT		(1U<<18U)	/**< Save IA32_PAT */
#define VMX_EXIT_CTLS_LOAD_PAT		(1U<<19U)	/**< Load IA32_PAT */
#define VMX_EXIT_CTLS_SAVE_EFER 	(1U<<20U)	/**< Save IA32_EFER */
#define VMX_EXIT_CTLS_LOAD_EFER 	(1U<<21U)	/**< Load IA32_EFER */
#define VMX_EXIT_CTLS_SAVE_PTMR 	(1U<<22U)	/**< Save VMX-preemption timer value */

/*
 * VMX Entry Controls
 * Refer to 25.8.1 VM-Entry Controls, Vol. 3, SDM 325462-078US for details.
 */
#define VMX_ENTRY_CTLS_LOAD_DBG 	(1U<<2U)	/**< Load debug controls */
#define VMX_ENTRY_CTLS_IA32E_MODE	(1U<<9U)	/**< IA-32e mode guest */
#define VMX_ENTRY_CTLS_ENTRY_SMM	(1U<<10U)	/**< Entry to SMM */
#define VMX_ENTRY_CTLS_DEACT_DUAL	(1U<<11U)	/**< Deactivate dual-monitor treatment */
#define VMX_ENTRY_CTLS_LOAD_PERF	(1U<<13U)	/**< Load IA32_PERF_GLOBAL_CTRL */
#define VMX_ENTRY_CTLS_LOAD_PAT 	(1U<<14U)	/**< Load IA32_PAT */
#define VMX_ENTRY_CTLS_LOAD_EFER	(1U<<15U)	/**< Load IA32_EFER */

/*
 * VM-Exit IDT-Vectoring Information
 * Refer to 25.9.3 Information for VM Exits That Occur During Event Delivery, Vol. 3, SDM 325462-078US for details.
 */
#define VMX_INT_INFO_ERR_CODE_VALID	(1U<<11U)	/**< Error code valid mask (0 = invalid; 1 = valid) */
#define VMX_INT_INFO_VALID		(1U<<31U)	/**< Interrupt information valid mask */
#define VMX_INT_TYPE_MASK		(0x700U)	/**< Interrupt type mask */
#define VMX_INT_TYPE_EXT_INT		0U		/**< External interrupt */
#define VMX_INT_TYPE_NMI		2U		/**< Non-maskable interrupt (NMI) */
#define VMX_INT_TYPE_HW_EXP		3U		/**< Hardware exception */
#define VMX_INT_TYPE_SW_EXP		6U		/**< Software exception */

/**
 * @brief Data structure that contains Posted-Interrupt related settings
 *
 * This structure is used to configure Posted Interrupt Descriptor (PID) settings so that
 * PI can work properly with hardware.
 *
 * @consistency N/A
 * @alignment 64
 *
 * @remark N/A
 */
struct pi_desc {
	uint64_t pir[4]; /**< Posted-Interrupt requests bitmap, one bit per requested vector */

	/**
	 * @brief Union to hold separated Posted-Interrupt fields and a counterpart 64bit value.
	 */
	union {
		/**
		 * @brief Struct to hold Posted-Interrupt delivery status and configuration.
		 */
		struct {
			uint16_t on:1; /**< Outstanding notification. */

			uint16_t sn:1; /**< Suppress notification for non-urgent interrupts. */

			uint16_t rsvd_1:14; /**< Reserved bits. */

			uint8_t nv; /**< Physical vector used for notification event.*/

			uint8_t rsvd_2; /**< Reserved bit. */

			uint32_t ndst; /**< Notification destination, a physical LAPIC ID. */
		} bits;

		uint64_t value; /**< Counterpart value for above bits. */
	} control;

	uint32_t rsvd[6]; /**< Reserved bits. */
} __aligned(64);


/* External Interfaces */
void vmx_on(void);
void vmx_off(void);

uint16_t exec_vmread16(uint32_t field);
uint32_t exec_vmread32(uint32_t field);
uint64_t exec_vmread64(uint32_t field_full);
#define exec_vmread exec_vmread64	/**< Alias for reading natural-width VMCS field */

void exec_vmwrite16(uint32_t field, uint16_t value);
void exec_vmwrite32(uint32_t field, uint32_t value);
void exec_vmwrite64(uint32_t field_full, uint64_t value);
#define exec_vmwrite exec_vmwrite64	/**< Alias for writing natural-width VMCS field */

void clear_va_vmcs(const uint8_t *vmcs_va);
void load_va_vmcs(const uint8_t *vmcs_va);

void init_cr0_cr4_flexible_bits(void);

uint32_t check_vmx_ctrl_64(uint32_t msr, uint64_t ctrl_req);

#define POSTED_INTR_ON  0U	/**< Outstanding notification bit index of pi_desc.control field. */
#endif /* VMX_H_ */

/**
 * @}
 */
