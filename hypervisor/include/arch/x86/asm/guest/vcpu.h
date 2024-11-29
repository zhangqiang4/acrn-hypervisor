/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef VCPU_H
#define VCPU_H

#ifndef ASSEMBLER

#include <acrn_common.h>
#include <asm/guest/guest_memory.h>
#include <asm/guest/virtual_cr.h>
#include <asm/guest/vlapic.h>
#include <asm/guest/vmtrr.h>
#include <schedule.h>
#include <event.h>
#include <io_req.h>
#include <asm/msr.h>
#include <asm/cpu.h>
#include <asm/guest/instr_emul.h>
#include <asm/vmx.h>
#include <asm/vm_config.h>

/**
 * @defgroup vp-base_vcpu vp-base.vcpu
 * @ingroup vp-base
 * @brief Implement vCPU related functions
 *
 * This module provides functions to operate vCPU registers, states, creation, initialization etc.
 *
 * @{
 */

/**
 * @file
 * @brief Declare vCPU external requests, data structures and APIs.
 *
 * This file defines vCPU related macros, data structures, types and external APIs to other modules.
 * Two main data structures are defined : acrn_vcpu and acrn_vcpu_arch, the two structures holds all
 * vCPU related run-time information and states for hypervisor to manage a vCPU during a VM's lifecycle.
 * The former holds the generic part of vCPU states and is the container for the latter to maintain
 * CPU architecture related information and states. External modules refer to the vCPU states and
 * modify the states via the APIs defined in this header file.
 */

 /**
  * @defgroup virt_int_injection Event ID supported for virtual interrupt injection
  *
  * This is a group that includes Event ID supported for virtual interrupt injection.
  * @{
  */


#define ACRN_REQUEST_EXCP			0U /**< Request to inject an exception */
#define ACRN_REQUEST_EVENT			1U /**< Request to handle a vlapic event */
#define ACRN_REQUEST_EXTINT			2U /**< Request to inject an external interrupt */
#define ACRN_REQUEST_NMI			3U /**< Request to inject an NMI */
#define ACRN_REQUEST_EOI_EXIT_BITMAP_UPDATE	4U /**< Request to update EOI bitmaps */
#define ACRN_REQUEST_EPT_FLUSH			5U /**< Request to flush ETP TLBs and page caches */
#define ACRN_REQUEST_TRP_FAULT			6U /**< Request to handle triple-fault exception */
#define ACRN_REQUEST_VPID_FLUSH 		7U /**< Request to do VPID based flush */
#define ACRN_REQUEST_INIT_VMCS			8U /**< Request to initialize VMCS structure */
#define ACRN_REQUEST_WAIT_WBINVD		9U /**< Request to wait for WBINVD completion */
#define ACRN_REQUEST_SPLIT_LOCK 		10U /**< Request to wait for completion of lock emulation */
#define ACRN_REQUEST_SMP_CALL			11U /**< Request to execute SMP function-call */
#define ACRN_REQUEST_ENTER_REFRIGERATOR 	12U /**< Request to freeze vCPU execution */

/**
 * @}
 */
/* End of virt_int_injection */

/**
 * @brief Save segment settings to extended context
 *
 * This macro is used to save guest segment, i.e., VMX_GUEST_{CS,SS,DS,ES,FS,GS,TR,LDTR}, settings and
 * attribute to vCPU extended context.
 *
 * Normal case behaviors:
 * - Save the VMCS field data to extended context
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[inout] seg The segment for which to save the settings and attributes.
 * @param[in] SEG_NAME VMCS field name of the segment
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark SEG_NAME is pre-defined VMCS field name.
 */
#define save_segment(seg, SEG_NAME)				\
{								\
	(seg).selector = exec_vmread16(SEG_NAME##_SEL);		\
	(seg).base = exec_vmread(SEG_NAME##_BASE);		\
	(seg).limit = exec_vmread32(SEG_NAME##_LIMIT);		\
	(seg).attr = exec_vmread32(SEG_NAME##_ATTR);		\
}

/**
 * @brief Load segment settings and attributes to VMCS fields
 *
 * This macro is used to load guest segment, i.e., VMX_GUEST_{CS,SS,DS,ES,FS,GS,TR,LDTR}, settings and
 * attribute to related VMCS fields.
 *
 * Normal case behaviors:
 * - Load the VMCS field with the data from extended context
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[in] seg The segment for which to load the settings and attributes.
 * @param[in] SEG_NAME VMCS field name of the segment
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark SEG_NAME is pre-defined VMCS field name.
 */
#define load_segment(seg, SEG_NAME)				\
{								\
	exec_vmwrite16(SEG_NAME##_SEL, (seg).selector);		\
	exec_vmwrite(SEG_NAME##_BASE, (seg).base);		\
	exec_vmwrite32(SEG_NAME##_LIMIT, (seg).limit);		\
	exec_vmwrite32(SEG_NAME##_ATTR, (seg).attr);		\
}

/* Define segments constants for guest */
#define REAL_MODE_BSP_INIT_CODE_SEL     (0xf000U) /**< Real-mode code segment selector */
#define REAL_MODE_DATA_SEG_AR           (0x0093U) /**< Real-mode data segment attribute */
#define REAL_MODE_CODE_SEG_AR           (0x009fU) /**< Real-mode code segment attribute */
#define PROTECTED_MODE_DATA_SEG_AR      (0xc093U) /**< Protected-mode data segment attribute */
#define PROTECTED_MODE_CODE_SEG_AR      (0xc09bU) /**< Protected-mode code segment attribute */
#define REAL_MODE_SEG_LIMIT             (0xffffU) /**< Real-mode segment length limit */
#define PROTECTED_MODE_SEG_LIMIT        (0xffffffffU) /**< Protected-mode segment length limit */
#define DR7_INIT_VALUE                  (0x400UL) /**< DR7 initial value */
#define LDTR_AR                         (0x0082U) /**< LDTR register attribute */
#define TR_AR                           (0x008bU) /**< TR register attribute */

/**
 * @brief Iterate each non-offline vCPU of a VM
 *
 * This macro is used to iterate each non-offline vCPU of a VM so as to operate on each vCPU.
 *
 * Normal case behaviors:
 * - Iterate each non-offline vCPU for a VM
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[in] idx Variable to hold vCPU index
 * @param[in] vm Target VM to iterate the vCPUs
 * @param[in] vcpu Variable to hold target vCPU pointer
 *
 * @return None
 *
 * @pre vm != NULL
 *
 * @post N/A
 */
#define foreach_vcpu(idx, vm, vcpu)				\
	for ((idx) = 0U, (vcpu) = &((vm)->hw.vcpu_array[(idx)]);	\
		(idx) < (vm)->hw.created_vcpus;			\
		(idx)++, (vcpu) = &((vm)->hw.vcpu_array[(idx)])) \
		if ((vcpu)->state != VCPU_OFFLINE)
/**
 * @brief vCPU state definition
 *
 * This enum type is used to differentiate vCPU states during vCPU lifecycle. A vCPU is marked as INIT state
 * after created. When it's brought up running, its state is set to RUNNING. If vCPU is offline, its state
 * is OFFLINE. If a VM is paused or vCPU encounters errors, its state is set to ZOMBIE.
 */
enum vcpu_state {
	VCPU_OFFLINE = 0U, /**< vCPU is in offline state */
	VCPU_INIT, /**< vCPU is in created or reset state */
	VCPU_RUNNING, /**< vCPU is in running state */
	VCPU_ZOMBIE, /**< vCPU is in blocked state */
};

/**
 * @brief CPU running mode definition
 *
 * This enum type defines CPU mode it can operate, refer to Chapter 2.2 MODES OF OPERATION, Vol.3,
 * SDM 325462-078US for details.
 */
enum vm_cpu_mode {
	CPU_MODE_REAL, /**< CPU Real-address mode */
	CPU_MODE_PROTECTED, /**< CPU Protected mode */
	CPU_MODE_COMPATIBILITY, /**< CPU IA-32e compatibility mode */
	CPU_MODE_64BIT, /**< CPU IA-32e mode*/
};

#define	VCPU_EVENT_IOREQ		0 /**< IO related event */
#define	VCPU_EVENT_VIRTUAL_INTERRUPT	1 /**< Virtual interrupt related event */
#define	VCPU_EVENT_SYNC_WBINVD		2 /**< WBINVD instruction related sync event */
#define	VCPU_EVENT_SPLIT_LOCK		3 /**< LOCK instruction related sync event */
#define	VCPU_EVENT_THAW			4 /**< Thaw vCPU event */
#define	VCPU_EVENT_NUM			5 /**< Max event supported */


enum reset_mode;

#define NUM_EMULATED_MSRS	40U /**< Number of emulated MSRs */

#define EOI_EXIT_BITMAP_SIZE	256U /**< EOI exit bitmap size */

#ifdef HV_DEBUG
	#define MAX_VMEXIT_LEVEL 14 /**< Maximum VM-Exit profiling level, from 0 to 14 for counts */
	#define TOTAL_ARRAY_LEVEL (MAX_VMEXIT_LEVEL + 1) /**< Profiling Array size */
#endif

/**
 * @brief Guest vCPU context structure
 *
 * This structure contains two parts of vCPU running states: run_ctx is used to store the registers info
 * for virtual CPU running context, and ext_ctx is used to store extended contexts that are not
 * saved/restored during VM exit/entry.
 *
 * @consistency N/A
 */
struct guest_cpu_context {
	struct run_context run_ctx; /**< vCPU states tend to be volatile */
	struct ext_context ext_ctx; /**< vCPU states tend to be fixed */
};

/**
 * @brief MSR entry structure
 *
 * This structure forms the MSR array elements of guest/host MSRs to be stored and loaded at VM-{Entry,Exit}.
 *
 * @consistency N/A
 *
 * @alignment 16
 */
struct msr_store_entry {
	uint32_t msr_index; /**< MSR encoding for MSRs to be stored and loaded at VM-{Entry,Exit} */
	uint32_t reserved; /**< Reserved field */
	uint64_t value; /**< MSR value */
} __aligned(16);

#define MSR_AREA_COUNT 2 	/**< Max MSRs to be stored and loaded at VM-{Entry,Exit} */

/**
 * @brief MSR save/restore area structure for MSRs to be stored and loaded at VM-{Entry,Exit}
 *
 * This structure contains the guest/host MSRs that need to be saved and restored at VM-Exit/VM-Entry.
 * index_of_pqr_assoc is used by RDT feature. The actual MSRs to be stored and loaded at VM-{Entry,Exit}
 * are indicated in count variable.
 *
 * @consistency N/A
 */
struct msr_store_area {
	struct msr_store_entry guest[MSR_AREA_COUNT]; /**< Guest MSRs to be stored and loaded at VM-{Entry,Exit}*/
	struct msr_store_entry host[MSR_AREA_COUNT]; /**< Host MSRs to be loaded at VM-Exit */
	uint32_t index_of_pqr_assoc; /**< Index to guest/host MSR array to be stored and loaded at VM-{Entry,Exit} */
	uint32_t count; /**< Actual count of entries to be loaded/restored during VMEntry/VMExit */
};

/**
 * @brief Internal wrapping keys structure
 *
 * This structure contains encryption key and integrity key used by KeyLocker feature.
 *
 * @consistency N/A
 */
struct iwkey {
	uint64_t encryption_key[4]; /**< 256bit encryption key */
	uint64_t integrity_key[2]; /**< 128bit integration key */
};

/**
 * @brief vCPU architecture specific setting structure
 *
 * This structure contains vCPU's architecture specific settings, such as VMCS structure, LAPIC, PID
 * and MTRR etc. Different CPU architectures share different layout of this structure.
 *
 * @consistency N/A
 * @alignment PAGE_SIZE
 */
struct acrn_vcpu_arch {
	uint8_t vmcs[PAGE_SIZE]; /**< VMCS structure used to run vCPU, must be 4KB-aligned. */

	uint8_t msr_bitmap[PAGE_SIZE]; /**< MSR bitmap used to enable/disable MSR interception, must be 4KB-aligned. */

	struct acrn_vlapic vlapic; /**< vLAPIC of this vCPU */

	struct pi_desc pid __aligned(64); /**< Posted-interrupt descriptor of this vCPU, must be 64-byte aligned. */

	struct guest_cpu_context contexts; /**< vCPU run-context and extended-context */
	struct acrn_vmtrr vmtrr; /**< MTRR register set for this vCPU */

	uint64_t guest_msrs[NUM_EMULATED_MSRS]; /**< Emulated MSRs for this vCPU */

#define ALLOCATED_MIN_L1_VPID	(0x10000U - CONFIG_MAX_VM_NUM * MAX_VCPUS_PER_VM) /**< Start VPID within HV */
	uint16_t vpid;				/**< VPID for this vCPU */

	/**
	 * @brief Structure for IRQ/exception handling
	 *
	 * This structure holds information for delivering an IRQ/exception request.
	 *
	 * @consistency N/A
	 * @alignment N/A
	 */
	struct {
		uint32_t exception; /**< Vector of interrupt or exception that shall be injected to guest */
		uint32_t error; /**< error code of the interrupt or exception */
	} exception_info;

	bool irq_window_enabled; /**< Whether guest interrupt window is enabled */
	bool emulating_lock; /**< Whether emulation is in process */
	bool xsave_enabled; /**< Whether XSAVES/XRSTORS is supported in guest */
	bool mc_pt_enabled; /**< Whether machine-check are passed through */

	/* VCPU context state information */
	uint32_t exit_reason; /**< vCPU basic exit reason */
	uint32_t idt_vectoring_info; /**< IDT-vectoring information for guest interrupt injection */
	uint64_t exit_qualification; /**< vCPU exit qualification */
	uint32_t proc_vm_exec_ctrls; /**< Processor-based VM execution control bits */
	uint32_t inst_len; /**< Length of guest instruction caused VM exit */

	/* Information related to secondary / AP VCPU start-up */
	enum vm_cpu_mode cpu_mode; /**< vCPU operating mode */
	uint8_t nr_sipi; /**< Received startup IPI count of this vCPU */

	uint64_t pending_req; /**< Bitmap indicating pending vCPU requests */

	struct msr_store_area msr_area; /**< Guest/host save-area for MSRs to be stored/loaded at VM-{Entry,Exit} */

	/**
	 * @brief Bitmaps indicate vLAPIC's EOI registers
	 *
	 * The bitmaps of interrupt from 0 to 255 used to determine which virtualized writes to the
	 * vLAPIC's EOI register cause VM exits.
	 */
	uint64_t eoi_exit_bitmap[EOI_EXIT_BITMAP_SIZE >> 6U];

	struct iwkey IWKey; /**< Internal wrapping keys used for KeyLocker */
	bool cr4_kl_enabled; /**< Whether CR4.KL bit is set */

	bool mc_injection_pending; /**< Whether machine-check injection is pending */

	/*
	 * Keylocker spec 4.4:
	 * Bit 0 - Status of most recent copy to or from IWKeyBackup.
	 * Bit 63:1 - Reserved.
	 */
	uint64_t iwkey_copy_status; /**< Whether IWKey local state are synced with platform state */

	union mtrr_def_type_reg mtrr_def_type; /**< Union to hold IA32_MTRR_DEF_TYPE MSR fields */
} __aligned(PAGE_SIZE);

struct acrn_vm;

/**
 * @brief vCPU structure used by hypervisor
 *
 * This structure contains vCPU configuration and settings that facilitate HV to manage a vCPU.
 * The structure contents can be split into two parts: vCPU generic part and architecture specific
 * part, the latter is contained in arch area.
 *
 * @consistency N/A
 * @alignment PAGE_SIZE
 */
struct acrn_vcpu {
	uint8_t stack[CONFIG_STACK_SIZE] __aligned(16); /**< vCPU runtime stack, must be 16-byte aligned. */

	struct acrn_vcpu_arch arch; /**< Architecture specific vCPU data */
	uint16_t vcpu_id; /**< Virtual identifier for this vCPU */
	struct acrn_vm *vm; /**< Reference to the VM this vCPU belongs to */

	volatile enum vcpu_state state;	/**< State of this vCPU */

	struct thread_object thread_obj; /**< Thread object this vCPU belongs to */
	bool launched; /**< Whether this vCPU is launched on target pCPU */
	bool frozen; /**< Whether this vCPU is in frozen state */

	struct instr_emul_ctxt inst_ctxt; /**< Instruction emulation context */
	struct io_request req; /**< PIO/MMIO/PCI request to be emulated */

	uint64_t reg_cached; /**< Bitmap indicating which registers are synced  with VMCS fields */
	uint64_t reg_updated; /**< Bitmap indicating which registers need to be loaded to VMCS fields */

	uint32_t idle_hint; /**< Saves the idle hint when a vCPU goes to sleep */

	struct sched_event events[VCPU_EVENT_NUM]; /**< Events used to sync between thread objects */

#ifdef HV_DEBUG
	uint64_t vmexit_begin; /**< VM-Exit profiling begin time-stamp counter */

	uint64_t vmexit_cnt[NR_VMX_EXIT_REASONS][TOTAL_ARRAY_LEVEL]; /**< TSC counts for different VM-Exit reasons */
	uint64_t vmexit_time[NR_VMX_EXIT_REASONS]; /**< VM-Exit latency for different VM-Exit reasons */
#endif

} __aligned(PAGE_SIZE);

/**
 * @brief Structure used for vCPU contents dumping
 *
 * This structure contains vCPU dumping information. The dumped contents are indicated in str which
 * contains informative data from vCPU GPR registers, control registers, stack etc. and other vCPU
 * management information such as VM ID, vCPU ID etc.
 *
 * @consistency N/A
 */
struct vcpu_dump {
	struct acrn_vcpu *vcpu; /**< Target vCPU to dump contents */
	char *str; /**< String pointer to the dumpping contents */
	uint32_t str_max; /**< Max dumpping string length */
};

/**
 * @brief Structure used for guest memory dumpping
 *
 * This structure contains information for dumpping a block of guest virtual memory.
 *
 * @consistency N/A
 */
struct guest_mem_dump {
	struct acrn_vcpu *vcpu; /**< Target vCPU to dump memory */
	uint64_t gva; /**< Guest virtual address to dump from */
	uint64_t len; /**< Length of the memory block */
};

uint16_t pcpuid_from_vcpu(const struct acrn_vcpu *vcpu);
void vcpu_thread(struct thread_object *obj);
/**
 * @brief Prepare and start to run guest code in non-root mode
 *
 * This function prepares VM-Entry settings and then starts to launch/resume guest code execution. It also
 * include code that saves guest GPRs and restores host side GPRs, and error handling if VM-Entry failed.
 * If HV enables speculation control, it also performs IBRS mitigation work before return.
 *
 * Normal case behaviors:
 * - Store host side ABI GPRs, i.e., %rdx, %rbx, %rbp, %r12 - %r15 and %rdi to vCPU context.
 * - If speculation control is not $IBRS_NONE, restore guest control value to MSR_IA32_SPEC_CTRL.
 * - Write host current stack pointer to VMX_HOST_RSP field.
 * - Load guest GPRs, %rax, %rcx, %rdx, %rbx, %rbp, %rdi, %rsi, %r8 - %r15 before VM-Entry.
 * - Check launch type and execute vmresume or vmlaunch instruction accordingly.
 * - If guest and host states checking passed, CPU starts executing guest code in non-root mode
 *   until a normal VM-Exit happens, then it does following things.
 * - Store guest GPRs, i.e., %rax, %rcx, %rdx, %rbx, %rbp, %rdi, %rsi, %r8 - %r15 to vCPU context.
 * - Restore host ABI GPRs.
 * - Reset guest %rcx, %r8 - %r11.
 * - Perform mitigation according to IBRS type:
 *   - $IBRS_NONE: Flush RSB only.
 *   - $IBRS_RAW: Save lower 32bit of MSR_IA32_SPEC_CTRL to vCPU context and set IBRS bit in
 *     MSR_IA32_SPEC_CTRL then flush RSB.
 *   - $IBRS_OPT: Set IBPB bit in MSR_IA32_PRED_CMD and save lower 32bit of MSR_IA32_SPEC_CTRL to vCPU
 *     context then set STIBP bit in MSR_IA32_SPEC_CTRL, finally flush RSB.
 * - Set return-value as $VM_SUCCESS and returns.
 *
 * Error case behaviors:
 * - Store host side ABI GPRs, i.e., %rdx, %rbx, %rbp, %r12 - %r15 and %rdi to vCPU context.
 * - If speculation control is not $IBRS_NONE, restore guest control value to MSR_IA32_SPEC_CTRL.
 * - Write host current stack pointer to VMX_HOST_RSP field.
 * - Load guest GPRs, %rax, %rcx, %rdx, %rbx, %rbp, %rdi, %rsi, %r8 - %r15 before VM-Entry.
 * - Check launch type and execute vmresume or vmlaunch instruction accordingly.
 * - If guest and host states checking failed, CPU won't execute guest code and it does following things.
 * - Store guest GPRs, i.e., %rax, %rcx, %rdx, %rbx, %rbp, %rdi, %rsi, %r8 - %r15 to vCPU context.
 * - Restore host ABI GPRs.
 * - Set return-value as $VM_FAIL and returns.

 * @param[inout] context Target vCPU runtime context
 * @param[in] ops vCPU launch type
 * @param[in] ibrs IBRS control option
 *
 * @return vCPU launch/resume result
 * @retval 0 If vCPU launch/resume successfully.
 * @retval non-zero If vCPU entry failed or due to other errors before VM-Entry.
 *
 * @pre context != NULL
 *
 * @post N/A
 */
int32_t vmx_vmrun(struct run_context *context, int32_t ops, int32_t ibrs);

/* External Interfaces */
uint64_t vcpu_get_gpreg(const struct acrn_vcpu *vcpu, uint32_t reg);
void vcpu_set_gpreg(struct acrn_vcpu *vcpu, uint32_t reg, uint64_t val);
uint64_t vcpu_get_rip(struct acrn_vcpu *vcpu);
void vcpu_set_rip(struct acrn_vcpu *vcpu, uint64_t val);
uint64_t vcpu_get_rsp(struct acrn_vcpu *vcpu);
void vcpu_set_rsp(struct acrn_vcpu *vcpu, uint64_t val);
uint64_t vcpu_get_efer(struct acrn_vcpu *vcpu);
void vcpu_set_efer(struct acrn_vcpu *vcpu, uint64_t val);
uint64_t vcpu_get_rflags(struct acrn_vcpu *vcpu);
void vcpu_set_rflags(struct acrn_vcpu *vcpu, uint64_t val);
uint64_t vcpu_get_guest_msr(const struct acrn_vcpu *vcpu, uint32_t msr);
void vcpu_set_guest_msr(struct acrn_vcpu *vcpu, uint32_t msr, uint64_t val);
void vcpu_set_vmcs_eoi_exit(const struct acrn_vcpu *vcpu);
void vcpu_reset_eoi_exit_bitmaps(struct acrn_vcpu *vcpu);
void vcpu_set_eoi_exit_bitmap(struct acrn_vcpu *vcpu, uint32_t vector);
void vcpu_clear_eoi_exit_bitmap(struct acrn_vcpu *vcpu, uint32_t vector);

void set_vcpu_regs(struct acrn_vcpu *vcpu, struct acrn_regs *vcpu_regs);
bool sanitize_cr0_cr4_pattern(struct acrn_vm *vm);
void init_vcpu_protect_mode_regs(struct acrn_vcpu *vcpu, uint64_t vgdt_base_gpa);
void set_vcpu_startup_entry(struct acrn_vcpu *vcpu, uint64_t entry);
struct acrn_vcpu *get_running_vcpu(uint16_t pcpu_id);
struct acrn_vcpu *get_ever_run_vcpu(uint16_t pcpu_id);
void load_iwkey(struct acrn_vcpu *vcpu);

int32_t run_vcpu(struct acrn_vcpu *vcpu);
void offline_vcpu(struct acrn_vcpu *vcpu);
void reset_vcpu(struct acrn_vcpu *vcpu, enum vcpu_reset_mode mode);
void zombie_vcpu(struct acrn_vcpu *vcpu);
void launch_vcpu(struct acrn_vcpu *vcpu);
void kick_vcpu(struct acrn_vcpu *vcpu);
int32_t prepare_vcpu(struct acrn_vm *vm, uint16_t pcpu_id);

uint64_t vcpumask2pcpumask(struct acrn_vm *vm, uint64_t vdmask);
void vcpu_handle_pi_notification(uint32_t vcpu_index);

/**
 * @brief Check whether target vCPU is BSP(Bootstrap Processor)
 *
 * Normal case behaviors:
 * - Return true if the vCPU is BSP, false otherwise.
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[in] vcpu Target vCPU to check
 *
 * @return true if target vCPU is BSP, otherwise false.
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static inline bool is_vcpu_bsp(const struct acrn_vcpu *vcpu)
{
	return (vcpu->vcpu_id == BSP_CPU_ID);
}

/**
 * @brief Get target vCPU operating mode
 *
 * This function is used to get target vCPU operating mode which is defined in enum vm_cpu_mode.
 *
 * Normal case behaviors:
 * - Return the vCPU operating mode.
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[in] vcpu Target vCPU to get mode
 *
 * @return Target vCPU operating mode
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static inline enum vm_cpu_mode get_vcpu_mode(const struct acrn_vcpu *vcpu)
{
	return vcpu->arch.cpu_mode;
}

/**
 * @brief  Don't update guest RIP for next VM-Enter
 *
 * This function is used to preserve guest RIP for next VM-Entry so that the instruction caused VM-Exit
 * will be executed again.
 *
 * Normal case behaviors:
 * - Reset guest instruction length to 0
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[inout] vcpu Target vCPU to reset instruction length
 *
 * @return N/A
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static inline void vcpu_retain_rip(struct acrn_vcpu *vcpu)
{
	(vcpu)->arch.inst_len = 0U;
}

/**
 * @brief Get target vCPU's vLAPIC
 *
 * Normal case behaviors:
 * - Return target vCPU's vLAPIC pointer
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[in] vcpu Target vCPU to get vLAPIC
 *
 * @return Target vCPU's vLAPIC pointer
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static inline struct acrn_vlapic *vcpu_vlapic(struct acrn_vcpu *vcpu)
{
	return &(vcpu->arch.vlapic);
}

/**
 * @brief Get target vCPU's Posted-Interrupt Descriptor(PID)
 *
 * Normal case behaviors:
 * - Return a pointer to vCPU's PID
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[in] vcpu Target vCPU to get PID
 *
 * @return Target vCPU's PID pointer
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static inline struct pi_desc *get_pi_desc(struct acrn_vcpu *vcpu)
{
	return &(vcpu->arch.pid);
}

uint16_t pcpuid_from_vcpu(const struct acrn_vcpu *vcpu);
int32_t vmx_vmrun(struct run_context *context, int32_t ops, int32_t ibrs);

/* External Interfaces */
uint64_t vcpu_get_gpreg(const struct acrn_vcpu *vcpu, uint32_t reg);
void vcpu_set_gpreg(struct acrn_vcpu *vcpu, uint32_t reg, uint64_t val);
uint64_t vcpu_get_rip(struct acrn_vcpu *vcpu);
void vcpu_set_rip(struct acrn_vcpu *vcpu, uint64_t val);
uint64_t vcpu_get_rsp(struct acrn_vcpu *vcpu);
void vcpu_set_rsp(struct acrn_vcpu *vcpu, uint64_t val);
uint64_t vcpu_get_efer(struct acrn_vcpu *vcpu);
void vcpu_set_efer(struct acrn_vcpu *vcpu, uint64_t val);
uint64_t vcpu_get_rflags(struct acrn_vcpu *vcpu);
void vcpu_set_rflags(struct acrn_vcpu *vcpu, uint64_t val);
uint64_t vcpu_get_guest_msr(const struct acrn_vcpu *vcpu, uint32_t msr);
void vcpu_set_guest_msr(struct acrn_vcpu *vcpu, uint32_t msr, uint64_t val);
void vcpu_set_vmcs_eoi_exit(const struct acrn_vcpu *vcpu);
void vcpu_reset_eoi_exit_bitmaps(struct acrn_vcpu *vcpu);
void vcpu_set_eoi_exit_bitmap(struct acrn_vcpu *vcpu, uint32_t vector);
void vcpu_clear_eoi_exit_bitmap(struct acrn_vcpu *vcpu, uint32_t vector);
void set_vcpu_regs(struct acrn_vcpu *vcpu, struct acrn_regs *vcpu_regs);
bool sanitize_cr0_cr4_pattern(struct acrn_vm *vm);
void init_vcpu_protect_mode_regs(struct acrn_vcpu *vcpu, uint64_t vgdt_base_gpa);
void set_vcpu_startup_entry(struct acrn_vcpu *vcpu, uint64_t entry);

/**
 * @brief Check whether target vCPU is in 64bit long-mode
 *
 * Normal case behaviors:
 * - Return true if target vCPU is in 64bit long-mode, otherwise return false.
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[in] vcpu Target vCPU to check long-mode
 *
 * @return true if vCPU is in long-mode, false otherwise.
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */

static inline bool is_long_mode(struct acrn_vcpu *vcpu)
{
	return (vcpu_get_efer(vcpu) & MSR_IA32_EFER_LMA_BIT) != 0UL;
}

/**
 * @brief Check whether target vCPU has enabled paging
 *
 * Normal case behaviors:
 * - Return true if target vCPU has enabled paging, otherwise return false.
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[in] vcpu Target vCPU to check paging
 *
 * @return true if vCPU has enabled paging, false otherwise.
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static inline bool is_paging_enabled(struct acrn_vcpu *vcpu)
{
	return (vcpu_get_cr0(vcpu) & CR0_PG) != 0UL;
}

/**
 * @brief Check whether target vCPU has enabled PAE
 *
 * Normal case behaviors:
 * - Return true if target vCPU has enabled PAE, otherwise return false.
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[in] vcpu Target vCPU to check PAE
 *
 * @return true if vCPU has enabled PAE, false otherwise.
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static inline bool is_pae(struct acrn_vcpu *vcpu)
{
	return (vcpu_get_cr4(vcpu) & CR4_PAE) != 0UL;
}

struct acrn_vcpu *get_running_vcpu(uint16_t pcpu_id);
struct acrn_vcpu *get_ever_run_vcpu(uint16_t pcpu_id);

void load_iwkey(struct acrn_vcpu *vcpu);

/**
 * @brief Check whether target vCPU has enabled machine-check passthrough(PT)
 *
 * Normal case behaviors:
 * - Return true if target vCPU has passed through machine-check banks, otherwise return false.
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[in] vcpu Target vCPU to check machine-check PT status
 *
 * @return true if vCPU has enabled machine-check PT, false otherwise.
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static inline bool is_mc_pt_enabled(struct acrn_vcpu *vcpu)
{
	return (vcpu->arch.mc_pt_enabled);
}

void vcpu_handle_pi_notification(uint32_t vcpu_index);

/**
 * @brief VM-Exit entry
 *
 * This function is called when physical CPU exits from non-root mode or when guest-state checks fail.
 * It first saves guest side GPRS and restores host side GPRs, and performs below handling:
 * - If VM-Entry failed due to guest consistency checks, return with $VM_FAIL directly.
 * - If it's a normal VM-Exit, reset host %rcx, %r8 - %r11, and run mitigation code according to IBRS type,
 *   then return with $VM_SUCCESS.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
extern void vm_exit(void);

/**
 * @}
 */
/* End of acrn_vcpu */

#endif /* ASSEMBLER */

#endif /* VCPU_H */
