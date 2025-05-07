/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <asm/init.h>
#include <asm/per_cpu.h>
#include <console.h>
#include <shell.h>
#include <asm/vmx.h>
#include <asm/guest/vm.h>
#include <logmsg.h>
#include <boot.h>
#include <dmi.h>

/**
 * @addtogroup init
 *
 * @{
 */

/**
 * @file
 * @brief Initialization routines.
 *
 * This file contains the initialization functions for both the primary processor and secondary processors. It
 * includes functions to set up the debug environment, initialize the VMX mode, and initialize the pCPUs.
 */

/**
 * @brief This external variable boot_regs stores the values in the general purpose registers EAX and EBX,
 * respectively, at the time the hypervisor starts execution. These registers holding the Multiboot magic number
 * and the physical address of the Multiboot information structure.
 */
extern uint32_t boot_regs[2];

/**
 * @brief Switch to a new stack and jump to a specified function.
 *
 * This function-like macro performs a context switch by updating the stack pointer \p rsp and jumping to the
 * specified function address \p to. It also pushes a magic value onto the stack. In ACRN initialization progress,
 * it is used to switch stack at runtime for BSP.
 *
 * @param[in] rsp The pointer to the new stack.
 * @param[in] to The pointer to the target function.
 *
 * @return None
 *
 * @pre rsp != NULL
 * @pre to != NULL
 *
 * @post N/A
 */
#define SWITCH_TO(rsp, to)                                              \
{                                                                       \
	asm volatile ("movq %0, %%rsp\n"                                \
			"pushq %1\n"                                    \
			"jmpq *%2\n"                                    \
			 :                                              \
			 : "r"(rsp), "rm"(SP_BOTTOM_MAGIC), "a"(to));   \
}

#ifdef HV_DEBUG
/*TODO: move into debug module */
/**
 * @brief Initializes debug related stuff (console) before switching to runtime stack.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
static void init_debug_pre(void)
{
	/* Initialize console */
	console_init();
}

/*TODO: move into debug module */
/**
 * @brief Initializes debug related stuff.
 *
 * This function sets up the shell if the current pCPU is BP, configures the console timer if the current pCPU
 * is VUART_TIMER_CPU, and sets up profiling. This function is called for both BSP and APs.
 *
 * @param[in] pcpu_id The physical CPU ID.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
static void init_debug_post(uint16_t pcpu_id)
{
	if (pcpu_id == BSP_CPU_ID) {
		/* Initialize the shell */
		shell_init();
	}

	if (pcpu_id == VUART_TIMER_CPU) {
		console_setup_timer();
	}

	profiling_setup();
}
#endif

/*TODO: move into guest-vcpu module */
/**
 * @brief Initializes the VMX mode for the given pCPU.
 *
 * This function enables VMX operation for the current processor by calling vmx_on(). Then it launches the Service VM
 * and pre-launched VMs if their vBSP is configured on the current pCPU by calling launch_vms().
 *
 * @param[in] pcpu_id The physical CPU ID.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
static void init_guest_mode(uint16_t pcpu_id)
{
	vmx_on();

	launch_vms(pcpu_id);
}

/**
 * @brief Initializes the pCPU after switching to the runtime stack and enters the scheduler loop.
 *
 * This function finalizes the pCPU initialization process by:
 * - Getting the current pCPU ID
 * - Completing pCPU initialization with init_pcpu_post()
 * - Setting up debug facilities if enabled
 * - Enabling VMX operation and launching VMs through init_guest_mode()
 * - Entering the idle thread loop with run_idle_thread()
 *
 * By invoking this function, each pCPU runs an idle thread and is prepared for vCPU thread scheduling.
 *
 * @return None, as it never returns.
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark This function never returns as it enters the idle thread loop.
 */
static void init_pcpu_comm_post(void)
{
	uint16_t pcpu_id;

	pcpu_id = get_pcpu_id();

	init_pcpu_post(pcpu_id);
#ifdef HV_DEBUG
	init_debug_post(pcpu_id);
#endif
	init_guest_mode(pcpu_id);
	run_idle_thread();
}

/**
 * @brief Initializes miscellaneous settings.
 *
 * This function initializes flexible bits in CR0 and CR4 registers and ensures their patterns are sanitized.
 * If sanitization fails, the system will panic.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
static void init_misc(void)
{
	init_cr0_cr4_flexible_bits();
}

/**
 * @brief Initializes the BP.
 *
 * This function clears BSS, initializes boot info, calls init_debug_pre(), sanitize boot info, calls init_pcpu_pre()
 * and init_misc() in sequence. Then it switches to runtime stack. This function is called only for BSP.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark This function is using temp stack, and after SWITCH_TO(runtime_sp, to), it will switch to runtime stack.
 */
void init_primary_pcpu(void)
{
	uint64_t rsp;

	/* Clear BSS */
	(void)memset(&ld_bss_start, 0U, (size_t)(&ld_bss_end - &ld_bss_start));

	init_acrn_boot_info(boot_regs);

#ifdef HV_DEBUG
	init_debug_pre();
#endif

	if (sanitize_acrn_boot_info(get_acrn_boot_info()) != 0) {
		panic("Sanitize boot info failed!");
	}

#ifdef CONFIG_DMI_CHECK_ENABLED
	check_board_dmi_info(get_acrn_boot_info());
#endif

	init_pcpu_pre(true);

	init_misc();

	/* Switch to run-time stack */
	rsp = (uint64_t)(&get_cpu_var(stack)[CONFIG_STACK_SIZE - 1]);
	rsp &= ~(CPU_STACK_ALIGN - 1UL);
	SWITCH_TO(rsp, init_pcpu_comm_post);
}

/**
 * @brief Initializes the AP.
 *
 * This function calls init_pcpu_pre() to initialize the physical CPU (pCPU) specific settings, then calls
 * init_pcpu_comm_post() in sequence to finalize the initialization of the pCPU, including setting up debug, guest
 * mode, and running the idle thread. This function is called for each AP.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
void init_secondary_pcpu(void)
{
	init_pcpu_pre(false);
	init_pcpu_comm_post();
}

/**
 * @}
 */
