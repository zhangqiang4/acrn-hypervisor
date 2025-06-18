/*
 * Copyright (C) 2018-2024 Intel Corporation.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <acrn_common.h>
#include <asm/default_acpi_info.h>
#include <platform_acpi_info.h>
#include <asm/per_cpu.h>
#include <asm/io.h>
#include <asm/msr.h>
#include <asm/pgtable.h>
#include <asm/host_pm.h>
#include <asm/trampoline.h>
#include <asm/vmx.h>
#ifdef HV_DEBUG
#include <console.h>
#endif
#include <asm/ioapic.h>
#include <asm/vtd.h>
#include <asm/lapic.h>
#include <asm/tsc.h>
#include <delay.h>
#include <asm/board.h>
#include <asm/cpuid.h>
#include <asm/platform_caps.h>

/**
 * @addtogroup hwmgmt_pm
 *
 * @{
 */

/**
 * @file
 *
 * @brief Implementation of host power and performance management.
 *
 * This file provides Functions for managing host machine power state, setting CPU performance policy
 * and accessing state tables.
 */

/**
 * @brief CPU context
 *
 * The CPU context is used to store/restore CPU context of BSP (Boot Strap Processor) for host S3.
 */
struct cpu_context cpu_ctx;

/**
 * @brief Platform S3 resume flag
 *
 * This platform S3 resume flag indicates whether current boot is from S3 resume or not.
 */
bool platform_s3_resume;


/**
 * @brief Host power management state (Sx) data.
 *
 * This data stores information for host power management state (Sx state defined by ACPI),
 * The content should match host ACPI table. We initialize it using value dumped from native
 * ACPI, except for except S3_PKG_VAL_PM1A/S3_PKG_VAL_PM1B and
 * S5_PKG_VAL_PM1A/S5_PKG_VAL_PM1B which are hard-coded.
 * When CONFIG_ACPI_PARSE_ENABLED is enabled, pm1a_evt/pm1a_cnt/wake_vector_32/wake_vector_64
 * will be initialized with the values from parsing ACPI table at runtime.
 */
static struct pm_s_state_data host_pm_s_state = {
	.pm1a_evt = {
		.space_id = PM1A_EVT_SPACE_ID,
		.bit_width = PM1A_EVT_BIT_WIDTH,
		.bit_offset = PM1A_EVT_BIT_OFFSET,
		.access_size = PM1A_EVT_ACCESS_SIZE,
		.address = PM1A_EVT_ADDRESS
	},
	.pm1b_evt = {
		.space_id = PM1B_EVT_SPACE_ID,
		.bit_width = PM1B_EVT_BIT_WIDTH,
		.bit_offset = PM1B_EVT_BIT_OFFSET,
		.access_size = PM1B_EVT_ACCESS_SIZE,
		.address = PM1B_EVT_ADDRESS
	},
	.pm1a_cnt = {
		.space_id = PM1A_CNT_SPACE_ID,
		.bit_width = PM1A_CNT_BIT_WIDTH,
		.bit_offset = PM1A_CNT_BIT_OFFSET,
		.access_size = PM1A_CNT_ACCESS_SIZE,
		.address = PM1A_CNT_ADDRESS
	},
	.pm1b_cnt = {
		.space_id = PM1B_CNT_SPACE_ID,
		.bit_width = PM1B_CNT_BIT_WIDTH,
		.bit_offset = PM1B_CNT_BIT_OFFSET,
		.access_size = PM1B_CNT_ACCESS_SIZE,
		.address = PM1B_CNT_ADDRESS
	},
	.s3_pkg = {
		.val_pm1a = S3_PKG_VAL_PM1A,
		.val_pm1b = S3_PKG_VAL_PM1B,
		.reserved = S3_PKG_RESERVED
	},
	.s5_pkg = {
		.val_pm1a = S5_PKG_VAL_PM1A,
		.val_pm1b = S5_PKG_VAL_PM1B,
		.reserved = S5_PKG_RESERVED
	},
	.wake_vector_32 = (uint32_t *)WAKE_VECTOR_32,
	.wake_vector_64 = (uint64_t *)WAKE_VECTOR_64,
	.pm1a_cnt_ready = 0U
};

/**
 * @brief Host ACPI reset register information data.
 *
 * This data stores host reset register information.
 * The content should match host ACPI table.
 * It is initialized by value dumped from native ACPI. When CONFIG_ACPI_PARSE_ENABLED is enabled,
 * it will be initialized with the values from parsing ACPI table at runtime.
 */
static struct acpi_reset_reg host_reset_reg = {
	.reg = {
		.space_id = RESET_REGISTER_SPACE_ID,
		.bit_width = RESET_REGISTER_BIT_WIDTH,
		.bit_offset = RESET_REGISTER_BIT_OFFSET,
		.access_size = RESET_REGISTER_ACCESS_SIZE,
		.address = RESET_REGISTER_ADDRESS,
	},
	.val = RESET_REGISTER_VALUE
};

/**
 * @brief Get the host power management state data stored in host_pm_s_state.
 *
 * @return Pointer to the global variable host_pm_s_state.
 */
struct pm_s_state_data *get_host_sstate_data(void)
{
	return &host_pm_s_state;
}

/**
 * @brief Get the host ACPI reset register information data stored in host_reset_reg.
 *
 * @return Pointer to the global variable host_reset_reg.
 */
struct acpi_reset_reg *get_host_reset_reg_data(void)
{
	return &host_reset_reg;
}

/**
 * @brief Restores MSR registers that needs to be restored after waking up from S3.
 *
 * Value of MSR_IA32_FS_BASE is lost after resume from S3 and needs to be restored.
 * It is restored with the stack canary value associated with the current CPU,
 * which is stored in the global array per_cpu_data[].
 *
 * @return None
 */
void restore_msrs(void)
{
#ifdef STACK_PROTECTOR
	struct stack_canary *psc = &get_cpu_var(stk_canary);

	msr_write(MSR_IA32_FS_BASE, (uint64_t)psc);
#endif
}

/**
 * @brief Write to an ACPI generic address (GAS).
 *
 * This is a helper function to write an ACPI generic address.
 * ACPI generic address (GAS) is a ACPI data structure to describe register locations.
 *
 * This function only support address space type memory and portio, and register width larger than 16.
 * If register width is larger than 16 bit, this function writes the low 16 bit only.
 *
 * @param[in] gas Stores register address information.
 * @param[in] val Value to be written to the register.
 *
 * @pre gas != NULL
 * @pre (gas->space_id == SPACE_SYSTEM_MEMORY) || (gas->space_id == SPACE_SYSTEM_IO)
 * @pre gas->bit_width >= 16
 *
 * @return None
 */
static void acpi_gas_write(const struct acrn_acpi_generic_address *gas, uint32_t val)
{
	uint16_t val16 = (uint16_t)val;

	if (gas->space_id == SPACE_SYSTEM_MEMORY) {
		mmio_write16(val16, hpa2hva(gas->address));
	} else {
		pio_write16(val16, (uint16_t)gas->address);
	}
}

/**
 * @brief Read from an ACPI generic address (GAS).
 *
 * This is a helper function to read an ACPI generic address.
 * ACPI generic address (GAS) is a ACPI data structure to describe register locations.
 *
 * This function only support address space type memory and portio, and register width larger than 16.
 * If register width is larger than 16 bit, this function reads the low 16 bit only.
 *
 * @param[in] gas Stores register address information.
 *
 * @return The register value.
 *
 * @pre gas != NULL
 * @pre (gas->space_id == SPACE_SYSTEM_MEMORY) || (gas->space_id == SPACE_SYSTEM_IO)
 * @pre gas->bit_width >= 16
 */
static uint32_t acpi_gas_read(const struct acrn_acpi_generic_address *gas)
{
	uint32_t ret = 0U;

	if (gas->space_id == SPACE_SYSTEM_MEMORY) {
		ret = mmio_read16(hpa2hva(gas->address));
	} else {
		ret = pio_read16((uint16_t)gas->address);
	}

	return ret;
}


/**
 * @brief Host enter Sx.
 *
 * This function makes host enter enter Sx by writing ACPI PM control registers with the specified values
 * given by pm1a_cnt_val and pm1b_cnt_val. In case the CPU resume running again (e.g. wake from S3),
 * it waits for transition out of working state by polling the ACPI PM event registers.
 *
 * @param[in] sstate_data Stores host Sx management information data.
 * @param[in] pm1a_cnt_val PM1a_CNT value that will decide the system Sx type.
 * @param[in] pm1b_cnt_val PM1b_CNT value that will decide the system Sx type.
 *
 * @return None
 *
 * @pre sstate_data != NULL
 */
void do_acpi_sx(const struct pm_s_state_data *sstate_data, uint32_t pm1a_cnt_val, uint32_t pm1b_cnt_val)
{
	uint32_t s1, s2;

	acpi_gas_write(&(sstate_data->pm1a_cnt), pm1a_cnt_val);

	if (sstate_data->pm1b_cnt.address != 0U) {
		acpi_gas_write(&(sstate_data->pm1b_cnt), pm1b_cnt_val);
	}

	do {
		/* polling PM1 state register to detect whether
		 * the Sx state enter is interrupted by wakeup event.
		 */
		s1 = acpi_gas_read(&(sstate_data->pm1a_evt));

		if (sstate_data->pm1b_evt.address != 0U) {
			s2 = acpi_gas_read(&(sstate_data->pm1b_evt));
			s1 |= s2;
		}

		/* According to ACPI spec 4.8.3.1.1 PM1 state register, the bit
		 * WAK_STS(bit 15) is set if system will transition to working
		 * state.
		 */
	} while ((s1 & (1U << BIT_WAK_STS)) == 0U);
}

/**
 * @brief System PM1A Control register value
 *
 * This global variable stores PM1a Control register value that makes the host enter S5.
 * The value is saved when service OS writes the register to initiate shutdown.
 */
static uint32_t system_pm1a_cnt_val;

/**
 * @brief System PM1B Control register value
 *
 * This global variable stores PM1b Control register value that makes the host enter S5.
 * The value is saved when service OS writes the register to initiate shutdown.
 */
static uint32_t system_pm1b_cnt_val;

/**
 * @brief Save PM1A/B Control register value
 *
 * Save pm1a/b_cnt value that is to be used to enter S5.
 * Stored in system_pm1a_cnt_val/system_pm1b_cnt_val
 *
 * @param[in] pm1a_cnt_val PM1a_CNT value that will be saved.
 * @param[in] pm1b_cnt_val PM1b_CNT value that will be saved.
 *
 * @return None
 */
void save_pm1_reg_val(uint32_t pm1a_cnt_val, uint32_t pm1b_cnt_val)
{
	system_pm1a_cnt_val = pm1a_cnt_val;
	system_pm1b_cnt_val = pm1b_cnt_val;
}

/**
 * @brief Shutdown the host system.
 *
 * When all VMs are shutdown, this function is called to perform system shutdown,
 * by using ACPI defined methods.
 * Host PM register information is dumped form host ACPI.
 * Shutdown reg values are stored in system_pm1a_cnt_val/system_pm1b_cnt_val,
 * which are trapped from service OS.
 *
 * @return None
 */
void shutdown_system(void)
{
	struct pm_s_state_data *sx_data = get_host_sstate_data();
	do_acpi_sx(sx_data, system_pm1a_cnt_val, system_pm1b_cnt_val);
}

/**
 * @brief Suspend the TSC timer before host S3.
 *
 * This function will save the host TSC value of the current physical core in the per_cpu_data[].
 *
 * @param[in] data Unused parameter for callback of smp_call_function.
 *
 * @return None
 */
static void suspend_tsc(__unused void *data)
{
	per_cpu(tsc_suspend, get_pcpu_id()) = rdtsc();
}

/**
 * @brief Resume the TSC timer after host S3.
 *
 * This MSR_IA32_TIME_STAMP_COUNTER MSR value is updated to the value we stored
 * by suspend_tsc().
 *
 * @param[in] data Unused parameter for callback of smp_call_function.
 *
 * @return None
 */
static void resume_tsc(__unused void *data)
{
	msr_write(MSR_IA32_TIME_STAMP_COUNTER, per_cpu(tsc_suspend, get_pcpu_id()));
}


/**
 * @brief Make the host enter s3 state.
 *
 * host_enter_s3 is main function for ACRN to enter S3 state with following process:
 * - Update the waking vector to point to the trampoline code entry where CPU is running in realmode
 * - Offline APs
 * - Backup and update the main entry of trampoline to resume entry. After BSP is resume from S3,
 *   it will jump to resume entry instead of AP startup routine
 * - Turn off VMX
 * - Suspend devices
 * - Save states
 * - Enter S3
 *
 * Exit S3 with following process:
 * - Restore states
 * - Resume devices
 * - Enable VMX
 * - Restore the main entry of trampoline to AP startup routine
 * - Online APs
 *
 * @return None
 *
 * @pre sstate_data != NULL
 */
void host_enter_s3(void)
{
	uint64_t pmain_entry_saved;
	struct pm_s_state_data *sstate_data = get_host_sstate_data();

	stac();

	/* set ACRN wakeup vec instead */
	*(sstate_data->wake_vector_32) = (uint32_t)get_trampoline_start16_paddr();

	clac();

	/* Save TSC on all PCPU */
	smp_call_function(get_active_pcpu_bitmap(), suspend_tsc, NULL);

	/* offline all APs */
	stop_pcpus();

	stac();
	/* Save default main entry and we will restore it after
	 * back from S3. So the AP online could jmp to correct
	 * main entry.
	 */
	pmain_entry_saved = read_trampoline_sym(main_entry);

	/* Set the main entry for resume from S3 state */
	write_trampoline_sym(main_entry, (uint64_t)restore_s3_context);
	clac();

	CPU_IRQ_DISABLE();
	vmx_off();

#ifdef HV_DEBUG
	suspend_console();
#endif
	suspend_vrtc();
	suspend_sched();
	suspend_ioapic();
	suspend_iommu();
	suspend_lapic();

	asm_enter_s3(sstate_data, system_pm1a_cnt_val, system_pm1b_cnt_val);

	platform_s3_resume = true;

	resume_lapic();
	resume_iommu();
	resume_ioapic();
	init_frequency_policy();

	vmx_on();
	CPU_IRQ_ENABLE();

	/* restore the default main entry */
	stac();
	write_trampoline_sym(main_entry, pmain_entry_saved);
	clac();

	/* online all APs again */
	if (!start_pcpus(AP_MASK)) {
		panic("Failed to start all APs!");
	}

	/* Restore TSC on all PCPU
	 * Caution: There should no timer setup before TSC resumed.
	 */
	smp_call_function(get_active_pcpu_bitmap(), resume_tsc, NULL);

	/* console must be resumed after TSC restored since it will setup timer base on TSC */
	resume_sched();
	resume_vrtc();
#ifdef HV_DEBUG
	resume_console();
#endif
}

/**
 * @brief Reset the host.
 *
 * This function resets the host by either:
 * - ACPI reset. The ACPI reset information is obtained from host ACPI.
 * - If ACPI not supported, use Intel legacy reset reg 0xCF9.
 * Does not work if neither way is supported.
 *
 * @param[in] warm True for warm boot when using 0xCF9 reset.
 *
 * @return None
 */
void reset_host(bool warm)
{
	struct acrn_acpi_generic_address *gas = &(host_reset_reg.reg);
	uint8_t reboot_code = warm ? CF9_RESET_WARM : CF9_RESET_COLD;

	/* TODO: gracefully shut down all guests before doing host reset. */

	/*
	 * Assumption:
	 * The platform we are running must support at least one of reset method:
	 *   - ACPI reset
	 *   - 0xcf9 reset
	 */
	if ((gas->space_id == SPACE_SYSTEM_IO) &&
		(gas->bit_width == 8U) && (gas->bit_offset == 0U) &&
		(gas->address != 0U) && (gas->address != 0xcf9U)) {
		pio_write8(host_reset_reg.val, (uint16_t)host_reset_reg.reg.address);
	} else {
		/* making sure bit 2 (RST_CPU) is '0', when the reset command is issued. */
		pio_write8(0x2U, 0xcf9U);
		udelay(50U);
		pio_write8(reboot_code, 0xcf9U);
		udelay(50U);
	}

	pr_fatal("%s(): can't reset host.", __func__);
	while (true) {
		asm_pause();
	}
}

/**
 * @brief CPU frequency policy.
 *
 * Stores CPU frequency policy that ACRN applies to the host CPU.
 */
static enum acrn_cpufreq_policy_type cpufreq_policy = CPUFREQ_POLICY_PERFORMANCE;

/**
 * @brief Initialize CPU frequency policy.
 *
 * ACRN manages CPU frequency by HWP or EIST.
 * This function is to be called by boot CPU to Initialize CPU frequency policy when system boot.
 * Including:
 * - Set frequency policy based on cmdline passed to ACRN
 *    cpu_perf_policy=Nominal or cpu_perf_policy=Performance
 * - Enable HWP if supported. MSR_IA32_PM_ENABLE is a global MSR.
 * After this, each CPU core will apply frequency policy based on its factor.
 * See apply_frequency_policy() for more details.
 *
 * @return None
 */
void init_frequency_policy(void)
{
	uint32_t cpuid_06_eax, unused;
	struct acrn_boot_info *abi = get_acrn_boot_info();
	const char *cmd_src = abi->cmdline;

	/*
	 * Parse cmdline, decide which policy type to use.
	 * User can either specify cpu_perf_policy=Nominal or cpu_perf_policy=Performance
	 * The default type is 'Performance'
	 */
	if(strstr_s(cmd_src, MAX_BOOTARGS_SIZE, "cpu_perf_policy=Nominal", 24U) != NULL) {
		cpufreq_policy = CPUFREQ_POLICY_NOMINAL;
	}

	cpuid_subleaf(CPUID_THERMAL_POWER, 0U, &cpuid_06_eax, &unused, &unused, &unused);
	if ((cpuid_06_eax & CPUID_EAX_HWP) != 0U) {
		/* If HWP is available, enable HWP early. This will unlock other HWP MSRs. */
		msr_write(MSR_IA32_PM_ENABLE, 1U);
	}
}

/**
 * @brief Apply CPU frequency policy.
 *
 * This Function is to be called by each pCPU after init_cpufreq().
 * It applies the frequency policy, which can be specified from boot parameters.
 *   - cpu_perf_policy=Performance: HWP autonomous selection, between highest HWP level and
 *     lowest HWP level.
 *   - cpu_perf_policy=Nominal: frequency is fixed to guaranteed HWP level.
 * The default policy is 'Performance'.
 *
 * ACRN will not be governing pCPU's frequency after this.
 *
 * @return None
 */
void apply_frequency_policy(void)
{
	struct acrn_cpufreq_limits *limits = &cpufreq_limits[get_pcpu_id()];
	uint64_t highest_lvl_req = limits->highest_hwp_lvl, lowest_lvl_req = limits->lowest_hwp_lvl, reg;
	uint32_t cpuid_06_eax, cpuid_01_ecx, unused;

	cpuid_subleaf(CPUID_THERMAL_POWER, 0U, &cpuid_06_eax, &unused, &unused, &unused);
	cpuid_subleaf(CPUID_FEATURES, 0U, &unused, &unused, &cpuid_01_ecx, &unused);
	/* only support HWP. */
	if ((cpuid_06_eax & CPUID_EAX_HWP) != 0U) {
		/*
		 * For Performance policy(default): CPU frequency will be autonomously selected between highest and lowest
		 * For Nominal policy: set to fixed frequency by letting highest=lowest=guaranteed
		 */
		if (cpufreq_policy == CPUFREQ_POLICY_NOMINAL) {
			highest_lvl_req = limits->guaranteed_hwp_lvl;
			lowest_lvl_req = limits->guaranteed_hwp_lvl;
		}
		/* EPP(0x80: default) | Desired_Performance(0: HWP auto) | Maximum_Performance | Minimum_Performance */
		reg = (0x80UL << 24U) | (0x00UL << 16U) | (highest_lvl_req << 8U) | lowest_lvl_req;
	    msr_write(MSR_IA32_HWP_REQUEST, reg);
	} else {
		/* If no frequency interface is presented, just let CPU run by itself. Do nothing here.*/
	}
}

/**
 * @}
 */
