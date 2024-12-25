/*
 * Copyright (C) 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ARCH_X86_IRQ_H
#define ARCH_X86_IRQ_H

#include <types.h>
#include <public/acrn_common.h>

/**
 * @file arch/x86/asm/irq.h
 *
 * @brief public APIs for x86 IRQ handling
 */

#define DBG_LEVEL_PTIRQ		6U
#define DBG_LEVEL_IRQ		6U

#define NR_MAX_VECTOR		0xFFU
#define VECTOR_INVALID		(NR_MAX_VECTOR + 1U)

/* # of NR_STATIC_MAPPINGS_1 entries for timer, vcpu notify, and PMI */
#define NR_STATIC_MAPPINGS_1	5U

/*
 * The static IRQ/Vector mapping table in irq.c consists of the following entries:
 * # of NR_STATIC_MAPPINGS_1 entries for timer, vcpu notify, and PMI
 *
 * # of CONFIG_MAX_VM_NUM entries for posted interrupt notification, platform
 * specific but known at build time:
 * Allocate unique Activation Notification Vectors (ANV) for each vCPU that belongs
 * to the same pCPU, the ANVs need only be unique within each pCPU, not across all
 * vCPUs. The max numbers of vCPUs may be running on top of a pCPU is CONFIG_MAX_VM_NUM,
 * since ACRN does not support 2 vCPUs of same VM running on top of same pCPU.
 * This reduces # of pre-allocated ANVs for posted interrupts to CONFIG_MAX_VM_NUM,
 * and enables ACRN to avoid switching between active and wake-up vector values
 * in the posted interrupt descriptor on vCPU scheduling state changes.
 */
#define NR_STATIC_MAPPINGS	(NR_STATIC_MAPPINGS_1 + CONFIG_MAX_VM_NUM)

#define HYPERVISOR_CALLBACK_HSM_VECTOR	0xF3U

/* vectors range for dynamic allocation, usually for devices */
#define VECTOR_DYNAMIC_START	0x20U
#define VECTOR_DYNAMIC_END	0xDFU

/* vectors range for fixed vectors, usually for HV service */
#define VECTOR_FIXED_START	0xE0U
#define VECTOR_FIXED_END	0xFFU

#define TIMER_VECTOR		(VECTOR_FIXED_START)
#define NOTIFY_VCPU_VECTOR	(VECTOR_FIXED_START + 1U)
#define PMI_VECTOR		(VECTOR_FIXED_START + 2U)
#define THERMAL_VECTOR		(VECTOR_FIXED_START + 3U)
#define CMCI_VECTOR		(VECTOR_FIXED_START + 4U)
/*
 * Starting vector for posted interrupts
 * # of CONFIG_MAX_VM_NUM (POSTED_INTR_VECTOR ~ (POSTED_INTR_VECTOR + CONFIG_MAX_VM_NUM - 1U))
 * consecutive vectors reserved for posted interrupts
 */
#define POSTED_INTR_VECTOR	(VECTOR_FIXED_START + NR_STATIC_MAPPINGS_1)

#define TIMER_IRQ		(NR_IRQS - 1U)
#define NOTIFY_VCPU_IRQ		(NR_IRQS - 2U)
#define PMI_IRQ			(NR_IRQS - 3U)
#define THERMAL_IRQ		(NR_IRQS - 4U)
#define CMCI_IRQ		(NR_IRQS - 5U)
/*
 * Starting IRQ for posted interrupts
 * # of CONFIG_MAX_VM_NUM (POSTED_INTR_IRQ ~ (POSTED_INTR_IRQ + CONFIG_MAX_VM_NUM - 1U))
 * consecutive IRQs reserved for posted interrupts
 */
#define POSTED_INTR_IRQ	(NR_IRQS - NR_STATIC_MAPPINGS_1 - CONFIG_MAX_VM_NUM)

/* the maximum number of msi entry is 2048 according to PCI
 * local bus specification
 */
#define MAX_MSI_ENTRY 0x800U

#define INVALID_INTERRUPT_PIN	0xffffffffU

/*
 * x86 irq data
 */
struct x86_irq_data {
	uint32_t vector;	/**< assigned vector */
#ifdef PROFILING_ON
	uint64_t ctx_rip;
	uint64_t ctx_rflags;
	uint64_t ctx_cs;
#endif
};

/**
 * @brief Allocate a vectror and bind it to irq
 *
 * For legacy irq (num < 16) and statically mapped ones, do nothing
 * if mapping is correct.
 *
 * @param[in]	irq	The irq num to bind
 *
 * @return valid vector num on susccess, VECTOR_INVALID on failure
 */
struct intr_excp_ctx {
	struct acrn_gp_regs gp_regs;	/**< General Purpose Register */
	uint64_t vector;		/**< Vector number to index the IDT entry */
	uint64_t error_code;		/**< Hardware pushed exception error code or a dummy 0 */
	uint64_t rip;			/**< RIP register before entering the gate */
	uint64_t cs;			/**< CS register before entering the gate */
	uint64_t rflags;		/**< RFLAGS register before entering the gate */
	uint64_t rsp;			/**< RSP register before entering the gate */
	uint64_t ss;			/**< SS register before entering the gate */
};

void dispatch_exception(struct intr_excp_ctx *ctx);
void handle_nmi(__unused struct intr_excp_ctx *ctx);
uint32_t alloc_irq_vector(uint32_t irq);
uint32_t irq_to_vector(uint32_t irq);

/**
 * @brief Dispatch interrupt
 *
 * To dispatch an interrupt, an action callback will be called if registered.
 *
 * @param ctx Pointer to interrupt exception context
 */
void dispatch_interrupt(const struct intr_excp_ctx *ctx);



#endif /* ARCH_X86_IRQ_H */
