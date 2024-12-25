/*
 * Copyright (C) 2018-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ARCH_X86_IRQ_H
#define ARCH_X86_IRQ_H

#include <types.h>
#include <public/acrn_common.h>

/**
 * @addtogroup hwmgmt_irq hwmgmt.irq
 *
 * @{
 */

/**
 * @file include/arch/x86/asm/irq.h
 *
 * @brief public APIs for x86 IRQ handling
 */

#define DBG_LEVEL_PTIRQ		6U	/**< Default debug log level for vp-dm.ptirq module */
#define DBG_LEVEL_IRQ		6U	/**< Default debug log level for hwmgmt.irq module */

#define NR_MAX_VECTOR		0xFFU	/**< Max vector number on x86 platforms */
#define VECTOR_INVALID		(NR_MAX_VECTOR + 1U)	/**< The number to mark a vector invalid */

/**
 * @brief Number of static IRQ/vector mapping entries
 *
 * Currently static IRQ/vector mappings are defined for timer, vcpu notify, PMI, thermal and CMCI.
 */
#define NR_STATIC_MAPPINGS_1	5U

/**
 * @brief Number of static allocated Vectors
 *
 * The static IRQ/Vector mapping table in irq.c consists of the following entries:
 * Number of NR_STATIC_MAPPINGS_1 entries for timer, vcpu notify, and PMI
 *
 * Number of CONFIG_MAX_VM_NUM entries for posted interrupt notification, platform
 * specific but known at build time:
 * Allocate unique Activation Notification Vectors (ANV) for each vCPU that belongs
 * to the same pCPU, the ANVs need only be unique within each pCPU, not across all
 * vCPUs. The max numbers of vCPUs may be running on top of a pCPU is CONFIG_MAX_VM_NUM,
 * since ACRN does not support 2 vCPUs of same VM running on top of same pCPU.
 * This reduces Number of pre-allocated ANVs for posted interrupts to CONFIG_MAX_VM_NUM,
 * and enables ACRN to avoid switching between active and wake-up vector values
 * in the posted interrupt descriptor on vCPU scheduling state changes.
 */
#define NR_STATIC_MAPPINGS	(NR_STATIC_MAPPINGS_1 + CONFIG_MAX_VM_NUM)

#define HYPERVISOR_CALLBACK_HSM_VECTOR	0xF3U	/**< Allocated vector for HSM */

/* Vectors range for dynamic allocation, usually for devices */
#define VECTOR_DYNAMIC_START	0x20U	/**< Start of dynamic vectors */
#define VECTOR_DYNAMIC_END	0xDFU	/**< End of dynamic vectors */

/* Vectors range for fixed vectors, usually for hypervisor service */
#define VECTOR_FIXED_START	0xE0U	/**< Start of fixed usage vectors */
#define VECTOR_FIXED_END	0xFFU	/**< End of fixed usage vectors */

#define TIMER_VECTOR		(VECTOR_FIXED_START)		/**< Fixed vector for local timer interrupt */
#define NOTIFY_VCPU_VECTOR	(VECTOR_FIXED_START + 1U)	/**< Fixed vector for SMP call and vCPU notification */
#define PMI_VECTOR		(VECTOR_FIXED_START + 2U)	/**< Fixed vector for PMU LVT */
#define THERMAL_VECTOR		(VECTOR_FIXED_START + 3U)	/**< Fixed vector for thermal LVT */
#define CMCI_VECTOR		(VECTOR_FIXED_START + 4U)	/**< Fixed vector for CMCI */
/**
 * @brief Starting vector for posted interrupts
 *
 * Number of CONFIG_MAX_VM_NUM (POSTED_INTR_VECTOR ~ (POSTED_INTR_VECTOR + CONFIG_MAX_VM_NUM - 1U))
 * consecutive vectors reserved for posted interrupts
 */
#define POSTED_INTR_VECTOR	(VECTOR_FIXED_START + NR_STATIC_MAPPINGS_1)

#define TIMER_IRQ		(NR_IRQS - 1U)	      /**< Fixed IRQ number for local timer interrupt */
#define NOTIFY_VCPU_IRQ		(NR_IRQS - 2U)	      /**< Fixed IRQ number for SMP call and vCPU notification */
#define PMI_IRQ			(NR_IRQS - 3U)	      /**< Fixed IRQ number for PMU LVT */
#define THERMAL_IRQ		(NR_IRQS - 4U)	      /**< Fixed IRQ number for thermal LVT */
#define CMCI_IRQ		(NR_IRQS - 5U)	      /**< Fixed IRQ number for CMCI */
/**
 * @brief Starting IRQ for posted interrupts
 *
 * Number of CONFIG_MAX_VM_NUM (POSTED_INTR_IRQ ~ (POSTED_INTR_IRQ + CONFIG_MAX_VM_NUM - 1U))
 * consecutive IRQs reserved for posted interrupts
 */
#define POSTED_INTR_IRQ	(NR_IRQS - NR_STATIC_MAPPINGS_1 - CONFIG_MAX_VM_NUM)

/**
 * @brief Maximum MSI entries
 *
 * The maximum number of MSI entries is 2048 according to PCI local bus specification
 */
#define MAX_MSI_ENTRY 0x800U

/**
 * @brief Number for invalid pin index
 */
#define INVALID_INTERRUPT_PIN	0xffffffffU

/**
 * @brief X86 irq data
 */
struct x86_irq_data {
	uint32_t vector;	/**< Assigned vector for this IRQ */
#ifdef PROFILING_ON
	uint64_t ctx_rip;	/**< RIP register in the interrupt context */
	uint64_t ctx_rflags;	/**< RFLAGS register in the interrupt context */
	uint64_t ctx_cs;	/**< CS register in the interrupt context */
#endif
};

/**
 * @brief Definition of the exception and interrupt stack frame layout
 *
 * On entering an interrupt gate, fields including ss, rsp, rflags, cs, rip and error_code are
 * pushed by hardware. Other fields, and a dummy error code for exceptions without error code are
 * pushed by software.
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
void dispatch_interrupt(const struct intr_excp_ctx *ctx);



/**
 * @}
 */
#endif /* ARCH_X86_IRQ_H */
