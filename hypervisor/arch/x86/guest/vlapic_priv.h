/*-
 * Copyright (c) 2013 Neel Natu <neel@freebsd.org>
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

#ifndef VLAPIC_PRIV_H
#define VLAPIC_PRIV_H

/**
 * @addtogroup vp-base_vlapic
 *
 * @{
 */

/**
 * @file
 * @brief This file defines offsets of the APIC registers
 *
 */

#define APIC_OFFSET_ID		0x20U	/**< offset of Local APIC ID register */
#define APIC_OFFSET_VER		0x30U	/**< offset of Local APIC Version */
#define APIC_OFFSET_TPR		0x80U	/**< offset of Task Priority Register */
#define APIC_OFFSET_APR		0x90U	/**< offset of Arbitration Priority */
#define APIC_OFFSET_PPR		0xA0U	/**< offset of Processor Priority Register */
#define APIC_OFFSET_EOI		0xB0U	/**< offset of EOI Register */
#define APIC_OFFSET_RRR		0xC0U	/**< offset of Remote Read Register(RRD) */
#define APIC_OFFSET_LDR		0xD0U	/**< offset of Logical Destination register */
#define APIC_OFFSET_DFR		0xE0U	/**< offset of Destination Format Register */
#define APIC_OFFSET_SVR		0xF0U	/**< offset of Spurious Vector Register	*/
#define APIC_OFFSET_ISR0	0x100U	/**< offset of In-Service Register (ISR), bits 31:0 */
#define APIC_OFFSET_ISR1	0x110U  /**< offset of In-Service Register (ISR), bits 63:32 */
#define APIC_OFFSET_ISR2	0x120U  /**< offset of In-Service Register (ISR), bits 95:64 */
#define APIC_OFFSET_ISR3	0x130U  /**< offset of In-Service Register (ISR), bits 127:96 */
#define APIC_OFFSET_ISR4	0x140U  /**< offset of In-Service Register (ISR), bits 159:128 */
#define APIC_OFFSET_ISR5	0x150U  /**< offset of In-Service Register (ISR), bits 191:160 */
#define APIC_OFFSET_ISR6	0x160U  /**< offset of In-Service Register (ISR), bits 223:192 */
#define APIC_OFFSET_ISR7	0x170U  /**< offset of In-Service Register (ISR), bits 255:224 */
#define APIC_OFFSET_TMR0	0x180U	/**< offset of Trigger Mode Register (TMR), bits 31:0 */
#define APIC_OFFSET_TMR1	0x190U  /**< offset of Trigger Mode Register (TMR), bits 63:32 */
#define APIC_OFFSET_TMR2	0x1A0U  /**< offset of Trigger Mode Register (TMR), bits 95:64 */
#define APIC_OFFSET_TMR3	0x1B0U  /**< offset of Trigger Mode Register (TMR), bits 127:96 */
#define APIC_OFFSET_TMR4	0x1C0U  /**< offset of Trigger Mode Register (TMR), bits 159:128 */
#define APIC_OFFSET_TMR5	0x1D0U  /**< offset of Trigger Mode Register (TMR), bits 191:160 */
#define APIC_OFFSET_TMR6	0x1E0U  /**< offset of Trigger Mode Register (TMR), bits 223:192 */
#define APIC_OFFSET_TMR7	0x1F0U  /**< offset of Trigger Mode Register (TMR), bits 255:224 */
#define APIC_OFFSET_IRR0	0x200U	/**< offset of Interrupt Request Register (IRR), bits 31:0 */
#define APIC_OFFSET_IRR1	0x210U	/**< offset of Interrupt Request Register (IRR), bits 63:32 */
#define APIC_OFFSET_IRR2	0x220U	/**< offset of Interrupt Request Register (IRR), bits 95:64 */
#define APIC_OFFSET_IRR3	0x230U	/**< offset of Interrupt Request Register (IRR), bits 127:96 */
#define APIC_OFFSET_IRR4	0x240U	/**< offset of Interrupt Request Register (IRR), bits 159:128 */
#define APIC_OFFSET_IRR5	0x250U	/**< offset of Interrupt Request Register (IRR), bits 191:160 */
#define APIC_OFFSET_IRR6	0x260U	/**< offset of Interrupt Request Register (IRR), bits 223:192 */
#define APIC_OFFSET_IRR7	0x270U	/**< offset of Interrupt Request Register (IRR), bits 255:224 */
#define APIC_OFFSET_ESR		0x280U	/**< offset of Error Status Register */
#define APIC_OFFSET_CMCI_LVT	0x2F0U	/**< offset of LVT Corrected Machine Check Interrupt (CMCI) Register */
#define APIC_OFFSET_ICR_LOW	0x300U	/**< offset of Interrupt Command Register (ICR), bits 0-31 */
#define APIC_OFFSET_ICR_HI	0x310U  /**< Interrupt Command Register (ICR), bits 32-63 */
#define APIC_OFFSET_TIMER_LVT	0x320U	/**< offset of Local Vector Table (Timer) */
#define APIC_OFFSET_THERM_LVT	0x330U	/**< offset of Local Vector Table (Thermal) */
#define APIC_OFFSET_PERF_LVT	0x340U	/**< offset of Local Vector Table (PMC)	*/
#define APIC_OFFSET_LINT0_LVT	0x350U	/**< offset of Local Vector Table (LINT0) */
#define APIC_OFFSET_LINT1_LVT	0x360U	/**< offset of Local Vector Table (LINT1) */
#define APIC_OFFSET_ERROR_LVT	0x370U	/**< offset of Local Vector Table (ERROR) */
#define APIC_OFFSET_TIMER_ICR	0x380U	/**< offset of Initial Count Register (for Timer) */
#define APIC_OFFSET_TIMER_CCR	0x390U	/**< offset of Current Count Register (for Timer) */
#define APIC_OFFSET_TIMER_DCR	0x3E0U	/**< offset of Divide Configuration Register (for Timer) */
#define APIC_OFFSET_SELF_IPI    0x3F0U  /**< offset of Self IPI Register */

/**
 * @}
 */
#endif /* VLAPIC_PRIV_H */
