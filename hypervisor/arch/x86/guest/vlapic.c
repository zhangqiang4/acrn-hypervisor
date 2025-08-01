/*-
 * Copyright (c) 2011 NetApp, Inc.
 * Copyright (c) 2017-2022 Intel Corporation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#define pr_prefix		"vlapic: " /**< Prefix tag of vp-base.vlapic module for debug message purpose */

#include <types.h>
#include <errno.h>
#include <asm/lib/bits.h>
#include <asm/lib/atomic.h>
#include <asm/per_cpu.h>
#include <asm/pgtable.h>
#include <asm/lapic.h>
#include <asm/cpuid.h>
#include <asm/guest/vmcs.h>
#include <asm/guest/vlapic.h>
#include <asm/guest/virq.h>
#include <ptdev.h>
#include <asm/vmx.h>
#include <asm/guest/vm.h>
#include <asm/guest/ept.h>
#include <trace.h>
#include <logmsg.h>
#include <asm/irq.h>
#include <ticks.h>
#include "vlapic_priv.h"

/**
 * @defgroup vp-base_vlapic vp-base.vlapic
 * @ingroup vp-base
 * @brief Definitions and implementations for virtual LAPIC.
 *
 * This module provides external APIs for creating virtual LAPICs, calculating the
 * destination vlapic for a specified MSI interrupt, and handling the processing
 * functions when virtual LAPIC registers are accessed or modified.
 *
 * @{
 */

/**
 * @file
 * @brief Data structures and functions for the vp-base.vlapic module.
 *
 * This file implements the external APIs provided by the vp-base.vlapic module.
 * Additionally, it includes helper functions to support the implementation of
 * the features and functionalities offered by these external APIs.
 */

#define VLAPIC_VERBOS 0  /**< This macro is used dump virtual local APIC verbose debug message */

#define VLAPIC_VERSION        (16U)      /**< APIC version */
#define APICBASE_BSP          0x00000100UL /**< Mask of BSP bit in IA32_APIC_BASE MSR */
#define APICBASE_X2APIC       0x00000400UL /**< Mask of x2APIC bit in IA32_APIC_BASE MSR */
#define APICBASE_XAPIC        0x00000800UL /**< Mask of xAPIC bit in IA32_APIC_BASE MSR */
#define APICBASE_LAPIC_MODE   (APICBASE_XAPIC | APICBASE_X2APIC) /**< Mask bits APIC mode in IA32_APIC_BASE MSR */
#define APICBASE_ENABLED      0x00000800UL /**< Mask of APIC global enable/disable bit in IA32_APIC_BASE MSR */
#define LOGICAL_ID_MASK       0xFU        /**< Mask of logical ID bits in APIC ID */
#define CLUSTER_ID_MASK       0xFFFF0U    /**< Mask of cluster ID bits in APIC ID */
#define DBG_LEVEL_VLAPIC      6U          /**< Debug level for virtual LAPIC debug */

#define PID_TABLE_ENTRY_VALID	1UL  /**< Bitmask indicating a valid PID table entry */

/**
 * @brief Check whether x2APIC mode is enabled for the given virtual Local APIC.
 *
 * This function inspects the APIC base MSR of the given virtual LAPIC and determines
 * whether it is operating in x2APIC mode.
 *
 * The check confirms both xAPIC global enable (IA32_APIC_BASE[11]) and x2APIC enable (IA32_APIC_BASE[10]) bits are set.
 *
 * @param[in] vlapic Pointer to the virtual Local APIC structure to check.
 *
 * @return true if x2APIC mode is enabled; false otherwise.
 *
 * @pre vlapic != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
static bool is_x2apic_enabled(const struct acrn_vlapic *vlapic)
{
	bool ret = false;

	if ((vlapic_get_apicbase(vlapic) & APICBASE_LAPIC_MODE) == (APICBASE_X2APIC | APICBASE_XAPIC)) {
		ret = true;
	}

	return ret;
}

/**
 * @brief Get vcpu context pointer from vlapic context.
 *
 * @param[in] vlapic pointer to the vlapic to be checked.
 *
 * @return Pointer to the vcpu corresponding to the given vlapic.
 *
 * @pre vlapic != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
static inline struct acrn_vcpu *vlapic2vcpu(const struct acrn_vlapic *vlapic)
{
	return container_of(container_of(vlapic, struct acrn_vcpu_arch, vlapic), struct acrn_vcpu, arch);
}

#if VLAPIC_VERBOS
/**
 * @brief Dump values of vISR registers in the virtual APIC page.
 *
 * Logs the values of the ISR (In-Service Register) entries for the given
 * virtual LAPIC (`vlapic`). This provides debugging information about
 * the state of in-service interrupts in the APIC.
 *
 * @param[in] vlapic Pointer to the vlapic whose ISR registers are to be dumped.
 *                   This represents the virtual LAPIC to be inspected.
 * @param[in] msg    Pointer to a string used as a debug message tag, providing
 *                   context for the logged information.
 *
 * @return None.
 *
 * @pre vlapic != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
static inline void vlapic_dump_isr(const struct acrn_vlapic *vlapic, const char *msg)
{
	const struct lapic_reg *isrptr = &(vlapic->apic_page.isr[0]);

	for (uint8_t i = 0U; i < 8U; i++) {
		dev_dbg(DBG_LEVEL_VLAPIC, "%s isr%u 0x%08x", msg, i, isrptr[i].v);
	}
}
#else
/**
 * @brief Stub function for dumping vISR registers when verbose debugging is disabled.
 *
 * Logs nothing when `VLAPIC_VERBOS` is set to 0.
 *
 * @param[in] vlapic Pointer to the vlapic whose ISR registers would have been dumped.
 *                   This is unused in the stub function.
 * @param[in] msg    Pointer to a string used as a debug message tag. This is also unused.
 *
 * @return None.
 *
 * @pre vlapic != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
static inline void vlapic_dump_isr(__unused const struct acrn_vlapic *vlapic, __unused const char *msg) {}
#endif

/**
 * @brief Operation API instance for passthrough APIC features.
 *
 * This structure defines the APICv operation API instance, which provides the necessary
 * operations for guest interrupt handling when local APIC is pass-through to guest.
 */
static const struct acrn_apicv_ops ptapic_ops;
static const struct acrn_apicv_ops *apicv_ops;

static bool apicv_set_intr_ready(struct acrn_vlapic *vlapic, uint32_t vector);

static void apicv_trigger_pi_anv(uint16_t dest_pcpu_id, uint32_t anv);

static int32_t vlapic_x2apic_pt_icr_access(struct acrn_vcpu *vcpu, uint64_t val);
static void vlapic_x2apic_self_ipi_handler(struct acrn_vlapic *vlapic);


static void vlapic_set_error(struct acrn_vlapic *vlapic, uint32_t mask);

static void vlapic_timer_expired(void *data);

/**
 * @brief Check if vlapic is enabled.
 *
 * This function checks if the virtual LAPIC (vlapic) is enabled by verifying two conditions:
 * - The APICBASE MSR's enabled flag [bit11] is set.
 * - The SVR register's enabled flag [bit8] is set.
 * According to section 12.4.3 Enabling or Disabling the Local APIC, Volume 3,
 * Intel SDM 325462-087US.
 *
 * @param[in] vlapic Pointer to the vlapic structure that is being checked for being enabled.
 *
 * @return Returns true if the vlapic is enabled, otherwise returns false.
 *
 * @pre vlapic != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
static inline bool vlapic_enabled(const struct acrn_vlapic *vlapic)
{
	const struct lapic_regs *lapic = &(vlapic->apic_page);

	return (((vlapic->msr_apicbase & APICBASE_ENABLED) != 0UL) &&
			((lapic->svr.v & APIC_SVR_ENABLE) != 0U));
}

/**
 * @brief This function is for converting the vCPU ID to its virtual LAPIC pointer.
 *
 * This function looks up the virtual CPU structure (vcpu) based on the given vCPU ID
 * and then returns the associated pointer to LAPIC (vlapic) structure.
 *
 * @param[in] vm        Pointer to the virtual machine structure which the virtual CPU belongs to.
 *                      This is required to look up the virtual CPU in the correct VM.
 * @param[in] vcpu_id   The virtual CPU ID, which identifies a specific virtual CPU within the VM.
 *                      This ID is used to search for the corresponding vCPU in the given VM.
 *
 * @return The LAPIC structure corresponding to the specified virtual CPU.
 *
 * @retval NULL if no virtual CPU with the given vcpu_id exists in given VM.
 * @retval Non-NULL pointer to the LAPIC structure if a valid vCPU is found in given VM.
 *
 * @pre vm != NULL
 *
 * @post N/A
 *
 * @remark N/A
 *
 */
static struct acrn_vlapic *
vm_lapic_from_vcpu_id(struct acrn_vm *vm, uint16_t vcpu_id)
{
	struct acrn_vcpu *vcpu;

	vcpu = vcpu_from_vid(vm, vcpu_id);

	return vcpu_vlapic(vcpu);
}

/**
 * @brief This function is for converting the LAPIC ID to its vCPU ID.
 *
 * This function looks up a given LAPIC ID and returns the corresponding vCPU ID in a given VM.
 * If no vCPU is found with the given APIC ID, it returns INVALID_CPU_ID.
 *
 * @param[in] vm        Pointer to the virtual machine structure which the virtual
 *                      local APIC belongs to.
 * @param[in] lapicid   The LAPIC ID.
 *
 * @return The converted vCPU ID or INVALID_CPU_ID when there is no vCPU in the given vm.
 *
 * @pre vm != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
static uint16_t vm_apicid2vcpu_id(struct acrn_vm *vm, uint32_t lapicid)
{
	uint16_t i;
	struct acrn_vcpu *vcpu;
	uint16_t cpu_id = INVALID_CPU_ID;

	foreach_vcpu(i, vm, vcpu) {
		if (vcpu_vlapic(vcpu)->vapic_id == lapicid) {
			cpu_id = vcpu->vcpu_id;
			break;
		}
	}

	if (cpu_id == INVALID_CPU_ID) {
		pr_err("%s: bad lapicid %u", __func__, lapicid);
	}

	return cpu_id;

}

/**
 * @brief This function initializes the virtual local x2APIC ID Register and
 *        Logical Destination Register in x2APIC mode.
 *
 * This function sets the values for the virtual local x2APIC ID Register and
 * Logical Destination Register:
 * - Local x2APIC ID of virtual LAPIC is initialized with the x2APIC ID of physical local APIC
 * - Initializes Logical Destination Register (LDR) following section 12.12.10.2 Deriving Logical
 *   x2APIC ID from the Local x2APIC ID, Volume 3, Intel SDM 325462-087US.
 *
 * @param[inout] vlapic Pointer to the virtual LAPIC structure for configuration.
 *
 * @return None.
 *
 * @pre vlapic != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
static inline void vlapic_build_x2apic_id(struct acrn_vlapic *vlapic)
{
	struct lapic_regs *lapic;
	uint32_t logical_id, cluster_id;

	lapic = &(vlapic->apic_page);
	lapic->id.v = vlapic->vapic_id;
	logical_id = lapic->id.v & LOGICAL_ID_MASK;
	cluster_id = (lapic->id.v & CLUSTER_ID_MASK) >> 4U;
	lapic->ldr.v = (cluster_id << 16U) | (1U << logical_id);
}

/**
 * @brief Calculate the vector number of the highest priority bit set in the ISR
 *        (Interrupt Service Register) in virtual-APIC page.
 *
 * This function iterates over the Interrupt Service Register (ISR) to find the highest
 * priority interrupt that has its bit set. The priority of interrupts is determined by
 * the position of the set bit,with higher bit numbers indicating higher priority.
 * If no bit is set, the function returns 0.
 *
 * @param[in] vlapic Pointer to the virtual LAPIC (vlapic) structure.
 *
 * @return The vector number of the highest priority bit that is set in the ISR.
 *         If no bit is set in the ISR, returns 0x00.
 *
 * @pre vlapic != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
static inline uint32_t vlapic_find_isrv(const struct acrn_vlapic *vlapic)
{
	const struct lapic_regs *lapic = &(vlapic->apic_page);
	uint32_t i, val, bitpos, isrv = 0U;
	const struct lapic_reg *isrptr;

	isrptr = &lapic->isr[0];
	for (i = 7U; i > 0U; i--) {
		val = isrptr[i].v;
		if (val != 0U) {
			bitpos = (uint32_t)fls32(val);
			isrv = (i << 5U) | bitpos;
			break;
		}
	}

	return isrv;
}

/**
 * @brief Calculate the timer shift value from Divide Configuration Register (for Timer).
 *
 * This function calculates the shift value for the virtual LAPIC timer based on the value
 * of the Divide Configuration Register (for Timer) according to Figure 12-10
 * Divide Configuration Register, Volume 3, Intel SDM 325462-087US.
 *
 * @param[in] dcr Value of Divide Configuration Register.
 *
 * @return Returns the calculated shift value based on the Divide Configuration Register
 *         (for Timer) value.
 *
 * @post N/A
 *
 * @remark N/A
 */
static inline uint32_t
vlapic_timer_divisor_shift(uint32_t dcr)
{
	uint32_t val;

	val = ((dcr & 0x3U) | ((dcr & 0x8U) >> 1U));
	return ((val + 1U) & 0x7U);
}

/**
 * @brief Check if timer mode of vlapic timer is One-shot mode.
 *
 * This function checks if the timer mode for the virtual LAPIC (vlapic) is
 * configured as One-shot Mode. it checks LVT timer register (APIC_LVT_TIMER)
 * according to Table 12-2. Local APIC Timer Modes, Volume 3, Intel SDM 325462-087US.
 *
 * @param[in] vlapic Pointer to the vlapic structure whose timer mode is to be checked.
 *
 * @return Returns true if the timer mode is One-shot Mode, otherwise returns false.
 *
 * @pre vlapic != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
static inline bool
vlapic_lvtt_oneshot(const struct acrn_vlapic *vlapic)
{
	return (((vlapic->apic_page.lvt[APIC_LVT_TIMER].v) & APIC_LVTT_TM)
				== APIC_LVTT_TM_ONE_SHOT);
}

/**
 * @brief Check if timer mode of vlapic timer is Periodic Mode.
 *
 * This function checks if the timer mode for the virtual LAPIC (vlapic) is
 * configured as Periodic Mode, it checks LVT timer register (APIC_LVT_TIMER)
 * according to Table 12-2 Local APIC Timer Modes, Volume 3, Intel SDM 325462-087US.
 *
 * @param[in] vlapic Pointer to the vlapic structure whose timer mode is to be checked.
 *
 * @return Returns true if the timer mode is Periodic Mode, otherwise returns false.
 *
 * @pre vlapic != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
static inline bool
vlapic_lvtt_periodic(const struct acrn_vlapic *vlapic)
{
	return (((vlapic->apic_page.lvt[APIC_LVT_TIMER].v) & APIC_LVTT_TM)
				==  APIC_LVTT_TM_PERIODIC);
}

/**
 * @brief Check if timer mode of vlapic timer is TSC-Deadline Mode.
 *
 * This function checks if the timer mode for the virtual LAPIC (vlapic) is configured as
 * TSC-Deadline Mode. it checks LVT timer register (APIC_LVT_TIMER) according to
 * Table 12-2. Local APIC Timer Modes, Volume 3, Intel SDM 325462-087US.
 *
 * @param[in] vlapic Pointer to the vlapic structure whose timer mode is to be checked.
 *
 * @return Returns true if the timer mode is TSC-Deadline Mode, otherwise returns false.
 *
 * @pre vlapic != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
static inline bool
vlapic_lvtt_tsc_deadline(const struct acrn_vlapic *vlapic)
{
	return (((vlapic->apic_page.lvt[APIC_LVT_TIMER].v) & APIC_LVTT_TM)
				==  APIC_LVTT_TM_TSCDLT);
}

/**
 * @brief Check vlapic interrupt mask state in LVT timer register.
 *
 * This function checks if the timer interrupt for the virtual LAPIC (vlapic)
 * is masked. It examines the LVT (Local Vector Table) timer register for the timer
 * according to section 12.5.1 Local Vector Table, Volume 3, Intel SDM 325462-087US.
 *
 * @param[in] vlapic Pointer to the vlapic structure whose LVT timer register is to be checked.
 *
 * @return Returns true if the APIC timer interrupt is masked, otherwise returns false.
 *
 * @pre vlapic != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
static inline bool
vlapic_lvtt_masked(const struct acrn_vlapic *vlapic)
{
	return (((vlapic->apic_page.lvt[APIC_LVT_TIMER].v) & APIC_LVTT_M) != 0U);
}

/**
 * @brief Initialize vlapic timer.
 *
 * This function initializes the virtual LAPIC (vlapic) timer by setting up the
 * timer structure.
 *
 * It clears the vlapic timer state by using `memset`, then it initializes the vlapic timer by
 * calling initialize_timer function with a timer timeout callback function. When this timer timeout,
 * this callback function fires a APIC timer LVT interrupt to virtual LAPIC by calling
 * vlapic_set_intr function if timer interrupt is not masked in LVT Timer Register.
 *
 * @param[inout] vlapic Pointer to the vlapic whose timer is to be initialized.
 *
 * @return None.
 *
 * @pre vlapic != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
static void vlapic_init_timer(struct acrn_vlapic *vlapic)
{
	struct vlapic_timer *vtimer;

	vtimer = &vlapic->vtimer;
	(void)memset(vtimer, 0U, sizeof(struct vlapic_timer));

	initialize_timer(&vtimer->timer, vlapic_timer_expired, vlapic2vcpu((const struct acrn_vlapic *)vlapic), 0UL, 0UL);
}

/**
 * @brief Reset vlapic timer.
 *
 * This function resets the virtual LAPIC (vlapic) timer by:
 * - Calling del_timer to delete the timer from the timer list to disarm the timer.
 * - calling update_timer to update the timer to a clear state.
 *
 * @param[inout] vlapic Pointer to the vlapic whose timer is to be reset.
 *
 * @return None.
 *
 * @pre vlapic != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
static void vlapic_reset_timer(struct acrn_vlapic *vlapic)
{
	struct hv_timer *timer;

	timer = &vlapic->vtimer.timer;
	del_timer(timer);
	update_timer(timer, 0UL, 0UL);
}

/**
 * @brief Get the vlapic timer ratio.
 *
 * This function calculates the ratio between the TSC frequency and the core crystal clock
 * frequency according to section 12.5.4 APIC Timer, Volume 3, Intel SDM 325462-087US.
 * If this ratio can be enumerated through guest CPUID leaf 0x15, it is used;
 * otherwise, the default ratio of 1 is returned.
 *
 * The CPUID instruction provides detailed processor information, including the TSC to core
 * crystal clock frequency ratio, which is necessary for proper timer configuration. If the
 * CPUID leaf 0x15 is unavailable or if the values in the CPUID output are invalid, the
 * function returns a ratio of 1 by default.
 *
 * @param[in] vlapic Pointer to the vlapic whose timer ratio is to be calculated.
 *
 * @return The ratio of the TSC frequency to the core crystal clock frequency if CPUID
 *         leaf 0x15 is available; otherwise, returns 1.
 *
 * @pre vlapic != NULL
 *
 * @post return != 0U
 *
 * @remark N/A
 */
static uint32_t get_timer_ratio(const struct acrn_vlapic *vlapic)
{
	uint32_t ratio, eax_denominator = CPUID_CLOCK, ebx_numerator = 0U, ecx_hz = 0U, reserved = 0U;
	struct acrn_vcpu *vcpu = vlapic2vcpu(vlapic);

	guest_cpuid(vcpu, &eax_denominator, &ebx_numerator, &ecx_hz, &reserved);

	if ((eax_denominator != 0U) && (ebx_numerator != 0U)) {
		ratio = ebx_numerator / eax_denominator;
	} else {
		/* According to section 11.5.4, Vol3, Intel SDM-325384-085US: The APIC timer frequency will be the
		   processor's bus clock or core crystal clock frequency (when TSC/core crystal clock ratio is
		   enumerated in CPUID leaf 0x15) divided by the value specified in the divide configuration register.
		   If CPUID leaf 0x15 is not available, there is no exact bus clock that can be used to calculate the
		   ratio, returns 1 to avoid faults.
		*/
		ratio = 1U;
	}
	return ratio;
}

/**
 * @brief Configure and arm the vlapic timer.
 *
 * This function calculates the timeout counter and starts a one-shot or periodic vlapic timer.
 * The timeout interval is determined by the combination of the Initial Count Register(for Timer),
 * the timer divisor shift, and the timer ratio.
 *
 * If the value of Initial Count Register(for Timer) is 0, or the value in the Divide Configuration
 * Register is invalid according to Figure 12-10. Divide Configuration Register, Volume 3,
 * Intel SDM 325462-087US, this function takes no further actions and returns false.
 *
 * Otherwise, it updates vlapic timer by calling update_timer function with target timeout value
 * timer mode which is determined by the configuration in LVT Timer Register according to
 * Table 12-2. Local APIC Timer Modes, Volume 3, Intel SDM 325462-087US.
 *
 * @param[inout] vlapic Pointer to the vlapic whose timer is to be configured.
 *
 * @return true if the timer is successfully configured; false if the ICR value is zero or the
 *         divisor shift value exceeds the valid range.
 *
 * @pre vlapic != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
static bool
set_expiration(struct acrn_vlapic *vlapic)
{
	struct vlapic_timer *vtimer;
	struct hv_timer *timer;
	uint32_t tmicr, divisor_shift, ratio;
	bool ret;

	ratio = get_timer_ratio((const struct acrn_vlapic *)vlapic);
	vtimer = &vlapic->vtimer;
	tmicr = vtimer->tmicr;
	divisor_shift = vtimer->divisor_shift;

	if ((tmicr == 0U) || (divisor_shift > 8U)) {
		ret = false;
	} else {
		uint64_t delta = ((uint64_t)tmicr << divisor_shift) * ratio;
		timer = &vtimer->timer;
		update_timer(timer, cpu_ticks() + delta,
			     vlapic_lvtt_periodic(vlapic) ? delta : 0UL);
		ret = true;
	}
	return ret;
}

/**
 * @brief Emulates writing to the LVT Timer Register of the vlapic.
 *
 * This function checks if modification is required on the Timer Mode field in
 * LVT Timer Register, if yes, the vlapic timer is deleted by del_timer function
 * and reset by calling update_timer function, then update the timer mode with the target value.
 *
 * @param[inout] vlapic Pointer to the vlapic whose LVT Timer Register is to be written.
 * @param[in] val Target value for the LVT Timer Register.
 *
 * @return None.
 *
 * @pre vlapic != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
static void vlapic_update_lvtt(struct acrn_vlapic *vlapic,
			uint32_t val)
{
	uint32_t timer_mode = val & APIC_LVTT_TM;
	struct vlapic_timer *vtimer = &vlapic->vtimer;

	if (vtimer->mode != timer_mode) {
		struct hv_timer *timer = &vtimer->timer;

		/*
		 * A write to the LVT Timer Register that changes
		 * the timer mode disarms the local APIC timer.
		 */
		del_timer(timer);
		update_timer(timer, 0UL, 0UL);

		vtimer->mode = timer_mode;
	}
}

/**
 * @brief Emulates reading the Current Count Register (for Timer) of the vlapic timer.
 *
 * This function calculates the remaining counter value of the vlapic timer with divisor shift
 * and timer ratio, which is calculated according to section 12.5.4 APIC Timer, Volume 3,
 * Intel SDM 325462-087US.
 *
 * @param[in] vlapic Pointer to the vlapic whose Current Count Register (for Timer) is to be read.
 *
 * @return The remaining counter value of the timer if it is not configured as TSC-Deadline mode
 *         and the Initial Count Register (for Timer) value is non-zero. Returns 0 otherwise.
 *
 * @pre vlapic != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
static uint32_t vlapic_get_ccr(const struct acrn_vlapic *vlapic)
{
	uint32_t ratio, remain_count = 0U;
	const struct vlapic_timer *vtimer = &vlapic->vtimer;
	ratio = get_timer_ratio(vlapic);
	if ((vtimer->tmicr != 0U) &&
		(vlapic_lvtt_periodic(vlapic) || vlapic_lvtt_oneshot(vlapic))) {
		uint64_t remains;
		if (!timer_expired(&vtimer->timer, cpu_ticks(), &remains)) {
			uint64_t shifted_delta = (remains / ratio) >> vtimer->divisor_shift;
			remain_count = (uint32_t)shifted_delta;
		}
	}

	return remain_count;
}

/**
 * @brief Emulates writing to the Logical Destination Register (LDR) of the vlapic.
 *
 * This function handles a write operation to the Logical Destination Register (LDR)
 * of the virtual Local APIC (vlapic). It clears the reserved bits in the LDR value.
 * Refer to Figure 12-13. Logical Destination Register (LDR), Vol. 3, SDM 325462-087.
 *
 * @param[inout] vlapic Pointer to the vlapic structure whose LDR is being written.
 *
 * @return None
 *
 * @pre vlapic != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
static void vlapic_write_ldr(struct acrn_vlapic *vlapic)
{
	struct lapic_regs *lapic = &(vlapic->apic_page);

	lapic->ldr.v &= ~APIC_LDR_RESERVED;
	dev_dbg(DBG_LEVEL_VLAPIC, "vlapic LDR set to %#x", lapic->ldr);
}

/**
 * @brief Emulates writing to the Destination Format Register (DFR) of the vlapic.
 *
 * This function handles a write to the Destination Format Register (DFR) of the
 * virtual Local APIC (vlapic). It ensures that only the valid model bits are retained
 * and sets the reserved bits.
 * Refer to Figure 12-14. Destination Format Register (DFR), Vol. 3, SDM 325462-087.
 *
 * @param[inout] vlapic Pointer to the vlapic structure whose DFR is being written.
 *
 * @return None
 *
 * @pre vlapic != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
static void vlapic_write_dfr(struct acrn_vlapic *vlapic)
{
	struct lapic_regs *lapic = &(vlapic->apic_page);

	lapic->dfr.v &= APIC_DFR_MODEL_MASK;
	lapic->dfr.v |= APIC_DFR_RESERVED;

	if ((lapic->dfr.v & APIC_DFR_MODEL_MASK) == APIC_DFR_MODEL_FLAT) {
		dev_dbg(DBG_LEVEL_VLAPIC, "vlapic DFR in Flat Model");
	} else if ((lapic->dfr.v & APIC_DFR_MODEL_MASK) == APIC_DFR_MODEL_CLUSTER) {
		dev_dbg(DBG_LEVEL_VLAPIC, "vlapic DFR in Cluster Model");
	} else {
		dev_dbg(DBG_LEVEL_VLAPIC, "DFR in Unknown Model %#x", lapic->dfr);
	}
}

/**
 * @brief Emulates writing the Divide Configuration Register (for Timer) of vlapic.
 *
 * This function interprets the value written to the Divide Configuration Register
 * (for Timer) of the vlapic.
 * It calculates the divisor shift for the vlapic timer based on the target Divide Configuration
 * Register (for Timer) value according to Figure 12-10. Divide Configuration Register, Volume 3,
 * Intel SDM 325462-087US
 *
 * @param[inout] vlapic Pointer to the vlapic structure whose Divide Configuration Register
 *               (for Timer) is to be written.
 *
 * @return None.
 *
 * @pre vlapic != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
static void vlapic_write_dcr(struct acrn_vlapic *vlapic)
{
	uint32_t divisor_shift;
	struct vlapic_timer *vtimer;
	struct lapic_regs *lapic = &(vlapic->apic_page);

	vtimer = &vlapic->vtimer;
	divisor_shift = vlapic_timer_divisor_shift(lapic->dcr_timer.v);
	vtimer->divisor_shift = divisor_shift;
}

/**
 * @brief Emulates writing the Initial Count Register (for Timer) for the vlapic timer.
 *
 * This function emulates writing to the initial-count register to start the one-shot or periodic
 * mode timer.
 *
 * It checks whether the current timer mode is one-shot or periodic, if yes, it calls del_timer to
 * disarm the existing timer, then calculate the expiration count according to section 12.5.4
 * APIC Timer, Volume 3, Intel SDM 325462-087US and calls add_timer to start the one-shot or
 * periodic timer, which depends on the timer mode configured in LVT Timer Register.
 *
 * @param[inout] vlapic Pointer to the vlapic structure whose Initial Count Register
 *               (for Timer) is to be written.
 *
 * @return None.
 *
 * @pre vlapic != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
static void vlapic_write_icrtmr(struct acrn_vlapic *vlapic)
{
	struct lapic_regs *lapic;
	struct vlapic_timer *vtimer;

	if (vlapic_lvtt_oneshot(vlapic) || vlapic_lvtt_periodic(vlapic)) {
		lapic = &(vlapic->apic_page);
		vtimer = &vlapic->vtimer;
		vtimer->tmicr = lapic->icr_timer.v;

		del_timer(&vtimer->timer);
		if (set_expiration(vlapic)) {
			/* vlapic_init_timer has been called,
			 * and timer->fire_tsc is not 0, here
			 * add_timer should not return error
			 */
			(void)add_timer(&vtimer->timer);
		}
	}
}

/**
 * @brief Emulates the read operation of the IA32_TSC_DEADLINE MSR for a vlapic.
 *
 * This function calculates the emulated value of the IA32_TSC_DEADLINE MSR for both
 * LAPIC pass-through and virtual APIC cases. It determines the value based on the
 * configuration, the timer's mode, and its current state.
 *
 * In LAPIC pass-through mode:
 * - If the physical TSC deadline MSR is zero, indicating the timer is disarmed, the
 *   function resets the virtual TSC deadline MSR to zero and returns 0.
 * - Otherwise, it returns the virtual TSC deadline MSR value.
 *
 * In LAPIC Virtualization mode:
 * - If the Local Vector Table Timer (LVTT) is not configured for TSC deadline mode,
 *   the function returns 0.
 * - If the timer is active, returns 0 if the deadline has expired, otherwise, returns the
 *   virtual TSC deadline MSR value.
 *
 * @param[in] vlapic Pointer to the vlapic whose IA32_TSC_DEADLINE MSR is to be read.
 *
 * @return Return 0 if the timer is not configured as TSC-Deadline mode or the physical value
 *         of MSR IA32_TSC_DEADLINE is 0 for the APIC pass-through case. Otherwise, return the
 *         value of MSR IA32_TSC_DEADLINE of vlapic.
 *
 * @pre vlapic->vcpu != NULL
 * @pre vlapic != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
uint64_t vlapic_get_tsc_deadline_msr(const struct acrn_vlapic *vlapic)
{
	uint64_t ret;
	struct acrn_vcpu *vcpu = vlapic2vcpu(vlapic);

	if (is_lapic_pt_configured(vcpu->vm)) {
		/* If physical TSC_DEADLINE is zero which means it's not armed (automatically disarmed
		 * after timer triggered), return 0 and reset the virtual TSC_DEADLINE;
		 * If physical TSC_DEADLINE is not zero, return the virtual TSC_DEADLINE value.
		 */
		if (msr_read(MSR_IA32_TSC_DEADLINE) == 0UL) {
			vcpu_set_guest_msr(vcpu, MSR_IA32_TSC_DEADLINE, 0UL);
			ret = 0UL;
		} else {
			ret = vcpu_get_guest_msr(vcpu, MSR_IA32_TSC_DEADLINE);
		}
	} else if (!vlapic_lvtt_tsc_deadline(vlapic)) {
		ret = 0UL;
	} else {
		ret = timer_expired(&vlapic->vtimer.timer, cpu_ticks(), NULL) ? 0UL :
			vcpu_get_guest_msr(vcpu, MSR_IA32_TSC_DEADLINE);
	}

	return ret;
}

/**
 * @brief Set the value of the virtual IA32_TSC_DEADLINE MSR for a given vlapic.
 *
 * This function writes a specified value to the IA32_TSC_DEADLINE MSR of the vlapic. Depending on
 * the configuration and intent (arming or disarming the timer), the function either updates the
 * physical MSR or adjusts the virtual timer according to section 12.5.4.1 TSC-Deadline Mode,
 * Volume 3, Intel SDM 325462-087US.
 *
 * For lapic pass-through, the function writes to the guest MSR with given value by calling
 * vcpu_set_guest_msr function and also directly writes to the physical MSR by calling msr_write
 * with adjusted value from VMX_TSC_OFFSET_FULL. To avoid disarming the physical timer
 * unintentionally, it adjusts the value if the calculated TSC deadline is zero while the intent
 * is to arm the timer. If the value is zero, the function disarms the physical timer.
 *
 * For virtual LAPICs using TSC deadline mode, the function sets the value in the virtual MSR and
 * schedules or disarms the virtual timer as necessary.
 * If the TSC deadline is zero, the timer is disarmed by calling update_timer function.
 * When arming the timer, the provided `val_arg` is adjusted for the TSC offset to synchronize
 * with the host's TSC, then a one-shot vlapic timer is scheduled to emulate this TSC timer
 * by calling update_timer and add_timer function.
 *
 * If the TSC deadline timer is unsupported or unnecessary, the function performs no action.
 *
 * @param[out] vlapic Pointer to the vlapic structure whose IA32_TSC_DEADLINE MSR is to be updated.
 * @param[in] val_arg The value to be written to the IA32_TSC_DEADLINE MSR.
 *
 * @return None
 *
 * @pre vlapic != NULL
 * @pre vlapic->vcpu != NULL
 *
 * @post N/A
 */
void vlapic_set_tsc_deadline_msr(struct acrn_vlapic *vlapic, uint64_t val_arg)
{
	struct hv_timer *timer;
	uint64_t val = val_arg;
	struct acrn_vcpu *vcpu = vlapic2vcpu((const struct acrn_vlapic *)vlapic);

	if (is_lapic_pt_configured(vcpu->vm)) {
		vcpu_set_guest_msr(vcpu, MSR_IA32_TSC_DEADLINE, val);
		/* If val is not zero, which mean guest intends to arm the tsc_deadline timer,
		 * if the calculated value to write to the physical TSC_DEADLINE msr is zero,
		 * we plus 1 to not disarm the physical timer falsely;
		 * If val is zero, which means guest intends to disarm the tsc_deadline timer,
		 * we disarm the physical timer.
		 */
		if (val != 0UL) {
			val -= exec_vmread64(VMX_TSC_OFFSET_FULL);
			if (val == 0UL) {
				val += 1UL;
			}
			msr_write(MSR_IA32_TSC_DEADLINE, val);
		} else {
			msr_write(MSR_IA32_TSC_DEADLINE, 0);
		}
	} else if (vlapic_lvtt_tsc_deadline((const struct acrn_vlapic *)vlapic)) {
		vcpu_set_guest_msr(vcpu, MSR_IA32_TSC_DEADLINE, val);

		timer = &vlapic->vtimer.timer;
		del_timer(timer);

		if (val != 0UL) {
			/* transfer guest tsc to host tsc */
			val -= exec_vmread64(VMX_TSC_OFFSET_FULL);
			update_timer(timer, val, 0UL);
			/* vlapic_init_timer has been called,
			 * and timer->fire_tsc is not 0,here
			 * add_timer should not return error
			 */
			(void)add_timer(timer);
		} else {
			update_timer(timer, 0UL, 0UL);
		}
	} else {
		/* No action required */
	}
}

/**
 * @brief Handle the write operation for Error Status Register (ESR) in the vlapic register page.
 *
 * According to section 12.5.3 Error Handling, Volume 3, Intel SDM 325462-087US, Before attempt
 * to read from the ESR, software should first write to it. Writing to the ESR clears any
 * previously logged errors and updates the register with new errors detected since the last
 * write operation.
 *
 * This function reinitializes the ESR with the recorded error flags stored in `esr_pending`
 * and clears the `esr_pending` field to reset the error tracking mechanism for future operations.
 *
 * @param[inout] vlapic Pointer to the vlapic structure.
 *
 * @return None
 *
 * @pre vlapic != NULL.
 *
 * @post N/A
 *
 * @remark N/A
 */
static void
vlapic_write_esr(struct acrn_vlapic *vlapic)
{
	struct lapic_regs *lapic;

	lapic = &(vlapic->apic_page);
	lapic->esr.v = vlapic->esr_pending;
	vlapic->esr_pending = 0U;
}

/**
 * @brief Update Trigger Mode Register (TMR) and EOI Exit Bitmap for a specific interrupt vector.
 *
 * This function modifies the TMR and the End-of-Interrupt (EOI) Exit Bitmap in the
 * Virtual Machine Control Structure (VMCS) for a specified interrupt vector with
 * given trigger level:
 *
 * - If the interrupt is level-triggered, the function sets the corresponding TMR bit
 *   and updates the EOI Exit Bitmap to reflect the level-triggered mode by calling
 *   vcpu_set_eoi_exit_bitmap function.
 *
 * - If the interrupt is edge-triggered, it clears the corresponding TMR bit and updates the EOI
 *   Exit Bitmap to reflect the edge-triggered mode by calling vcpu_clear_eoi_exit_bitmap.
 *
 *
 * These updates ensure that the vlapic properly handles the trigger mode of the interrupt
 * and synchronizes the VMCS configuration with the desired interrupt behavior.
 *
 * @param[inout]  vlapic   Pointer to the vlapic structure.
 * @param[in]     vector   Interrupt vector for which the TMR and EOI Exit Bitmap are updated.
 * @param[in]     level    Indicates the interrupt trigger mode, true for level-triggered mode,
 *                         and false for edge-triggered mode.
 *
 * @return None
 *
 * @pre vlapic != NULL
 *
 * @post N/A
 *
 * @remark N/A.
 */
static void
vlapic_set_tmr(struct acrn_vlapic *vlapic, uint32_t vector, bool level)
{
	struct lapic_reg *tmrptr = &(vlapic->apic_page.tmr[0]);
	if (level) {
		if (!bitmap32_test_and_set_lock((uint16_t)(vector & 0x1fU), &tmrptr[(vector & 0xffU) >> 5U].v)) {
			vcpu_set_eoi_exit_bitmap(vlapic2vcpu(vlapic), vector);
		}
	} else {
		if (bitmap32_test_and_clear_lock((uint16_t)(vector & 0x1fU), &tmrptr[(vector & 0xffU) >> 5U].v)) {
			vcpu_clear_eoi_exit_bitmap(vlapic2vcpu(vlapic), vector);
		}
	}
}

/**
 * @brief Resets the Trigger Mode Register (TMR) in the vlapic.
 *
 * This function set all Trigger Mode Register (TMR) in virtual-APIC page to 0 to reset all
 * interrupt vectors to edge-triggered mode. Afterward, it clear the End-of-Interrupt (EOI)
 * exit bitmaps in the Virtual Machine Control Structure (VMCS) by calling
 * vcpu_reset_eoi_exit_bitmaps function.
 *
 * This ensures that the vlapic is fully reset with respect to its TMR state and
 * interrupt handling configuration.
 *
 * @param[inout] vlapic Pointer to the vlapic structure.
 *
 * @return None
 *
 * @pre vlapic != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
static void
vlapic_reset_tmr(struct acrn_vlapic *vlapic)
{
	int16_t i;
	struct lapic_regs *lapic;

	dev_dbg(DBG_LEVEL_VLAPIC,
			"vlapic resetting all vectors to edge-triggered");

	lapic = &(vlapic->apic_page);
	for (i = 0; i < 8; i++) {
		lapic->tmr[i].v = 0U;
	}

	vcpu_reset_eoi_exit_bitmaps(vlapic2vcpu(vlapic));
}

/**
 * @brief Accepts an interrupt with the specified vector and trigger level for vlapic
 *        supporting APICv Posted-Interrupt features.
 *
 * This function handles interrupt acceptance for a vlapic that utilizes APICv advanced
 * features such as Posted Interrupts (PI).
 *
 * It updates the Trigger Mode Register (TMR) to reflect the current trigger level of the
 * interrupt, and set the EOI Exit Bitmap in VMCS for level-triggered interrupt by calling
 * vcpu_set_eoi_exit_bitmap function and clear the EOI Exit Bitmap in the VMCS for
 * edge-triggered interrupt by calling vcpu_clear_eoi_exit_bitmap function.
 *
 * The interrupt delivery is optimized based on the execution state of the target vCPU:
 *
 * - Root Mode:      The interrupt is recorded in the Pending Interrupt Request (PIR) as an
 *                   `ACRN_REQUEST_EVENT`. The interrupt will be delivered during the next
 *                   VM entry when the vCPU resumes execution.
 *
 * - Non-Root Mode:  In this case, target vCPU is not running on current pCPU, a Posted Interrupt
 *                   Notification is sent to the target pCPU. This allows the hardware to
 *                   synchronize the PIR to the Virtual Interrupt Request Register (vIRR) without
 *                   causing a VM exit.
 *
 * @param[inout] vlapic  Pointer to the vlapic structure.
 * @param[in] vector     Interrupt vector to be handled. Must be a valid interrupt number
 *                       greater than 15.
 * @param[in] level      Boolean value indicating trigger mode of the interrupt, true for
 *                       level-triggered interrupts false for edge-triggered interrupts.
 *
 * @return None
 *
 * @pre vector >= 16
 * @pre vlapic != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
static void apicv_advanced_accept_intr(struct acrn_vlapic *vlapic, uint32_t vector, bool level)
{
	/* update TMR if interrupt trigger mode has changed */
	vlapic_set_tmr(vlapic, vector, level);

	if (apicv_set_intr_ready(vlapic, vector)) {
		struct acrn_vcpu *vcpu = vlapic2vcpu(vlapic);

		bitmap_set_lock(ACRN_REQUEST_EVENT, &vcpu->arch.pending_req);

		if (get_pcpu_id() != pcpuid_from_vcpu(vcpu)) {
			apicv_trigger_pi_anv(pcpuid_from_vcpu(vcpu), (uint32_t)vcpu->arch.pid.control.bits.nv);
		}
	}
}

/**
 * @brief Accepts an interrupt with the specified vector and trigger level for a vlapic.
 *
 * This function processes an incoming interrupt for the given vlapic.
 * If the vlapic is software-disabled (indicated by the `APIC_SVR_ENABLE` bit in the SVR),
 * the interrupt is ignored, and a debug message is logged. Otherwise, the interrupt is
 * passed to the appropriate handler defined by the APICv operational interface
 * of `accept_intr` callback.
 *
 * it calls signal_event to signal a VCPU_EVENT_VIRTUAL_INTERRUPT event to wakeup vCPU.
 *
 * @param[in] vlapic  Pointer to the vlapic structure.
 * @param[in] vector  The interrupt vector to be handled.
 * @param[in] level   Boolean value indicating the trigger mode of the interrupt, true for
 *                    level-triggered interrupts and false for edge-triggered interrupts.
 *
 * @return None
 *
 * @pre vector >= 16
 * @pre vlapic != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
static void vlapic_accept_intr(struct acrn_vlapic *vlapic, uint32_t vector, bool level)
{
	struct lapic_regs *lapic;
	ASSERT(vector <= NR_MAX_VECTOR, "invalid vector %u", vector);

	lapic = &(vlapic->apic_page);
	if ((lapic->svr.v & APIC_SVR_ENABLE) == 0U) {
		dev_dbg(DBG_LEVEL_VLAPIC, "vlapic is software disabled, ignoring interrupt %u", vector);
	} else {
		vlapic->ops->accept_intr(vlapic, vector, level);
		signal_event(&vlapic2vcpu(vlapic)->events[VCPU_EVENT_VIRTUAL_INTERRUPT]);
	}
}

/**
 * @brief Sends Notification Vectors to the target physical CPU .
 *
 * This function uses the posted-interrupt notification vector to send the IPI to the target pCPU.
 * The behavior of the target pCPU depends on its execution mode according to section
 * 31.6 POSTED-INTERRUPT PROCESSING, Volume 3, Intel SDM 325462-087US.
 *
 * - Non-root mode:
 *     If the target pCPU is in non-root mode (VMX non-root operation), the pCPU will automatically
 *     synchronize pending virtual interrupts from the Post Interrupt Request (PIR) to the Virtual
 *     Interrupt Request Register (vIRR) without causing VM exit.
 *
 * - Root mode:
 *     If the target pCPU is in root mode, the virtual interrupt will be queued
 *     and injected during the next VM entry on that pCPU.
 *
 * @param[in] dest_pcpu_id  The ID of the target physical CPU (pCPU) to which the ANV will be sent.
 * @param[in] anv           The Notification Vector to be delivered to the target pCPU.
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark N/A
 */
static void apicv_trigger_pi_anv(uint16_t dest_pcpu_id, uint32_t anv)
{
	send_single_ipi(dest_pcpu_id, anv);
}

/**
 * @brief Converts an APIC LVT register offset to its corresponding index in the cached LVT entries.
 *
 * This function maps an APIC LVT register offset to the respective index in the cached LVT
 * registers field of the vlapic structure. These indices are used to reference cached LVT
 * registers efficiently.
 *
 * - Offset to Index Mapping:
 *     The function uses a switch-case structure to handle each valid offset and return its
 *     corresponding index as defined in the vlapic structure.
 * - Default Case:
 *     The `default` case handles `APIC_OFFSET_ERROR_LVT`. While theoretically unreachable, the caller
 *     guarantees that only valid offsets are provided per the precondition.
 *
 * @param[in] offset APIC LVT register offset in the virtual-APIC page. This value determines
 *            the LVT register being accessed or modified.
 *
 * @return The index corresponding to the given LVT offset in the `lvt_last[]` array.
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark
 * - The caller is responsible for ensuring that the `offset` parameter must be one of the following:
 *   - `APIC_OFFSET_CMCI_LVT`
 *   - `APIC_OFFSET_TIMER_LVT`
 *   - `APIC_OFFSET_THERM_LVT`
 *   - `APIC_OFFSET_PERF_LVT`
 *   - `APIC_OFFSET_LINT0_LVT`
 *   - `APIC_OFFSET_LINT1_LVT`
 *   - `APIC_OFFSET_ERROR_LVT`
 *
 * - This function does not validate the input offset, as it relies on the caller to provide valid values.
 */
static inline uint32_t
lvt_off_to_idx(uint32_t offset)
{
	uint32_t index;

	switch (offset) {
	case APIC_OFFSET_CMCI_LVT:
		index = APIC_LVT_CMCI;
		break;
	case APIC_OFFSET_TIMER_LVT:
		index = APIC_LVT_TIMER;
		break;
	case APIC_OFFSET_THERM_LVT:
		index = APIC_LVT_THERMAL;
		break;
	case APIC_OFFSET_PERF_LVT:
		index = APIC_LVT_PMC;
		break;
	case APIC_OFFSET_LINT0_LVT:
		index = APIC_LVT_LINT0;
		break;
	case APIC_OFFSET_LINT1_LVT:
		index = APIC_LVT_LINT1;
		break;
	case APIC_OFFSET_ERROR_LVT:
	default:
		/*
		 * The function caller could guarantee the pre condition.
		 * So, all of the possible 'offset' other than
		 * APIC_OFFSET_ERROR_LVT has been handled in prior cases.
		 */
		index = APIC_LVT_ERROR;
		break;
	}

	return index;
}

/**
 * @brief Retrieves a pointer to the Local Vector Table (LVT) entry for a specified APIC register offset.
 *
 * This function identifies the correct LVT register based on the provided offset and returns a pointer
 * to the corresponding LVT field in the virtual-APIC page. The function supports specific offsets associated
 * with LVT registers only.
 *
 * @param[in] vlapic Pointer to the vlapic structure containing the APIC LVT entries.
 * @param[in] offset APIC LVT register offset in the virtual-APIC page. This value determines
 *                   the LVT register being accessed or modified.
 *
 * @return A pointer to the target LVT register field in the vlapic structure.
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark N/A
 */
static inline uint32_t *
vlapic_get_lvtptr(struct acrn_vlapic *vlapic, uint32_t offset)
{
	struct lapic_regs *lapic = &(vlapic->apic_page);
	uint32_t i;
	uint32_t *lvt_ptr;

	switch (offset) {
	case APIC_OFFSET_CMCI_LVT:
		lvt_ptr = &lapic->lvt_cmci.v;
		break;
	default:
		/*
		 * The function caller could guarantee the pre condition.
		 * All the possible 'offset' other than APIC_OFFSET_CMCI_LVT
		 * could be handled here.
		 */
		i = lvt_off_to_idx(offset);
		lvt_ptr = &(lapic->lvt[i].v);
		break;
	}
	return lvt_ptr;
}

/**
 * @brief Retrieves the cached value of a Local Vector Table (LVT) register for a specified offset.
 *
 * This function returns cached value of an LVT register. The cache is maintained in the `lvt_last`
 * array, ensuring efficient access to the most recently updated value of the LVT entry
 * corresponding to the provided APIC register offset.
 *
 * @param[in] vlapic Pointer to the vlapic structure containing the LVT entries.
 * @param[in] offset APIC LVT register offset in the register page. This offset determines which
 *                   LVT register value to retrieve.
 *
 * @return The cached value of the target LVT register.
 *
 * @pre vlapic != NULL
 *
 * @post N/A
 *
 * @remark
 * - The function does not validate the offset; the caller must ensure its correctness.
 * - The cached value reflects the last state written to the LVT register, which might differ from
 *   the hardware register if not synchronized.
 */
static inline uint32_t
vlapic_get_lvt(const struct acrn_vlapic *vlapic, uint32_t offset)
{
	uint32_t idx;

	idx = lvt_off_to_idx(offset);
	return vlapic->lvt_last[idx];
}

/**
 * @brief Handles updates to a Local Vector Table (LVT) register in the virtual-APIC page.
 *
 * This function updates a specific LVT register based on the provided APIC offset. It ensures the
 * correct configuration of the LVT register by applying appropriate masks and values, and performs
 * additional processing if the LVT register corresponds to the timer.
 *
 * - If the APIC is disabled (SVR.APIC_ENABLE bit is unset), mask bit (`APIC_LVT_M`) of bit16
 *     is set.
 *
 * - Masks the LVT register with relevant fields based on the type of LVT register according
 *     to section Figure 12-8. Local Vector Table (LVT), Volume 3, Intel SDM 325462-087US.
 *
 * -  Timer-Specific Updates:
 *    For the Timer LVT (`APIC_OFFSET_TIMER_LVT`), additional processing is performed:
 *    If Timer Mode field in LVT Timer Register is modified, it delete vlapic timer by calling
 *    del_timer function, and reset this timer by calling update_timer function, then update
 *    the timer mode with target value.
 *    If the modification is not required on timer mode, it does nothing.
 *
 * - LVT Entry Update:
 *   - Updates the target LVT register in the virtual-APIC page with the calculated value.
 *   - Stores the final value in `lvt_last` for tracking.
 *
 * @param[inout] vlapic Pointer to the vlapic structure.
 * @param[in]    offset APIC LVT register offset in the register page.
 *
 * @return None.
 *
 * @pre vlapic != NULL
 * @pre vec = (lvt & APIC_LVT_VECTOR) >= 16
 *
 * @post N/A
 *
 * @remark
 * - The initial target LVT register value should be prepared by the caller and stored
 *   in the LVT register in the register page prior to calling this function.
 * - This function only processes and validates the LVT register configuration.
 */
static void
vlapic_write_lvt(struct acrn_vlapic *vlapic, uint32_t offset)
{
	uint32_t *lvtptr, mask, val, idx;
	struct lapic_regs *lapic;

	lapic = &(vlapic->apic_page);
	lvtptr = vlapic_get_lvtptr(vlapic, offset);
	val = *lvtptr;

	if ((lapic->svr.v & APIC_SVR_ENABLE) == 0U) {
		val |= APIC_LVT_M;
	}
	mask = APIC_LVT_M | APIC_LVT_VECTOR;
	switch (offset) {
	case APIC_OFFSET_TIMER_LVT:
		mask |= APIC_LVTT_TM;
		break;
	case APIC_OFFSET_ERROR_LVT:
		break;
	case APIC_OFFSET_LINT0_LVT:
	case APIC_OFFSET_LINT1_LVT:
		mask |= APIC_LVT_TM | APIC_LVT_RIRR | APIC_LVT_IIPP;
		/* FALLTHROUGH */
	default:
		mask |= APIC_LVT_DM;
		break;
	}
	val &= mask;

	if (offset == APIC_OFFSET_TIMER_LVT) {
		vlapic_update_lvtt(vlapic, val);
	} else {
		/* No action required. */
	}

	*lvtptr = val;
	idx = lvt_off_to_idx(offset);
	vlapic->lvt_last[idx] = val;
}

/**
 * @brief Masks all Local Vector Table (LVT) entries to inhibit the reception of local interrupts.
 *
 * It follows Figure 12-8. Local Vector Table (LVT), Volume 3, Intel SDM 325462-087US to program
 * all LVT registers.
 *
 * @param[inout] vlapic Pointer to the vlapic structure.
 *
 * @return None.
 *
 * @pre vlapic != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
static void
vlapic_mask_lvts(struct acrn_vlapic *vlapic)
{
	struct lapic_regs *lapic = &(vlapic->apic_page);

	lapic->lvt_cmci.v |= APIC_LVT_M;
	vlapic_write_lvt(vlapic, APIC_OFFSET_CMCI_LVT);

	lapic->lvt[APIC_LVT_TIMER].v |= APIC_LVT_M;
	vlapic_write_lvt(vlapic, APIC_OFFSET_TIMER_LVT);

	lapic->lvt[APIC_LVT_THERMAL].v |= APIC_LVT_M;
	vlapic_write_lvt(vlapic, APIC_OFFSET_THERM_LVT);

	lapic->lvt[APIC_LVT_PMC].v |= APIC_LVT_M;
	vlapic_write_lvt(vlapic, APIC_OFFSET_PERF_LVT);

	lapic->lvt[APIC_LVT_LINT0].v |= APIC_LVT_M;
	vlapic_write_lvt(vlapic, APIC_OFFSET_LINT0_LVT);

	lapic->lvt[APIC_LVT_LINT1].v |= APIC_LVT_M;
	vlapic_write_lvt(vlapic, APIC_OFFSET_LINT1_LVT);

	lapic->lvt[APIC_LVT_ERROR].v |= APIC_LVT_M;
	vlapic_write_lvt(vlapic, APIC_OFFSET_ERROR_LVT);
}

/**
 * @brief Fires a local interrupt using the specified LVT (Local Vector Table) entry.
 *
 * The function checks whether the interrupt is masked using the mask bit in the LVT register.
 * If the mask bit is set, the interrupt is not delivered.
 *
 * If mask bit is not set, this function delivers a local interrupt to the vCPU associated with
 * the given vlapic, following section 12.5.1 Local Vector Table, Volume 3,
 * Intel SDM 325462-087US,  based on the delivery mode specified in the provided LVT register.
 * Only below delivery mode options are supported:
 *
 * - APIC_LVT_DM_FIXED: The function fires a fixed-mode interrupt to target vCPU with
 *                      the specified vector by calling vlapic_set_intr function.
 * - APIC_LVT_DM_NMI:   The function fires a Non-Maskable Interrupt (NMI) to target vCPU by calling
 *                      vcpu_inject_nmi function.
 *
 * - Other delivery modes are ignored, and a warning message is logged for unsupported modes.
 *
 * @param[in] vlapic Pointer to the vlapic structure.
 * @param[in] lvt Target Local Vector Table (LVT) entry that specifies the interrupt details.
 *
 * @return None.
 *
 * @pre vlapic != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
static void
vlapic_fire_lvt(struct acrn_vlapic *vlapic, uint32_t lvt)
{
	if ((lvt & APIC_LVT_M) == 0U) {
		struct acrn_vcpu *vcpu = vlapic2vcpu(vlapic);
		uint32_t vec = lvt & APIC_LVT_VECTOR;
		uint32_t mode = lvt & APIC_LVT_DM;

		switch (mode) {
		case APIC_LVT_DM_FIXED:
			vlapic_set_intr(vcpu, vec, LAPIC_TRIG_EDGE);
			break;
		case APIC_LVT_DM_NMI:
			vcpu_inject_nmi(vcpu);
			break;
		default:
			/* Other modes ignored */
			pr_warn("func:%s other mode is not support\n",__func__);
			break;
		}
	}
	return;
}

/**
 * @brief Updates the Processor Priority Register (PPR) in the vlapic.
 *
 * This function updates the PPR based on the current values of the Task Priority Register (TPR)
 * and the highest-priority vector in the In-Service Register (ISR). The algorithm follows section
 * 12.8.3.1 Task and Processor Priorities, Volume 3, Intel SDM 325462-087US.
 *
 * The PPR reflects the processor's current priority, which determines whether new interrupts can
 * preempt the processor. The following logic is applied:
 * - If the priority of the TPR is greater than or equal to the priority of the ISRV, the PPR is
 *   set to the value of the TPR.
 * - Otherwise, the PPR is set to the priority class of the ISRV, which is the higher nibble
 *   (bit7~bit4) of the ISRV vector.
 *
 *
 * @param[inout] vlapic Pointer to the vlapic structure.
 *
 * @return None
 *
 * @pre vlapic != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
static void
vlapic_update_ppr(struct acrn_vlapic *vlapic)
{
	uint32_t isrv, tpr, ppr;

	isrv = vlapic->isrv;
	tpr = vlapic->apic_page.tpr.v;

	if (prio(tpr) >= prio(isrv)) {
		ppr = tpr;
	} else {
		ppr = isrv & 0xf0U;
	}

	vlapic->apic_page.ppr.v = ppr;
	dev_dbg(DBG_LEVEL_VLAPIC, "%s 0x%02x", __func__, ppr);
}

/**
 * @brief Handles the End of Interrupt (EOI) operation on the APIC.
 *
 * This function processes an EOI signal following 12.8.5 Signaling Interrupt Servicing Completion,
 * Volume 3, Intel SDM 325462-087US by:
 * - Clearing the corresponding bit in the Interrupt Service Register (ISR).
 * - Updating the in-service vector (ISRV) and recalculating the Priority Processor Register (PPR)
 *   according to section 12.8.3.1 Task and Processor Priorities, Volume 3, Intel SDM 325462-087US.
 * - If the interrupt is level-triggered, it calls vioapic_broadcast_eoi to broadcast EOI event to
 *   the virtual I/O APIC (vioapic).
 * - It also makes a request to the virtual CPU (vCPU) to evaluate the interrupt on next VM entry
 *   by calling vcpu_make_request function.
 *
 * @param[inout] vlapic Pointer to the vlapic structure.
 *
 * @return None
 *
 * @pre vlapic != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
static void
vlapic_process_eoi(struct acrn_vlapic *vlapic)
{
	struct lapic_regs *lapic = &(vlapic->apic_page);
	struct lapic_reg *isrptr, *tmrptr;
	uint32_t i, vector, bitpos;

	isrptr = &lapic->isr[0];
	tmrptr = &lapic->tmr[0];

	if (vlapic->isrv != 0U) {
		vector = vlapic->isrv;
		i = (vector >> 5U);
		bitpos = (vector & 0x1fU);
		bitmap32_clear_nolock((uint16_t)bitpos, &isrptr[i].v);

		dev_dbg(DBG_LEVEL_VLAPIC, "EOI vector %u", vector);
		vlapic_dump_isr(vlapic, "vlapic_process_eoi");

		vlapic->isrv = vlapic_find_isrv(vlapic);
		vlapic_update_ppr(vlapic);

		if (bitmap32_test((uint16_t)bitpos, &tmrptr[i].v)) {
			/*
			 * Per Intel SDM 10.8.5, Software can inhibit the broadcast of
			 * EOI by setting bit 12 of the Spurious Interrupt Vector
			 * Register of the LAPIC.
			 * TODO: Check if the bit 12 "Suppress EOI Broadcasts" is set.
			 */
			vioapic_broadcast_eoi(vlapic2vcpu(vlapic)->vm, vector);
		}

		vcpu_make_request(vlapic2vcpu(vlapic), ACRN_REQUEST_EVENT);
	}

	dev_dbg(DBG_LEVEL_VLAPIC, "Gratuitous EOI");
}

/**
 * @brief This function records an ESR error during interrupt handling and fires an interrupt to
 *        vlapic.
 *
 * This function records errors detected during interrupt handling and send an interrupt
 * to the virtual LAPIC which detected the errors during interrupt handling according to section
 * 12.5.3 Error Handling, Volume 3, Intel SDM 325462-087US.
 *
 * The vector of this interrupt is specified in LVT Error Register in virtual-APIC page.
 *
 * If the vlapic is software-disabled (indicated by the `APIC_SVR_ENABLE` bit in the SVR),
 * this interrupt delivery is ignored. Otherwise, the interrupt is passed to the appropriate
 * handler defined by the APICv operational interface of `accept_intr` callback.
 *
 * @param[inout] vlapic Pointer to the vlapic structure.
 * @param[in]    mask   Error flags in ESR as specified in section 12.5.3 Error Handling,
 *                      Volume 3, Intel SDM 325462-087US.
 *
 * @return None
 *
 * @pre vlapic != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
static void
vlapic_set_error(struct acrn_vlapic *vlapic, uint32_t mask)
{
	uint32_t lvt, vec;

	vlapic->esr_pending |= mask;
	if (vlapic->esr_firing == 0) {
		vlapic->esr_firing = 1;

		/* The error LVT always uses the fixed delivery mode. */
		lvt = vlapic_get_lvt(vlapic, APIC_OFFSET_ERROR_LVT);
		if ((lvt & APIC_LVT_M) == 0U) {
			vec = lvt & APIC_LVT_VECTOR;
			if (vec >= 16U) {
				vlapic_accept_intr(vlapic, vec, LAPIC_TRIG_EDGE);
			}
		}
		vlapic->esr_firing = 0;
	}
}

/**
 * @brief Triggers a local interrupt using the specified Local Vector Table (LVT) register.
 *
 * This function first check whether the virtual LAPIC is enabled (APIC global enable/disable bit
 * in IA32_APIC_BASE MSR and APIC Software Enable/Disable bit in Spurious-Interrupt Vector Register).
 *
 * - If the vlapic is not enabled, no further processing occurs, and the function returns without
 *   firing any interrupts.
 *
 * - If the vlapic is enabled, this function gets cached LVT register value, then it
 *   checks whether the interrupt is masked using the mask bit in the LVT register.
 *   If the mask bit is set, the interrupt is not delivered.
 *   If mask bit is not set, this function delivers a local interrupt to the vCPU associated with
 *   the given vlapic, following section 12.5.1 Local Vector Table, Volume 3,
 *   Intel SDM 325462-087US, based on the delivery mode specified in the provided LVT register.
 *
 *   It only supports Fixed and NMI Delivery Modes:
 *   Fixed Delivery Mode:  It calls vlapic_set_intr to fire an local interrupt.
 *   NMI Delivery Mode:    It calls vcpu_inject_nmi to inject NMI interrupt.
 *   Other Delivery Modes: They're not supported and ignored.
 *
 * @param[in] vlapic      Pointer to the vlapic structure.
 * @param[in] lvt_index   Index to the LVT in the vlapic, identifying the type of interrupt.
 *
 * @return  0 on successful interrupt fire, otherwise -EINVAL if the LVT index is invalid.
 *
 * @pre vlapic != NULL
 * @pre APIC_LVT_TIMER <= lvt_index <= APIC_LVT_MAX
 *
 * @post N/A
 *
 * @remark  N/A.
 */
static int32_t
vlapic_trigger_lvt(struct acrn_vlapic *vlapic, uint32_t lvt_index)
{
	uint32_t lvt;
	int32_t ret = 0;

	if (vlapic_enabled(vlapic)) {
		switch (lvt_index) {
		case APIC_LVT_LINT0:
			lvt = vlapic_get_lvt(vlapic, APIC_OFFSET_LINT0_LVT);
			break;
		case APIC_LVT_LINT1:
			lvt = vlapic_get_lvt(vlapic, APIC_OFFSET_LINT1_LVT);
			break;
		case APIC_LVT_TIMER:
			lvt = vlapic_get_lvt(vlapic, APIC_OFFSET_TIMER_LVT);
			lvt |= APIC_LVT_DM_FIXED;
			break;
		case APIC_LVT_ERROR:
			lvt = vlapic_get_lvt(vlapic, APIC_OFFSET_ERROR_LVT);
			lvt |= APIC_LVT_DM_FIXED;
			break;
		case APIC_LVT_PMC:
			lvt = vlapic_get_lvt(vlapic, APIC_OFFSET_PERF_LVT);
			break;
		case APIC_LVT_THERMAL:
			lvt = vlapic_get_lvt(vlapic, APIC_OFFSET_THERM_LVT);
			break;
		case APIC_LVT_CMCI:
			lvt = vlapic_get_lvt(vlapic, APIC_OFFSET_CMCI_LVT);
			break;
		default:
			lvt = 0U; /* make MISRA happy */
			ret =  -EINVAL;
			break;
		}

		if (ret == 0) {
			vlapic_fire_lvt(vlapic, lvt);
		}
	}
	return ret;
}

/**
 * @brief This function locates the target vcpu with the given LAPIC ID and sets the
 *        corresponding bit in the vCPU ID bitmap of dmask.
 *
 * This function takes the LAPIC ID (dest), finds the corresponding vcpu, and sets the
 * bit in the provided vCPU bitmap for Physical Destination Mode.
 *
 * @param[in] vm      Pointer to the virtual machine structure in which the destination vCPU
 *                    belongs to.
 * @param[out] dmask  Pointer to the bitmap of the target vCPU IDs.
 * @param[in] dest    The LAPIC ID for locating the destination vcpu.
 *
 * @return None.
 *
 * @pre vm != NULL
 * @pre dmask != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
static inline void set_dest_mask_phys(struct acrn_vm *vm, uint64_t *dmask, uint32_t dest)
{
	uint16_t vcpu_id;

	vcpu_id = vm_apicid2vcpu_id(vm, dest);
	if (vcpu_id < vm->hw.created_vcpus) {
		bitmap_set_nolock(vcpu_id, dmask);
	}
}

/**
 * @brief This function checks whether the vlapic belongs to the given destination
 *        of the IPI message.
 *
 * This function determines whether the vlapic belongs to the given destination of the IPI message,
 * based on the APIC mode and logical model configuration. It supports both x2APIC and xAPIC modes.
 *
 * - In **x2APIC mode**, it checks for a match between the cluster ID and logical ID.
 * - In **xAPIC Flat model**, the match is based on an 8-bit logical ID mask.
 * - In **xAPIC Cluster model**, the match is based on a combination of 4-bit
 *   cluster and logical IDs.
 *
 * Refer to 12.6.2.2 Logical Destination Mode and 12.12.10 Determining IPI Destination in x2APIC Mode,
 * Vol. 3, SDM 325462-087 for more details.
 *
 * @param[in] vlapic  Pointer to the vlapic structure.
 * @param[in] dest    The LAPIC ID for locating the destination vcpu.
 *
 * @return true if the vlapic belongs to the given destination of the IPI message,
 *         otherwise false.
 *
 * @pre vlapic != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */

static inline bool is_dest_field_matched(const struct acrn_vlapic *vlapic, uint32_t dest)
{
	uint32_t logical_id, cluster_id, dest_logical_id, dest_cluster_id;
	uint32_t ldr = vlapic->apic_page.ldr.v;
	bool ret = false;

	if (is_x2apic_enabled(vlapic)) {
		logical_id = ldr & 0xFFFFU;
		cluster_id = (ldr >> 16U) & 0xFFFFU;
		dest_logical_id = dest & 0xFFFFU;
		dest_cluster_id = (dest >> 16U) & 0xFFFFU;
		if ((cluster_id == dest_cluster_id) && ((logical_id & dest_logical_id) != 0U)) {
			ret = true;
		}
	} else {
		uint32_t dfr = vlapic->apic_page.dfr.v;
		if ((dfr & APIC_DFR_MODEL_MASK) == APIC_DFR_MODEL_FLAT) {
			/*
			 * In the "Flat Model" the MDA is interpreted as an 8-bit wide
			 * bitmask. This model is available in the xAPIC mode only.
			 */
			logical_id = ldr >> 24U;
			dest_logical_id = dest & 0xffU;
			if ((logical_id & dest_logical_id) != 0U) {
				ret = true;
			}
		} else if ((dfr & APIC_DFR_MODEL_MASK) == APIC_DFR_MODEL_CLUSTER) {
			/*
			 * In the "Cluster Model" the MDA is used to identify a
			 * specific cluster and a set of APICs in that cluster.
			 */
			logical_id = (ldr >> 24U) & 0xfU;
			cluster_id = ldr >> 28U;
			dest_logical_id = dest & 0xfU;
			dest_cluster_id = (dest >> 4U) & 0xfU;
			if ((cluster_id == dest_cluster_id) && ((logical_id & dest_logical_id) != 0U)) {
				ret = true;
			}
		} else {
			/* Guest has configured a bad logical model for this vcpu. */
			dev_dbg(DBG_LEVEL_VLAPIC, "vlapic has bad logical model %x", dfr);
		}
	}

	return ret;
}

/**
 * @brief Computes the destination mask of vCPU(s) in the given VM that match the addressing
 *        specified by the (dest, phys, lowprio) tuple for the case that Destination Shorthand
 *        is no shorthand.
 *
 * This function calculates the destination mask for an interrupt based on the specified
 * destination mode, broadcast flag, physical mode, and priority settings. It determines which
 * vcpus should receive the interrupt following section 12.6.2 Determining IPI Destination,
 * Volume 3, Intel SDM 325462-087US.
 *
 * @param[in]  vm            Pointer to the virtual VM structure.
 * @param[in]  is_broadcast  Whether to broadcast the interrupt to all vcpus. True for
 *                           broadcast, false otherwise.
 * @param[in]  dest          The value for locating the destination of the vcpu.
 * @param[in]  phys          Whether it is physical destination mode. True for physical
 *                           mode, false otherwise.
 * @param[in]  lowprio       Whether it is lowest priority delivery mode. true for low
 *                           priority, false otherwise.
 *
 * @return The destination bitmap of the target vCPU IDs.
 *
 * @pre vm != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
uint64_t
vlapic_calc_dest_noshort(struct acrn_vm *vm, bool is_broadcast,
		uint32_t dest, bool phys, bool lowprio)
{
	uint64_t dmask = 0UL;
	struct acrn_vlapic *vlapic, *lowprio_dest = NULL;
	struct acrn_vcpu *vcpu;
	uint16_t vcpu_id;

	if (is_broadcast) {
		/* Broadcast in both logical and physical modes. */
		dmask = vm_active_cpus(vm);
	} else if (phys) {
		/* Physical mode: "dest" is local APIC ID. */
		set_dest_mask_phys(vm, &dmask, dest);
	} else {
		/*
		 * Logical mode: "dest" is message destination addr
		 * to be compared with the logical APIC ID in LDR.
		 */
		foreach_vcpu(vcpu_id, vm, vcpu) {
			vlapic = vm_lapic_from_vcpu_id(vm, vcpu_id);
			if (!is_dest_field_matched(vlapic, dest)) {
				continue;
			}

			if (lowprio) {
				/*
				 * for lowprio delivery mode, the lowest-priority one
				 * among all "dest" matched processors accepts the intr.
				 */
				if (lowprio_dest == NULL) {
					lowprio_dest = vlapic;
				} else if (lowprio_dest->apic_page.ppr.v > vlapic->apic_page.ppr.v) {
					lowprio_dest = vlapic;
				} else {
					/* No other state currently, do nothing */
				}
			} else {
				bitmap_set_nolock(vcpu_id, &dmask);
			}
		}

		if (lowprio && (lowprio_dest != NULL)) {
			bitmap_set_nolock(vlapic2vcpu(lowprio_dest)->vcpu_id, &dmask);
		}
	}

	return dmask;
}

/**
 * @brief Calculates destination mask with the set of vcpus that match the addressing specified
 *        by the (shorthand, dest, phys, lowprio) tuple.
 *
 * This function calculates the destination mask of vCPU(s) for an interrupt based on the
 * shorthand addressing mode, broadcast flag, physical destination mode, and priority
 * of the interrupt, according to section 12.6.2 Determining IPI Destination, Volume 3,
 * Intel SDM 325462-087US.
 *
 * @param[in]  vcpu         Pointer to the virtual CPU structure.
 * @param[in]  shorthand    Indicator value of shorthand addressing mode.
 * @param[in]  is_broadcast Whether to broadcast the interrupt to all vcpus. True if broadcast,
 *                          false otherwise.
 * @param[in]  dest         The value for locating the destination of the vcpu.
 * @param[in]  phys         Whether it is physical destination mode. True for physical mode,
 *                          false otherwise.
 * @param[in]  lowprio      Whether it is lowest priority delivery mode. True for low priority,
 *                          false otherwise.
 *
 * @return The destination bitmap of the target vCPU IDs.
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
static uint64_t
vlapic_calc_dest(struct acrn_vcpu *vcpu, uint32_t shorthand, bool is_broadcast,
		uint32_t dest, bool phys, bool lowprio)
{
	uint64_t dmask = 0UL;

	switch (shorthand) {
	case APIC_DEST_NOSHORT:
		dmask = vlapic_calc_dest_noshort(vcpu->vm, is_broadcast, dest, phys, lowprio);
		break;
	case APIC_DEST_SELF:
		bitmap_set_nolock(vcpu->vcpu_id, &dmask);
		break;
	case APIC_DEST_ALLISELF:
		dmask = vm_active_cpus(vcpu->vm);
		break;
	case APIC_DEST_ALLESELF:
		dmask = vm_active_cpus(vcpu->vm);
		bitmap_clear_nolock(vcpu->vcpu_id, &dmask);
		break;
	default:
		/*
		 * All possible values of 'shorthand' has been handled in prior
		 * case clauses.
		 */
		break;
	}

	return dmask;
}

/**
 * @brief Processes INIT or STARTUP IPIs (Inter-Processor Interrupts) for a target virtual CPU.
 *
 * This function handles specific delivery modes (INIT or STARTUP) when writing to the guest Local
 * APIC Interrupt Command Register (ICR) of a target virtual CPU (vCPU). The behavior is determined
 * by the delivery mode specified:
 *
 * - **INIT Mode**:
 *   - If the target vCPU is not the Bootstrap Processor (BSP), this function calls zombie_vcpu to put
 *     vCPU into zombie state then calls reset_vcpu to put target vcpu to INIT state and wait for SIPI.
 *   - The target vCPU's state is reset, and its `nr_sipi` (number of SIPIs expected) is initialized
 *     to 1, requiring a single STARTUP IPI to proceed.
 *
 * - **STARTUP Mode**:
 *   - If the target vCPU is in the wait-for-SIPI state (INIT state) and has remaining expected SIPIs,
 *     it transitions out of the wait-for-SIPI state upon receiving a valid STARTUP IPI.
 *   - The target vCPU's startup entry point is set based on the startup vector specified in the ICR
 *     by calling set_vcpu_startup_entry function, triggers an ACRN_REQUEST_INIT_VMCS event to target
 *     vCPU by calling vcpu_make_request function to trigger VMCS initialization, the vCPU is launched
 *     for execution by calling launch_vcpu function.
 *
 * @param[inout] target_vcpu  Pointer to the vCPU that is the target of the INIT or STARTUP IPI.
 *                            The vCPU's state and configuration are modified as necessary.
 * @param[in]     mode        The delivery mode of the IPI, either `APIC_DELMODE_INIT` or
 *                            `APIC_DELMODE_STARTUP`.
 * @param[in]     icr_low     The lower 32 bits of the ICR, containing mode-specific configuration
 *                            and addressing details.
 *
 * @return None
 *
 * @pre target_vcpu != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
static void
vlapic_process_init_sipi(struct acrn_vcpu* target_vcpu, uint32_t mode, uint32_t icr_low)
{
	get_vm_lock(target_vcpu->vm);
	if (mode == APIC_DELMODE_INIT) {
		/*
		 * vBSP will ignore INIT IPI.
		 *
		 * The default Intel CPU behavior is "execute the BIOS boot-strap code (if it's the BSP)"
		 * from Intel SDM Chapter 9.4.2, Vol. 3, version 325462-078. However it's not feasible for
		 * the vBSP to go to reset vector in this case in all ACRN VM environment.
		 */
		if (((icr_low & APIC_LEVEL_MASK) != APIC_LEVEL_DEASSERT) && !is_vcpu_bsp(target_vcpu)) {

			dev_dbg(DBG_LEVEL_VLAPIC,
				"Sending INIT to %hu",
				target_vcpu->vcpu_id);

			if (target_vcpu->state != VCPU_INIT) {
				/* TODO: handle while vcpu is running in x2apic
				 * mode if lapic_pt_enalbed is true.
				 */
				/* put target vcpu to INIT state and wait for SIPI */
				zombie_vcpu(target_vcpu);
				reset_vcpu(target_vcpu, VCPU_INIT_RESET);
			}
			/* new cpu model only need one SIPI to kick AP run,
			 * the second SIPI will be ignored as it move out of
			 * wait-for-SIPI state.
			*/
			target_vcpu->arch.nr_sipi = 1U;
		} else {
			pr_info("Ignore INIT IPI to VCPU%hu for VM[%d]",
				target_vcpu->vcpu_id, target_vcpu->vm->vm_id);
		}
	} else if (mode == APIC_DELMODE_STARTUP) {
		/* Ignore SIPIs in any state other than wait-for-SIPI */
		if ((target_vcpu->state == VCPU_INIT) &&
			(target_vcpu->arch.nr_sipi != 0U)) {

			dev_dbg(DBG_LEVEL_VLAPIC,
				"Sending SIPI to %hu with vector %u",
				 target_vcpu->vcpu_id,
				(icr_low & APIC_VECTOR_MASK));

			target_vcpu->arch.nr_sipi--;
			if (target_vcpu->arch.nr_sipi <= 0U) {

				pr_info("Start Secondary VCPU%hu for VM[%d]...",
					target_vcpu->vcpu_id,
					target_vcpu->vm->vm_id);

				set_vcpu_startup_entry(target_vcpu, (uint64_t)(icr_low & APIC_VECTOR_MASK) << 12U);
				vcpu_make_request(target_vcpu, ACRN_REQUEST_INIT_VMCS);
				launch_vcpu(target_vcpu);
			}
		}
	} else {
		/* No other state currently, do nothing */
	}
	put_vm_lock(target_vcpu->vm);
	return;
}

/**
 * @brief Emulate a write to the low doubleword of the Interrupt Command Register (ICR).
 *
 * Writing to the low doubleword of the ICR triggers the delivery of an Inter-Processor
 * Interrupt (IPI). This function interprets the ICR configuration to determine the type
 * of IPI message to send IPI to destination(s).
 *
 * When an interrupt vector in the range of 0 to 15 is sent through the local APIC in
 * Fixed delivery mode, the APIC indicates an illegal vector in its Error Status Register,
 * and it signals an illegal vector error, according to
 * 12.5.2 Valid Interrupt Vectors, Volume 3, Intel SDM 325462-087US.
 *
 * It calculates the destination mask of vCPU(s) for an interrupt based on the destination
 * shorthand, broadcast flag, destination mode, and delivery mode of the interrupt, according to
 * section 12.6.2 Determining IPI Destination, Volume 3, Intel SDM 325462-087US.
 *
 * Then it delivers interrupt to target vCPU(s) one by one identified by calculated destination
 * vCPU bitmask with below delivery modes:
 * - APIC_DELMODE_FIXED:   Injects an interrupt to the target vCPU(s) with given vector
 *                         and trigger mode of edge-triggered by calling vlapic_set_intr function.
 * - APIC_DELMODE_NMI:     Injects NMI to the target vCPU(s) by calling vcpu_inject_nmi function.
 * - APIC_DELMODE_INIT,
 *   APIC_DELMODE_STARTUP: Process INIT (`APIC_DELMODE_INIT`) or Startup (`APIC_DELMODE_STARTUP`)
 *                         IPI for vCPU(s) according to section 10.4.3 MP Initialization Protocol
 *                         Algorithm for MP Systems, Volume 3, Intel SDM 25462-087US.
 * - APIC_DELMODE_SMI is not supported.
 *
 * @param[in] vlapic Pointer to the vLAPIC structure.
 *
 * @return None
 *
 * @pre vlapic != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
static void vlapic_write_icrlo(struct acrn_vlapic *vlapic)
{
	uint16_t vcpu_id;
	bool phys = false, is_broadcast = false;
	uint64_t dmask;
	uint32_t icr_low, icr_high, dest;
	uint32_t vec, mode, shorthand;
	struct acrn_vcpu *target_vcpu;
	const struct lapic_regs *lapic = &(vlapic->apic_page);

	icr_low = lapic->icr_lo.v;

	if (is_x2apic_enabled(vlapic)) {
		icr_high = lapic->icr_lo.v_ext;
		dest = icr_high;
		is_broadcast = (dest == 0xffffffffU);
	} else {
		icr_high = lapic->icr_hi.v;
		dest = icr_high >> APIC_ID_SHIFT;
		is_broadcast = (dest == 0xffU);
	}

	vec = icr_low & APIC_VECTOR_MASK;
	mode = icr_low & APIC_DELMODE_MASK;
	phys = ((icr_low & APIC_DESTMODE_LOG) == 0UL);
	shorthand = icr_low & APIC_DEST_MASK;

	if ((mode == APIC_DELMODE_FIXED) && (vec < 16U)) {
		vlapic_set_error(vlapic, APIC_ESR_SEND_ILLEGAL_VECTOR);
		dev_dbg(DBG_LEVEL_VLAPIC, "Ignoring invalid IPI %u", vec);
	} else if (((shorthand == APIC_DEST_SELF) || (shorthand == APIC_DEST_ALLISELF))
			&& ((mode == APIC_DELMODE_NMI) || (mode == APIC_DELMODE_INIT)
			|| (mode == APIC_DELMODE_STARTUP))) {
		dev_dbg(DBG_LEVEL_VLAPIC, "Invalid ICR value");
	} else {
		struct acrn_vcpu *vcpu = vlapic2vcpu(vlapic);

		dev_dbg(DBG_LEVEL_VLAPIC,
			"icrlo 0x%08x icrhi 0x%08x triggered ipi %u",
				icr_low, icr_high, vec);

		dmask = vlapic_calc_dest(vcpu, shorthand, is_broadcast, dest, phys, false);

		for (vcpu_id = 0U; vcpu_id < vcpu->vm->hw.created_vcpus; vcpu_id++) {
			if ((dmask & (1UL << vcpu_id)) != 0UL) {
				target_vcpu = vcpu_from_vid(vcpu->vm, vcpu_id);

				if (mode == APIC_DELMODE_FIXED) {
					vlapic_set_intr(target_vcpu, vec, LAPIC_TRIG_EDGE);
					dev_dbg(DBG_LEVEL_VLAPIC,
						"vlapic sending ipi %u to vcpu_id %hu",
						vec, vcpu_id);
				} else if (mode == APIC_DELMODE_NMI) {
					vcpu_inject_nmi(target_vcpu);
					dev_dbg(DBG_LEVEL_VLAPIC,
						"vlapic send ipi nmi to vcpu_id %hu", vcpu_id);
				} else if (mode == APIC_DELMODE_INIT) {
					vlapic_process_init_sipi(target_vcpu, mode, icr_low);
				} else if (mode == APIC_DELMODE_STARTUP) {
					vlapic_process_init_sipi(target_vcpu, mode, icr_low);
				} else if (mode == APIC_DELMODE_SMI) {
					pr_info("vlapic: SMI IPI do not support\n");
				} else {
					pr_err("Unhandled icrlo write with mode %u\n", mode);
				}
			}
		}
	}
}

/**
 * @brief Retrieve the highest-priority bit set in the IRR registers.
 *
 * This function scans the Interrupt Request Register (IRR) to find the highest-priority
 * interrupt vector. If no bits are set in the IRR, it returns 0.
 *
 * @param[in] vlapic Pointer to the target vLAPIC data structure.
 *
 * @return The interrupt vector corresponding to the highest-priority bit in IRR, or 0
 *         if no bits are set.
 *
 * @pre vlapic != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
static inline uint32_t vlapic_find_highest_irr(const struct acrn_vlapic *vlapic)
{
	const struct lapic_regs *lapic = &(vlapic->apic_page);
	uint32_t i, val, bitpos, vec = 0U;
	const struct lapic_reg *irrptr;

	irrptr = &lapic->irr[0];

	/* i ranges effectively from 7 to 1 */
	for (i = 7U; i > 0U; i--) {
		val = irrptr[i].v;
		if (val != 0U) {
			bitpos = (uint32_t)fls32(val);
			vec = (i * 32U) + bitpos;
			break;
		}
	}

	return vec;
}


/**
 * @brief Emulation of writing an APIC register Spurious-Interrupt Vector Register (SVR)
 *
 * This function caches the SVR value in virtual-APIC page and follows 12.4.3 Enabling or Disabling
 * the Local APIC and 12.4.7.2 Local APIC State After It Has Been Software Disabled, Volume 3,
 * Intel SDM 325462-087US to emulate APIC Software Enable/Disable change.
 *
 * - If the LAPIC is to be enabled, the emulated APIC timer is restarted if it's in periodic
 *   timer mode, calculates the timeout counter and periodic value for the vlapic timer, then
 *   the timer is updated by calling update_timer function and armed by calling add_timer function.
 *
 * - If the LAPIC is to be disabled, delete emulated APIC timer by calling del_timer function and
 *   inhibits reception of all interrupts via LVT.
 *
 * @param[inout] vlapic Pointer to the vlapic structure.
 *
 * @return None.
 *
 * @pre vlapic != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
static void
vlapic_write_svr(struct acrn_vlapic *vlapic)
{
	struct lapic_regs *lapic;
	uint32_t old, new, changed;

	lapic = &(vlapic->apic_page);

	new = lapic->svr.v;
	old = vlapic->svr_last;
	vlapic->svr_last = new;

	changed = old ^ new;
	if ((changed & APIC_SVR_ENABLE) != 0U) {
		if ((new & APIC_SVR_ENABLE) == 0U) {
			/*
			 * The apic is now disabled so stop the apic timer
			 * and mask all the LVT entries.
			 */
			dev_dbg(DBG_LEVEL_VLAPIC, "vlapic is software-disabled");
			del_timer(&vlapic->vtimer.timer);

			vlapic_mask_lvts(vlapic);
		} else {
			/*
			 * The apic is now enabled so restart the apic timer
			 * if it is configured in periodic mode.
			 */
			dev_dbg(DBG_LEVEL_VLAPIC, "vlapic is software-enabled");
			if (vlapic_lvtt_periodic(vlapic)) {
				if (set_expiration(vlapic)) {
					/* vlapic_init_timer has been called,
					 * and timer->fire_tsc is not 0,here
					 *  add_timer should not return error
					 */
					(void)add_timer(&vlapic->vtimer.timer);
				}
			}
		}
	}
}

/**
 * @brief Emulation of reading an APIC register with MMIO offset in the vlapic.
 *
 * This function emulates reading on the virtual LAPIC register, and delegates
 * specific actions to other functions for registers being reading from:
 *
 * - APIC_OFFSET_TIMER_ICR: If the timer mode if vlapic timer is One-shot or Periodic, read is from
 *                          the cached value of Initial Count Register (for Timer) in virtual-APIC
 *                          page, otherwise, read value is hardcoded to 0.
 *
 * - APIC_OFFSET_TIMER_CCR: Emulates reading Current Count Register (for Timer), it calculates
 *                          the remaining counter value of the vlapic timer with the divisor
 *                          shift and timer ratio which is calculated according to section
 *                          12.5.4 APIC Timer, Volume 3, Intel SDM 325462-087US.
 *
 * For other registers, value in virtual-APIC page for given register offset will be used
 * as read value.
 *
 * @param[in]   vlapic       Pointer to the vlapic structure.
 * @param[in]   offset_arg   The offset of the register in the virtual-APIC page.
 * @param[out]  data         Pointer to a buffer that will hold the result of the read operation.
 *
 * @return 0 if the read was successful, otherwise return -EACCES if the register is not supported.
 *
 * @pre vlapic != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
static int32_t vlapic_read(const struct acrn_vlapic *vlapic, uint32_t offset_arg, uint64_t *data)
{
	int32_t ret = 0;
	const struct lapic_regs *lapic = &(vlapic->apic_page);
	uint32_t i;
	uint32_t offset = offset_arg;
	*data = 0UL;

	if (offset > sizeof(*lapic)) {
		ret = -EACCES;
	} else {

		offset &= ~0x3UL;
		switch (offset) {
		case APIC_OFFSET_ID:
			*data = lapic->id.v;
			break;
		case APIC_OFFSET_VER:
			*data = lapic->version.v;
			break;
		case APIC_OFFSET_PPR:
			*data = lapic->ppr.v;
			break;
		case APIC_OFFSET_LDR:
			*data = lapic->ldr.v;
			break;
		case APIC_OFFSET_SVR:
			*data = lapic->svr.v;
			break;
		case APIC_OFFSET_ISR0:
		case APIC_OFFSET_ISR1:
		case APIC_OFFSET_ISR2:
		case APIC_OFFSET_ISR3:
		case APIC_OFFSET_ISR4:
		case APIC_OFFSET_ISR5:
		case APIC_OFFSET_ISR6:
		case APIC_OFFSET_ISR7:
			i = (offset - APIC_OFFSET_ISR0) >> 4U;
			*data = lapic->isr[i].v;
			break;
		case APIC_OFFSET_TMR0:
		case APIC_OFFSET_TMR1:
		case APIC_OFFSET_TMR2:
		case APIC_OFFSET_TMR3:
		case APIC_OFFSET_TMR4:
		case APIC_OFFSET_TMR5:
		case APIC_OFFSET_TMR6:
		case APIC_OFFSET_TMR7:
			i = (offset - APIC_OFFSET_TMR0) >> 4U;
			*data = lapic->tmr[i].v;
			break;
		case APIC_OFFSET_IRR0:
		case APIC_OFFSET_IRR1:
		case APIC_OFFSET_IRR2:
		case APIC_OFFSET_IRR3:
		case APIC_OFFSET_IRR4:
		case APIC_OFFSET_IRR5:
		case APIC_OFFSET_IRR6:
		case APIC_OFFSET_IRR7:
			i = (offset - APIC_OFFSET_IRR0) >> 4U;
			*data = lapic->irr[i].v;
			break;
		case APIC_OFFSET_ESR:
			*data = lapic->esr.v;
			break;
		case APIC_OFFSET_ICR_LOW:
			*data = lapic->icr_lo.v;
			if (is_x2apic_enabled(vlapic)) {
				*data |= ((uint64_t)lapic->icr_lo.v_ext << 32U);
			}
			break;
		case APIC_OFFSET_CMCI_LVT:
		case APIC_OFFSET_TIMER_LVT:
		case APIC_OFFSET_THERM_LVT:
		case APIC_OFFSET_PERF_LVT:
		case APIC_OFFSET_LINT0_LVT:
		case APIC_OFFSET_LINT1_LVT:
		case APIC_OFFSET_ERROR_LVT:
			*data = vlapic_get_lvt(vlapic, offset);
#ifdef INVARIANTS
			reg = vlapic_get_lvtptr(vlapic, offset);
			ASSERT(*data == *reg, "inconsistent lvt value at offset %#x: %#lx/%#x", offset, *data, *reg);
#endif
			break;
		case APIC_OFFSET_TIMER_ICR:
			/* if TSCDEADLINE mode always return 0*/
			if (vlapic_lvtt_tsc_deadline(vlapic)) {
				*data = 0UL;
			} else {
				*data = lapic->icr_timer.v;
			}
			break;
		case APIC_OFFSET_TIMER_CCR:
			*data = vlapic_get_ccr(vlapic);
			break;
		case APIC_OFFSET_TIMER_DCR:
			*data = lapic->dcr_timer.v;
			break;
		case APIC_OFFSET_EOI:
			*data = lapic->eoi.v;
			break;
		case APIC_OFFSET_DFR:
			*data = lapic->dfr.v;
			break;
		case APIC_OFFSET_ICR_HI:
			*data = lapic->icr_hi.v;
			break;
		default:
			ret = -EACCES;
			break;
		}
	}

	dev_dbg(DBG_LEVEL_VLAPIC, "vlapic read offset %x, data %lx", offset, *data);
	return ret;
}

/**
 * @brief Emulation of writing an APIC register with MMIO offset in the vlapic.
 *
 * This function emulates LAPIC register writing on the virtual LAPIC register, and delegates
 * specific actions to other functions based on the register being written to for below registers:
 *
 * - APIC_OFFSET_ICR_LOW:  For LAPIC pass-through case, it sends IPI to target vCPU(s) by following
 *                         Section 12.6.1 Interrupt Command Register (ICR), Volume 3,
 *                         Intel SDM 325462-087US, either writes ICR register directly or uses
 *                         additional logic to emulate INIT and SIPI IPI to target vCPU(s) following
 *                         section 10.4.3 MP Initialization Protocol Algorithm for MP Systems,
 *                         Volume 3, Intel SDM 325462-087US.
 *
 *                         For LAPIC virtualization case, it sends Interrupt to target vCPU(s) by
 *                         emulating ICR writing following section 12.6.1, Interrupt Command
 *                         Register (ICR), Volume 3, Intel SDM 325462-087US.
 *
 * - APIC_OFFSET_EOI:      Emulates writing EOI register in virtual-APIC page following section
 *                         12.8.5 Signaling Interrupt Servicing Completion, Volume 3,
 *                         Intel SDM 325462-087US.
 *
 * - APIC_OFFSET_SVR:      Emulates writing SVR(Spurious-Interrupt Vector Register) in virtual-APIC
 *                         page, caches the SVR value and updates the emulated APIC timer when the
 *                         APIC Software Enable/Disable bit (bit 8) is modified.
 * - APIC_OFFSET_CMCI_LVT,
 *   APIC_OFFSET_TIMER_LVT,
 *   APIC_OFFSET_THERM_LVT,
 *   APIC_OFFSET_PERF_LVT,
 *   APIC_OFFSET_LINT0_LVT,
 *   APIC_OFFSET_LINT1_LVT,
 *   APIC_OFFSET_ERROR_LVT:
 *                          Emulates writing LVT registers in virtual-APIC page following section
 *                          12.5.1 Local Vector Table, and Figure 12-8. Local Vector Table (LVT),
 *                          Volume 3, Intel SDM 325462-087US, and value of target LVT register
 *                          is also cached in `lvt_last` table of vlapic.
 *                          For APIC_OFFSET_TIMER_LVT case, additionally disarms and resets emulated
 *                          vlapic timer if timer mode to be updated.
 *
 * - APIC_OFFSET_TIMER_ICR: Emulates writing on Initial Count Register(for Timer) if timer mode of
 *                          APIC timer is configured as One-shot or periodic. Disarm the existing
 *                          timer, then calculate the expiration count according to section 12.5.4
 *                          APIC Timer, Volume 3, Intel SDM 325462-087US and then starts the timer.
 *
 * - APIC_OFFSET_TIMER_DCR: Calculates the divisor shift for the vlapic timer based on the target
 *                          Divide Configuration Register (for Timer) value according to
 *                          Figure 12-10. Divide Configuration Register, Volume 3,
 *                          Intel SDM 325462-087US.
 *
 * - APIC_OFFSET_ESR:       Emulates writing ESR, according to section 12.5.3 Error Handling,
 *                          Error Handling, Volume 3, Intel SDM 325462-087US, this writing clears
 *                          any previously logged errors and updates the ESR with any errors
 *                          detected since the last write to the ESR, it reloads virtual ESR
 *                          register with any pending errors.
 *
 * - APIC_OFFSET_SELF_IPI:  Emulates writing SELF IPI MSR.
 *                          When x2APIC is enabled, if given vector number is valid (> 15),
 *                          it triggers an edge-triggered self-IPI delivery by calling
 *                          vlapic_set_intr function, otherwise it logs
 *                          APIC_ESR_SEND_ILLEGAL_VECTOR ESR error bit and delivers an interrupt.
 *                          When x2APIC is not enabled, it returns -EACCES to indicate that the access is invalid.
 *
 * - APIC_OFFSET_ICR_HI:    Emulates writing Interrupt Command Register (ICR) bits 32-63.
 *                          It caches the value in the apic_page of the vlapic.
 *
 * - APIC_OFFSET_ID:        Emulates writing Local APIC ID register.
 *                          It ignores the write and returns 0.
 *
 * - APIC_OFFSET_DFR:       Emulates writing Destination Format Register.
 *                          It caches the processed value in the apic_page of the vlapic,
 *                          according to Figure 12-14. Destination Format Register (DFR), Vol. 3, SDM 325462-087.
 *
 * @param[inout] vlapic    Pointer to the vlapic structure.
 * @param[in]    offset    The register with MMIO offset in the LAPIC register page.
 * @param[in]    data      The target value to be written to the APIC register.
 *
 * @return 0 if the writing emulation is successful, return -EACCES if the register is not supported or
 *         return value returned from vlapic_x2apic_pt_icr_access function if given offset
 *         is APIC_OFFSET_ICR_LOW with LAPIC pass-through configuration.
 *
 * @pre vlapic != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
static int32_t vlapic_write(struct acrn_vlapic *vlapic, uint32_t offset, uint64_t data)
{
	struct lapic_regs *lapic = &(vlapic->apic_page);
	uint32_t *regptr;
	uint32_t data32 = (uint32_t)data;
	int32_t ret = 0;
	struct acrn_vcpu *vcpu = vlapic2vcpu(vlapic);

	dev_dbg(DBG_LEVEL_VLAPIC, "vlapic write offset %#x, data %#lx", offset, data);

	if (offset <= sizeof(*lapic)) {
		switch (offset) {
		case APIC_OFFSET_ICR_LOW:
			if (is_lapic_pt_configured(vcpu->vm)) {
				ret = vlapic_x2apic_pt_icr_access(vcpu, data);
			} else {
				if (is_x2apic_enabled(vlapic)) {
					lapic->icr_lo.v_ext = (uint32_t)(data >> 32U);
				}
				lapic->icr_lo.v = data32;
				vlapic_write_icrlo(vlapic);
			}
			break;
		case APIC_OFFSET_EOI:
			vlapic_process_eoi(vlapic);
			break;
		case APIC_OFFSET_SVR:
			lapic->svr.v = data32;
			vlapic_write_svr(vlapic);
			break;
		case APIC_OFFSET_CMCI_LVT:
		case APIC_OFFSET_TIMER_LVT:
		case APIC_OFFSET_THERM_LVT:
		case APIC_OFFSET_PERF_LVT:
		case APIC_OFFSET_LINT0_LVT:
		case APIC_OFFSET_LINT1_LVT:
		case APIC_OFFSET_ERROR_LVT:
			regptr = vlapic_get_lvtptr(vlapic, offset);
			*regptr = data32;
			vlapic_write_lvt(vlapic, offset);
			break;
		case APIC_OFFSET_TIMER_ICR:
			/* if TSCDEADLINE mode ignore icr_timer */
			if (vlapic_lvtt_tsc_deadline(vlapic)) {
				break;
			}
			lapic->icr_timer.v = data32;
			vlapic_write_icrtmr(vlapic);
			break;

		case APIC_OFFSET_TIMER_DCR:
			lapic->dcr_timer.v = data32;
			vlapic_write_dcr(vlapic);
			break;
		case APIC_OFFSET_ESR:
			vlapic_write_esr(vlapic);
			break;
		case APIC_OFFSET_SELF_IPI:
			if (is_x2apic_enabled(vlapic)) {
				lapic->self_ipi.v = data32;
				vlapic_x2apic_self_ipi_handler(vlapic);
			} else {
				ret = -EACCES;
			}
			break;
		case APIC_OFFSET_ICR_HI:
			lapic->icr_hi.v = data32;
			break;
		case APIC_OFFSET_ID:
			/* Force APIC ID as read only */
			break;
		case APIC_OFFSET_DFR:
			lapic->dfr.v = data32;
			vlapic_write_dfr(vlapic);
			break;
		default:
			ret = -EACCES;
			/* Read only */
			break;
		}
	} else {
		ret = -EACCES;
	}

	return ret;
}

/**
 * @brief Perform a reset of the vLAPIC for a given vCPU.
 *
 * This function retrieves the vLAPIC structure associated with the specified vCPU and invokes the reset
 * operation defined in the vLAPIC's operations structure.
 *
 * @param[inout] vcpu Pointer to the vcpu whose vlapic is going to be reset.
 *
 * @return None.
 *
 * @pre vlapic != NULL
 *
 * @post N/A
 *
 * @remark N/A.
 */
void
vlapic_reset(struct acrn_vcpu *vcpu)
{
	// Retrieve the vLAPIC structure associated with the given vCPU.
	struct acrn_vlapic *vlapic = vcpu_vlapic(vcpu);
	vlapic->ops->reset(vcpu);
}

/**
 * @brief Get the value of MSR IA32_APIC_BASE associated with vlapic.
 *
 * This function returns the cached IA32_APIC_BASE MSR value for given virtual LAPIC.
 *
 * @param[in] vlapic Pointer to the vlapic whose associated MSR IA32_APIC_BASE is
 *             to be read.
 *
 * @return The cached value of MSR IA32_APIC_BASE associated with vlapic.
 *
 * @pre vlapic != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
uint64_t vlapic_get_apicbase(const struct acrn_vlapic *vlapic)
{
	return vlapic->msr_apicbase;
}

/**
 * @brief Accept an interrupt via vlapic configured for LAPIC pass-through.
 *
 * This is a stub callback function and only prints a debug message with information of
 * vector, VM ID and vCPU ID.
 *
 * @param[in] vlapic Pointer to the vlapic structure.
 * @param[in] vector Interrupt vector number.
 * @param[in] level  Unused interrupt trigger mode.
 *
 * @return None.
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark N/A
 */
static void ptapic_accept_intr(struct acrn_vlapic *vlapic, uint32_t vector, __unused bool level)
{
	pr_err("Invalid op %s, VM%u, vCPU%u, vector %u", __func__,
			vlapic2vcpu(vlapic)->vm->vm_id, vlapic2vcpu(vlapic)->vcpu_id, vector);
}

/**
 * @brief Inject interrupt to given virtual LAPIC which is configured LAPIC pass-through.
 *
 * This is a stub callback function and print debug message only.
 *
 * @param[in] vlapic Pointer to the virtual LAPIC structure.
 * @param[in] guest_irq_enabled Unused.
 * @param[in] injected Unused.
 *
 * @return None.
 *
 * @pre vlapic != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
static void ptapic_inject_intr(struct acrn_vlapic *vlapic, __unused bool guest_irq_enabled, __unused bool injected)
{
	pr_err("Invalid op %s, VM%u, vCPU%u", __func__, vlapic2vcpu(vlapic)->vm->vm_id, vlapic2vcpu(vlapic)->vcpu_id);
}

/**
 * @brief Check if there are any pending interrupts for a given virtual CPU with
 *        LAPIC pass-through configured.
 *
 *  In LAPIC pass-through case, interrupts are delivered via physical local APIC interface.
 *
 * @param[in] vcpu Unused pointer to the virtual CPU structure.
 *
 * @return Always returns false, indicating there are no pending interrupts for the given vCPU.
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark Used as a placeholder invalid APIC operations.
 */
static bool ptapic_has_pending_intr(__unused struct acrn_vcpu *vcpu)
{
	return false;
}

/**
 * @brief Verify if a read operation is permitted for a given register offset for LAPIC pass-through.
 *
 * Register with below offset is readable:
 * - APIC_OFFSET_ID
 * - APIC_OFFSET_LDR
 * - APIC_OFFSET_THERM_LVT
 *
 * @param[in] offset The offset of the LAPIC register in the virtual-APIC page.
 *
 * @return true if the register with given offset can be read, false otherwise.
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark N/A
 */
static bool ptapic_read_msr_may_may_invalid(uint32_t offset)
{
	return ((offset == APIC_OFFSET_ID)
		|| (offset == APIC_OFFSET_LDR)
		|| (offset == APIC_OFFSET_THERM_LVT));

}

/**
 * @brief Verify if a write operation is permitted for a given register offset for LAPIC pass-through.
 *
 * Register with below offset is writable:
 * - APIC_OFFSET_ICR_LOW
 * - APIC_OFFSET_THERM_LVT
 *
 * @param[in] offset The offset of the LAPIC MSR in the APIC register page.
 *
 * @return true if the register with given offset can be read, false otherwise.
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark N/A
 */
static bool ptapic_write_msr_may_may_invalid(uint32_t offset)
{
	return ((offset == APIC_OFFSET_ICR_LOW)
		|| (offset == APIC_OFFSET_THERM_LVT));
}

/**
 * @brief Reset the Local APIC with pass-through mode for a given virtual CPU (vCPU).
 *
 * This function initializes the local APIC for the specified vCPU with local APIC pass-through.
 * It invokes the init_lapic() function to initialize the local APIC registers.
 *
 * @param[in] vcpu Pointer to the virtual CPU structure.
 *
 * @return None.
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark This function should run in the context of the vCPU that is being reset.
 */
static void ptapic_reset(__unused struct acrn_vcpu *vcpu)
{
	init_lapic();
}

/**
 * @brief APICv operation callback functions set supporting local APIC pass-through configuration.
 *
 * Specifically, it initializes acrn_apicv_ops callback functions as below:
 *
 * - accept_intr  This callback function is dummy and print debug message only.
 *
 * - inject_intr  This callback function is dummy and print debug message only.
 *
 * - has_pending_intr  This callback function always returns false, interrupts are injected via
 *                     physical local APIC interface.
 *
 * - x2apic_read_msr_may_valid   This callback function verifies if a read operation is permitted
 *                               for a given register offset, Reading APIC_OFFSET_ID,
 *                               APIC_OFFSET_LDR, or APIC_OFFSET_THERM_LVT is allowed.
 *
 * - x2apic_write_msr_may_valid  This callback function verifies if a write operation is permitted
 *                               for a given register offset, writing APIC_OFFSET_ICR_LOW,
 *                               APIC_OFFSET_THERM_LVT is allowed.
 *
 * - reset  This callback function resets the virtual LAPIC, initializes the local APIC operations.
 */
static const struct acrn_apicv_ops ptapic_ops = {
	.accept_intr = ptapic_accept_intr,
	.inject_intr = ptapic_inject_intr,
	.has_pending_intr = ptapic_has_pending_intr,
	.x2apic_read_msr_may_valid  = ptapic_read_msr_may_may_invalid,
	.x2apic_write_msr_may_valid  = ptapic_write_msr_may_may_invalid,
	.reset = ptapic_reset,
};

/**
 * @brief Set the IA32_APIC_BASE MSR for the virtual LAPIC.
 *
 * - When GUEST_FLAG_LOCKED_IN_XAPIC is set, the VM is restricted to xAPIC mode.
 *
 *   ACRN does not support transitioning a guest from xAPIC to x2APIC.
 *   So, only APICBASE_ENABLED bit is writable.
 *
 * - When GUEST_FLAG_LOCKED_IN_XAPIC is not set, the VM is locked in x2APIC mode.
 *
 *    This function compares the cached value and given target value of IA32_APIC_BASE MSR,
 *    and reports error if they're not the same, which means guest is not allowed to write this MSR.
 *
 *    ACRN emulates IA32_XAPIC_DISABLE_STATUS MSR and always sets LEGACY_XAPIC_DISABLED bit of this
 *    MSR, which indicates that the local APIC locked in x2APIC mode.
 *
 *    So the only part of IA32_APIC_BASE MSR could be written is the `APIC Base`(bit 35:12) field.
 *    While ACRN Hypervisor does not support change on this field to other value except 0xFEE.
 *    As a result, ACRN Hypervisor emulates this MSR as a Read-Only register.
 *
 * @param[in] vlapic Pointer to the virtual LAPIC data structure.
 * @param[in] new    Target value for the IA32_APIC_BASE MSR.
 *
 * @return Return 0 on success; otherwise, return -EACCES as modification is not allowed.
 *
 * @post N/A
 *
 * @pre vlapic != NULL
 *
 * @remark N/A
 */
int32_t vlapic_set_apicbase(struct acrn_vlapic *vlapic, uint64_t new)
{
	int32_t ret = 0;
	struct acrn_vcpu *vcpu = vlapic2vcpu(vlapic);

	if (vlapic->msr_apicbase != new) {
		if (is_locked_in_xapic_configured(vcpu->vm)) {
			uint64_t changed = vlapic->msr_apicbase ^ new;
			/*
			 * When GUEST_FLAG_LOCKED_IN_XAPIC is set, the VM is restricted to xAPIC mode.
			 * ACRN does not support transitioning a guest from xAPIC to x2APIC.
			 * So, only APICBASE_ENABLED bit is writable.
			 */
			if ((changed & ~APICBASE_ENABLED) != 0UL) {
				pr_err("%s: only APICBASE_ENABLED bit can be changed (old=0x%llx, new=0x%llx).",
					__func__, vlapic->msr_apicbase, new);
				ret = -EACCES;
			} else {
				vlapic->msr_apicbase = new;
			}
		} else {
			/*
			 * When GUEST_FLAG_LOCKED_IN_XAPIC is not set, the VM is locked in x2APIC mode.
			 * Guest can only use x2APIC mode, the whole IA32_APIC_BASE is Read-only.
			 */
			pr_err("%s, guest can't change it.", __func__);
			ret = -EACCES;
		}
	}

	return ret;
}

/**
 * @brief Receive an interrupt from the IOAPIC or MSI and inject it to the virtual local APIC
 *
 * Only Delivery Mode of IOAPIC_RTE_DELMODE_FIXED and IOAPIC_RTE_DELMODE_LOPRI are supported,
 * SMI/NMI/INIT/ExtINT interrupts are not supported from devices.
 *
 * It calculates the destination bitmask of vCPU(s) by following
 * section 12.6.2 Determining IPI Destination, Volume 3, Intel SDM 325462-087US,
 * with No Shorthand and the destination is specified in the destination field.
 *
 * According to the virtual LAPIC configuration:
 * - LAPIC pass-through: Write ICR MSR directly to fire interrupt(s) by calling  msr_write function
 *                       with constructed interrupt message according to
 *                       section 12.6.1 Interrupt Command Register (ICR), Volume 3,
 *                       Intel SDM 325462-087US.
 * - LAPIC Virtualization: Call vlapic_set_intr() to deliver interrupt to target vCPU(s) with given
 *                         vector and trigger level, the target vCPU(s) bitmask depends on the
 *                         calculated destination bitmask.
 *
 * @param[in] vm      Pointer to the VM data structure.
 * @param[in] level   Interrupt trigger mode. Use LAPIC_TRIG_EDGE for edge trigger mode or
 *                    LAPIC_TRIG_LEVEL for level trigger mode.
 * @param[in] dest    Destination ID for this interrupt.
 * @param[in] phys    Boolean indicating whether the Destination ID is interpreted as a
 *                    logical (false) or physical (true) APIC ID.
 * @param[in] delmode Delivery mode specifying how the interrupt receipt is handled.
 * @param[in] vec     Interrupt vector.
 * @param[in] rh      Redirection hint. True directs the message to the processor with
 *                    the lowest interrupt priority among eligible processors. False
 *                    directs the interrupt to the processor listed in the Destination ID.
 *
 * @return Return 0 on success; otherwise, return -1 if given delivery mode is not
 *         IOAPIC_RTE_DELMODE_FIXED or IOAPIC_RTE_DELMODE_LOPRI.
 *
 * @post N/A
 *
 * @pre vm != NULL
 * @pre vec <= 255U
 *
 * @remark N/A
 */
int32_t
vlapic_receive_intr(struct acrn_vm *vm, bool level, uint32_t dest, bool phys,
		uint32_t delmode, uint32_t vec, bool rh)
{
	bool lowprio;
	uint16_t vcpu_id;
	uint64_t dmask;
	struct acrn_vcpu *target_vcpu;
	int32_t ret = 0;

	if ((delmode != IOAPIC_RTE_DELMODE_FIXED) &&
			(delmode != IOAPIC_RTE_DELMODE_LOPRI)) {
		pr_err("%s receive invalid inrt delmode %#x", __func__, delmode);
		ret = -1;
	} else {
		lowprio = (delmode == IOAPIC_RTE_DELMODE_LOPRI) || rh;

		/*
		 * We don't provide any virtual interrupt redirection hardware so
		 * all interrupts originating from the ioapic or MSI specify the
		 * 'dest' in the legacy xAPIC format.
		 */
		dmask = vlapic_calc_dest_noshort(vm, false, dest, phys, lowprio);
		dev_dbg(DBG_LEVEL_VLAPIC, "%s: target_vcpu destination mask 0x%016lx", __func__, dmask);

		if (is_lapic_pt_configured(vm)) {
			union apic_icr icr;
			uint32_t pdest = 0;
			vcpu_id = ffs64(dmask);
			while (vcpu_id != INVALID_BIT_INDEX) {
				bitmap_clear_nolock(vcpu_id, &dmask);
				target_vcpu = vcpu_from_vid(vm, vcpu_id);
				pdest |= per_cpu(lapic_ldr, pcpuid_from_vcpu(target_vcpu));
				vcpu_id = ffs64(dmask);
			}
			icr.value = 0UL;
			icr.bits.dest_field = pdest;
			icr.bits.vector = vec;
			icr.bits.delivery_mode = delmode;
			icr.bits.destination_mode = IOAPIC_RTE_DESTMODE_LOGICAL;
			icr.bits.level = level;

			msr_write(MSR_IA32_EXT_APIC_ICR, icr.value);
			dev_dbg(DBG_LEVEL_LAPICPT, "%s: icr.value 0x%016lx", __func__, icr.value);
		} else {
			for (vcpu_id = 0U; vcpu_id < vm->hw.created_vcpus; vcpu_id++) {
				struct acrn_vlapic *vlapic;
				if ((dmask & (1UL << vcpu_id)) != 0UL) {
					target_vcpu = vcpu_from_vid(vm, vcpu_id);

					/* only make request when vlapic enabled */
					vlapic = vcpu_vlapic(target_vcpu);
					if (vlapic_enabled(vlapic)) {
						vlapic_set_intr(target_vcpu, vec, level);
					}
				}
			}
		}
	}

	return ret;
}

/**
 * @brief Fire an interrupt with the specified vector and trigger level to the virtual CPU.
 *
 * This function checks the given vector number, and if vector is less than 16, it logs
 * APIC_ESR_RECEIVE_ILLEGAL_VECTOR ESR error event and delivers interrupt to virtual APIC via
 * Error LVT register.
 *
 * If the given vector is valid, this function fires interrupt to the target vCPU by passing
 * it to the appropriate handler defined by the APICv operational interface of accept_intr
 * callback if associated vlapic is enabled.
 *
 * Then, it calls signal_event to signal a VCPU_EVENT_VIRTUAL_INTERRUPT event to wakeup vCPU.
 *
 * @param[in] vcpu   Pointer to the virtual CPU data structure.
 * @param[in] vector Interrupt vector number.
 * @param[in] level  Interrupt trigger mode, LAPIC_TRIG_EDGE for edge trigger mode or
 *                   LAPIC_TRIG_LEVEL for level trigger mode.
 *
 * @return None.
 *
 * @post N/A
 *
 * @pre vcpu != NULL
 * @pre vector <= 255U
 *
 * @remark N/A
 */
void
vlapic_set_intr(struct acrn_vcpu *vcpu, uint32_t vector, bool level)
{
	struct acrn_vlapic *vlapic;

	vlapic = vcpu_vlapic(vcpu);
	if (vector < 16U) {
		vlapic_set_error(vlapic, APIC_ESR_RECEIVE_ILLEGAL_VECTOR);
		dev_dbg(DBG_LEVEL_VLAPIC,
		    "vlapic ignoring interrupt to vector %u", vector);
	} else {
		vlapic_accept_intr(vlapic, vector, level);
	}
}

/**
 * @brief Triggers LAPIC local interrupt via LVT to current vCPU.
 *
 * This function triggers a Local Vector Table (LVT) interrupt for current vCPU with
 * given LVT index by following section 12.5 HANDLING LOCAL INTERRUPTS, Volume 3,
 * Intel SDM 325462-087US, based on value of LVT register.
 *
 * @param[in] vcpu       Pointer to the acrn_vcpu data structure.
 * @param[in] lvt_index  Index to the LVT that will be triggered.
 *
 * @return status returned by callee function to trigger this interrupt.
 *
 * @pre vm != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
int32_t
vlapic_set_local_intr(struct acrn_vcpu *vcpu, uint32_t lvt_index)
{
	return vlapic_trigger_lvt(vcpu_vlapic(vcpu), lvt_index);
}

/**
 * @brief Inject Interrupt(s) to target VM with given MSI message.
 *
 * The function fires interrupt(s) to target VM based on a given MSI message.
 * It verifies given MSI address data which shall be MSI_ADDR_BASE, then exacts parameters,
 * such as destination mode, delivery mode, redirection hint and vector from given MSI data
 * and address message.
 *
 * Then, it fires an edge-triggered interrupt with above parameters to target by calling
 * vlapic_receive_intr function.
 *
 * @param[in] vm   Pointer to VM data structure.
 * @param[in] addr Target value for MSI address register.
 * @param[in] data Target value for MSI Message data register.
 *
 * @return 0 on success, otherwise return -1 on failure if MSI address is invalid or MSI
 *         interrupt delivers fails.
 *
 * @pre vm != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
int32_t vlapic_inject_msi(struct acrn_vm *vm, uint64_t addr, uint64_t data)
{
	int32_t ret;
	union msi_addr_reg vmsi_addr;
	union msi_data_reg vmsi_data;
	uint32_t vdest, delmode, vec;
	bool phys, rh;

	vmsi_addr.full = addr;
	vmsi_data.full = (uint32_t)data;

	dev_dbg(DBG_LEVEL_VLAPIC, "%s: vmsi_addr 0x%016lx, vmsi_data 0x%016lx",
		__func__, addr, data);

	if (vmsi_addr.bits.addr_base == MSI_ADDR_BASE) {
		vdest = vmsi_addr.bits.dest_field;
		phys = (vmsi_addr.bits.dest_mode == MSI_ADDR_DESTMODE_PHYS);
		rh = (vmsi_addr.bits.rh == MSI_ADDR_RH);
		delmode = (uint32_t)(vmsi_data.bits.delivery_mode);
		vec = (uint32_t)(vmsi_data.bits.vector);

		ret = vlapic_receive_intr(vm, LAPIC_TRIG_EDGE, vdest, phys, delmode, vec, rh);
	} else {
		dev_dbg(DBG_LEVEL_VLAPIC, "lapic MSI invalid addr %#lx", vmsi_addr.full);
	        ret = -1;
	}

	return ret;
}

/**
 * @brief Timeout handler of emulated timer of vlapic.
 *
 * This function checks if timer interrupt is masked in imer LVT register (bit16), if no,
 * it delivers a edge-triggered interrupt to the virtual CPU by calling vlapic_set_intr,
 * where vector is extracted from timer LVT register of this vlapic.
 *
 * This function does nothing if timer interrupt is masked in timer LVT register of vlapic.
 *
 * @param[in] data Pointer to the virtual CPU structure.
 *
 * @return None.
 *
 * @pre data != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
static void vlapic_timer_expired(void *data)
{
	struct acrn_vcpu *vcpu = (struct acrn_vcpu *)data;
	struct acrn_vlapic *vlapic;
	struct lapic_regs *lapic;

	vlapic = vcpu_vlapic(vcpu);
	lapic = &(vlapic->apic_page);

	/* inject vcpu timer interrupt if not masked */
	if (!vlapic_lvtt_masked(vlapic)) {
		vlapic_set_intr(vcpu, lapic->lvt[APIC_LVT_TIMER].v & APIC_LVTT_VECTOR, LAPIC_TRIG_EDGE);
	}
}

/**
 * @brief Convert the x2APIC MSR address to the corresponding MMIO offset in virtual-APIC page.
 *
 * This function converts a given x2APIC MSR address to an offset within the virtual-LAPIC page
 * by following section 12.4.1 The Local APIC Block Diagram and
 * section 12.12.1.2 x2APIC Register Address Space, Volume 3, Intel SDM 325462-087US:
 *
 * - MMIO Offset = (((MSR Index - 0x800U) & 0x3FFU) << 4U)
 *
 * @param[in] msr The x2APIC MSR address to be converted.
 *
 * @return The corresponding MMIO offset in the virtual-APIC page.
 *
 * @pre msr >= 0x800 && msr < 0x900
 *
 * @post N/A
 *
 * @remark N/A
 */
static inline  uint32_t x2apic_msr_to_regoff(uint32_t msr)
{

	return (((msr - 0x800U) & 0x3FFU) << 4U);
}

/**
 * @brief Emulation of writing to the LAPIC Interrupt Command Register(ICR) for LAPIC pass-through.
 *
 * This function emulates the writing of LAPIC ICR for LAPIC pass-through configuration.
 * Target vCPU(s) bitmask is calculated following section 12.6.2 Determining IPI Destination,
 * Volume 3, Intel SDM 325462-087US, with given shorthand, destination and delivery mode
 * which are parsed from target ICR value.
 *
 * Then, this function sends IPI to target vCPU(s) based on the calculated target destination
 * bitmask, and following deliver modes are handled:
 *
 * - INIT and Startup delivery modes: Process INIT or STARTUP IPI for vCPU(s) according to section
 *                                    10.4.3 MP Initialization Protocol Algorithm for MP System,
 *                                    Volume 3, Intel SDM 325462-087US.
 * - FIXED and NMI delivery mode: Writes ICR Register to trigger an IPI to target vCPU(s) by calling
 *                                msr_write function with Destination Mode of physical and APIC ID
 *                                of target physical CPU(s).
 * - LOWPRIO delivery mode: Delivery mode of Lowest Priority is not supported in x2APIC mode,
 *                          according to Figure 12-28. Interrupt Command Register (ICR) in x2APIC
 *                          Mode, Volume3, Intel SDM 25462-087US.
 * - SMI delivery mode is not supported.
 *
 * @param[in] vcpu Pointer to the virtual CPU structure whose LAPIC ICR is accessed.
 * @param[in] val Target value to be written to the ICR MSR.
 *
 * @return Always returns 0, indicating ICR write is successfully handled.
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
static int32_t
vlapic_x2apic_pt_icr_access(struct acrn_vcpu *vcpu, uint64_t val)
{
	uint32_t papic_id, dest = (uint32_t)(val >> 32U);
	uint32_t icr_low = (uint32_t)val;
	uint32_t mode = icr_low & APIC_DELMODE_MASK;
	uint16_t vcpu_id;
	struct acrn_vcpu *target_vcpu;
	bool phys;
	uint32_t shorthand;
	uint64_t dmask;

	phys = ((icr_low & APIC_DESTMODE_LOG) == 0UL);
	shorthand = icr_low & APIC_DEST_MASK;
	dmask = vlapic_calc_dest(vcpu, shorthand, (dest == 0xffffffffU), dest, phys, false);

	/**
	 * The hypervisor sets the "Destination Shorthand" field to 00B (No Shorthand)
	 * since the emulation is done through sending IPI to each VCPU in dmask one by one.
	 */
	icr_low = icr_low & (~APIC_DEST_MASK);
	for (vcpu_id = 0U; vcpu_id < vcpu->vm->hw.created_vcpus; vcpu_id++) {
		if (((dmask & (1UL << vcpu_id)) != 0UL) &&
				(vcpu->vm->hw.vcpu_array[vcpu_id].state != VCPU_OFFLINE)) {
			target_vcpu = vcpu_from_vid(vcpu->vm, vcpu_id);

			switch (mode) {
			case APIC_DELMODE_INIT:
				vlapic_process_init_sipi(target_vcpu, mode, icr_low);
				break;
			case APIC_DELMODE_STARTUP:
				vlapic_process_init_sipi(target_vcpu, mode, icr_low);
				break;
			/* Reserved in x2APIC mode */
			case APIC_DELMODE_LOWPRIO:
			case APIC_DELMODE_RR:
			case APIC_DELMODE_RESV:
			/* Not supported by ACRN */
			case APIC_DELMODE_SMI:
				dev_dbg(DBG_LEVEL_LAPICPT, "%s vapic_id: 0x%08x, unsupported delivery mode %x",
					__func__, target_vcpu->arch.vlapic.vapic_id, mode);
				break;
			default:
				/* convert the dest from virtual apic_id to physical apic_id */
				papic_id = per_cpu(lapic_id, pcpuid_from_vcpu(target_vcpu));
				/* set to physical destination mode */
				icr_low = icr_low & (~(APIC_DESTMODE_MASK | APIC_ICRLO_RESV_MASK));
				dev_dbg(DBG_LEVEL_LAPICPT, "%s vapic_id: 0x%08x papic_id: 0x%08x icr_low:0x%08x",
					__func__, target_vcpu->arch.vlapic.vapic_id, papic_id, icr_low);
				msr_write(MSR_IA32_EXT_APIC_ICR, (((uint64_t)papic_id) << 32U) | icr_low);
				break;
			}
		}
	}

	return 0;
}

/**
 * @brief Verify if a read operation is permitted for a given register offset in virtual-APIC
 *        with x2APIC mode.
 *
 *  Only reading Current Count Register (for Timer) is allowed:
 * - APIC_OFFSET_TIMER_CCR
 *
 * @param[in] offset The offset of the LAPIC MSR in the APIC register page.
 *
 * @return true if the register with given offset can be read, false otherwise.
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark N/A
 */
static bool apicv_advanced_x2apic_read_msr_may_valid(uint32_t offset)
{
	return (offset == APIC_OFFSET_TIMER_CCR);
}

/**
 * @brief Verify if a write operation is permitted for a given register offset in virtual-APIC
 *        with x2APIC mode.
 *
 *  Writing LAPIC registers except below ones are allowed:
 * - APIC_OFFSET_DFR
 * - APIC_OFFSET_ICR_HI
 * - APIC_OFFSET_EOI
 * - APIC_OFFSET_SELF_IPI
 *
 * @param[in] offset The offset of the LAPIC MSR in the APIC register page.
 *
 * @return true if the register with given offset can be programmed, false otherwise.
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark N/A
 */
static bool apicv_advanced_x2apic_write_msr_may_valid(uint32_t offset)
{
	return (offset != APIC_OFFSET_DFR) && (offset != APIC_OFFSET_ICR_HI) &&
		(offset != APIC_OFFSET_EOI) && (offset != APIC_OFFSET_SELF_IPI);
}

/**
 * @brief Reset the APICv for a given virtual CPU.
 *
 * Reset the registers of APICv to default value according to the section 12.4.7 Local APIC State,
 * Volume 3, Intel SDM 325462-087US.
 *
 * @param[inout] vcpu Pointer to the virtual CPU structure whose APICv state is to be reset.
 *
 * @return none.
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static void apicv_reset(struct acrn_vcpu *vcpu)
{
	struct acrn_vlapic *vlapic = vcpu_vlapic(vcpu);
	struct lapic_regs *lapic = &(vlapic->apic_page);
	uint32_t preserved_apic_id = lapic->id.v;

	/* Set default value for registers of APICv */
	/* clear all to zero */
	(void)memset((void *)lapic, 0U, sizeof(struct lapic_regs));

	if (!is_locked_in_xapic_configured(vcpu->vm)) {
		/* LAPIC ID register is Read-only in x2APIC mode */
		vlapic_build_x2apic_id(vlapic);
	} else {
		if (vlapic2vcpu(vlapic)->vcpu_id == BSP_CPU_ID) {
			/* BSP only supports reset */
			vlapic->msr_apicbase = DEFAULT_APIC_BASE | APICBASE_XAPIC | APICBASE_BSP;
			lapic->id.v = vlapic->vapic_id;
			lapic->id.v <<= APIC_ID_SHIFT;
		} else {
			/*
			 * AP only supports INIT.
			 *
			 * The value of IA32_APIC_BASE is preserved, since only APICBASE_ENABLED bit is writable and
			 * the LAPIC mode shall be preserved after INIT.
			 *
			 * The local APIC ID register shall be preserved after INIT.
			 */
			lapic->id.v = preserved_apic_id;
		}
	}

	/* set non-zero default */
	lapic->version.v = VLAPIC_VERSION;
	lapic->version.v |= (VLAPIC_MAXLVT_INDEX << MAXLVTSHIFT);
	lapic->dfr.v = 0xffffffffU;
	lapic->svr.v = APIC_SVR_VECTOR;

	vlapic_mask_lvts(vlapic);
	vlapic_reset_tmr(vlapic);
	vlapic_write_dcr(vlapic);
	vlapic_reset_timer(vlapic);
	vlapic->svr_last = lapic->svr.v;
	vlapic->isrv = 0U;
}

/**
 * @brief Handle a read from MSR in virtual LAPIC registers in x2APIC mode.
 *
 * This API emulates x2APIC MSR read for both APIC pass-through and LAPIC virtualization cases.
 * Read operation is emulated only if it is allowed as below.
 *
 * For APIC virtualization case, reading on below registers are emulated:
 *
 * - APIC_OFFSET_TIMER_CCR: Emulates reading Current Count Register (for Timer), it calculates
 *                          the remaining counter value of the vlapic timer with the divisor
 *                          shift and timer ratio which is calculated according to section
 *                          12.5.4 APIC Timer, Volume 3, Intel SDM 325462-087US.
 *
 * For APIC pass-through case, reading on below registers are emulated:
 * - APIC_OFFSET_ID,
 *   APIC_OFFSET_LDR:
 *                     Value in virtual-APIC page for given register is used as read value.
 * - APIC_OFFSET_THERM_LVT: Cached value in `lvt_last` in vlapic with index of APIC_LVT_THERMAL is
 *                          used as read value.
 *
 * @param[in] vcpu Pointer to the virtual CPU associated with the x2APIC.
 * @param[in] msr The MSR offset to be read.
 * @param[out] val Pointer to store the read value of the MSR.
 *
 * @return 0 if successful; otherwise, returns -1 for an error if given register is
 *           not supported or read operation fails.
 *
 * @pre vcpu != NULL
 * @pre val != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
int32_t vlapic_x2apic_read(struct acrn_vcpu *vcpu, uint32_t msr, uint64_t *val)
{
	struct acrn_vlapic *vlapic;
	uint32_t offset;
	int32_t error = -1;

	vlapic = vcpu_vlapic(vcpu);
	offset = x2apic_msr_to_regoff(msr);
	if (vlapic->ops->x2apic_read_msr_may_valid(offset)) {
		error = vlapic_read(vlapic, offset, val);
	}

	return error;
}

/**
 * @brief Handle a write to MSR register in virtual LAPIC in x2APIC mode.
 *
 * This API emulates x2APIC MSR write for both APIC pass-through and LAPIC virtualization cases.
 * Write operation is emulated only if it is allowed as below.
 *
 * For APIC virtualization case, writing on below registers are emulated:
 *
 * - APIC_OFFSET_ICR_LOW:  Sends Interrupt to target vCPU(s) by emulating ICR writing following
 *                         section 12.6.1, Interrupt Command Register (ICR), Volume 3,
 *                         Intel SDM 325462-087US.
 *
 * - APIC_OFFSET_SVR:      Emulates writing SVR(Spurious-Interrupt Vector Register) in virtual-APIC
 *                         page, caches the SVR value and updates the emulated APIC timer when the
 *                         APIC Software Enable/Disable bit (bit 8) is modified.
 * - APIC_OFFSET_CMCI_LVT,
 *   APIC_OFFSET_TIMER_LVT,
 *   APIC_OFFSET_THERM_LVT,
 *   APIC_OFFSET_PERF_LVT,
 *   APIC_OFFSET_LINT0_LVT,
 *   APIC_OFFSET_LINT1_LVT,
 *   APIC_OFFSET_ERROR_LVT:
 *                          Emulates writing LVT registers in virtual-APIC page following section
 *                          12.5.1 Local Vector Table, and Figure 12-8. Local Vector Table (LVT),
 *                          Volume 3, Intel SDM 325462-087US, and value of target LVT register
 *                          is cached in `lvt_last` table of vlapic, for APIC_OFFSET_TIMER_LVT,
 *                          additionally disarms and resets emulated vlapic timer if timer mode
 *                          to be updated.
 *
 * - APIC_OFFSET_TIMER_ICR: Emulates writing on Initial Count Register(for Timer) if timer mode of
 *                          APIC timer is configured as One-shot or periodic. Disarm the existing
 *                          timer, then calculate the expiration count according to section 12.5.4
 *                          APIC Timer, Volume 3, Intel SDM 325462-087US and then starts the timer.
 *
 * - APIC_OFFSET_TIMER_DCR: Calculates the divisor shift for the vlapic timer based on the target
 *                          Divide Configuration Register (for Timer) value according to
 *                          Figure 12-10. Divide Configuration Register, Volume 3,
 *                          Intel SDM 325462-087US.
 *
 * - APIC_OFFSET_ESR:       Emulates writing ESR, according to section 12.5.3 Error Handling,
 *                          Error Handling, Volume 3, Intel SDM 325462-087US, this writing clears
 *                          any previously logged errors and updates the ESR with any errors
 *                          detected since the last write to the ESR, it reloads virtual ESR
 *                          register with any pending errors.
 *
 * For APIC pass-through case, writing on below registers are emulated:
 *
 * - APIC_OFFSET_ICR_LOW:  Sends IPI to target vCPU(s) by following
 *                         Section 12.6.1 Interrupt Command Register (ICR), Volume 3,
 *                         Intel SDM 325462-087US, either writes ICR register directly or processes
 *                         INIT and SIPI IPI for target vCPU(s) following
 *                         section 10.4.3 MP Initialization Protocol Algorithm for MP Systems,
 *                         Volume 3, Intel SDM 325462-087US.
 *
 * - APIC_OFFSET_THERM_LVT: Emulates writing LVT Thermal Sensor Register following section
 *                          12.5.1 Local Vector Table, and Figure 12-8. Local Vector Table (LVT),
 *                          Volume 3, Intel SDM 325462-087US, and value of target LVT register
 *                          is cached in `lvt_last` table of vlapic.
 *
 * @param[inout] vcpu Pointer to the virtual CPU.
 * @param[in] msr The index of MSR to be written.
 * @param[in] val The value to be written to the MSR.
 *
 * @return 0 if successful; otherwise, returns -1 for an error if given register is
 *           not supported or write operation fails.
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
int32_t vlapic_x2apic_write(struct acrn_vcpu *vcpu, uint32_t msr, uint64_t val)
{
	struct acrn_vlapic *vlapic;
	uint32_t offset;
	int32_t error = -1;

	vlapic = vcpu_vlapic(vcpu);
	offset = x2apic_msr_to_regoff(msr);
	if (vlapic->ops->x2apic_write_msr_may_valid(offset)) {
		error = vlapic_write(vlapic, offset, val);
	}
	return error;
}

/**
 * @brief Initialize a virtual LAPIC for a given vCPU.
 *
 * This API initializes the virtual LAPIC instance:
 *
 * - Tears down the EPT mapping for GPA of DEFAULT_APIC_BASE if current VM is Service VM.
 *   For other types of VM, this mapping is not set by default hence tearing down is not required.
 *
 * - Sets up the EPT mapping for GPA of DEFAULT_APIC_BASE on the address to APIC-Access page,
 *   which is statically allocated and fetched by calling vlapic_apicv_get_apic_access_addr function.
 *
 * - Initializes a emulated APIC timer of vlapic, this timer is configured to one-shot mode and not armed.
 *
 * - Configures the APIC ID of vlapic to be same as physical LAPIC ID.
 *
 * - If IPI virtualization is supported and LAPIC pass-through is not configured, it also
 *   initializes the PID (Posted-Interrupt Descriptor) pointer field which is indexed by
 *   APIC ID of current virtual LAPIC.
 *
 * - The vLAPIC operation callback for this virtual LAPIC is initialized based on the virtual
 *   LAPIC configuration (whether local APIC pass-through or virtual APIC is used).
 *
 * - The `IA32_APIC_BASE` MSR and the local APIC ID register are initialized based on the
 *   `GUEST_FLAG_LOCKED_IN_XAPIC` flag:
 *
 *   - If `GUEST_FLAG_LOCKED_IN_XAPIC` is set:
 *     - The VM is restricted to xAPIC mode.
 *     - The `IA32_APIC_BASE` of the vLAPIC is initialized to `DEFAULT_APIC_BASE | APICBASE_XAPIC`.
 *     - Bit 8 (the BSP bit) of `IA32_APIC_BASE` is set based on whether the given vCPU is the BSP of the VM.
 *     - The local APIC ID register is initialized according to Figure 12-6. Local APIC ID Register,
 *       Vol. 3, SDM 325462-087.
 *
 *   - If `GUEST_FLAG_LOCKED_IN_XAPIC` is not set:
 *     - The VM is locked in x2APIC mode.
 *     - The `IA32_APIC_BASE` of the vLAPIC is initialized to `DEFAULT_APIC_BASE | APICBASE_XAPIC | APICBASE_X2APIC`.
 *     - Bit 8 (the BSP bit) of `IA32_APIC_BASE` is set based on whether the given vCPU is the BSP of the VM.
 *     - The local APIC ID register is initialized according to Figure 12-6. Local APIC ID Register,
 *       Vol. 3, SDM 325462-087.
 *
 * @param[in] vcpu Pointer to the vCPU whose virtual LAPIC is being created.
 * @param[in] pcpu_id ID of the pCPU where the vCPU will run.
 *
 * @return None.
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
void vlapic_create(struct acrn_vcpu *vcpu, uint16_t pcpu_id)
{
	struct acrn_vlapic *vlapic = vcpu_vlapic(vcpu);
	struct lapic_regs *lapic;

	if (is_vcpu_bsp(vcpu)) {
		uint64_t *pml4_page =
			(uint64_t *)vcpu->vm->arch_vm.nworld_eptp;
		/* only need unmap it from Service VM as User VM never mapped it */
		if (is_service_vm(vcpu->vm)) {
			ept_del_mr(vcpu->vm, pml4_page,
				DEFAULT_APIC_BASE, PAGE_SIZE);
		}

		ept_add_mr(vcpu->vm, pml4_page,
			vlapic_apicv_get_apic_access_addr(),
			DEFAULT_APIC_BASE, PAGE_SIZE,
			EPT_WR | EPT_RD | EPT_UNCACHED);
	}

	vlapic_init_timer(vlapic);

	/* Set vLAPIC ID to be same as pLAPIC ID */
	vlapic->vapic_id = per_cpu(lapic_id, pcpu_id);

	if (can_ipiv_enabled(vcpu->vm)) {
		/* PID_ADDR[5:0] != 000001b // PID pointer not valid or reserved bits set */
		vcpu->vm->arch_vm.pid_table[vlapic->vapic_id] = hva2hpa(get_pi_desc(vcpu)) | PID_TABLE_ENTRY_VALID;
		if (vcpu->vm->arch_vm.max_lapic_id < vlapic->vapic_id) {
			/* the max lapic_id is small than 512 when arch.pid_table is valid */
			vcpu->vm->arch_vm.max_lapic_id = (uint16_t)vlapic->vapic_id;
		}
	}

	lapic = &(vlapic->apic_page);
	(void)memset((void *)lapic, 0U, sizeof(struct lapic_regs));

	vlapic->msr_apicbase = DEFAULT_APIC_BASE | APICBASE_XAPIC;

	if (vlapic2vcpu(vlapic)->vcpu_id == BSP_CPU_ID) {
		vlapic->msr_apicbase |= APICBASE_BSP;
	}

	vlapic->ops = apicv_ops;
	if (is_locked_in_xapic_configured(vcpu->vm)) {
		lapic->id.v = vlapic->vapic_id;
		lapic->id.v <<= APIC_ID_SHIFT;
	} else {
		/* vlapic is always locked on x2APIC mode if GUEST_FLAG_LOCKED_IN_XAPIC is not set. */
		vlapic->msr_apicbase |= APICBASE_X2APIC;
		/* LAPIC ID register is Read-only in x2APIC mode */
		vlapic_build_x2apic_id(vlapic);
		if (is_lapic_pt_configured(vcpu->vm)) {
			vlapic->ops = &ptapic_ops;
		}
	}

	dev_dbg(DBG_LEVEL_VLAPIC, "vlapic APIC ID : 0x%04x", vlapic->vapic_id);
}

/**
 * @brief Deinitialize the virtual LAPIC of a given vCPU.
 *
 * This API deinitializes the virtual LAPIC of the specified vCPU by deleting its associated
 * timer by calling del_timer function.
 *
 * @param[in] vcpu Pointer to the vCPU whose virtual LAPIC is being deinitialized.
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
void vlapic_free(struct acrn_vcpu *vcpu)
{
	struct acrn_vlapic *vlapic = vcpu_vlapic(vcpu);

	del_timer(&vlapic->vtimer.timer);

}

/**
 * @brief Record an interrupt to the PID of virtual LAPIC with Posted-Interrupt support.
 *
 * This API sets a bit corresponding to the specified interrupt vector in the Posted-Interrupt
 * Requests(PIR) field of the Posted-Interrupt Descriptor(PID) following
 * section 31.6 POSTED-INTERRUPT PROCESSING, Volume 3, Intel SDM 325462-087US,
 * each bit in the PIR represents an interrupt vector.
 *
 * The function also sets the Outstanding Notification(ON) bit in the PID if PIR is updated
 * successfully to indicate there is an outstanding notification for the interrupt.
 *
 * @param[in] vlapic Pointer to the virtual LAPIC structure.
 * @param[in] vector Interrupt vector to be injected.
 *
 * @return A boolean indicating whether the ON bit in the PID was
 *         set successfully (true) or not (false).
 *
 * @pre vlapic != NULL
 * @pre get_pi_desc(vlapic2vcpu(vlapic)) != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
static bool
apicv_set_intr_ready(struct acrn_vlapic *vlapic, uint32_t vector)
{
	struct pi_desc *pid;
	uint32_t idx;
	bool notify = false;

	pid = get_pi_desc(vlapic2vcpu(vlapic));
	idx = vector >> 6U;
	if (!bitmap_test_and_set_lock((uint16_t)(vector & 0x3fU), &pid->pir[idx])) {
		notify = !bitmap_test_and_set_lock(POSTED_INTR_ON, &pid->control.value);
	}
	return notify;
}

/**
 * @brief Retrieve the Host Physical Address (HPA) to the APIC-access page.
 *
 * This API statically defines a 4-KByte APIC-access page and returns its physical address
 * as the APIC-access address.
 *
 * APIC-access page is used for Virtualize APIC accesses feature of APIC virtualization
 * according to section 31.4 VIRTUALIZING MEMORY-MAPPED APIC ACCESSES,
 * Volume 3, Intel SDM 25462-087US.
 *
 * @return HPA of the APIC-access page.
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark N/A
 */
uint64_t
vlapic_apicv_get_apic_access_addr(void)
{
	/*APIC-v APIC-access address */
	static uint8_t apicv_apic_access_addr[PAGE_SIZE] __aligned(PAGE_SIZE);

	return hva2hpa(apicv_apic_access_addr);
}

/**
 * @brief Retrieve the Host Physical Address (HPA) to the virtual-APIC page.
 *
 * This API returns the HPA corresponding to the virtual-APIC page for the given virtual LAPIC.
 *
 * The virtual-APIC page is a 4-KByte region of memory that the processor uses to virtualize
 * certain accesses to APIC registers and to manage virtual interrupts according to section
 * 31.1 VIRTUAL APIC STATE, Volume 3, Intel SDM 25462-087US.
 *
 * @param[in] vlapic Pointer to the virtual LAPIC structure.
 *
 * @return HPA of the virtualized APIC registers page.
 *
 * @pre vlapic != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
uint64_t
vlapic_apicv_get_apic_page_addr(struct acrn_vlapic *vlapic)
{
	return hva2hpa(&(vlapic->apic_page));
}


/**
 * @brief Inject all pending interrupts into a virtual LAPIC via Posted-Interrupt features.
 *
 * This API fires Self-IPI with Posted Interrupts notify vector(NV) if outstanding-notification
 * (ON) bit in the posted-interrupt descriptor(PID) is set. This Self-IPI is serviced by the
 * logical processor on next VM entry to logical-OR of PIR into vIRR and clears PIR, also updates
 * RVI for interrupt evaluation and delivery according to section 31.6 POSTED-INTERRUPT PROCESSING,
 * Volume 3, Intel SDM 25462-087US.
 *
 * @param[in] vlapic Pointer to the virtual LAPIC structure.
 * @param[in] guest_irq_enabled Unused.
 * @param[in] injected Unused.
 *
 * @return None.
 *
 * @pre vlapic != NULL
 * @pre get_pi_desc(vlapic->vcpu) != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
static void apicv_advanced_inject_intr(struct acrn_vlapic *vlapic,
		__unused bool guest_irq_enabled, __unused bool injected)
{
	struct acrn_vcpu *vcpu = vlapic2vcpu(vlapic);
	struct pi_desc *pid = get_pi_desc(vcpu);
	/*
	 * From SDM Vol3 26.3.2.5:
	 * Once the virtual interrupt is recognized, it will be delivered
	 * in VMX non-root operation immediately after VM entry(including
	 * any specified event injection) completes.
	 *
	 * So the hardware can handle vmcs event injection and
	 * evaluation/delivery of apicv virtual interrupts in one time
	 * vm-entry.
	 *
	 * Here to sync the pending interrupts to irr and update rvi
	 * self-IPI with Posted Interrupt Notification Vector is sent.
	 */
	if (bitmap_test(POSTED_INTR_ON, &(pid->control.value))) {
		apicv_trigger_pi_anv(pcpuid_from_vcpu(vcpu), (uint32_t)(vcpu->arch.pid.control.bits.nv));
	}
}

/**
 * @brief Inject an interrupt into a virtual LAPIC.
 *
 * This API triggers the injection of an interrupt into the virtual LAPIC, by calling
 * APICv inject_intr callback function.
 *
 * - APIC Virtualization case: it fires Self-IPI with notify vector(NV) if the
 *   outstanding-notification(ON) bit in the posted-interrupt descriptor(PID) is set.
 *
 *   This self-IPI is serviced by the logical processor to logical-OR of PIR into vIRR and
 *   clears PIR, and updates RVI for interrupt evaluation and delivery,
 *   Following section 31.6 POSTED-INTERRUPT PROCESSING, Volume 3, Intel SDM 325462-087US.
 *
 * - APIC pass-through case: it is not applicable and do nothing.
 *
 * @param[in] vlapic             Pointer to the virtual LAPIC structure.
 * @param[in] guest_irq_enabled  Boolean value indicating whether local interrupt of a vCPU
 *                               is blocked..
 * @param[in] injected           Boolean value indicating whether a vector has been injected.
 *
 * @return None.
 *
 * @pre vlapic != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
void vlapic_inject_intr(struct acrn_vlapic *vlapic, bool guest_irq_enabled, bool injected)
{
	vlapic->ops->inject_intr(vlapic, guest_irq_enabled, injected);
}

/**
 * @brief Check if there are any pending interrupts on given virtual CPU, with advanced APICv
 *        is supported.
 *
 * This function checks if there is any bit set in all vIRR registers in virtual-APIC page, which
 * means there are interrupt(s) pending on current vlapic and return true if yes.
 *
 * @param[in] vcpu pointer to the virtual CPU structure.
 *
 * @return Return true if there is pending interrupts on given vCPU otherwise return false.
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
static bool apicv_advanced_has_pending_intr(struct acrn_vcpu *vcpu)
{
	return vlapic_find_highest_irr(vcpu_vlapic(vcpu)) != 0UL;
}

/**
 * @brief Check if there are any pending interrupts on a given virtual CPU.
 *
 * This API determines whether there are any pending interrupts on the specified virtual CPU
 * by calling has_pending_intr callback function in APICv operation.
 *
 * - APIC Virtualization case: it checks if there is any bit set in vIRR registers in
 *   virtual-APIC page and return true if yes.
 *
 * - APIC pass-through case:  it is not applicable and always return false.
 *
 * @param[in] vcpu Pointer to the virtual CPU structure.
 *
 * @return Returns true if there are pending interrupts on the given vCPU;
 *         otherwise, returns false.
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
bool vlapic_has_pending_intr(struct acrn_vcpu *vcpu)
{
	struct acrn_vlapic *vlapic = vcpu_vlapic(vcpu);
	return vlapic->ops->has_pending_intr(vcpu);
}

/**
 * @brief Check if the APIC read access is potentially valid in xAPIC mode.
 *
 * This function determines whether the specified APIC register offset
 * is a supported read target for advanced APIC virtualization under xAPIC mode.
 *
 * Below registers are allowed:
 * - LVT Corrected Machine Check Interrupt (CMCI) Register
 * - Current Count Register (for Timer)
 *
 * @param[in] offset Offset within the APIC-access page to validate.
 *
 * @return true if the offset corresponds to a supported read register; false otherwise.
 *
 * @pre N/A
 *
 * @remark N/A
 */
static bool apicv_advanced_apic_read_access_may_valid(uint32_t offset)
{
	return ((offset == APIC_OFFSET_CMCI_LVT) || (offset == APIC_OFFSET_TIMER_CCR));
}

/**
 * @brief Check if the APIC write access is potentially valid in xAPIC mode.
 *
 * This function determines whether the specified APIC register offset
 * is a supported write target for advanced APIC virtualization under xAPIC mode.
 *
 * Below registers are allowed:
 * - LVT Corrected Machine Check Interrupt (CMCI) Register
 *
 * @param[in] offset Offset within the APIC-access page to validate.
 *
 * @return true if the offset corresponds to a supported write register; false otherwise.
 *
 * @pre N/A
 *
 * @remark N/A
 */
static bool apicv_advanced_apic_write_access_may_valid(uint32_t offset)
{
	return (offset == APIC_OFFSET_CMCI_LVT);
}

/**
 * @brief Handle VM exit caused by guest access to the APIC-access page.
 *
 * This function emulates guest access (read or write) to the virtual Local APIC (vLAPIC)
 * when a VM exit is triggered due to access to the APIC-access page. This is
 * only applicable when the VM is operated in xAPIC mode (`GUEST_FLAG_LOCKED_IN_XAPIC` set).
 *
 * The function performs the following steps:
 * 1. Retrieves the APIC access type from the VM-exit qualification.
 * 2. Attempts to decode the guest instruction via `decode_instruction()`.
 * 3. If the access type is supported and instruction decoding succeeds:
 *    - For valid write offsets: emulates the instruction via `emulate_instruction()` and
 *      writes the value to the vLAPIC register.
 *    - For valid read offsets: reads from the vLAPIC register, stores the result in the MMIO request and
 *      completes the handling via `emulate_instruction()`.
 *    - For invalid write offsets: silently ignored.
 *    - For invalid read offsets: stores all zero in the MMIO request and
 *      completes the handling via `emulate_instruction()`.
 *
 * ### Supported access types
 * - Linear access for a data read during instruction execution.
 * - Linear access for a data write during instruction execution.
 *
 * ### Allowed register offsets
 * - Read: APIC_OFFSET_CMCI_LVT and APIC_OFFSET_TIMER_CCR
 * - Write: APIC_OFFSET_CMCI_LVT
 *
 * @param[inout] vcpu Pointer to the vCPU structure that triggered the VM exit.
 *
 * @return Status code indicating the result of the VM exit handling.
 *
 * @retval 0       The VM exit was successfully handled.
 * @retval -EINVAL One of the following occurred:
 *                 - The access type is unsupported.
 *                 - Instruction decoding failed.
 * @retval Others  The instruction emulation failed. See `emulate_instruction()` for details.
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
int32_t apic_access_vmexit_handler(struct acrn_vcpu *vcpu)
{
	int32_t err = 0;
	uint32_t offset;
	struct acrn_mmio_request *mmio;
	uint64_t qual = vcpu->arch.exit_qualification;
	uint64_t access_type = apic_access_type(qual);
	struct acrn_vlapic *vlapic = vcpu_vlapic(vcpu);

	/*
	 * We only support linear access for a data read/write during instruction execution.
	 * for other access types:
	 * a) we don't support vLAPIC work in real mode;
	 * 10 = guest-physical access during event delivery
	 * 15 = guest-physical access for an instruction fetch or during instruction execution
	 * b) we don't support fetch from APIC-access page since its memory type is UC;
	 * 2 = linear access for an instruction fetch
	 * c) we suppose the guest goes wrong when it will access the APIC-access page
	 * when process event-delivery. According chap 26.5.1.2 VM Exits During Event Injection,
	 * vol 3, SDM: If the "virtualize APIC accesses" VM-execution control is 1 and
	 * event delivery generates an access to the APIC-access page, that access is treated as
	 * described in Section 29.4 and may cause a VM exit.
	 * 3 = linear access (read or write) during event delivery
	 */
	if (((access_type == TYPE_LINEAR_APIC_INST_READ) || (access_type == TYPE_LINEAR_APIC_INST_WRITE)) &&
			(decode_instruction(vcpu, true) >= 0)) {
		offset = (uint32_t)apic_access_offset(qual);
		mmio = &vcpu->req.reqs.mmio_request;
		if (access_type == TYPE_LINEAR_APIC_INST_WRITE) {
			err = emulate_instruction(vcpu);
			if (err == 0) {
				if (apicv_advanced_apic_write_access_may_valid(offset)) {
					(void)vlapic_write(vlapic, offset, mmio->value);
				}
			}
		} else {
			if (apicv_advanced_apic_read_access_may_valid(offset)) {
				(void)vlapic_read(vlapic, offset, &mmio->value);
			} else {
				mmio->value = 0UL;
			}
			err = emulate_instruction(vcpu);
		}
		TRACE_2L(TRACE_VMEXIT_APICV_ACCESS, qual, (uint64_t)vlapic);
	} else {
		pr_err("%s, unhandled access type: %lu\n", __func__, access_type);
		err = -EINVAL;
	}

	return err;
}

/**
 * @brief Handler to VM Exit with reason of Virtualized EOI.
 *
 * According to section 26.6.8 Controls for APIC Virtualization, and
 * section 31.1.4 EOI Virtualization, Volume 3, Intel SDM 325462-087US,
 * EOI-Exit bitmap is used to determine which virtualized writes to the APIC's EOI register
 * cause VM exits.
 *
 * This function completes EOI virtualization performed for a virtual interrupt whose vector
 * indexed a bit set in the EOI-Exit bitmap. It calls vioapic_broadcast_eoi function to
 * broadcast EOI event to all IO-APICs if this interrupt is LEVEL triggered.
 *
 * This function does not update guest RIP.
 *
 * @param[in] vcpu pointer to the virtual CPU structure.
 *
 * @return Always return 0 to indicate this vmexit event has been processed successfully.
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
int32_t veoi_vmexit_handler(struct acrn_vcpu *vcpu)
{
	struct acrn_vlapic *vlapic = NULL;

	uint32_t vector;
	struct lapic_regs *lapic;
	struct lapic_reg *tmrptr;
	uint32_t idx;

	vcpu_retain_rip(vcpu);

	vlapic = vcpu_vlapic(vcpu);
	lapic = &(vlapic->apic_page);
	vector = (uint32_t)(vcpu->arch.exit_qualification & 0xFFUL);

	tmrptr = &lapic->tmr[0];
	idx = vector >> 5U;

	if (bitmap32_test((uint16_t)(vector & 0x1fU), &tmrptr[idx].v)) {
		/* hook to vIOAPIC */
		vioapic_broadcast_eoi(vcpu->vm, vector);
	}

	TRACE_2L(TRACE_VMEXIT_APICV_VIRT_EOI, vector, 0UL);

	return 0;
}

/**
 * @brief Handler to APIC-write VM exit.
 *
 * This function processes VM APIC-write VM exits, to complete the virtualization of a write
 * access to registers in the APIC-access page, according to below sections in Volume 3, Intel SDM 325462-087US:
 * - 31.4.3.3 APIC-Write VM Exits,
 * - 31.5 Virtualizing MSR-Based APIC Accesses, and
 * - 31.1.6.2 IPI Virtualization Using Virtual-Interrupt Posting.
 *
 * The behaviors are different depending on the VM's APIC mode, determined by the `GUEST_FLAG_LOCKED_IN_XAPIC` flag:
 *
 * ### VM operated in x2APIC mode (`GUEST_FLAG_LOCKED_IN_XAPIC` not set)
 * In this mode, below cases may lead to an APIC-Write exit:
 * - Execution of WRMSR with ECX = 83FH (self-IPI MSR) in non-root mode if the
 *   "virtual-interrupt delivery" VM-execution control is 1;
 * - IPI virtualization and execution of SENDUIPI, according to
 *   section 31.4.3.2 APIC-Write Emulation, Volume 3, Intel SDM 325462-087US,
 *   APIC-write VM exit occurs if the processor checks the value of VICR_L fail when it performs
 *   IPI virtualization.
 *
 * #### Handled Registers:
 *
 * - APIC_OFFSET_ICR_LOW: It examines the target value of ICR, and emulates an IPI to target vCPU
 *                        following 12.6.1 Interrupt Command Register (ICR), Volume 3,
 *                        Intel SDM 325462-087US, with given Delivery Mode in ICR:
 *                         - Fixed: Fires an IPI with vector number and trigger level to target vCPU
 *                                  via `accept_intr` APICv callback function.
 *                         - NMI: Injects NMI to target vCPU by calling vcpu_inject_nmi function..
 *                         - INIT & Startup: Process INIT or SIPI IPI to target vCPU following:
 *                                           10.4.3 MP Initialization Protocol Algorithm for MP Systems,
 *                                           Volume 3, Intel SDM 325462-087US.
 *                         - Other Delivery Modes are not supported.
 * - APIC_OFFSET_SELF_IPI: According to 31.1.6.2 IPI Virtualization Using Virtual-Interrupt Posting,
 *                         Volume 3, Intel SDM 325462-087US, APIC-write VM exit for illegal vector.
 *                         It logs APIC_ESR_SEND_ILLEGAL_VECTOR ESR error event and delivers
 *                         interrupt following section
 *                         12.5.2 Valid Interrupt Vectors, and 12.5.3 Error Handling, Volume 3,
 *                         Intel SDM 325462-087US.
 *
 * ### VM operated in xAPIC mode (`GUEST_FLAG_LOCKED_IN_XAPIC` set)
 * In this mode, APIC-write VM exits occur as described in 31.4.3.2 APIC-Write Emulation, Vol. 3, SDM 325462-087.
 *
 * #### Handled Registers:
 *
 * - APIC_OFFSET_ICR_LOW: It examines the target value of ICR, and emulates an IPI to target vCPU
 *                        following 12.6.1 Interrupt Command Register (ICR), Volume 3,
 *                        Intel SDM 325462-087US, with given Delivery Mode in ICR:
 *                         - Fixed: Fires an IPI with vector number and trigger level to target vCPU
 *                                  via `accept_intr` APICv callback function.
 *                         - NMI: Injects NMI to target vCPU by calling vcpu_inject_nmi function..
 *                         - INIT & Startup: Process INIT or SIPI IPI to target vCPU following:
 *                                           10.4.3 MP Initialization Protocol Algorithm for MP Systems,
 *                                           Volume 3, Intel SDM 325462-087US.
 *                         - Other Delivery Modes are not supported.
 * - APIC_OFFSET_ID: Read-only; writes are ignored.
 * - APIC_OFFSET_LDR: Updates the Local Destination Register according to
 *                    Figure 12-13. Logical Destination Register (LDR), Vol. 3, SDM 325462-087.
 * - APIC_OFFSET_DFR: Updates the Destination Format Register according to
 *                    Figure 12-14. Destination Format Register (DFR), Vol. 3, SDM 325462-087.
 * - APIC_OFFSET_SVR: Updates the Spurious Interrupt Vector Register according to
 *                    12.4.3 Enabling or Disabling the Local APIC and
 *                    12.4.7.2 Local APIC State After It Has Been Software Disabled, Vol. 3, SDM 325462-087.
 * - APIC_OFFSET_ESR: Updates the Error Status Register according to 12.5.3 Error Handling, Vol. 3, SDM 325462-087.
 * - Local Vector Table (LVT) entries are emulated according to
 *   12.5.1 Local Vector Table, and Figure 12-8 Local Vector Table (LVT), Vol. 3, SDM 325462-087.
 *   Below entries are covered:
 *   - APIC_OFFSET_CMCI_LVT
 *   - APIC_OFFSET_TIMER_LVT
 *   - APIC_OFFSET_THERM_LVT
 *   - APIC_OFFSET_PERF_LVT
 *   - APIC_OFFSET_LINT0_LVT
 *   - APIC_OFFSET_LINT1_LVT
 *   - APIC_OFFSET_ERROR_LVT
 * - APIC_OFFSET_TIMER_ICR: Updates the Timer Initial Count Register according to 12.5.4 APIC Timer,
 *                          Vol. 3, SDM 325462-087.
 * - APIC_OFFSET_TIMER_DCR: Updates the Timer Divide Configuration Register according to
 *                          Figure 12-10. Divide Configuration Register, Vol. 3, SDM 325462-087.
 *
 * ### Unhandled Offsets:
 * Any write to an unsupported register offset will result in an error return (`-EACCES`),
 * and the access will be logged.
 *
 * @param[in] vcpu Pointer to the virtual CPU structure.
 *
 * @return 0 on successful handling; -EACCES if the register offset is unsupported.
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 *
 * @remark This handler is invoked when the guest writes to APIC registers that are
 *         configured to cause VM exits.
 */
int32_t apic_write_vmexit_handler(struct acrn_vcpu *vcpu)
{
	int32_t err = 0;
	struct acrn_vlapic *vlapic = NULL;
	uint64_t qual = vcpu->arch.exit_qualification;
	uint32_t offset = (uint32_t)(qual & 0xFFFUL);

	vcpu_retain_rip(vcpu);
	vlapic = vcpu_vlapic(vcpu);

	if (is_x2apic_enabled(vlapic)) {
		if (offset == APIC_OFFSET_ICR_LOW) {
			/* IPI virtualization */
			vlapic_write_icrlo(vlapic);
		} else {
			if (offset == APIC_OFFSET_SELF_IPI) {
				/* virtual-interrupt delivery, Vector < 16 */
				vlapic_set_error(vlapic, APIC_ESR_SEND_ILLEGAL_VECTOR);
			} else {
				err = -EACCES;
				pr_err("%s Unhandled APIC-Write, offset:0x%x", __func__, offset);
			}
		}
	} else {
		switch (offset) {
		case APIC_OFFSET_ICR_LOW:
			vlapic_write_icrlo(vlapic);
			break;
		case APIC_OFFSET_ID:
			/* Force APIC ID as read only */
			break;
		case APIC_OFFSET_LDR:
			vlapic_write_ldr(vlapic);
			break;
		case APIC_OFFSET_DFR:
			vlapic_write_dfr(vlapic);
			break;
		case APIC_OFFSET_SVR:
			vlapic_write_svr(vlapic);
			break;
		case APIC_OFFSET_ESR:
			vlapic_write_esr(vlapic);
			break;
		case APIC_OFFSET_CMCI_LVT:
		case APIC_OFFSET_TIMER_LVT:
		case APIC_OFFSET_THERM_LVT:
		case APIC_OFFSET_PERF_LVT:
		case APIC_OFFSET_LINT0_LVT:
		case APIC_OFFSET_LINT1_LVT:
		case APIC_OFFSET_ERROR_LVT:
			vlapic_write_lvt(vlapic, offset);
			break;
		case APIC_OFFSET_TIMER_ICR:
			vlapic_write_icrtmr(vlapic);
			break;
		case APIC_OFFSET_TIMER_DCR:
			vlapic_write_dcr(vlapic);
			break;
		default:
			err = -EACCES;
			pr_err("%s Unhandled APIC-Write, offset:0x%x", __func__, offset);
			break;
		}
	}

	TRACE_2L(TRACE_VMEXIT_APICV_WRITE, offset, 0UL);
	return err;

}

/**
 * @brief Handle self-interrupt in virtual LAPIC operating in x2APIC mode.
 *
 * This function emulates write operation on SELF IPI MSR with given vector which is cached
 * in virtual-APIC page before calling this function.
 *
 * - Vector number shall be greater than 15, otherwise it logs APIC_ESR_SEND_ILLEGAL_VECTOR
 *   ESR error bit and delivers an interrupt.
 * - If given vector is valid, it triggers an edge-triggered self-IPI delivery by calling
 *   vlapic_set_intr function.
 *
 * @param[inout] vlapic Pointer to the virtual LAPIC structure initiating the self-interrupt.
 *
 * @return None.
 *
 * @pre vlapic != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
static void vlapic_x2apic_self_ipi_handler(struct acrn_vlapic *vlapic)
{
	struct lapic_regs *lapic;
	uint32_t vector;

	lapic = &(vlapic->apic_page);
	vector = lapic->self_ipi.v & APIC_VECTOR_MASK;
	if (vector < 16U) {
		vlapic_set_error(vlapic, APIC_ESR_SEND_ILLEGAL_VECTOR);
		dev_dbg(DBG_LEVEL_VLAPIC, "Ignoring invalid IPI %u", vector);
	} else {
		vlapic_set_intr(vlapic2vcpu(vlapic), vector, LAPIC_TRIG_EDGE);
	}
}

/**
 * @brief Handle VM exit due to TPR (Task Priority Register) below threshold.
 *
 * This handler is invoked when the guest's TPR value drops below the threshold,
 * indicating that previously interrupts may now be deliverable.
 * It requests the hypervisor to reevaluate pending virtual interrupts for the vCPU
 * by calling vcpu_make_request function, and keeps guest RIP not updated by calling
 * vcpu_retain_rip function.
 *
 * @param[in] vcpu Pointer to the virtual CPU structure.
 *
 * @return Always returns 0 to indicate successful handling of the VM exit.
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
int32_t tpr_below_threshold_vmexit_handler(struct acrn_vcpu *vcpu)
{
	vcpu_make_request(vcpu, ACRN_REQUEST_EVENT);
	vcpu_retain_rip(vcpu);

	return 0;
}

#ifdef CONFIG_ENFORCE_APICv_BASIC_SUPPORT

/**
 * @brief Update TPR threshold VM-execution control field.
 *
 * This function updates TPR threshold VM-execution control field based on TPR and IRR in virtual
 * LAPIC page, according to 28.2.1.1 VM-Execution Control Fields, Volume 3, Intel SDM 25462-087US,
 * the value of bits 3:0 of the TPR threshold VM-execution control field should not be greater than
 * the value of bits 7:4 of VTPR.
 *
 * Bits 3:0 of the TPR threshold VM-execution control field is set with the value of bits 7:4
 * for the highest vector in IRR if it is not greater than virtual TPR( bits 7:4) value, otherwise,
 * TPR threshold is set to 0.
 *
 * @param[inout] vlapic Pointer to the virtual LAPIC structure.
 *
 * @return None.
 *
 * @pre vlapic != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
static void vlapic_update_tpr_threshold(const struct acrn_vlapic *vlapic)
{
	uint32_t irr, tpr, threshold;

	tpr = vlapic->apic_page.tpr.v;
	tpr = ((tpr & 0xf0U) >> 4U);
	irr = vlapic_find_highest_irr(vlapic);
	irr >>= 4U;
	threshold = (irr > tpr) ? 0U : irr;

	exec_vmwrite32(VMX_TPR_THRESHOLD, threshold);
}

/**
 * @brief Find a deliverable virtual interrupts in IRR in LAPIC Page for vLAPIC.
 *
 * This function retrieves vector number for the highest-priority bit set in the IRR in LAPIC page,
 * when it is available, it returns this vector number if its priority (bits 7:4) is higher than
 * virtual PPR(bits 7:4) in LAPIC page.
 *
 * @param[in]    vlapic Pointer to target vLAPIC data structure
 * @param[inout] vecptr Pointer to vector buffer and will be filled with eligible vector if any.
 *
 * @return Returns true if There is deliverable vecto on the given vlapic,
 *         otherwise, returns false.
 *
 * @remark The vector does not automatically transition to the ISR as a
 *	       result of calling this function.
 *	       This function is only for case that APICv/VID is NOT supported.
 */
static bool vlapic_find_deliverable_intr(const struct acrn_vlapic *vlapic, uint32_t *vecptr)
{
	const struct lapic_regs *lapic = &(vlapic->apic_page);
	uint32_t vec;
	bool ret = false;

	vec = vlapic_find_highest_irr(vlapic);
	if (prio(vec) > prio(lapic->ppr.v)) {
		ret = true;
		if (vecptr != NULL) {
			*vecptr = vec;
		}
	}

	return ret;
}

/**
 * @brief Move a deliverable virtual interrupt from vIRR to vISR.
 *
 * This function clear corresponding bit in vIRR and set corresponding bit in vISR
 * for given interrupt vector number, caches the vector number and updates vPPR.
 *
 * @param[in] vlapic Pointer to target vLAPIC data structure
 * @param[in] vector Target virtual interrupt vector
 *
 * @return None.
 *
 * @pre vlapic != NULL
 *
 * @remark N/A
 */
static void vlapic_get_deliverable_intr(struct acrn_vlapic *vlapic, uint32_t vector)
{
	struct lapic_regs *lapic = &(vlapic->apic_page);
	struct lapic_reg *irrptr, *isrptr;
	uint32_t idx;

	/*
	 * clear the ready bit for vector being accepted in irr
	 * and set the vector as in service in isr.
	 */
	idx = vector >> 5U;

	irrptr = &lapic->irr[0];
	bitmap32_clear_lock((uint16_t)(vector & 0x1fU), &irrptr[idx].v);

	isrptr = &lapic->isr[0];
	bitmap32_set_nolock((uint16_t)(vector & 0x1fU), &isrptr[idx].v);

	vlapic->isrv = vector;

	/*
	 * Update the PPR
	 */
	vlapic_update_ppr(vlapic);
}

/**
 * @brief Accepts an interrupt with the specified vector and trigger level for vlapic
 *        supporting APICv basic features.
 *
 * This function handles interrupt acceptance for a vlapic that utilizes APICv basic
 * features only.
 *
 * It sets corresponding bit vIRR register for given vector number and if this set operation
 * is successful:
 *
 * - This function updates the Trigger Mode Register (TMR) to reflect the current trigger level
 *   of the interrupt.
 * - It makes a request to the virtual CPU (vCPU) to evaluate the interrupt on next VM entry
 *   by calling vcpu_make_request function.
 *
 * @param[inout] vlapic  Pointer to the vlapic structure.
 * @param[in] vector     Interrupt vector to be handled. Must be a valid interrupt number
 *                       greater than 15.
 * @param[in] level      Boolean value indicating trigger mode of the interrupt, true for
 *                       level-triggered interrupts false for edge-triggered interrupts.
 *
 * @return None
 *
 * @pre vector >= 16
 * @pre vlapic != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
static void apicv_basic_accept_intr(struct acrn_vlapic *vlapic, uint32_t vector, bool level)
{
	struct lapic_regs *lapic;
	struct lapic_reg *irrptr;
	uint32_t idx;

	lapic = &(vlapic->apic_page);
	idx = vector >> 5U;
	irrptr = &lapic->irr[0];

	/* If the interrupt is set, don't try to do it again */
	if (!bitmap32_test_and_set_lock((uint16_t)(vector & 0x1fU), &irrptr[idx].v)) {
		/* update TMR if interrupt trigger mode has changed */
		vlapic_set_tmr(vlapic, vector, level);
		vcpu_make_request(vlapic2vcpu(vlapic), ACRN_REQUEST_EVENT);
	}
}

/**
 * @brief Inject a pending interrupt into a virtual LAPIC with only basic APICv features support.
 *
 * This function injects a pending interrupt to specified vlapic if local interrupt of
 * corresponding vCPU is not blocked and there is no injected vector in VM-entry
 * interruption-information field:
 *
 * - Update virtual PPR(Processor Priority Register) in virtual LAPIC page.
 * - Find a deliverable virtual interrupts from virtual IRR(Interrupt Request Register) in virtual
 *   LAPIC page.
 * - Inject interrupt to virtual LAPIC following 26.8.3 VM-Entry Controls for Event Injection,
 *   Volume 3, Intel SDM 25462-087US.
 * - Update virtual IRR and virtual ISR(In Service Register) in virtual LAPIC page.
 *
 * Update TPR threshold according to 28.2.1.1 VM-Execution Control Fields, Volume 3,
 * Intel SDM 25462-087US.
 *
 * @param[in] vlapic             Pointer to the virtual LAPIC structure.
 * @param[in] guest_irq_enabled  Boolean value indicating whether local interrupt of a vCPU
 *                               is blocked.
 * @param[in] injected           Boolean value indicating whether a vector has been injected.
 *
 * @return None.
 *
 * @pre vlapic != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
static void apicv_basic_inject_intr(struct acrn_vlapic *vlapic,
		bool guest_irq_enabled, bool injected)
{
	uint32_t vector = 0U;

	if (guest_irq_enabled && (!injected)) {
		vlapic_update_ppr(vlapic);
		if (vlapic_find_deliverable_intr(vlapic, &vector)) {
			exec_vmwrite32(VMX_ENTRY_INT_INFO_FIELD, VMX_INT_INFO_VALID | vector);
			vlapic_get_deliverable_intr(vlapic, vector);
		}
	}

	vlapic_update_tpr_threshold(vlapic);
}

/**
 * @brief Check if there are any pending deliverable interrupts on given virtual CPU supporting
 *        basic APICv features only.
 *
 * - It updates the PPR based on the current values of the Task Priority Register (TPR)
 *   and the highest-priority vector in the In-Service Register (ISR) following section
 *   12.8.3.1 Task and Processor Priorities, Volume 3, Intel SDM 325462-087US.
 *
 * - Then it retrieves vector number for the highest-priority bit set in the IRR in LAPIC page,
 *   and validate if it is deliverable by comparing the priority of it with virtual PPR.
 *
 * - If above interrupt is deliverable, it makes a request to the virtual CPU (vCPU) to evaluate
 *   the interrupt on next VM entry by calling vcpu_make_request function.
 *
 * @param[in] vcpu pointer to the virtual CPU structure.
 *
 * @return Return true if there is pending deliverable interrupts on given vCPU,
 *         otherwise return false.
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
static bool apicv_basic_has_pending_delivery_intr(struct acrn_vcpu *vcpu)
{
	uint32_t vector;
	struct acrn_vlapic *vlapic = vcpu_vlapic(vcpu);

	vlapic_update_ppr(vlapic);

	/* check and raise request if we have a deliverable irq in LAPIC IRR */
	if (vlapic_find_deliverable_intr(vlapic, &vector)) {
		/* we have pending IRR */
		vcpu_make_request(vcpu, ACRN_REQUEST_EVENT);
	}

	return vcpu->arch.pending_req != 0UL;
}

/**
 * @brief Check if there are any pending interrupts on given virtual CPU, with basic APICv
 *        is supported.
 *
 * This function checks if there is any bits set in all vIRR registers in virtual-APIC page, which
 * means there are interrupt(s) pending on current vlapic and return true if yes.
 *
 * @param[in] vcpu pointer to the virtual CPU structure.
 *
 * @return Return true if there is pending interrupts on given vCPU, otherwise return false.
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
static bool apicv_basic_has_pending_intr(struct acrn_vcpu *vcpu)
{
	return vlapic_find_highest_irr(vcpu_vlapic(vcpu)) != 0UL;
}

/**
 * @brief Verify if a read operation is permitted for a given register offset in virtual-APIC
 *        with x2APIC mode supporting basic APICv features only.
 *
 *  Reading LAPIC registers except below ones are allowed:
 * - APIC_OFFSET_DFR
 * - APIC_OFFSET_ICR_HI
 *
 * @param[in] offset The offset of the LAPIC MSR in the APIC register page.
 *
 * @return true if the register with given offset can be read, false otherwise.
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark N/A
 */
static bool apicv_basic_x2apic_read_msr_may_valid(uint32_t offset)
{
	return (offset != APIC_OFFSET_DFR) && (offset != APIC_OFFSET_ICR_HI);
}

/**
 * @brief Verify if a write operation is permitted for a given register offset in virtual-APIC
 *        with x2APIC mode supporting basic APICv features only.
 *
 *  Writing LAPIC registers except below ones are allowed:
 * - APIC_OFFSET_DFR
 * - APIC_OFFSET_ICR_HI
 *
 * @param[in] offset The offset of the LAPIC MSR in the APIC register page.
 *
 * @return true if the register with given offset can be programmed, false otherwise.
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark N/A
 */
static bool apicv_basic_x2apic_write_msr_may_valid(uint32_t offset)
{
	return (offset != APIC_OFFSET_DFR) && (offset != APIC_OFFSET_ICR_HI);
}

/**
 * @brief Check if there are any pending deliverable interrupts on given virtual CPU.
 *
 * This API checks if there are any pending deliverable interrupts on the virtual LAPIC, by calling
 * APICv `has_pending_delivery_intr` callback function.
 *
 * - APIC Virtualization supporting basic APICv features only:  the callback function updates PPR,
 *   then it retrieves vector number for the highest-priority bit set in the IRR in LAPIC page,
 *   and validates if this interrupt is deliverable by comparing the priority of it with virtual PPR.
 *
 *   If above interrupt is deliverable, it makes a request to the virtual CPU (vCPU) to evaluate
 *   the interrupt on next VM entry by calling vcpu_make_request function.
 *
 *  - APIC Virtualization supporting advanced APICv features: it always returns false.
 *
 *  - APIC pass-through case: it always returns false.
 *
 * @param[in] vcpu pointer to the virtual CPU structure.
 *
 * @return Return true if there is pending deliverable interrupts on given vCPU,
 *         otherwise return false.
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
bool vlapic_has_pending_delivery_intr(struct acrn_vcpu *vcpu)
{
	struct acrn_vlapic *vlapic = vcpu_vlapic(vcpu);

	return vlapic->ops->has_pending_delivery_intr(vcpu);
}

/**
 * @brief APICv operation set implementing basic APIC virtualization features.
 *
 * This structure defines a set of function pointers corresponding to basic APICv operations
 * which supports basic APIC virtualization capabilities only as below to provide interrupt
 * handling for vCPUs:
 * - Virtualize APIC accesses
 * - Use TPR shadow
 * - Virtualize x2APIC mode
 *
 * Specifically, it initializes acrn_apicv_ops callback functions as below:
 * - accept_intr  This callback function handles interrupt acceptance of a vlapic that utilizes
 *                APICv basic APICv features.
 *
 *                It sets corresponding bit vIRR register for given vector number, and updates the
 *                Trigger Mode Register (TMR) to reflect current trigger level of the interrupt.
 *
 *                Then, this callback function makes a request to the virtual CPU (vCPU) to evaluate
 *                the interrupt on next VM entry by calling vcpu_make_request function.
 *
 * - inject_intr  This callback function injects a pending interrupt to specified vlapic if local
 *                interrupt of corresponding vCPU is not blocked and there is no injected vector in
 *                VM-entry interruption-information field:
 *
 *                It updates virtual PPR in virtual LAPIC page, and find a deliverable virtual
 *                interrupts from virtual IRR in virtual LAPIC page.
 *
 *                Then it injects interrupt to virtual LAPIC following section
 *                26.8.3 VM-Entry Controls for Event Injection, Volume 3, Intel SDM 25462-087US.
 *                and updates virtual IRR and virtual ISR in virtual LAPIC page.
 *
 *                Finally it updates TPR threshold according to 28.2.1.1 VM-Execution Control Fields,
 *                Volume 3, Intel SDM 25462-087US.
 *
 * - has_pending_delivery_intr   This callback function checks if there are any pending deliverable
 *                               interrupts on given virtual CPU.
 *
 *                               It updates the PPR based on the priority of Task Priority Register
 *                               (TPR) and the highest-priority vector in the In-Service Register
 *                               (ISR) following section 12.8.3.1 Task and Processor Priorities,
 *                               Volume 3, Intel SDM 325462-087US.
 *
 *                                Then it retrieves vector number for the highest-priority bit set
 *                                in the IRR in LAPIC page, and validate if it is deliverable by
 *                                comparing the priority of it with virtual PPR.
 *
 *                                If above interrupt is deliverable, it makes a request to the virtual
 *                                CPU to evaluate the interrupt on next VM entry by calling
 *                                vcpu_make_request function.
 *
 * - has_pending_intr  This callback function checks if there is any bits set in all vIRR registers in
 *                     virtual-APIC page, which means there are interrupt(s) pending on current vlapic
 *                     and return true if yes.
 *
 * - x2apic_read_msr_may_valid   This callback function verifies if a read operation is permitted
 *                               for a given register offset in virtual-APIC page. Read on LAPIC
 *                               registers except APIC_OFFSET_DFR and APIC_OFFSET_ICR_HI is emulated.
 *
 * - x2apic_write_msr_may_valid  This callback function verifies if a write operation is permitted
 *                               for a given register offset in virtual-APIC page. Write on LAPIC
 *                               regisers except APIC_OFFSET_DFR and APIC_OFFSET_ICR_HI is emulated.
 *
 * - reset  This callback function resets the APICv by calling apicv_reset function.
 */
static const struct acrn_apicv_ops apicv_basic_ops = {
	.accept_intr = apicv_basic_accept_intr,
	.inject_intr = apicv_basic_inject_intr,
	.has_pending_delivery_intr = apicv_basic_has_pending_delivery_intr,
	.has_pending_intr = apicv_basic_has_pending_intr,
	.x2apic_read_msr_may_valid = apicv_basic_x2apic_read_msr_may_valid,
	.x2apic_write_msr_may_valid = apicv_basic_x2apic_write_msr_may_valid,
	.reset = apicv_reset,
};
#endif /* #ifdef CONFIG_ENFORCE_APICv_BASIC_SUPPORT */

/**
 * @brief APICv operation set implementing advanced APIC virtualization features.
 *
 * This structure defines a set of function pointers corresponding to advanced APICv operations
 * which supports enhanced APIC virtualization capabilities as below to provide optimized interrupt
 * handling for vCPUs:
 * - Virtual-interrupt delivery
 * - Use TPR shadow
 * - Virtualize x2APIC mode
 * - APIC-register virtualization
 * - Process posted interrupts
 * - IPI virtualization
 *
 * Specifically, it initializes acrn_apicv_ops callback functions as below:
 * - accept_intr  This callback function handles interrupt acceptance of a vlapic that utilizes
 *                APICv advanced feature of Posted Interrupts (PI).
 *                It updates the Trigger Mode Register (TMR) in virtual-APIC page and request
 *                VMCS EOI Exit Bitmap update based on the trigger mode of given interrupt
 *                according to section 12.8.5 Signaling Interrupt Servicing Completion,
 *                26.6.8 Controls for APIC Virtualization, and 31.1.4 EOI Virtualization,
 *                Volume 3, Intel SDM 25462-087US.
 *
 *                It records the interrupt in PIR of PID and request ACRN_REQUEST_EVENT event
 *                for interrupt injection before next VM entry.
 *                If the target vCPU is not executing on current pCPU, it fires posted-interrupt
 *                notification IPI to the target pCPU to inject this interrupt directly to target
 *                vCPU without VM Exit according to section 31.6 POSTED-INTERRUPT PROCESSING,
 *                Volume 3, Intel SDM 25462-087US.
 *
 * - inject_intr  This callback function fires a self-IPI with posted interrupts notify vector(NV)
 *                if outstanding-notification(ON) bit in posted-interrupt descriptor(PID) is set.
 *
 *                Interrupts are disabled on the pCPU at this point. Upon the next VM Enter, the
 *                self-IPI is serviced by the logical processor to logical-OR of PIR into vIRR and
 *                clears PIR, also update RVI for interrupt evaluation and delivery according to
 *                section 31.6 POSTED-INTERRUPT PROCESSING, Volume 3, Intel SDM 25462-087US.
 *
 * - has_pending_intr  This callback function checks if there is any bit set in vIRR registers
 *                     in virtual-APIC page.
 *
 * - x2apic_read_msr_may_valid   This callback function verifies if a read operation is permitted
 *                               for a given register offset in virtual-APIC page. Only read on
 *                               APIC_OFFSET_TIMER_CCR is emulated in x2APIC mode.
 *
 * - x2apic_write_msr_may_valid  This callback function verifies if a write operation is permitted
 *                               for a given register offset in virtual-APIC page. Write on below
 *                               registers are emulated in x2APIC mode: APIC_OFFSET_DFR,
 *                               APIC_OFFSET_ICR_HI, APIC_OFFSET_EOI and APIC_OFFSET_SELF_IPI.
 *
 * - reset  This callback function resets the APICv by calling apicv_reset function.
 */
#ifdef CONFIG_ENFORCE_APICv_BASIC_SUPPORT
static __unused const struct acrn_apicv_ops apicv_advanced_ops = {
#else
static const struct acrn_apicv_ops apicv_advanced_ops = {
#endif
	.accept_intr = apicv_advanced_accept_intr,
	.inject_intr = apicv_advanced_inject_intr,
	.has_pending_intr = apicv_advanced_has_pending_intr,
	.x2apic_read_msr_may_valid  = apicv_advanced_x2apic_read_msr_may_valid,
	.x2apic_write_msr_may_valid  = apicv_advanced_x2apic_write_msr_may_valid,
	.reset = apicv_reset,
};

/**
 * @brief Initialize global APICv operation callback function set.
 *
 * This global variable pointers to an instance of `struct acrn_apicv_ops`, and it is initialized
 * with pointer to APICv operation set implementing advanced APIC virtualization features if it is
 * supporeted on current hardware platform. Otherwise, it pointers to APICv operation set
 * implementing basic APIC virtualization features.
 *
 * @return None.
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark N/A
 */
void vlapic_set_apicv_ops(void)
{
#ifdef CONFIG_ENFORCE_APICv_BASIC_SUPPORT
	apicv_ops = &apicv_basic_ops;
#else
	apicv_ops = &apicv_advanced_ops;
#endif
}

/**
 * @}
 */
