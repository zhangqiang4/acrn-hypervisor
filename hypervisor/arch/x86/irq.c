/*
 * Copyright (C) 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <asm/lib/bits.h>
#include <asm/lib/spinlock.h>
#include <asm/per_cpu.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/idt.h>
#include <asm/ioapic.h>
#include <asm/lapic.h>
#include <dump.h>
#include <logmsg.h>
#include <asm/vmx.h>
#include <irq.h>

/**
 * @addtogroup hwmgmt_irq hwmgmt.irq
 *
 * @{
 */

/**
 * @file
 *
 * @brief X86 specific interrupt resource management and interrupt handling
 */

/**
 * @brief Lock to protect the vector_to_irq mapping
 */
static spinlock_t x86_irq_spinlock = { .head = 0U, .tail = 0U, };

/**
 * @brief X86 private data for each irq_desc.
 *
 * It contains the vector number mapped to the IRQ number.
 */
static struct x86_irq_data irq_data[NR_IRQS];

/**
 * @brief Map x86 internal vector to common irq number
 */
static uint32_t vector_to_irq[NR_MAX_VECTOR + 1];

/**
 * @brief The function prototype for spurious interrupt handler.
 *
 * The function accepts the interrupt vector as the argument and returns nothing.
 */
typedef void (*spurious_handler_t)(uint32_t vector);

/**
 * @brief Stub for spurious IDT vector (unrequested) handlers
 */
spurious_handler_t spurious_handler;

/**
 * @brief Static mapping for hypervisor reserved vectors and irq numbers
 */
struct irq_static_mapping {
	uint32_t irq;		/**< The irq number in a map */
	uint32_t vector;	/**< The vector number in a map */
};

/**
 * @brief Array of static IRQ mappings.
 */
static struct irq_static_mapping irq_static_mappings[NR_STATIC_MAPPINGS] = {
	{ .irq = TIMER_IRQ,     .vector = TIMER_VECTOR },
	{ .irq = THERMAL_IRQ,   .vector = THERMAL_VECTOR },
	{ .irq = CMCI_IRQ,      .vector = CMCI_VECTOR },
	{ .irq = NOTIFY_VCPU_IRQ, .vector = NOTIFY_VCPU_VECTOR },
	{ .irq = PMI_IRQ,       .vector = PMI_VECTOR }
};

/**
 * @brief Allocate a vector and bind it to irq
 *
 * If the irq has already been bound to a vector, just return the vector.
 * else find a free vector from VECTOR_DYNAMIC_START to VECTOR_DYNAMIC_END and bind it to the irq.
 * if the input irq is >= NR_IRQS, return VECTOR_INVALID.
 *
 * @param[in]	irq	The irq number to bind
 *
 * @return valid vector number on success, VECTOR_INVALID on failure
 * @retval [0-255] valid vector number on success
 * @retval VECTOR_INVALID invalid vector on failure.
 */
uint32_t alloc_irq_vector(uint32_t irq)
{
	struct x86_irq_data *irqd;
	uint64_t rflags;
	uint32_t vr = VECTOR_INVALID;
	uint32_t ret = VECTOR_INVALID;

	if (irq < NR_IRQS) {
		irqd = &irq_data[irq];
		spinlock_irqsave_obtain(&x86_irq_spinlock, &rflags);

		if (irqd->vector != VECTOR_INVALID) {
			if (vector_to_irq[irqd->vector] == irq) {
				/* statically bound */
				vr = irqd->vector;
			} else {
				pr_err("[%s] irq[%u]:vector[%u] mismatch",
					__func__, irq, irqd->vector);
			}
		} else {
			/* allocate a vector between:
			 *   VECTOR_DYNAMIC_START ~ VECTOR_DYNAMIC_END
			 */
			for (vr = VECTOR_DYNAMIC_START;
				vr <= VECTOR_DYNAMIC_END; vr++) {
				if (vector_to_irq[vr] == IRQ_INVALID) {
					irqd->vector = vr;
					vector_to_irq[vr] = irq;
					break;
				}
			}
			vr = (vr > VECTOR_DYNAMIC_END) ? VECTOR_INVALID : vr;
		}
		spinlock_irqrestore_release(&x86_irq_spinlock, rflags);
		ret = vr;
	} else {
		pr_err("invalid irq[%u] to alloc vector", irq);
	}

	return ret;
}

/**
 * @brief X86 implementation of irq request.
 *
 * Allocate a vector for the given IRQ number and bind them.
 *
 * @param[in] irq The IRQ number to request.
 *
 * @return A boolean value indicating if the request succeeded or not
 * @retval true IRQ Request succeeded
 * @retval false IRQ Request failed
 */
bool request_irq_arch(uint32_t irq)
{
	return (alloc_irq_vector(irq) != VECTOR_INVALID);
}

/**
 * @brief Free the vector allocated via alloc_irq_vector().
 *
 * @param[in] irq The IRQ number to free.
 *
 * @return None
 */
static void free_irq_vector(uint32_t irq)
{
	struct x86_irq_data *irqd;
	uint32_t vr;
	uint64_t rflags;

	if (irq < NR_IRQS) {
		irqd = &irq_data[irq];
		spinlock_irqsave_obtain(&x86_irq_spinlock, &rflags);

		if (irqd->vector < VECTOR_FIXED_START) {
			/* do nothing for LEGACY_IRQ and static allocated ones */
			vr = irqd->vector;
			irqd->vector = VECTOR_INVALID;

			if ((vr <= NR_MAX_VECTOR) && (vector_to_irq[vr] == irq)) {
				vector_to_irq[vr] = IRQ_INVALID;
			}
		}
		spinlock_irqrestore_release(&x86_irq_spinlock, rflags);
	}
}

/**
 * @brief X86 implementation to free IRQ number.
 *
 * Free the internal vector for the given IRQ number.
 *
 * @param[in] irq The IRQ number to free.
 *
 * @return None
 */
void free_irq_arch(uint32_t irq)
{
	free_irq_vector(irq);
}

/**
 * @brief Get vector number of an interrupt from irq number
 *
 * @param[in]	irq	The irq_num to convert
 *
 * @return Vector number corresponding to given irq number
 */
uint32_t irq_to_vector(uint32_t irq)
{
	uint64_t rflags;
	uint32_t ret = VECTOR_INVALID;

	if (irq < NR_IRQS) {
		spinlock_irqsave_obtain(&x86_irq_spinlock, &rflags);
		ret = irq_data[irq].vector;
		spinlock_irqrestore_release(&x86_irq_spinlock, rflags);
	}

	return ret;
}

/**
 * @brief Handle spurious interrupts
 *
 * Spurious interrupts are those triggered from unused vectors. This means a bug in hardware or
 * the irq framework. To keep system working, send EOI to LAPIC to allow further interrupts.
 * Account the spurious interrupts and prints a warning message. If more action is needed, other
 * modules could register the spurious_handler to handle it.
 *
 * @param[in] vector The vector of the spurious interrupt.
 *
 * @return None
 */
static void handle_spurious_interrupt(uint32_t vector)
{
	send_lapic_eoi();

	get_cpu_var(spurious)++;

	pr_warn("Spurious vector: 0x%x.", vector);

	if (spurious_handler != NULL) {
		spurious_handler(vector);
	}
}

/**
 * @brief Check whether an IRQ descriptor needs to be masked on IOAPIC's side
 *
 * Level triggered GSI should be masked before handling the irq action.
 *
 * @param[in] desc The IRQ descriptor to check
 *
 * @return A boolean value indicating the check result
 *
 * @retval true This IRQ needs to be masked on IOAPIC
 * @retval false This IRQ doesn't need to be masked on IOAPIC
 */
static inline bool irq_need_mask(const struct irq_desc *desc)
{
	return (((desc->flags & IRQF_LEVEL) != 0U)
		&& is_ioapic_irq(desc->irq));
}

/**
 * @brief Check whether an IRQ descriptor needs to be unmasked on IOAPIC's side
 *
 * Level triggered GSI for non-passthrough device should be unmasked before exit interrupt handler.
 *
 * @param[in] desc The IRQ descriptor to check
 *
 * @return A boolean value indicating the check result
 *
 * @retval true This IRQ needs to be unmasked on IOAPIC
 * @retval false This IRQ doesn't need to be unmasked on IOAPIC
 */
static inline bool irq_need_unmask(const struct irq_desc *desc)
{
	/* level triggered gsi for non-ptdev should be unmasked */
	return (((desc->flags & IRQF_LEVEL) != 0U)
		&& ((desc->flags & IRQF_PT) == 0U)
		&& is_ioapic_irq(desc->irq));
}

/**
 * @brief X86 hook before invoking requested irq action handler
 *
 * Mask IOAPIC pin if it's configured as level triggered.
 * Send EOI to LAPIC to allow to queue in new interrupts. Although as this time the local
 * interrupt is masked by clearing RFLAGS.IF after entered interrupt gate, IOAPICs can queue in
 * external interrupts in LAPIC as pending when executing irq action handler.
 *
 * @param[in] desc The irq_desc to handle
 *
 * @return None
 */
void pre_irq_arch(const struct irq_desc *desc)
{
	if (irq_need_mask(desc))  {
		ioapic_gsi_mask_irq(desc->irq);
	}

	/* Send EOI to LAPIC/IOAPIC IRR */
	send_lapic_eoi();
}

/**
 * @brief X86 hook after executing requested irq action handler
 *
 * Unmask IOAPIC pin if it's configured as level triggered.
 *
 * @param[in] desc The irq_desc to handle
 *
 * @return None
 */
void post_irq_arch(const struct irq_desc *desc)
{
	if (irq_need_unmask(desc)) {
		ioapic_gsi_unmask_irq(desc->irq);
	}
}

/**
 * @brief Dispatch interrupt
 *
 * To dispatch an interrupt, an action callback will be called if registered.
 * If no irq is registered for the vector, it is handled as a spurious interrupt by sending EOI
 * and calling the registered spurious interrupt handler if it is available.
 * Otherwise, call the generic IRQ handling routine to invoke regsitered irq actions and handle
 * pending softirqs.
 *
 * @param[in] ctx Pointer to interrupt exception context
 *
 * @return None
 */
void dispatch_interrupt(const struct intr_excp_ctx *ctx)
{
	uint32_t vr = ctx->vector;
	uint32_t irq = vector_to_irq[vr];
	struct x86_irq_data *irqd;

	/* The value from vector_to_irq[] must be:
	 * IRQ_INVALID, which means the vector is not allocated;
	 * or
	 * < NR_IRQS, which is the irq number it bound with;
	 * Any other value means there is something wrong.
	 */
	if (irq < NR_IRQS) {
		irqd = &irq_data[irq];

		if (vr == irqd->vector) {
#ifdef PROFILING_ON
			/* Saves ctx info into irq_desc */
			irqd->ctx_rip = ctx->rip;
			irqd->ctx_rflags = ctx->rflags;
			irqd->ctx_cs = ctx->cs;
#endif
			/* Call the generic IRQ handling routine */
			do_irq(irq);
		}
	} else {
		handle_spurious_interrupt(vr);
	}
}

/**
 * @brief X86 implementation of irq_desc setup
 *
 * Setup static mapping between IRQ number and vectors for hypervisor owned interrupts. These IRQ
 * numbers are reserved to prevent dynamic use.
 * For each VM, a vector is reserved for Post Interrupt because there can at most only one vCPU
 * from a VM to be assigned to a pCPU. Other hypervisor owned interrupts and their vectors are
 * statically defined.
 *
 * @param[inout] descs The array of irq_desc to be initialized, must have NR_IRQS entries
 *
 * @return None
 */
void init_irq_descs_arch(struct irq_desc *descs)
{
	uint32_t i;

	/*
	 * Fill in #CONFIG_MAX_VM_NUM posted interrupt specific irq and vector pairs
	 * at runtime
	 */
	for (i = 0U; i < CONFIG_MAX_VM_NUM; i++) {
		uint32_t idx = i + NR_STATIC_MAPPINGS_1;

		ASSERT(irq_static_mappings[idx].irq == 0U, "");
		ASSERT(irq_static_mappings[idx].vector == 0U, "");

		irq_static_mappings[idx].irq = POSTED_INTR_IRQ + i;
		irq_static_mappings[idx].vector = POSTED_INTR_VECTOR + i;
	}

	for (i = 0U; i < NR_IRQS; i++) {
		irq_data[i].vector = VECTOR_INVALID;
		descs[i].arch_data = &irq_data[i];
	}

	for (i = 0U; i <= NR_MAX_VECTOR; i++) {
		vector_to_irq[i] = IRQ_INVALID;
	}

	/* init fixed mapping for specific irq and vector */
	for (i = 0U; i < NR_STATIC_MAPPINGS; i++) {
		uint32_t irq = irq_static_mappings[i].irq;
		uint32_t vr = irq_static_mappings[i].vector;

		irq_data[irq].vector = vr;
		vector_to_irq[vr] = irq;

		reserve_irq_num(irq);
	}
}

/**
 * @brief x86 implementation of IRQ setup
 *
 * Initialize IOAPIC pins and allocate vectors for legacy IRQs
 * must be called after IRQ setup
 *
 * @return None
 */
void setup_irqs_arch(void)
{
	ioapic_setup_irqs();
}

/**
 * @brief Disable interrupts of the Primary and Secondary PICs
 *
 * @return None
 */
static void disable_pic_irqs(void)
{
	pio_write8(0xffU, 0xA1U);
	pio_write8(0xffU, 0x21U);
}

/**
 * @brief Fixup early defined IDT descriptors
 *
 * The function iterates all the 128-bit IDT entries and put the handler offset field which was
 * temporarily stored at high 64 bit to three corresponding bit fields.
 *
 * @param[in] idtd Base address of the IDT descriptors
 *
 * @return None
 */
static inline void fixup_idt(const struct host_idt_descriptor *idtd)
{
	uint32_t i;
	struct idt_64_descriptor *idt_desc = idtd->idt->host_idt_descriptors;
	uint32_t entry_hi_32, entry_lo_32;

	for (i = 0U; i < HOST_IDT_ENTRIES; i++) {
		entry_lo_32 = idt_desc[i].offset_63_32;
		entry_hi_32 = idt_desc[i].rsvd;
		idt_desc[i].rsvd = 0U;
		idt_desc[i].offset_63_32 = entry_hi_32;
		idt_desc[i].high32.bits.offset_31_16 = (uint16_t)(entry_lo_32 >> 16U);
		idt_desc[i].low32.bits.offset_15_0 = (uint16_t)entry_lo_32;
	}
}

/**
 * @brief Load IDT descriptor
 *
 * Load IDT descriptor to IDTR with lidtq instruction
 *
 * @param[in] idtd Base address of the IDT descriptor.
 *
 * @return None
 */
static inline void set_idt(struct host_idt_descriptor *idtd)
{
	asm volatile ("   lidtq %[idtd]\n" :	/* no output parameters */
		      :		/* input parameters */
		      [idtd] "m"(*idtd));
}

/**
 * @brief x86 specific exception and interrupt setup
 *
 * This function is called from every logical processor.
 * If the specified physical cpu is the BSP,
 *	Fix up the 64-bit IDT descriptor.
 *	Disable PIC.
 * For any cpu, this function loads the 64-bit IDT descriptor and initializes local APIC.
 *
 * @param[in] pcpu_id Physical cpu id.
 *
 * @return None
 */
void init_interrupt_arch(uint16_t pcpu_id)
{
	struct host_idt_descriptor *idtd = &HOST_IDTR;

	if (pcpu_id == BSP_CPU_ID) {
		fixup_idt(idtd);
	}
	set_idt(idtd);
	init_lapic(pcpu_id);

	if (pcpu_id == BSP_CPU_ID) {
		/* we use ioapic only, disable legacy PIC */
		disable_pic_irqs();
	}
}

/**
 * @}
 */
