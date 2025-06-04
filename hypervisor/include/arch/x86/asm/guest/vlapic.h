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

#ifndef VLAPIC_H
#define VLAPIC_H

#include <acrn_common.h>
#include <asm/page.h>
#include <timer.h>
#include <asm/apicreg.h>

/**
 * @addtogroup vp-base_vlapic
 *
 * @{
 */

/**
 * @file
 * @brief This file contains the external data structures and APIs for the vp-base.vlapic module
 *
 */

#define VLAPIC_MAXLVT_INDEX	APIC_LVT_CMCI /**< Maximum index of Local Vector Table (LVT) registers. */

/**
 * @brief Data structure to define the virtual Local APIC timer.
 *
 * @consistency N/A
 * @alignment N/A
 *
 * @remark N/A
 */
struct vlapic_timer {
	struct hv_timer timer;  /**< hv_timer instance to emulate timer of vlapic */
	uint32_t mode;  /**< timer mode,  */
	uint32_t tmicr;  /**< value of Initial Count register (ICR) for timer */
	uint32_t divisor_shift; /**< shift of Divide Configuration Register(DCR) value */
};

/**
 * @brief Data structure to define the vlapic used in ACRN.
 *
 * @consistency N/A
 * @alignment 4096
 *
 * @remark N/A
 */
struct acrn_vlapic {
	/**
	 * @brief This field is used to cache the virtual LAPIC registers.
	 *
	 * Please keep 'apic_page' as the first field in current structure,
	 * as below alignment restrictions are mandatory to support APICv features:
	 * - 'apic_page' MUST be 4KB aligned.
	 *
	 * IRR, TMR and PIR could be accessed by other vCPUs when deliver
	 * an interrupt to vLAPIC.
	 */
	struct lapic_regs	apic_page;

	uint32_t		vapic_id; /**< Virtual Local APIC ID */
	uint32_t		esr_pending; /**< Cache ESR(Error Status Register value */
	/**
	 * @brief  Injection status of ESR interrupt
	 *
	 * 0 means ESR interrupt injection is in-progress otherwise it is 1
	 */
	int32_t			esr_firing;

	struct vlapic_timer	vtimer; /**< Emulated local APIC timer */

	uint32_t	isrv;  /**< Vector number for the highest priority bit that is set in the ISR */

	uint64_t	msr_apicbase; /**< Cache virtual IA32_APIC_BASE MSR which associates with the virtual LAPIC. */

	const struct acrn_apicv_ops *ops; /**< Pointer to APICv callback instance of struct acrn_apicv_ops */

	uint32_t	svr_last; /**< Store last value of SVR */
	uint32_t	lvt_last[VLAPIC_MAXLVT_INDEX + 1]; /**< Store last values of LVT registers */
} __aligned(PAGE_SIZE);

struct acrn_vm;
struct acrn_vcpu;

/**
 * @brief Data structure to define the APICv operation callback function types.
 *
 * @consistency N/A
 * @alignment N/A
 *
 * @remark N/A
 */
struct acrn_apicv_ops {
	/**
	 * @brief Accept interrupt callback function type.
	 */
	void (*accept_intr)(struct acrn_vlapic *vlapic, uint32_t vector, bool level);

	/**
	 * @brief Inject interrupt callback function type.
	 */
	void (*inject_intr)(struct acrn_vlapic *vlapic);

	/**
	 * @brief Check pending interrupt callback function type.
	 */
	bool (*has_pending_intr)(struct acrn_vcpu *vcpu);

	/**
	 * @brief Check x2APIC MSR readable callback function type.
	 */
	bool (*x2apic_read_msr_may_valid)(uint32_t offset);

	/**
	 * @brief Check x2APIC MSR writable callback function type.
	 */
	bool (*x2apic_write_msr_may_valid)(uint32_t offset);
};

void vlapic_inject_intr(struct acrn_vlapic *vlapic);
bool vlapic_has_pending_intr(struct acrn_vcpu *vcpu);
uint64_t vlapic_get_tsc_deadline_msr(const struct acrn_vlapic *vlapic);
void vlapic_set_tsc_deadline_msr(struct acrn_vlapic *vlapic, uint64_t val_arg);
uint64_t vlapic_get_apicbase(const struct acrn_vlapic *vlapic);
int32_t vlapic_set_apicbase(struct acrn_vlapic *vlapic, uint64_t new);
int32_t vlapic_x2apic_read(struct acrn_vcpu *vcpu, uint32_t msr, uint64_t *val);
int32_t vlapic_x2apic_write(struct acrn_vcpu *vcpu, uint32_t msr, uint64_t val);

/*
 * Signals to the LAPIC that an interrupt at 'vector' needs to be generated
 * to the 'cpu', the state is recorded in IRR.
 *  @pre vcpu != NULL
 *  @pre vector <= 255U
 */
void vlapic_set_intr(struct acrn_vcpu *vcpu, uint32_t vector, bool level);

#define	LAPIC_TRIG_LEVEL	true /**< Trigger mode indicating level-triggered interrupts. */
#define	LAPIC_TRIG_EDGE		false /**< Trigger mode indicating edge-triggered interrupts. */

/**
 * @brief Extract the priority level from a given vector value.
 *
 * This function calculates the priority level by shifting the input value to the right by 4 bits.
 *
 * @param[in] x Input value from which the priority level is to be extracted.
 *
 * @return The priority level as a 4-bit integer, ranging from 0 (lowest) to 15 (highest).
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark N/A
 */
static inline uint32_t prio(uint32_t x)
{
	return (x >> 4U);
}

int32_t vlapic_set_local_intr(struct acrn_vcpu *vcpu, uint32_t lvt_index);


int32_t vlapic_inject_msi(struct acrn_vm *vm, uint64_t addr, uint64_t data);


int32_t vlapic_receive_intr(struct acrn_vm *vm, bool level, uint32_t dest,
		bool phys, uint32_t delmode, uint32_t vec, bool rh);

/**
 * @brief Retrieve the virtual APIC ID of a given vlapic.
 *
 * This function returns the cached virtual Local APIC ID (`vapic_id`) for the specified virtual
 * LAPIC (vlapic).
 *
 * @param[in] vlapic Pointer to the virtual LAPIC structure.
 *
 * @return The virtual APIC ID of the specified vlapic.
 *
 * @pre vlapic != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
static inline uint32_t vlapic_get_apicid(const struct acrn_vlapic *vlapic)
{
	return vlapic->vapic_id;
}

void vlapic_create(struct acrn_vcpu *vcpu, uint16_t pcpu_id);
void vlapic_free(struct acrn_vcpu *vcpu);
void vlapic_reset(struct acrn_vcpu *vcpu);
uint64_t vlapic_apicv_get_apic_access_addr(void);
uint64_t vlapic_apicv_get_apic_page_addr(struct acrn_vlapic *vlapic);
int32_t apic_write_vmexit_handler(struct acrn_vcpu *vcpu);
int32_t veoi_vmexit_handler(struct acrn_vcpu *vcpu);
int32_t tpr_below_threshold_vmexit_handler(struct acrn_vcpu *vcpu);
uint64_t vlapic_calc_dest_noshort(struct acrn_vm *vm, bool is_broadcast,
		uint32_t dest, bool phys, bool lowprio);
/**
 * @}
 */
#endif /* VLAPIC_H */
