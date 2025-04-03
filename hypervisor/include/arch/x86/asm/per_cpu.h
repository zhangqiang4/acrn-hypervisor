/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef PER_CPU_H
#define PER_CPU_H

#include <types.h>
#include <sbuf.h>
#include <irq.h>
#include <timer.h>
#include <profiling.h>
#include <logmsg.h>
#include <schedule.h>
#include <asm/notify.h>
#include <asm/page.h>
#include <asm/gdt.h>
#include <asm/security.h>
#include <asm/vm_config.h>
/**
 * @addtogroup hwmgmt_cpu
 *
 * @{
 */

/**
 * @file
 * @brief Defines structures and implements per-CPU interface to hold per CPU information.
 */

/**
 * @brief The structure to hold all per CPU information.
 * @alignment PAGE_SIZE
 */
struct per_cpu_region {
	/* vmxon_region MUST be 4KB-aligned */
	uint8_t vmxon_region[PAGE_SIZE]; /**< Array that the logical processor uses to support VMX operation. */
	void *vmcs_run; /**< VMCS region used to record the Active Current VMCS for vCPU run on the logical
			 *   processor. */
	struct shared_buf *sbuf[ACRN_SBUF_PER_PCPU_ID_MAX]; /**< Shared buffer address array. */
	char logbuf[LOG_MESSAGE_MAX_SIZE]; /**< The buffer of log message per CPU. */
	char temp_logbuf[TEMP_LOG_BUFFER_SIZE][LOG_MESSAGE_MAX_SIZE]; /**< Temp log buffer per CPU. */
	uint32_t temp_logbuf_count;  /**< Temp log buffer counts. */
#ifdef HV_DEBUG
	uint32_t npk_log_ref; /**< Reference to calculate the channel offset of NPK log. */
#endif
	uint64_t irq_count[NR_IRQS]; /**< Counter of IRQ */
	uint64_t softirq_pending; /**< Bitmap indicating the number of the pending softirqs. */
	uint64_t spurious; /**< Count of spurious interrupts. */
	struct acrn_vcpu *ever_run_vcpu; /**< Pointer to acrn_vcpu data structure that runs on the current logical
					  *   processor. */
#ifdef STACK_PROTECTOR
	struct stack_canary stk_canary; /**< Structure including a 64-bit integer used to detect a stack buffer
					 *   overflow before execution of malicious code can occur. */
#endif
	struct per_cpu_timers cpu_timers; /**< Structure representing the head of timer list per CPU. */
	struct sched_control sched_ctl; /**< Scheduler control block of the physical processor. */
#ifdef CONFIG_SCHED_NOOP
	struct sched_noop_control sched_noop_ctl; /**< Records the thread object of the thread which will run or is
						   *   running on the physical processor while the noop scheduler is
						   *   configured. */
#endif
#ifdef CONFIG_SCHED_IORR
	struct sched_iorr_control sched_iorr_ctl; /**< Records the structure variable controlling the IO-sensitive
						   *   Round-robin scheduler. */
#endif
#ifdef CONFIG_SCHED_BVT
	struct sched_bvt_control sched_bvt_ctl; /**< Records the structure variable controlling the BVT (Borrowed
						 *   virtual time) scheduler. */
#endif
#ifdef CONFIG_SCHED_PRIO
	struct sched_prio_control sched_prio_ctl; /**< Records the structure variable controlling the priority-based
						   *   scheduler. */
#endif
	struct thread_object idle; /**< Structure representing the idle thread. */
	struct host_gdt gdt; /**< Structure representing the Global Descriptor Table used for the logical processor. */
	struct tss_64 tss; /**< Structure representing the Task State Segment used for the logical processor. */
	enum pcpu_boot_state boot_state; /**< Field indicating the per CPU boot state (halt or running). */
	uint64_t pcpu_flag; /**< Field to store the physical processor offline request or shutdown VM request. */
 	/**
 	 * @brief Stack used to handle machine check on the logical processor.
	 *
	 * @alignment 16
	 */
	uint8_t mc_stack[CONFIG_STACK_SIZE] __aligned(16);
 	/**
 	 * @brief Stack used to handle stack double fault on the logical processor.
	 *
	 * @alignment 16
	 */
	uint8_t df_stack[CONFIG_STACK_SIZE] __aligned(16);
	/**
	 * @brief Stack used to handle stack segment fault on the logical processor.
	 *
	 * @alignment 16
	 */
	uint8_t sf_stack[CONFIG_STACK_SIZE] __aligned(16);
	/**
	 * @brief Runtime stack on the logical processor.
	 *
	 * @alignment 16
	 */
	uint8_t stack[CONFIG_STACK_SIZE] __aligned(16);
	uint32_t lapic_id; /**< Local APIC ID. */
	uint32_t lapic_ldr; /**< Local destination register of local APIC. */
	uint32_t softirq_servicing; /**< Flag indicating whether the logical processor is handling softirq. */
	uint32_t mode_to_kick_pcpu; /**< Field indicating the kick-mode of the logical processor. */
	uint32_t mode_to_idle; /**< Field indicating the idle-mode of the logical processor. */
	struct smp_call_info_data smp_call_info; /**< Structure recording SMP call information. */
	struct list_head softirq_dev_entry_list; /**< Structure recording the list head of managing softirq. */
#ifdef PROFILING_ON
	struct profiling_info_wrapper profiling_info; /**< Structure recording the profiling information. */
#endif
	uint64_t shutdown_vm_bitmap; /**< Bitmap representing the VMs that request to be shutdown. */
	uint64_t tsc_suspend;  /**< Field used to record TSC value when entering suspend. */
	struct acrn_vcpu *whose_iwkey; /**< Pointer to acrn_vcpu data structure of vCPU which IWKey belongs to. */
	/**
	 * @brief Pointer array of acrn_vcpu data structure recording vCPUs information running on pCPU.
	 *
	 * vCPUs of a VM won't share the same pCPU. So the maximum possible number of vCPUs that can run on a pCPU is
	 * CONFIG_MAX_VM_NUM.
	 * vcpu_array address must be aligned to 64-bit for atomic access to avoid contention between offline_vcpu and
	 * posted interrupt handler.
	 *
	 * @alignment 8
	 */
	struct acrn_vcpu *vcpu_array[CONFIG_MAX_VM_NUM] __aligned(8);
} __aligned(PAGE_SIZE); /* per_cpu_region size aligned with PAGE_SIZE */

/**
 * @brief Declare a global variable per_cpu_data with the type 'struct per_cpu_region'.
 */
 extern struct per_cpu_region per_cpu_data[MAX_PCPU_NUM] __aligned(PAGE_SIZE);
/**
 * @brief Macro to get the specified field of per_cpu_data for the specified CPU.
 *
 * Implementation: Returns 'per_cpu_data[(pcpu_id)].name'
 *
 * @param[in] name The name of the specified field (whose data is to be fetched) in 'struct per_cpu_region'.
 * @param[in] pcpu_id The CPU id of the physical CPU that will be used.
 *
 * @return The specified name field of the per_cpu_data of the CPU whose CPU id is the specified pcpu_id.
 *
 * @pre N/A
 * @post N/A
 */
#define per_cpu(name, pcpu_id)	\
	(per_cpu_data[(pcpu_id)].name)
/**
 * @brief Macro to get the specified field of per_cpu_data for the current CPU.
 *
 * Implementation: Returns 'per_cpu(name, get_pcpu_id())'
 *
 * @param[in] name The name of the specified field (whose data is to be fetched) in 'struct per_cpu_region'.
 *
 * @return The specified name field of the per_cpu_data of the current CPU.
 *
 * @pre N/A
 * @post N/A
 */
#define get_cpu_var(name)	per_cpu(name, get_pcpu_id())

/**
 * @}
 */
#endif
