/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @addtogroup vp-base_vm
 *
 * @{
 */

/**
 * @file
 * @brief Header for VM management APIs.
 *
 * Header file for VM lifecycle management APIs, including preparation, creation,
 * resume, start or shutdown of VMs.
 */

#ifndef VM_H_
#define VM_H_

/* Defines for VM Launch and Resume */
#define VM_RESUME		0	/**< VM Resume */
#define VM_LAUNCH		1	/**< VM Launch */

#ifndef ASSEMBLER

#include <asm/lib/bits.h>
#include <asm/lib/spinlock.h>
#include <asm/pgtable.h>
#include <asm/guest/vcpu.h>
#include <vioapic.h>
#include <asm/guest/vmx_io.h>
#include <vuart.h>
#include <vrtc.h>
#include <asm/guest/vcpuid.h>
#include <vpci.h>
#include <asm/cpu_caps.h>
#include <asm/e820.h>
#include <asm/vm_config.h>
#include <io_req.h>

/**
 * @brief Hardware information of a VM
 *
 * This struct stores the hardware related information. Including physical
 * CPU bitmap, virtual CPU structure array and number of vcpus.
 *
 * @alignment PAGE_SIZE
 */
struct vm_hw_info {
	struct acrn_vcpu vcpu_array[MAX_VCPUS_PER_VM]; /**< Virtual CPU array of this VM */
	uint16_t created_vcpus;	/**< Number of created vcpus */
	uint64_t cpu_affinity;	/**< Actual pCPUs this VM runs on. The set bits represent the pCPU IDs */
} __aligned(PAGE_SIZE);

/**
 * @brief Software modules information of a VM
 *
 * This struct records the software module information. A software module
 * is a block of data in memory that the VM used for its booting. The data might
 * be kernel image, kernel ramdisk, firmware and command line, etc.
 */
struct sw_module_info {
	void *src_addr;			/**< Source address of software module. HVA */
	void *load_addr;		/**< Target load address of software module. GPA */
	uint32_t size;			/**< Size of software module */
};

/**
 * @brief VM's Kernel info
 *
 * This struct records the kernel load information of a VM.
 */
struct sw_kernel_info {
	void *kernel_src_addr;		/**< Source address of kernel. HVA */
	void *kernel_entry_addr;	/**< Kernel entry address. GPA */
	uint32_t kernel_size;		/**< Kernel size */
};

/**
 * @brief VM's software information
 *
 * This struct records the top-level software information related to
 * VM's software modules (kernels, ramdisk, pre-built ACPI information, etc)
 * and shared buffer page (IO shared page, Async IO shared buffer, VM event
 * shared buffer), and some controlling flag.
 *
 * The ACPI info represents a pre-built ACPI binary that gets loaded into
 * VM's address space.
 */
struct vm_sw_info {
	enum os_kernel_type kernel_type;	/**< Guest kernel type */
	/* Kernel information (common for all guest types) */
	struct sw_kernel_info kernel_info; /**< Kernel sw module info */
	struct sw_module_info bootargs_info; /**< Bootargs sw module info */
	struct sw_module_info ramdisk_info; /**< Ramdisk sw module info */
	struct sw_module_info acpi_info; /**< Pre-built ACPI binary info */
	/* HVA to IO shared page */
	void *io_shared_page; /**< IO shared page, HVA */
	void *asyncio_sbuf; /**< Async IO shared buffer, HVA */
	void *vm_event_sbuf; /**< VM event shared buffer, HVA */
	/* If enable IO completion polling mode */
	bool is_polling_ioreq; /**< Whether IO completion polling is enabled */
};

/**
 * @brief Power management information of a VM
 *
 * This struct records the P and C state data.
 */
struct vm_pm_info {
	uint8_t			px_cnt;		/**< Count of all Px states */
	struct acrn_pstate_data	px_data[MAX_PSTATE]; /**< P-state data */
	uint8_t			cx_cnt;		/**< Count of all Cx entries */
	struct acrn_cstate_data	cx_data[MAX_CSTATE]; /**< C-state data */
	struct pm_s_state_data	*sx_state_data;	/**< Data for S3/S5 implementation */
};

/**
 * @brief VM state enums
 *
 * Enumerated type for VM states.
 */
enum vm_state {
	/**
	 * @brief VM is powered off
	 *
	 * This enum must be zero because vm_state
	 * is initialized by clearing BSS section.
	 */
	VM_POWERED_OFF = 0,
	VM_CREATED,	/**< VM created / awaiting start (boot) */
	VM_RUNNING,	/**< VM running */
	VM_READY_TO_POWEROFF,     /**< RTVM only, it is trying to poweroff itself */
	VM_PAUSED,	/**< VM paused */
};

/**
 * @brief Architecture information of a VM
 *
 * This struct records architecture information of a VM.
 *
 * @alignment PAGE_SIZE
 */
struct vm_arch {
	uint8_t io_bitmap[PAGE_SIZE*2]; /**< I/O bitmaps A and B for this VM, MUST be 4-Kbyte aligned */

	/* EPT hierarchy for Normal World */
	void *nworld_eptp; /**< EPT Pagetable root pointer */
	struct pgtable ept_pgtable; /**< EPT mapping information */

	uint64_t *pid_table; /**< PID-pointer table for IPI Virtualization */
	uint16_t max_lapic_id; /**< Largest local APIC ID. Used in IPI Virtualization */

	struct acrn_vioapics vioapics;	/**< Virtual IOAPIC structure */

	/**
	 * @brief IWKey backup status
	 *
	 * Refer to Keylocker spec 4.5:
	 * Bit 0 - Backup/restore valid.
	 * Bit 1 - Reserved.
	 * Bit 2 - Backup key storage read/write error.
	 * Bit 3 - IWKeyBackup consumed.
	 * Bit 63:4 - Reserved.
	 */
	uint64_t iwkey_backup_status;
	spinlock_t iwkey_backup_lock;	/**< Spin-lock used to protect internal key backup/restore */
	struct iwkey iwkey_backup;	/**< IWKey backup structure */

	/* Reference to virtual platform to come here (as needed) */
	bool vm_mwait_cap; /**< Whether VM monitor is supported */
} __aligned(PAGE_SIZE);

/**
 * @brief ACRN Virtual Machine structure
 *
 * This structure represent an ACRN VM.
 *
 * @alignment PAGE_SIZE
 */
struct acrn_vm {
	struct vm_arch arch_vm; /**< Reference to this VM's arch information */
	struct vm_hw_info hw;	/**< Reference to this VM's HW information */
	struct vm_sw_info sw;	/**< Reference to SW associated with this VM */
	struct vm_pm_info pm;	/**< Reference to this VM's arch information */
	uint32_t e820_entry_num;	/**< Number of E820 entry */
	struct e820_entry *e820_entries; /**< Pointer to E820 entries array */
	uint16_t vm_id;		    /**< Virtual machine identifier */
	enum vm_state state;	/**< VM state */
	struct acrn_vuart vuart[MAX_VUART_NUM_PER_VM];		/**< Virtual UART */
	struct asyncio_desc	aio_desc[ACRN_ASYNCIO_MAX];		/**< Async IO descriptors */
	struct list_head aiodesc_queue;		/**< Async IO descriptor queue list */
	spinlock_t asyncio_lock; /**< Spin-lock used to protect asyncio add/remove for a VM */
	spinlock_t vm_event_lock; /**< Spin-lock used to protect VM event injection to a VM */

	struct iommu_domain *iommu;	/**< IOMMU domain of this VM */
	/* vm_state_lock used to protect vm/vcpu state transition,
	 * the initialization depends on the clear BSS section
	 */
	spinlock_t vm_state_lock;	/**< Spin-lock to guard VM state transition */
	spinlock_t wbinvd_lock;		/**< Spin-lock used to serialize wbinvd emulation */
	spinlock_t vlapic_mode_lock;	/**< Spin-lock used to protect vlapic_mode modifications for a VM */
	spinlock_t ept_lock;	/**< Spin-lock used to protect ept add/modify/remove for a VM */
	spinlock_t emul_mmio_lock;	/**< Spin-lock used to protect emulation mmio_node concurrent access for a VM */
	uint16_t nr_emul_mmio_regions;	/**< Number of emulated mmio_region */
	struct mem_io_node emul_mmio[CONFIG_MAX_EMULATED_MMIO_REGIONS]; /**< Emulated MMIO node structure */

	struct vm_io_handler_desc emul_pio[EMUL_PIO_IDX_MAX]; /**< VM IO Handler descriptor */

	char name[MAX_VM_NAME_LEN];	/**< VM name */

	/**
	 * @brief Secure World's snapshot
	 *
	 * Currently, Secure World is only running on vcpu[0],
	 * so the snapshot only stores the vcpu0's run_context
	 * of secure world.
	 */
	struct guest_cpu_context sworld_snapshot;

	uint32_t vcpuid_entry_nr; /**< Number of vcpuid entries of vcpuid_entries array */
	uint32_t vcpuid_level; /**< vCPUID level */
	uint32_t vcpuid_xlevel; /**< vCPUID extended level */
	struct vcpuid_entry vcpuid_entries[MAX_VM_VCPUID_ENTRIES];	/**< Virtual CPUID entries array */
	struct acrn_vpci vpci;	/**< ACRN virtual PCI bus */
	struct acrn_vrtc vrtc;	/**< ACRN virtual RTC */

	uint64_t intr_inject_delay_delta; /**< Delay of interrupt injection */
	/**
	 * @brief Reset control value.
	 *
	 * Stores lowest 4bits of Reset Control register at I/O port 0xcf9.
	 */
	uint32_t reset_control;
} __aligned(PAGE_SIZE);

/**
 * @brief Returns currently active virtual CPUs of a VM
 *
 * Returns currently active virtual CPUs of a VM. Lock free.
 *
 * @pre vm != NULL
 * @post N/A
 *
 * @param[in] vm The VM to be checked
 *
 * @return A bitmap representing currently active vCPUs of \p vm
 */
static inline uint64_t vm_active_cpus(const struct acrn_vm *vm)
{
	uint64_t dmask = 0UL;
	uint16_t i;
	const struct acrn_vcpu *vcpu;

	foreach_vcpu(i, vm, vcpu) {
		bitmap_set_nolock(vcpu->vcpu_id, &dmask);
	}

	return dmask;
}

/**
 * @brief Get vcpu of a VM from vcpu id
 *
 * Get ACRN vcpu structure pointer from vcpu_id.
 *
 * @pre vcpu_id < MAX_VCPUS_PER_VM
 * @pre &(vm->hw.vcpu_array[vcpu_id])->state != VCPU_OFFLINE
 *
 * @param[in] vm The VM that target vCPU belongs to
 * @param[in] vcpu_id vCPU ID
 *
 * @return ACRN vcpu structure pointer of \p vm that has ID \p vcpu_id
 */
static inline struct acrn_vcpu *vcpu_from_vid(struct acrn_vm *vm, uint16_t vcpu_id)
{
	return &(vm->hw.vcpu_array[vcpu_id]);
}

/**
 * @brief Get vcpu of a VM from physical CPU ID.
 *
 * Get ACRN vcpu structure pointer from pCPU ID. ACRN does not put two vcpus of
 * same VM onto same pCPU, so within each VM, one pCPU maps to one vCPU. ACRN also
 * does not migrate vCPU, so within each VM, one vCPU maps to one pCPU.
 *
 * @pre vm != NULL
 * @pre pcpu_id < ACRN_PLATFORM_LAPIC_IDS_MAX
 *
 * @param[in] vm The VM that target vCPU belongs to
 * @param[in] pcpu_id pCPU ID
 *
 * @return ACRN vcpu structure pointer of \p vm that runs on \p pcpu_id
 */
static inline struct acrn_vcpu *vcpu_from_pcpu_id(struct acrn_vm *vm, uint16_t pcpu_id)
{
	uint16_t i;
	struct acrn_vcpu *vcpu, *target_vcpu = NULL;

	foreach_vcpu(i, vm, vcpu) {
		if (pcpuid_from_vcpu(vcpu) == pcpu_id) {
			target_vcpu = vcpu;
			break;
		}
	}

	return target_vcpu;
}

/**
 * @brief Convert relative vm id to absolute vm id
 *
 * A relative VM ID is a VM ID relative to Service VM. Convert
 * a relative VM ID to an absolute one.
 *
 * @pre None
 * @post None
 *
 * @param[in] service_vmid VM ID of Service VM
 * @param[in] rel_vmid Relative VM ID of target VM
 *
 * @return Absolute VM ID of target VM
 */
static inline uint16_t rel_vmid_2_vmid(uint16_t service_vmid, uint16_t rel_vmid) {
	return (service_vmid + rel_vmid);
}

/**
 * @brief Convert absolute vm id to relative vm id
 *
 * A relative VM ID is a VM ID relative to Service VM. Convert
 * an absolute VM ID to a relative one.
 *
 * @pre None
 * @post None
 *
 * @param[in] service_vmid VM ID of Service VM
 * @param[in] vmid Absolute VM ID of target VM
 *
 * @return Relative VM ID of target VM
 */
static inline uint16_t vmid_2_rel_vmid(uint16_t service_vmid, uint16_t vmid) {
	return (vmid - service_vmid);
}

/**
 * @brief Check if target VM has higher severity than Service VM.
 *
 * Check if target VM has higher severity than Service VM.
 * This is usually used in privilege check.
 *
 * @pre target_vmid < CONFIG_MAX_VM_NUM
 * @post N/A
 *
 * @param[in] target_vmid VM ID of VM to be checked
 *
 * @return true if Service VM has equal or higher severity. False otherwise.
 */
static inline bool is_severity_pass(uint16_t target_vmid)
{
	return SEVERITY_SERVICE_VM >= get_vm_severity(target_vmid);
}

/**
 * @brief Check if IPI virtualization feature can be enabled.
 *
 * Check if IPI virtualization feature can be enabled. IPI virtualization
 * can be enabled if the platform supports IPI virtualization feature, and
 * if the VM has not configured Local APIC passthrough.
 *
 * @remark This function shall be called after the vm has been created.
 *
 * @pre vm != NULL
 * @post N/A
 *
 * @param[in] vm The VM to be checked
 *
 * @return Returns true if IPI virtualization feature can be enabled for \p VM.
 */
static inline bool can_ipiv_enabled(struct acrn_vm *vm)
{
	return (vm->arch_vm.pid_table != NULL);
}

/**
 * @brief Get VM structure to which a virtual PCI subsystem belongs.
 *
 * This function returns the VM structure to which virtual PCI subsystem structure
 * \p vpci belongs.
 *
 * @pre vpci != NULL
 * @post N/A
 *
 * @param[in] vpci The source virtual PCI subsystem to check
 *
 * @return The VM to which \p vpci belongs
 */
static inline struct acrn_vm *vpci2vm(const struct acrn_vpci *vpci)
{
	return container_of(vpci, struct acrn_vm, vpci);
}

void make_shutdown_vm_request(uint16_t pcpu_id);
bool need_shutdown_vm(uint16_t pcpu_id);
int32_t shutdown_vm(struct acrn_vm *vm);
void poweroff_if_rt_vm(struct acrn_vm *vm);
void pause_vm(struct acrn_vm *vm);
void resume_vm_from_s3(struct acrn_vm *vm, uint32_t wakeup_vec);
void start_vm(struct acrn_vm *vm);
int32_t reset_vm(struct acrn_vm *vm, enum vm_reset_mode mode);
int32_t create_vm(uint16_t vm_id, uint64_t pcpu_bitmap, struct acrn_vm_config *vm_config, struct acrn_vm **rtn_vm);
int32_t prepare_vm(uint16_t vm_id, struct acrn_vm_config *vm_config);
void launch_vms(uint16_t pcpu_id);
bool is_poweroff_vm(const struct acrn_vm *vm);
bool is_created_vm(const struct acrn_vm *vm);
bool is_paused_vm(const struct acrn_vm *vm);
bool is_service_vm(const struct acrn_vm *vm);
bool is_postlaunched_vm(const struct acrn_vm *vm);
bool is_prelaunched_vm(const struct acrn_vm *vm);
uint16_t get_vmid_by_name(const char *name);
struct acrn_vm *get_vm_from_vmid(uint16_t vm_id);
struct acrn_vm *get_service_vm(void);

void create_service_vm_e820(struct acrn_vm *vm);
void create_prelaunched_vm_e820(struct acrn_vm *vm);
uint64_t find_space_from_ve820(struct acrn_vm *vm, uint32_t size, uint64_t min_addr, uint64_t max_addr);

bool is_lapic_pt_configured(const struct acrn_vm *vm);
bool is_pmu_pt_configured(const struct acrn_vm *vm);
bool is_rt_vm(const struct acrn_vm *vm);
bool is_static_configured_vm(const struct acrn_vm *vm);
uint16_t get_unused_vmid(void);
bool is_pi_capable(const struct acrn_vm *vm);
bool has_rt_vm(void);
struct acrn_vm *get_highest_severity_vm(bool runtime);
bool vm_hide_mtrr(const struct acrn_vm *vm);
bool is_vhwp_configured(const struct acrn_vm *vm);
bool is_mc_pt_configured(const struct acrn_vm *vm);
bool is_tm_pt_configured(const struct acrn_vm *vm);
bool is_ptm_pt_configured(const struct acrn_vm *vm);
void get_vm_lock(struct acrn_vm *vm);
void put_vm_lock(struct acrn_vm *vm);

#endif /* !ASSEMBLER */

#endif /* VM_H_ */

/**
 * @}
 */
