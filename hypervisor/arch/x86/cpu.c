/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <asm/lib/bits.h>
#include <asm/page.h>
#include <asm/e820.h>
#include <asm/mmu.h>
#include <asm/guest/ept.h>
#include <asm/vtd.h>
#include <asm/lapic.h>
#include <asm/irq.h>
#include <asm/per_cpu.h>
#include <asm/cpufeatures.h>
#include <asm/cpu_caps.h>
#include <acpi.h>
#include <asm/ioapic.h>
#include <asm/trampoline.h>
#include <asm/cpuid.h>
#include <version.h>
#include <asm/vmx.h>
#include <asm/msr.h>
#include <asm/host_pm.h>
#include <ptdev.h>
#include <logmsg.h>
#include <asm/rdt.h>
#include <asm/sgx.h>
#include <uart16550.h>
#include <vpci.h>
#include <ivshmem.h>
#include <reloc.h>
#include <asm/tsc.h>
#include <ticks.h>
#include <delay.h>
#include <thermal.h>
#include <asm/mce.h>
#include <asm/platform_caps.h>
/**
 * @defgroup hwmgmt_cpu hwmgmt.cpu
 * @ingroup hwmgmt
 * @brief The definition and implementation of CPU related stuff.
 * @{
 */
/**
 * @file
 * @brief This file provides the interface implementation for CPU operations.
 *
 * This file contains the data structures and functions for the hwmgmt.cpu to operate physical CPU and provide
 * interfaces to other modules.
 */

/**
 * @brief Macro representing the expiration time for waiting for the CPU to start up, in milliseconds.
 */
#define CPU_UP_TIMEOUT		100U
/**
 * @brief Macro representing the expiration time for waiting for the CPU to go offline, in milliseconds.
 */
#define CPU_DOWN_TIMEOUT	100U
/**
 * @brief An array variable that contains information will be held by per CPU, it is page aligned.
 *
 * @alignment PAGE_SIZE
 */
struct per_cpu_region per_cpu_data[MAX_PCPU_NUM] __aligned(PAGE_SIZE);
/**
 * @brief The static variable indicating the number of physical CPUs on the platform.
 */
static uint16_t phys_cpu_num = 0U;
/**
 * @brief The static variable to be used to synchronize the initialization of physical CPUs.
 */
static uint64_t pcpu_sync = 0UL;
/**
 * @brief The static variable representing the startup address of APs.
 */
static uint64_t startup_paddr = 0UL;

/**
 * @brief The static variable indicating the physical CPU active bitmap, supporting up to 64 CPUs.
 */
static volatile uint64_t pcpu_active_bitmap = 0UL;

static void init_pcpu_xsave(void);
static void init_keylocker(void);
static void set_current_pcpu_id(uint16_t pcpu_id);
static void print_hv_banner(void);
static uint16_t get_pcpu_id_from_lapic_id(uint32_t lapic_id);

/**
 * @brief The static variable to store the tick of hypervisor start.
 */
static uint64_t start_tick __attribute__((__section__(".bss_noinit")));
/**
 * @brief The static variable to store the maximum local APIC ID.
 */
static uint32_t max_lapic_id = 0U;

/**
 * @brief The constant char array with unique version information generated during the build.
 *
 * This constant character array represents the version string of the hypervisor. It includes the branch version,
 * commit time, commit dirty flag, build type, commit tags, build scenario, build board, build user, and build time.
 */
const char version_info[] __attribute__((__section__(".version"))) =
	"HV:" HV_BRANCH_VERSION "-" HV_COMMIT_TIME "-" HV_COMMIT_DIRTY "-" HV_BUILD_TYPE " (tag:" HV_COMMIT_TAGS
	") " HV_BUILD_SCENARIO " " HV_BUILD_BOARD " build by " HV_BUILD_USER "@" HV_BUILD_TIME;

/**
 * @brief Initializes the lapic_id field of per_cpu_data for each physical CPU.
 *
 * This function calling parse_madt() to retrieve the Local APIC ID from the MADT (Multiple APIC Description Table) and
 * stores it in the lapic_id field.
 *
 * @return A Boolean value indicating the success of the initialization.
 *
 * @retval true  Initialization of the lapic_id field succeeded.
 * @retval false Initialization of the lapic_id field failed in case that parse_madt() returns 0.
 *
 * @pre N/A
 * @post N/A
 */
static bool init_percpu_lapic_id(void)
{
	uint16_t i;
	uint32_t lapic_id_array[MAX_PCPU_NUM];
	bool success = false;

	/* Save all lapic_id detected via parse_madt in lapic_id_array */
	phys_cpu_num = parse_madt(lapic_id_array);

	if (phys_cpu_num != 0U) {
		/* TODO: flag error bit to record CPU number mismatch */
		if (phys_cpu_num > MAX_PCPU_NUM) {
			phys_cpu_num = MAX_PCPU_NUM;
		}

		for (i = 0U; i < phys_cpu_num; i++) {
			per_cpu(lapic_id, i) = lapic_id_array[i];
			if (per_cpu(lapic_id, i) > max_lapic_id) {
				max_lapic_id = per_cpu(lapic_id, i);
			}
		}
		success = true;
	}

	return success;
}

/**
 * @brief Set the state of the specified physical CPU.
 *
 * This function sets the current state of the specified physical CPU. If the state is PCPU_STATE_INITIALIZING, it also
 * saves the CPU's logical ID to the pseudo MSR.
 *
 * @param[in] pcpu_id The ID of the physical CPU to be updated.
 * @param[in] state The new state to be assigned to the CPU.
 *
 * @return None
 *
 * @pre pcpu_id < MAX_PCPU_NUM
 * @post N/A
 */
static void pcpu_set_current_state(uint16_t pcpu_id, enum pcpu_boot_state state)
{
	/* Check if state is initializing */
	if (state == PCPU_STATE_INITIALIZING) {

		/* Save this CPU's logical ID to the pseudo MSR */
		set_current_pcpu_id(pcpu_id);
	}

	/* Set state for the specified CPU */
	per_cpu(boot_state, pcpu_id) = state;
}

/**
 * @brief Get the maximum Local APIC ID.
 *
 * This function returns the maximum Local APIC (Advanced Programmable Interrupt Controller) ID that has been detected
 * or configured in the system.
 *
 * @return The maximum Local APIC ID as a 32-bit unsigned integer.
 *
 * @pre N/A
 * @post N/A
 */
uint32_t get_max_lapic_id(void)
{
	return max_lapic_id;
}

/**
 * @brief The function retrieves the boot state for the specified physical CPU.
 *
 * @param[in] pcpu_id The ID of the physical CPU whose boot state is to be retrieved.
 *
 * @return The current boot state of the specified physical CPU.
 *
 * @pre pcpu_id < MAX_PCPU_NUM
 * @post N/A
 */
enum pcpu_boot_state pcpu_get_current_state(uint16_t pcpu_id)
{
	/* Get state for the specified CPU */
	return per_cpu(boot_state, pcpu_id);
}

/**
 * @brief The function returns the number of physical CPU cores detected by the hypervisor.
 *
 * @return A uint16_t value indicating the number of physical CPUs.
 *
 * @pre N/A
 * @post return <= MAX_PCPU_NUM
 * @remark This API can be called only after init_pcpu_pre() has been called once on any processor.
 */
uint16_t get_pcpu_nums(void)
{
	return phys_cpu_num;
}

/**
 * @brief The function checks whether the specified physical CPU is active.
 *
 * @return A boolean value indicating whether the CPU is active.
 *
 * @pre pcpu_id < MAX_PCPU_NUM
 * @post N/A
 */
bool is_pcpu_active(uint16_t pcpu_id)
{
	return bitmap_test(pcpu_id, &pcpu_active_bitmap);
}

/**
 * @brief The function gets the bitmap of active physical CPUs.
 *
 * This function returns the bitmap representing the active physical CPUs.
 *
 * @return A 64-bit unsigned integer representing the bitmap of active physical CPUs.
 *
 * @pre N/A
 * @post N/A
 */
uint64_t get_active_pcpu_bitmap(void)
{
	return pcpu_active_bitmap;
}

/**
 * @brief The internal function to enable \#AC (Alignment Check) for split lock detection.
 *
 * This function enables the alignment check for split lock detection if the split lock detection feature is supported.
 * It reads the current value of the MSR_TEST_CTL register, sets the AC_SPLITLOCK bit, and writes the updated value
 * back to the MSR_TEST_CTL register.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 * @remark This function is only available if CONFIG_SPLIT_LOCK_DETECTION_ENABLED is defined.
 */
static void enable_ac_for_splitlock(void)
{
#ifdef CONFIG_SPLIT_LOCK_DETECTION_ENABLED
	uint64_t test_ctl;

	if (has_core_cap(CORE_CAP_SPLIT_LOCK)) {
		test_ctl = msr_read(MSR_TEST_CTL);
		test_ctl |= MSR_TEST_CTL_AC_SPLITLOCK;
		msr_write(MSR_TEST_CTL, test_ctl);
	}
#endif /*CONFIG_SPLIT_LOCK_DETECTION_ENABLED*/
}

/**
 * @brief The internal function to enable \#GP (General Protection) fault for uclock.
 *
 * This function enables the general protection fault for uclock if the UC_LOCK detection is enabled and the CPU has
 * the UC_LOCK capability. It reads the current value of the MSR_TEST_CTL register, sets the MSR_TEST_CTL_GP_UCLOCK
 * bit, and writes the updated value back to the MSR_TEST_CTL register.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 * @remark This function is only available when CONFIG_UC_LOCK_DETECTION_ENABLED is defined.
 */
static void enable_gp_for_uclock(void)
{
#ifdef CONFIG_UC_LOCK_DETECTION_ENABLED
	uint64_t test_ctl;

	if (has_core_cap(CORE_CAP_UC_LOCK)) {
		test_ctl = msr_read(MSR_TEST_CTL);
		test_ctl |= MSR_TEST_CTL_GP_UCLOCK;
		msr_write(MSR_TEST_CTL, test_ctl);
	}
#endif /*CONFIG_UC_LOCK_DETECTION_ENABLED*/
}

/**
 * @brief The interface to perform the physical CPU's early stage initialization.
 *
 * This function performs the necessary initialization for a physical CPU (pCPU) before it starts executing the
 * hypervisor. The initialization steps differ depending on whether the CPU is the Bootstrap Processor (BSP) or an
 * Application Processor (AP).
 *
 * @param[in] is_bsp A boolean indicating if the CPU is the Bootstrap Processor (BSP).
 *               - true: The CPU is the BSP.
 *               - false: The CPU is an AP.
 *
 * The initialization steps for the BSP include:
 * - Setting the BSP CPU ID.
 * - Getting the CPU start tick counter.
 * - Initializing CPU capabilities through CPUID.
 * - Detecting hardware support.
 * - Initializing the CPU model name.
 * - Initializing the frequency policy.
 * - Initializing the E820 memory map.
 * - Reserving the PPT buffer from E820.
 * - Initializing hypervisor paging.
 * - Initializing the UART.
 * - Initializing the Local APIC.
 * - Initializing ACPI.
 * - Parsing and fixing up the ACPI table (if CONFIG_ACPI_PARSE_ENABLED is defined).
 * - Initializing the per-CPU Local APIC ID.
 * - Initializing the IOAPIC ID information.
 * - Switching to MMIO-based PCI configuration space access.
 *
 * The initialization steps for an AP include:
 * - Enabling paging using the same page tables set up by the BSP.
 * - Initializing the Local APIC.
 * - Getting the pCPU ID from the Local APIC ID.
 *
 * After the initializations, the function sets the state of the pCPU to initializing and marks the pCPU as active in
 * the active bitmap.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 * @remark This function may call panic() to halt the system if critical initialization steps fail.
 */
void init_pcpu_pre(bool is_bsp)
{
	uint16_t pcpu_id;
	int32_t ret;

	if (is_bsp) {
		pcpu_id = BSP_CPU_ID;
		start_tick = cpu_ticks();

		/* Get CPU capabilities thru CPUID, including the physical address bit
		 * limit which is required for initializing paging.
		 */
		init_pcpu_capabilities();

		if (detect_hardware_support() != 0) {
			panic("hardware not support!");
		}

		init_pcpu_model_name();

		init_frequency_policy();

		init_e820();

		/* reserve ppt buffer from e820 */
		allocate_ppt_pages();

		/* Initialize the hypervisor paging */
		init_paging();

		/*
		 * Need update uart_base_address here for vaddr2paddr mapping may changed
		 * WARNING: DO NOT CALL PRINTF BETWEEN ENABLE PAGING IN init_paging AND HERE!
		 */
		uart16550_init(false);

		early_init_lapic();

		init_acpi();
#ifdef CONFIG_ACPI_PARSE_ENABLED
		ret = acpi_fixup();
		if (ret != 0) {
			panic("failed to parse/fix up ACPI table!");
		}
#endif

		if (!init_percpu_lapic_id()) {
			panic("failed to init_percpu_lapic_id!");
		}

		ret = init_ioapic_id_info();
		if (ret != 0) {
			panic("System IOAPIC info is incorrect!");
		}

		/* NOTE: this must call after MMCONFIG is parsed in acpi_fixup() and before APs are INIT.
		 * We only support platform with MMIO based CFG space access.
		 * IO port access only support in debug version.
		 */
		pci_switch_to_mmio_cfg_ops();
	} else {

		/* Switch this CPU to use the same page tables set-up by the
		 * primary/boot CPU
		 */
		enable_paging();

		early_init_lapic();

		pcpu_id = get_pcpu_id_from_lapic_id(get_cur_lapic_id());
		if (pcpu_id >= MAX_PCPU_NUM) {
			panic("Invalid pCPU ID!");
		}
	}

	bitmap_set_lock(pcpu_id, &pcpu_active_bitmap);

	/* Set state for this CPU to initializing */
	pcpu_set_current_state(pcpu_id, PCPU_STATE_INITIALIZING);
}

/**
 * @brief The interface to perform the physical CPU's late stage initialization.
 *
 * This function performs various initialization tasks for each physical CPU (pCPU) after the boot process. It includes
 * setting up the stack protector, loading the Global Descriptor Table Register (GDTR) and Task Register (TR), enabling
 * \#AC split-lock and \#GP uclock protections, initializing XSAVE, and disabling RRSBA if retpoline is enabled.
 *
 * For the Boot Strap Processor (BSP), additional tasks are performed such as printing the hypervisor banner,
 * initializing the High Precision Event Timer (HPET), calibrating the Time Stamp Counter (TSC) frequency, initializing
 * interrupts, timers, thermal management, machine check events, notifications, IOMMU, PCI devices, SGX, and reserving
 * memory for EPT pages. It also starts all secondary cores and ensures they are properly initialized.
 *
 * For the Application Processor (AP), it initializes interrupts, timers, thermal management, machine check events, and
 * PCI devices.
 *
 * Finally, it applies the frequency policy, initializes the scheduler, sets up CLOS if RDT is enabled, enables SMEP
 * and SMAP, initializes KeyLocker, and synchronizes the initialization process across all pCPUs.
 *
 * @param[in] pcpu_id The ID of the physical CPU being initialized.
 *
 * @return None
 *
 * @pre pcpu_id == get_pcpu_id()
 * @post N/A
 * @remark This API can be called only after init_pcpu_pre() has been called once on the current logical processor.
 */
void init_pcpu_post(uint16_t pcpu_id)
{
#ifdef STACK_PROTECTOR
	set_fs_base();
#endif
	load_gdtr_and_tr();

	enable_ac_for_splitlock();
	enable_gp_for_uclock();

	init_pcpu_xsave();

#ifdef CONFIG_RETPOLINE
	disable_rrsba();
#endif

	if (pcpu_id == BSP_CPU_ID) {
		/* Print Hypervisor Banner */
		print_hv_banner();

		/* Initialie HPET */
		hpet_init();

		/* Calibrate TSC Frequency */
		calibrate_tsc();

		pr_acrnlog("%s, start time %luus", version_info, ticks_to_us(start_tick));

		pr_acrnlog("Detect processor: %s", (get_pcpu_info())->model_name);

		pr_dbg("Core %hu is up", BSP_CPU_ID);

		/* Warn for security feature not ready */
		if (!check_cpu_security_cap()) {
			pr_fatal("SECURITY WARNING!!!!!!");
			pr_fatal("Please apply the latest CPU uCode patch!");
		}

		/* Initialize interrupts */
		init_interrupt(BSP_CPU_ID);

		timer_init();
		thermal_init();
		init_machine_check_events();
		setup_notification();
		setup_pi_notification();

		if (init_iommu() != 0) {
			panic("failed to initialize iommu!");
		}

#ifdef CONFIG_IVSHMEM_ENABLED
		init_ivshmem_shared_memory();
#endif
		init_pci_pdev_list(); /* init_iommu must come before this */
		ptdev_init();

#ifdef CONFIG_SGX_ENABLED
		if (init_sgx() != 0) {
			panic("failed to initialize sgx!");
		}
#endif

		pcpu_sync = ALL_CPUS_MASK;
		/* Start all secondary cores */
		startup_paddr = prepare_trampoline();
		if (!start_pcpus(AP_MASK)) {
			panic("Failed to start all secondary cores!");
		}

		ASSERT(get_pcpu_id() == BSP_CPU_ID, "");
	} else {
		pr_dbg("Core %hu is up", pcpu_id);

		pr_warn("Skipping VM configuration check which should be done before building HV binary.");

		/* Initialize secondary processor interrupts. */
		init_interrupt(pcpu_id);

		timer_init();
		thermal_init();
		init_machine_check_events();
		ptdev_init();
	}

	apply_frequency_policy();

	init_sched(pcpu_id);

#ifdef CONFIG_RDT_ENABLED
	setup_clos(pcpu_id);
#endif

	enable_smep();

	enable_smap();

	init_keylocker();

	bitmap_clear_lock(pcpu_id, &pcpu_sync);
	/* Waiting for each pCPU has done its initialization before to continue */
	wait_sync_change(&pcpu_sync, 0UL);
}


/**
 * @brief Get the physical CPU ID whose local APIC ID matches the given one.
 *
 * This function iterates through the physical CPUs and compares their local APIC IDs with the specified one. If a
 * match is found, it returns the corresponding physical CPU ID.
 *
 * @param[in] lapic_id Local APIC ID to be used as a key to find the physical CPU ID.
 *
 * @return A uint16_t value indicating the physical CPU ID whose local APIC ID matches lapic_id.
 * @retval INVALID_CPU_ID If no physical CPU with the given local APIC ID is found.
 *
 * @pre N/A
 * @post N/A
 * @remark This function can be called only after init_percpu_lapic_id() has been called once on any processor.
 */

static uint16_t get_pcpu_id_from_lapic_id(uint32_t lapic_id)
{
	uint16_t i;
	uint16_t pcpu_id = INVALID_CPU_ID;

	for (i = 0U; i < phys_cpu_num; i++) {
		if (per_cpu(lapic_id, i) == lapic_id) {
			pcpu_id = i;
			break;
		}
	}

	return pcpu_id;
}

/**
 * @brief Start the specified physical CPU.
 *
 * This function updates the stack of the specified physical CPU and starts it by sending a startup IPI sequence to the
 * CPU. It then waits for the CPU to become active and sets the active bitmap or times out if the CPU does not start.
 *
 * @param[in] pcpu_id The physical CPU ID of the CPU that will be started.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
static void start_pcpu(uint16_t pcpu_id)
{
	uint32_t timeout;

	/* Update the stack for pcpu */
	stac();
	write_trampoline_stack_sym(pcpu_id);
	clac();

	/* Using the MFENCE to make sure trampoline code
	 * has been updated (clflush) into memory beforing start APs.
	 */
	cpu_memory_barrier();
	send_startup_ipi(pcpu_id, startup_paddr);

	/* Wait until the pcpu with pcpu_id is running and set the active bitmap or
	 * configured time-out has expired
	 */
	timeout = CPU_UP_TIMEOUT * 1000U;
	while (!is_pcpu_active(pcpu_id) && (timeout != 0U)) {
		/* Delay 10us */
		udelay(10U);

		/* Decrement timeout value */
		timeout -= 10U;
	}

	/* Check to see if expected CPU is actually up */
	if (!is_pcpu_active(pcpu_id)) {
		pr_fatal("Secondary CPU%hu failed to come up", pcpu_id);
		pcpu_set_current_state(pcpu_id, PCPU_STATE_DEAD);
	}
}


/**
 * @brief Start physical CPUs based on the given mask except itself.
 *
 * @param[in] mask A bitmask where each bit represents a pCPU to be started.
 *
 * @return true if all pCPUs specified in the mask are active, false otherwise.
 *
 * @pre N/A
 * @post N/A
 */
bool start_pcpus(uint64_t mask)
{
	uint16_t i;
	uint16_t pcpu_id = get_pcpu_id();
	uint64_t expected_start_mask = mask;

	i = ffs64(expected_start_mask);
	while (i != INVALID_BIT_INDEX) {
		bitmap_clear_nolock(i, &expected_start_mask);

		if (pcpu_id == i) {
			continue; /* Avoid start itself */
		}

		start_pcpu(i);
		i = ffs64(expected_start_mask);
	}

	return ((pcpu_active_bitmap & mask) == mask);
}

/**
 * @brief Set a physical CPU (pCPU) offline.
 *
 * This function sets the NEED_OFFLINE bit for the specified physical CPU. If the current CPU is not the specified one,
 * it sends an Inter-Processor Interrupt (IPI) or INIT signal to the specified CPU by calling kick_pcpu() to take the
 * offline process.
 *
 * @param[in] pcpu_id The ID of the physical CPU to be taken offline.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
void make_pcpu_offline(uint16_t pcpu_id)
{
	bitmap_set_lock(NEED_OFFLINE, &per_cpu(pcpu_flag, pcpu_id));
	if (get_pcpu_id() != pcpu_id) {
		kick_pcpu(pcpu_id);
	}
}


/**
 * @brief Test and clear the NEED_OFFLINE bit of the specified physical CPU.
 *
 * @param[in] pcpu_id The ID of the physical CPU to check.
 *
 * @return true if the NEED_OFFLINE bit was set and cleared, indicating the CPU needs to be taken offline.
 *         false if the NEED_OFFLINE bit was not set.
 *
 * @pre N/A
 * @post N/A
 */
bool need_offline(uint16_t pcpu_id)
{
	return bitmap_test_and_clear_lock(NEED_OFFLINE, &per_cpu(pcpu_flag, pcpu_id));
}


/**
 * @brief Wait for the specified physical CPUs (pCPUs) to go offline with a timeout of 100ms.
 *
 * @param[in] mask Bitmask of pCPUs that need to wait to go offline.
 *
 * @return 0 on success, -ETIMEDOUT if the timeout period expires before all specified pCPUs go offline.
 *
 * @pre (mask & ~(1 << get_pcpu_nums() - 1)) == 0
 * @post N/A
 */
int32_t wait_pcpus_offline(uint64_t mask)

{
	uint32_t timeout;
	int ret = 0;

	timeout = CPU_DOWN_TIMEOUT * 1000U;
	while (((pcpu_active_bitmap & mask) != 0UL) && (timeout != 0U)) {
		udelay(10U);
		timeout -= 10U;
	}

	if (timeout == 0) {
		ret = -ETIMEDOUT;
	}

	return ret;
}

/**
 * @brief Stop all physical CPUs (pCPUs) except the current one.
 *
 * This function iterates over all physical CPUs and stops them, except for the current CPU. It sets a bit in a mask
 * for each CPU to be stopped and then calls make_pcpu_offline() to stop the CPU. After all target CPUs are marked
 * offline, it waits for them to be fully offline using wait_pcpus_offline() with the mask as a parameter.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
void stop_pcpus(void)
{
	uint16_t pcpu_id;
	uint64_t mask = 0UL;

	for (pcpu_id = 0U; pcpu_id < phys_cpu_num; pcpu_id++) {
		if (get_pcpu_id() == pcpu_id) {	/* avoid offline itself */
			continue;
		}

		bitmap_set_nolock(pcpu_id, &mask);
		make_pcpu_offline(pcpu_id);
	}

	/**
	 * Timeout never occurs here:
	 *   If target cpu received a NMI and panic, it has called cpu_dead and make_pcpu_offline success.
	 *   If target cpu is running, an IPI will be delivered to it and then call cpu_dead.
	 */
	wait_pcpus_offline(mask);
}


/**
 * @brief Set up a linear address range to be monitored by hardware and activate the monitor.
 *
 * @param[in] addr The start address to be monitored.
 * @param[in] ecx Optional extensions of the MONITOR instruction.
 * @param[in] edx Optional hints of the MONITOR instruction.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
static inline void asm_monitor(volatile const uint64_t *addr, uint64_t ecx, uint64_t edx)
{
	asm volatile("monitor\n" : : "a" (addr), "c" (ecx), "d" (edx));
}

/**
 * @brief Call MWAIT with interrupt enabled in a safe way.
 *
 * STI is called right before the MWAIT instruction, so that events happen on the instruction boundary
 * are able to wake MWAIT.
 *
 * @param[in] eax A value containing hints such as the preferred optimized state the processor should enter.
 * @param[in] ecx Optional extensions for the MWAIT instruction.
 *
 * @return None
 */
static inline void asm_safe_mwait(uint64_t eax, uint64_t ecx)
{
	asm volatile("sti; mwait; cli\n" : : "a" (eax), "c" (ecx));
}

/**
 * @brief Get idle hint for a pCPU.
 *
 * This function returns idle hint for a pCPU for it to enter idle state by MWAIT.
 *
 * It goes through all vCPUs on that pCPU and finds out the shallowest idle hint.
 *
 * If no vcpu is launched on that pCPU, return 0xE0 to suggest the deepest Cx.
 *
 * @param[in] pcpu_id Id of pCPU to get the idle hint.
 *
 * @return Idle hint for the target pCPU.
 */
static uint32_t get_pcpu_idle_hint(uint16_t pcpu_id)
{
	uint32_t hint = 0xF0U;
	uint32_t i, launched_cnt = 0U;
	struct acrn_vcpu *vcpu;

	for (i = 0; i < CONFIG_MAX_VM_NUM; i++) {
		vcpu = per_cpu(vcpu_array[i], pcpu_id);
		if ((vcpu != NULL) && vcpu->launched) {
			hint = (vcpu->idle_hint < hint) ? vcpu->idle_hint : hint;
			launched_cnt ++;
		}
	}

	if (launched_cnt == 0) {
		/* Give a big hint to suggest the deepest Cx.*/
		hint = 0xE0U;
	}

	/* TODO
	 * SDM doest not regulate what the hint value should be valid or invalid.
	 * But the actual supported Cx can be enumerated by CPUID.05H:EDX, we may
	 * need to add valid check on hint value in the future.
	 */
	return hint;
}

/**
 * @brief Put the current physical CPU in an idle state.
 *
 * This function puts the current physical CPU in an idle state, by executing either of:
 *  - MWAIT, if mode_to_idle IDLE_MODE_CX.
 *  - PAUSE, if mode_to_idle is IDLE_MODE_PAUSE.
 *
 * When the pCPU goes to idle with MWAIT, an event (e.g. IRQ, IPI) is needed to break the
 * idle state.
 *
 * Note that MWAIT can be disabled by clearing host's MSR_IA32_MISC_ENABLE MONITOR_ENA
 * bit. That MSR bit is not supposed to be changed as it may cause unwanted results.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
void cpu_do_idle(void)
{
	uint16_t pcpu_id = get_pcpu_id();
	uint64_t dummy;

	if (per_cpu(mode_to_idle, pcpu_id) == IDLE_MODE_CX) {
		uint32_t hint = get_pcpu_idle_hint(pcpu_id);
		asm_monitor(&dummy, 0UL, 0UL);
		asm_safe_mwait(hint, 0UL);
	} else {
		/* IDLE_MODE_PAUSE only used in lapic_pt VMs. So we don't need to enable IRQ here. */
		asm_pause();
	}
}

/**
 * @brief Put the current physical CPU in a halt state.
 *
 * This function handles the shutdown process for current physical CPU (pCPU). It deinitializes the scheduler control
 * block. If current pCPU is in active state, it also flushes the cache, sets the CPU state to dead and clear active
 * bitmap in per_cpu_data, leaves VMX operation, and halts the CPU.
 * The function also includes a mechanism for debugging, allowing the CPU to be resumed using a JTAG probe if needed.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
void cpu_dead(void)
{
	/* For debug purposes, using a stack variable in the while loop enables
	 * us to modify the value using a JTAG probe and resume if needed.
	 */
	int32_t halt = 1;
	uint16_t pcpu_id = get_pcpu_id();

	deinit_sched(pcpu_id);
	if (bitmap_test(pcpu_id, &pcpu_active_bitmap)) {
		stac();
		flush_cache_range((void *)get_hv_image_base(), get_hv_image_size());
		clac();

		/* Set state to show CPU is dead */
		pcpu_set_current_state(pcpu_id, PCPU_STATE_DEAD);
		bitmap_clear_lock(pcpu_id, &pcpu_active_bitmap);

		/* clean up native stuff */
		/* In order to put pCPU dead, an INIT ipi will be sent to kick
		 * pCPU in x2apic mode when lapic_pt_enalbed is true. However,
		 * if the pCPU is already in idle thread, the INIT will be
		 * blocked until vmxoff is executed.
		 * */
		vmx_off();
		/* Halt the CPU */
		do {
			asm_hlt();
		} while (halt != 0);
	} else {
		pr_err("pcpu%hu already dead", pcpu_id);
	}
}

/**
 * @brief Set the current physical CPU ID.
 *
 * This function writes the given physical CPU ID to a pseudo MSR register.
 *
 * @param[in] pcpu_id The physical CPU ID to set.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
static void set_current_pcpu_id(uint16_t pcpu_id)
{
	/* Write pseudo MSR register */
	msr_write(ACRN_PSEUDO_PCPUID_MSR, (uint32_t) pcpu_id);
}

/**
 * @brief Print the boot message of the hypervisor.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
static void print_hv_banner(void)
{
	const char *boot_msg = "ACRN Hypervisor\n\r";

	/* Print the boot message */
	printf(boot_msg);
}

/**
 * @brief Enter an implementation-dependent optimized state.
 *
 * Make the processor stop instruction execution and enter an implementation-dependent optimized state until the
 * occurrence of a class of events.
 *
 * @param[in] eax A value containing hints such as the preferred optimized state the processor should enter.
 * @param[in] ecx Optional extensions for the MWAIT instruction.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
static
inline void asm_mwait(uint64_t eax, uint64_t ecx)
{
	asm volatile("mwait\n" : : "a" (eax), "c" (ecx));
}

/**
 * @brief Wait for a synchronization variable to change to a specific value.
 *
 * If the CPU supports the MONITOR/MWAIT instructions, the function will use these instructions to efficiently wait for
 * the synchronization variable to change. Otherwise, it will use a busy-wait loop with the PAUSE instruction.
 *
 * @param[in] sync Pointer to the synchronization variable.
 * @param[in] wake_sync The value to wait for.
 *
 * @return None
 *
 * @pre sync != NULL
 * @post N/A
 */
void wait_sync_change(volatile const uint64_t *sync, uint64_t wake_sync)
{
	if (pcpu_has_cap(X86_FEATURE_MONITOR)) {
		/* Wait for the event to be set using monitor/mwait */
		while ((*sync) != wake_sync) {
			asm_monitor(sync, 0UL, 0UL);
			if ((*sync) != wake_sync) {
				asm_mwait(0UL, 0UL);
			}
		}
	} else {
		while ((*sync) != wake_sync) {
			asm_pause();
		}
	}
}

/**
 * @brief Initialize XSAVE capabilities for the physical CPU (pCPU).
 *
 * This function initializes the XSAVE feature on the current pCPU if supported. It enables the OSXSAVE bit in CR4.
 * If current pCPU is the BSP, it checks the OSXSAVE bit from CPUID_FEATURES. If set, it updates the XCR0 and IA32_XSS
 * registers with the components bitmap obtained from CPUID. It also verifies the XSAVE area size, ensuring that it
 * does not exceed the pre-allocated 4K region.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 * @remark This function should be called during the CPU initialization process.
 */
 static void init_pcpu_xsave(void)
{
	uint64_t val64;
	struct cpuinfo_x86 *cpu_info;
	uint64_t xcr0, xss;
	uint32_t eax, ecx, unused, xsave_area_size;

	if (pcpu_has_cap(X86_FEATURE_XSAVE)) {
		CPU_CR_READ(cr4, &val64);
		val64 |= CR4_OSXSAVE;
		CPU_CR_WRITE(cr4, val64);

		if (get_pcpu_id() == BSP_CPU_ID) {
			cpuid_subleaf(CPUID_FEATURES, 0x0U, &unused, &unused, &ecx, &unused);

			/* if set, update it */
			if ((ecx & CPUID_ECX_OSXSAVE) != 0U) {
				cpu_info = get_pcpu_info();
				cpu_info->cpuid_leaves[FEAT_1_ECX] |= CPUID_ECX_OSXSAVE;

				/* set xcr0 and xss with the components bitmap get from cpuid */
				xcr0 = ((uint64_t)cpu_info->cpuid_leaves[FEAT_D_0_EDX] << 32U)
					+ cpu_info->cpuid_leaves[FEAT_D_0_EAX];
				xss = ((uint64_t)cpu_info->cpuid_leaves[FEAT_D_1_EDX] << 32U)
					+ cpu_info->cpuid_leaves[FEAT_D_1_ECX];
				write_xcr(0, xcr0);
				msr_write(MSR_IA32_XSS, xss);

				/* get xsave area size, containing all the state components
				 * corresponding to bits currently set in XCR0 | IA32_XSS */
				cpuid_subleaf(CPUID_XSAVE_FEATURES, 1U,
					&eax,
					&xsave_area_size,
					&ecx,
					&unused);
				if (xsave_area_size > XSAVE_STATE_AREA_SIZE) {
					panic("XSAVE area (%d bytes) exceeds the pre-allocated 4K region\n",
							xsave_area_size);
				}
			}
		}
	}
}

/**
 * @brief Initialize Keylocker feature if supported.
 *
 * This function checks if the Keylocker feature is supported by the CPU. If supported, it enables the Keylocker
 * feature by setting the KL bit in the CR4 control register.
 * @return None
 *
 * @pre N/A
 * @post N/A
 * @remark This function should be called during the CPU initialization process.
 */
static void init_keylocker(void)
{
	uint64_t val64;

	/* Enable host CR4.KL if keylocker feature is supported */
	if (pcpu_has_cap(X86_FEATURE_KEYLOCKER)) {
		CPU_CR_READ(cr4, &val64);
		val64 |= CR4_KL;
		CPU_CR_WRITE(cr4, val64);
	}
}

/**
 * @brief Write a Model-Specific Register (MSR) on the current physical CPU (pCPU).
 *
 * This function is intended to be called in an SMP (Symmetric Multi-Processing) call context where it writes the value
 * of the specified MSR stored in the provided data structure.
 *
 * @param[in] data A pointer to a struct msr_data_struct containing the MSR index and the value to write.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
static void smpcall_write_msr_func(void *data)
{
	struct msr_data_struct *msr = (struct msr_data_struct *)data;

	msr_write(msr->msr_index, msr->write_val);
}

/**
 * @brief Write a Model-Specific Register (MSR) on a specified physical CPU (pCPU).
 *
 * If the specified CPU is the current CPU, the MSR is written directly. Otherwise, it uses an SMP
 * (Symmetric Multi-Processing) call to write the MSR on the target CPU.
 *
 * @param[in] msr_index The index of the MSR to write.
 * @param[in] value64 The 64-bit value to write to the MSR.
 * @param[in] pcpu_id The ID of the target physical CPU.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
void msr_write_pcpu(uint32_t msr_index, uint64_t value64, uint16_t pcpu_id)
{
	struct msr_data_struct msr = {0};
	uint64_t mask = 0UL;

	if (pcpu_id == get_pcpu_id()) {
		msr_write(msr_index, value64);
	} else {
		msr.msr_index = msr_index;
		msr.write_val = value64;
		bitmap_set_nolock(pcpu_id, &mask);
		smp_call_function(mask, smpcall_write_msr_func, &msr);
	}
}

/**
 * @brief Read the value of a Model-Specific Register (MSR) on the current physical CPU (pCPU).
 *
 * This function is intended to be called in an SMP (Symmetric Multi-Processing) call context where it reads the
 * value of the specified MSR and stores it in the provided data structure.
 *
 * @param[inout] data Pointer to a structure containing the MSR index and a field to store the read value.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
static void smpcall_read_msr_func(void *data)
{
	struct msr_data_struct *msr = (struct msr_data_struct *)data;

	msr->read_val = msr_read(msr->msr_index);
}

/**
 * @brief Read the value of a Model-Specific Register (MSR) from a specified physical CPU (pCPU).
 *
 * If the specified physical CPU is the current CPU, it directly reads the MSR. Otherwise, it uses an SMP
 * (Symmetric Multi-Processing) call to read the MSR on the specified CPU.
 *
 * @param[in] msr_index A uint32_t value indicating the index of the MSR to read.
 * @param[in] pcpu_id A uint16_t value indicating the ID of the physical CPU from which to read the MSR.
 *
 * @return The value of the specified MSR.
 *
 * @pre N/A
 * @post N/A
 */
uint64_t msr_read_pcpu(uint32_t msr_index, uint16_t pcpu_id)
{
	struct msr_data_struct msr = {0};
	uint64_t mask = 0UL;
	uint64_t ret = 0;

	if (pcpu_id == get_pcpu_id()) {
		ret = msr_read(msr_index);
	} else {
		msr.msr_index = msr_index;
		bitmap_set_nolock(pcpu_id, &mask);
		smp_call_function(mask, smpcall_read_msr_func, &msr);
		ret = msr.read_val;
	}

	return ret;
}

/**
 * @}
 */
