/*
 * Copyright (C) 2021-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>
#include <asm/lib/bits.h>
#include <irq.h>
#include <common/softirq.h>
#include <asm/irq.h>
#include <asm/per_cpu.h>

/**
 * @defgroup hwmgmt_irq hwmgmt.irq
 * @ingroup hwmgmt
 * @brief The definition and implementation of Interrupt related stuff.
 *
 * @{
 */

/**
 * @file common/irq.c
 *
 * @brief Implementation for common IRQ handling
 */

/**
 * @brief Lock to protect irq_alloc_bitmap and irq_rsvd_bitmap
 */
static spinlock_t irq_alloc_spinlock = { .head = 0U, .tail = 0U, };

/**
 * @brief A bitmap to track allocated IRQ numbers, including reserved.
 */
uint64_t irq_alloc_bitmap[IRQ_ALLOC_BITMAP_SIZE];

/**
 * @brief A bitmap to track reserved IRQ numbers.
 */
static uint64_t irq_rsvd_bitmap[IRQ_ALLOC_BITMAP_SIZE];

/**
 * @brief IRQ descriptor structures for all possible IRQ numbers.
 */
struct irq_desc irq_desc_array[NR_IRQS];

/**
 * @brief Allocate an IRQ number and reserve it if requested
 *
 * Allocate an IRQ if req_irq is IRQ_INVALID, or try the requested IRQ.
 * If req_irq is not a valid irq in [0, NR_IRQS - 1] or IRQ_INVALID, return IRQ_INVALID.
 * If req_irq is IRQ_INVALID, allocate one from irq_alloc_bitmap. If no free irq numbers, return
 *   IRQ_INVALID.
 * Otherwise mark the requested irq allocated.
 * If reserve is set, the allocated irq will be marked reserved in irq_alloc_bitmap.
 *
 * @param[in] req_irq The requested IRQ number, IRQ_INVALID means dynamic allocation.
 * @param[in] reserve Whether reserve the allocated IRQ number or not.
 *
 * @return IRQ number on success, IRQ_INVALID on failure
 *
 * @retval [0, NR_IRQS-1] The allocated IRQ number
 * @retval IRQ_INVALID Allocation failed
 */
static uint32_t alloc_irq_num(uint32_t req_irq, bool reserve)
{
	uint32_t irq = req_irq;
	uint64_t rflags;
	uint32_t ret;

	if ((irq >= NR_IRQS) && (irq != IRQ_INVALID)) {
		pr_err("[%s] invalid req_irq %u", __func__, req_irq);
	        ret = IRQ_INVALID;
	} else {
		spinlock_irqsave_obtain(&irq_alloc_spinlock, &rflags);
		if (irq == IRQ_INVALID) {
			/* if no valid irq num given, find a free one */
			irq = (uint32_t)ffz64_ex(irq_alloc_bitmap, NR_IRQS);
		}

		if (irq >= NR_IRQS) {
			irq = IRQ_INVALID;
		} else {
			bitmap_set_nolock((uint16_t)(irq & 0x3FU),
					irq_alloc_bitmap + (irq >> 6U));
			if (reserve) {
				bitmap_set_nolock((uint16_t)(irq & 0x3FU),
						irq_rsvd_bitmap + (irq >> 6U));
			}
		}
		spinlock_irqrestore_release(&irq_alloc_spinlock, rflags);
		ret = irq;
	}
	return ret;
}

/**
 * @brief Reserve an IRQ number
 *
 * Allocate and reserve an IRQ number that will not be available for dynamic IRQ allocations.
 * This is normally used by the hypervisor for static IRQ mappings and/or
 * arch specific, e.g. IOAPIC, interrupts during initialization.
 *
 * @param[in] irq Allocate an free IRQ if irq is IRQ_INVALID, or else try to use it
 *
 * @return IRQ number on success, IRQ_INVALID on failure
 *
 * @retval [0, NR_IRQS-1] The allocated IRQ number
 * @retval IRQ_INVALID Allocation failed
 */
uint32_t reserve_irq_num(uint32_t irq)
{
	return alloc_irq_num(irq, true);
}

/**
 * @brief Free a previously allocated dynamic IRQ number
 *
 * Free irq number allocated via alloc_irq_num().
 * If the irq number is reserved, this function does nothing.
 * If the irq >= NR_IRQS, this function does nothing.
 *
 * @param[in] irq A previously allocated IRQ number
 *
 * @return None
 */
static void free_irq_num(uint32_t irq)
{
	uint64_t rflags;

	if (irq < NR_IRQS) {
		spinlock_irqsave_obtain(&irq_alloc_spinlock, &rflags);

		if (bitmap_test((uint16_t)(irq & 0x3FU),
			irq_rsvd_bitmap + (irq >> 6U)) == false) {
			bitmap_clear_nolock((uint16_t)(irq & 0x3FU),
					irq_alloc_bitmap + (irq >> 6U));
		}
		spinlock_irqrestore_release(&irq_alloc_spinlock, rflags);
	}
}

/**
 * @brief Free an irq descriptor
 *
 * Unregister the irq action and free irq number and corresponding arch resources (in x86, it's
 * the vector for the irq).
 *
 * @param[in] irq Number of a previously requested IRQ descriptor
 *
 * @return None
 *
 * @pre irq_rsvd_bitmap[irq/64] & (1 << (irq & 0x3F)) == 0
 */
void free_irq(uint32_t irq)
{
	uint64_t rflags;
	struct irq_desc *desc;

	if (irq < NR_IRQS) {
		desc = &irq_desc_array[irq];

		spinlock_irqsave_obtain(&desc->lock, &rflags);
		desc->action = NULL;
		desc->priv_data = NULL;
		desc->flags = IRQF_NONE;
		spinlock_irqrestore_release(&desc->lock, rflags);

		free_irq_arch(irq);
		free_irq_num(irq);
	}
}

/**
 * @brief Request an irq descriptor and setup irq action handler
 *
 * Request interrupt number if not specified, and register irq action for the
 * specified/allocated irq.
 *
 * There are four cases as to irq/vector allocation:
 *  case 1: req_irq == IRQ_INVALID
 *      caller did not know which irq to use, and want system to
 *	allocate available irq for it. These irq are in range:
 *	nr_gsi ~ NR_IRQS
 *	an irq will be allocated and a vector will be assigned to this
 *	irq automatically.
 *  case 2: req_irq >= NR_LAGACY_IRQ && req_irq < nr_gsi
 *	caller want to add device ISR handler into ioapic pins.
 *	a vector will automatically assigned.
 *  case 3: req_irq >=0 && req_irq < NR_LEGACY_IRQ
 *	caller want to add device ISR handler into ioapic pins, which
 *	is a legacy irq, vector already reserved.
 *	Nothing to do in this case.
 *  case 4: irq with special type (not from IOAPIC/MSI)
 *	These irq value are pre-defined for Timer, IPI, Spurious etc,
 *	which is listed in irq_static_mappings[].
 *	Nothing to do in this case.
 *
 * @param[in]	req_irq	IRQ number to request, if IRQ_INVALID, a free irq
 *		number will be allocated
 * @param[in]	action_fn	Function to be called when the IRQ occurs
 * @param[in]	priv_data	Private data for action function.
 * @param[in]	flags	Interrupt type flags (IRQF_*)
 *
 * @return The requested IRQ number or error code
 * @retval >=0 on success
 * @retval IRQ_INVALID on failure
 */
int32_t request_irq(uint32_t req_irq, irq_action_t action_fn, void *priv_data,
			uint32_t flags)
{
	struct irq_desc *desc;
	uint32_t irq;
	uint64_t rflags;
	int32_t ret;

	irq = alloc_irq_num(req_irq, false);
	if (irq == IRQ_INVALID) {
		pr_err("[%s] invalid irq num", __func__);
		ret = -EINVAL;
	} else {
		if (!request_irq_arch(irq)) {
			pr_err("[%s] failed to alloc vector for irq %u",
				__func__, irq);
			free_irq_num(irq);
			ret = -EINVAL;
		} else {
			desc = &irq_desc_array[irq];
			if (desc->action == NULL) {
				spinlock_irqsave_obtain(&desc->lock, &rflags);
				desc->flags = flags;
				desc->priv_data = priv_data;
				desc->action = action_fn;
				spinlock_irqrestore_release(&desc->lock, rflags);
				ret = (int32_t)irq;
			} else {
				ret = -EBUSY;
				pr_err("%s: request irq(%u) failed, already requested",
				       __func__, irq);
			}
		}
	}

	return ret;
}

/**
 * @brief Set interrupt trigger mode
 *
 * Set the irq trigger mode: edge-triggered or level-triggered
 *
 * @param[in] irq irq_num of interrupt to be set
 * @param[in] is_level_triggered Trigger mode to set
 *
 * @return None
 */
void set_irq_trigger_mode(uint32_t irq, bool is_level_triggered)
{
	uint64_t rflags;
	struct irq_desc *desc;

	if (irq < NR_IRQS) {
		desc = &irq_desc_array[irq];
		spinlock_irqsave_obtain(&desc->lock, &rflags);
		if (is_level_triggered) {
			desc->flags |= IRQF_LEVEL;
		} else {
			desc->flags &= ~IRQF_LEVEL;
		}
		spinlock_irqrestore_release(&desc->lock, rflags);
	}
}

/**
 * @brief Handle one interrupt - Internal
 *
 * Invoke irq action handler for an interrupt, preceded by pre_irq_arch and followed by
 * post_irq_arch.
 *
 * @param[in] desc IRQ descriptor of the interrupt
 *
 * @return None
 */
static inline void handle_irq(const struct irq_desc *desc)
{
	irq_action_t action = desc->action;

	pre_irq_arch(desc);

	if (action != NULL) {
		action(desc->irq, desc->priv_data);
	}

	post_irq_arch(desc);
}

/**
 * @brief Process an IRQ
 *
 * To process an IRQ, an action callback will be called if registered.
 * At the end of interrupt handler, pending softirqs are handled.
 *
 * @param[in] irq irq number to be processed
 *
 * @return None
 */
void do_irq(const uint32_t irq)
{
	struct irq_desc *desc;

	if (irq < NR_IRQS) {
		desc = &irq_desc_array[irq];
		per_cpu(irq_count, get_pcpu_id())[irq]++;

		/* XXX irq_alloc_bitmap is used lockless here */
		if (bitmap_test((uint16_t)(irq & 0x3FU), irq_alloc_bitmap + (irq >> 6U))) {
			handle_irq(desc);
		}
	}

	do_softirq();
}

/**
 * @brief Initialize irq descriptors
 *
 * Initialize each supported IRQ descriptors. Some IRQ descriptors are statically reserved on
 * some architectures.
 *
 * @return None
 */
static void init_irq_descs(void)
{
	uint32_t i;

	for (i = 0U; i < NR_IRQS; i++) {
		struct irq_desc *desc = &irq_desc_array[i];
		desc->irq = i;
		spinlock_init(&desc->lock);
	}

	init_irq_descs_arch(irq_desc_array);
}

/**
 * @brief Initialize interrupt functionality for a processor
 *
 * Invoke architecture API to setup interrupt controllers (LAPIC and IOAPIC in x86). The BSP also
 * needs to initialize shared data structures (irq descriptors, soft interrupts).
 * And last enable local IRQ for this processor.
 *
 * @param[in] pcpu_id Physical processor id of the processor to enable interrupt functionality.
 *
 * @return None
 */
void init_interrupt(uint16_t pcpu_id)
{
	init_interrupt_arch(pcpu_id);

	if (pcpu_id == BSP_CPU_ID) {
		init_irq_descs();
		setup_irqs_arch();
		init_softirq();
	}

	CPU_IRQ_ENABLE();
}

/**
 * @}
 */
