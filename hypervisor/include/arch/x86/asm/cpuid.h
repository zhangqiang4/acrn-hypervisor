/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
* cpuid.h
*
*  Created on: Jan 4, 2018
*      Author: don
*/

#ifndef CPUID_H_
#define CPUID_H_
/**
 * @addtogroup hwmgmt_cpu
 *
 * @{
 */
/**
 * @file
 * @brief This file defines the macros, and APIs for CPUID operations.
 *
 */

/* CPUID bit definitions */
/**
 * @brief A bit representing whether the processor supports Streaming SIMD Extensions 3 (SSE3).
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):ECX[0]
 */
#define CPUID_ECX_SSE3          (1U<<0U)
/**
 * @brief A bit representing whether the processor supports the PCLMULQDQ instruction.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):ECX[1]
 */
#define CPUID_ECX_PCLMUL        (1U<<1U)
/**
 * @brief A bit representing whether the processor supports DS area using 64-bit layout.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):ECX[2]
 */
#define CPUID_ECX_DTES64        (1U<<2U)
/**
 * @brief A bit representing whether the processor supports MONITOR/MWAIT.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):ECX[3]
 */
#define CPUID_ECX_MONITOR       (1U<<3U)
/**
 * @brief A bit representing whether the processor supports the extensions to the Debug Store feature to allow for
 * branch message storage qualified by CPL.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):ECX[4]
 */
#define CPUID_ECX_DS_CPL        (1U<<4U)
/**
 * @brief A bit representing whether the processor supports Virtual Machine Extensions.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):ECX[5]
 */
#define CPUID_ECX_VMX           (1U<<5U)
/**
 * @brief A bit representing whether the processor supports Safer Mode Extensions.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):ECX[6]
 */
#define CPUID_ECX_SMX           (1U<<6U)
/**
 * @brief A bit representing whether the processor supports Enhanced Intel SpeedStep technology.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):ECX[7].
 */
#define CPUID_ECX_EST           (1U<<7U)
/**
 * @brief A bit representing whether the processor supports Thermal Monitor 2.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):ECX[8].
 */
#define CPUID_ECX_TM2           (1U<<8U)
/**
 * @brief A bit representing whether the processor supports Supplemental Streaming SIMD Extensions 3 (SSSE3).
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):ECX[9].
 */
#define CPUID_ECX_SSSE3         (1U<<9U)
/**
 * @brief A bit representing whether the processor supports L1 Context ID.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):ECX[10].
 */
#define CPUID_ECX_CID           (1U<<10U)
/**
 * @brief A bit representing whether the processor supports IA32_DEBUG_INTERFACE MSR for silicon debug.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):ECX[11].
 */
#define CPUID_ECX_SDBG          (1U<<11U)
/**
 * @brief A bit representing whether the processor supports FMA extensions using YMM state.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):ECX[12].
 */
#define CPUID_ECX_FMA           (1U<<12U)
/**
 * @brief A bit representing whether the feature CMPXCHG16B is available.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):ECX[13].
 */
#define CPUID_ECX_CX16          (1U<<13U)
/**
 * @brief A bit representing whether the processor supports changing IA32_MISC_ENABLE[23].
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):ECX[14].
 */
#define CPUID_ECX_ETPRD         (1U<<14U)
/**
 * @brief A bit representing whether the processor supports Perfmon and Debug Capability.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):ECX[15].
 */
#define CPUID_ECX_PDCM          (1U<<15U)
/**
 * @brief A bit representing whether the processor supports Process-context identifiers.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):ECX[17].
 */
 #define CPUID_ECX_PCID         (1U<<17U)
/**
 * @brief A bit representing whether the processor supports the ability to prefetch data from a memory-mapped device.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):ECX[18].
 */
#define CPUID_ECX_DCA           (1U<<18U)
/**
 * @brief A bit representing whether the processor supports SSE4.1.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):ECX[19].
 */
#define CPUID_ECX_SSE4_1        (1U<<19U)
/**
 * @brief A bit representing whether the processor supports SSE4.2.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):ECX[20].
 */
#define CPUID_ECX_SSE4_2        (1U<<20U)
/**
 * @brief A bit representing whether the processor supports x2APIC feature.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):ECX[21].
 */
#define CPUID_ECX_x2APIC        (1U<<21U)
/**
 * @brief A bit representing whether the processor supports MOVBE instruction.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):ECX[22].
 */
#define CPUID_ECX_MOVBE         (1U<<22U)
/**
 * @brief A bit representing whether the processor supports POPCNT instruction.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):ECX[23].
 */
#define CPUID_ECX_POPCNT        (1U<<23U)
/**
 * @brief A bit representing whether the processor supports the AESNI instruction extensions.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):ECX[25].
 */
#define CPUID_ECX_AES           (1U<<25U)
/**
 * @brief A bit representing whether the processor supports XSAVE instructions and XSAVE supported features.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):ECX[26].
 */
#define CPUID_ECX_XSAVE         (1U<<26U)
/**
 * @brief A bit representing whether the CR4.OSXSAVE is set.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):ECX[27].
 *
 */
#define CPUID_ECX_OSXSAVE       (1U<<27U)
/**
 * @brief A bit representing whether the processor supports the AVX instruction extensions.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):ECX[28].
 */
#define CPUID_ECX_AVX           (1U<<28U)
/**
 * @brief A bit representing whether the hypervisor is present.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):ECX[31].
 */
#define CPUID_ECX_HV            (1U<<31U)
/**
 * @brief A bit representing whether the processor supports Floating-Point Unit On-Chip.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):EDX[0].
 */
#define CPUID_EDX_FPU           (1U<<0U)
/**
 * @brief A bit representing whether the processor supports Virtual 8086 Mode Enhancements.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):EDX[1].
 */
#define CPUID_EDX_VME           (1U<<1U)
/**
 * @brief A bit representing whether the processor supports Debugging Extensions.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):EDX[2].
 */
#define CPUID_EDX_DE            (1U<<2U)
/**
 * @brief A bit representing whether the processor supports Page Size Extension.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):EDX[3].
 */
#define CPUID_EDX_PSE           (1U<<3U)
/**
 * @brief A bit representing whether the processor supports Time Stamp Counter.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):EDX[4].
 */
#define CPUID_EDX_TSC           (1U<<4U)
/**
 * @brief A bit representing whether the processor supports Model Specific Registers RDMSR and WRMSR Instructions.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):EDX[5].
 */
#define CPUID_EDX_MSR           (1U<<5U)
/**
 * @brief A bit representing whether the processor supports Physical Address Extension.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):EDX[6].
 */
#define CPUID_EDX_PAE           (1U<<6U)
/**
 * @brief A bit representing whether the processor supports Machine Check Exception.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):EDX[7].
 */
#define CPUID_EDX_MCE           (1U<<7U)
/**
 * @brief A bit representing whether the processor supports CMPXCHG8B Instruction.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):EDX[8].
 */
#define CPUID_EDX_CX8           (1U<<8U)
/**
 * @brief A bit representing whether the processor supports APIC On-Chip.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):EDX[9].
 */
#define CPUID_EDX_APIC          (1U<<9U)
/**
 * @brief A bit representing whether the processor supports SYSENTER and SYSEXIT Instructions.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):EDX[11].
 */
#define CPUID_EDX_SEP           (1U<<11U)
/**
 * @brief A bit representing whether the processor supports Memory Type Range Registers.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):EDX[12].
 */
#define CPUID_EDX_MTRR          (1U<<12U)
/**
 * @brief A bit representing whether the processor supports Page Global Bit.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):EDX[13].
 */
#define CPUID_EDX_PGE           (1U<<13U)
/**
 * @brief A bit representing whether the processor supports Machine Check Architecture.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):EDX[14].
 */
#define CPUID_EDX_MCA           (1U<<14U)
/**
 * @brief A bit representing whether the processor supports Conditional Move Instructions.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):EDX[15].
 */
#define CPUID_EDX_CMOV          (1U<<15U)
/**
 * @brief A bit representing whether the processor supports Page Attribute Table.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):EDX[16].
 */
#define CPUID_EDX_PAT           (1U<<16U)
/**
 * @brief A bit representing whether the processor supports 36-Bit Page Size Extension.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):EDX[17].
 */
#define CPUID_EDX_PSE36         (1U<<17U)
/**
 * @brief A bit representing whether the processor supports Processor Serial Number.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):EDX[18].
 */
#define CPUID_EDX_PSN           (1U<<18U)
/**
 * @brief A bit representing whether the processor supports CLFLUSH Instruction.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):EDX[19].
 */
#define CPUID_EDX_CLF           (1U<<19U)
/**
 * @brief A bit representing whether the processor supports Debug Store.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):EDX[21].
 */
#define CPUID_EDX_DTES          (1U<<21U)
/**
 * @brief A bit representing whether the processor supports Thermal Monitor and Software Controlled Clock Facilities.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):EDX[22].
 */
#define CPUID_EDX_ACPI          (1U<<22U)
/**
 * @brief A bit representing whether the processor supports Intel MMX Technology.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):EDX[23].
 */
#define CPUID_EDX_MMX           (1U<<23U)
/**
 * @brief A bit representing whether the processor supports FXSAVE and FXRSTOR Instructions.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):EDX[24].
 */
#define CPUID_EDX_FXSR          (1U<<24U)
/**
 * @brief A bit representing whether the processor supports SSE.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):EDX[25].
 */
#define CPUID_EDX_SSE           (1U<<25U)
/**
 * @brief A bit representing whether the processor supports SSE2.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):EDX[26].
 */
#define CPUID_EDX_SSE2          (1U<<26U)
/**
 * @brief A bit representing whether the processor supports Self Snoop.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):EDX[27].
 */
#define CPUID_EDX_SS            (1U<<27U)
/**
 * @brief A bit representing whether Max APIC IDs reserved field is valid.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):EDX[28].
 */
#define CPUID_EDX_HTT           (1U<<28U)
/**
 * @brief A bit representing whether the processor supports Thermal Monitor.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):EDX[29].
 */
#define CPUID_EDX_TM1           (1U<<29U)
/**
 * @brief A bit representing whether the processor supports Pending Break Enable.
 *
 * This bit is fetched from CPUID(EAX=1H,ECX=0H):EDX[31].
 */
#define CPUID_EDX_PBE           (1U<<31U)
/**
 * @brief A bit representing whether the processor supports Digital temperature sensor.
 *
 * This bit is fetched from CPUID(EAX=6H,ECX=0H):EAX[0].
 */
#define CPUID_EAX_DTS           (1U<<0U)
/**
 * @brief A bit representing whether the processor supports Power limit notification controls.
 *
 * This bit is fetched from CPUID(EAX=6H,ECX=0H):EAX[4].
 */
#define CPUID_EAX_PLN           (1U<<4U)
/**
 * @brief A bit representing whether the processor supports Clock modulation duty cycle extension.
 *
 * This bit is fetched from CPUID(EAX=6H,ECX=0H):EAX[5].
 */
#define CPUID_EAX_ECMD          (1U<<5U)
/**
 * @brief A bit representing whether the processor supports Package thermal management.
 *
 * This bit is fetched from CPUID(EAX=6H,ECX=0H):EAX[6].
 */
#define CPUID_EAX_PTM           (1U<<6U)
/**
 * @brief A bit representing whether the processor supports HWP base registers.
 *
 * This bit is fetched from CPUID(EAX=6H,ECX=0H):EAX[7].
 */
#define CPUID_EAX_HWP           (1U<<7U)
/**
 * @brief A bit representing whether the processor supports HWP Notification.
 *
 * This bit is fetched from CPUID(EAX=6H,ECX=0H):EAX[8].
 */
#define CPUID_EAX_HWP_N         (1U<<8U)
/**
 * @brief A bit representing whether the processor supports HWP Activity Window.
 *
 * This bit is fetched from CPUID(EAX=6H,ECX=0H):EAX[9].
 */
#define CPUID_EAX_HWP_AW        (1U<<9U)
/**
 * @brief A bit representing whether the processor supports HWP Energy Performance Preference.
 *
 * This bit is fetched from CPUID(EAX=6H,ECX=0H):EAX[10].
 */
#define CPUID_EAX_HWP_EPP       (1U<<10U)
/**
 * @brief A bit representing whether the processor supports HWP Package Level Request.
 *
 * This bit is fetched from CPUID(EAX=6H,ECX=0H):EAX[11].
 */
#define CPUID_EAX_HWP_PLR       (1U<<11U)
/**
 * @brief A bit representing whether the processor supports Hardware Duty Cycling.
 *
 * This bit is fetched from CPUID(EAX=6H,ECX=0H):EAX[13].
 */
#define CPUID_EAX_HDC           (1U<<13U)
/**
 * @brief A bit representing whether the processor supports HWP Favored Cores support (Turbo Boost Max Technology 3.0).
 *
 * This bit is fetched from CPUID(EAX=6H,ECX=0H):EAX[14].
 */
#define CPUID_EAX_HWP_FC        (1U<<14U)
/**
 * @brief A bit representing whether the processor supports highest performance change notification.
 *
 * This bit is fetched from CPUID(EAX=6H,ECX=0H):EAX[15].
 */
#define CPUID_EAX_HWP_HPC       (1U<<15U)
/**
 * @brief A bit representing whether the processor supports HWP PECI override notification.
 *
 * This bit is fetched from CPUID(EAX=6H,ECX=0H):EAX[16].
 */
#define CPUID_EAX_HWP_PECI      (1U<<16U)
/**
 * @brief A bit representing whether the processor supports Flexible HWP.
 *
 * This bit is fetched from CPUID(EAX=6H,ECX=0H):EAX[17].
 */
#define CPUID_EAX_FLEX_HWP      (1U<<17U)
/**
 * @brief A bit representing whether the processor supports Fast MSR access mode for IA32_HWP_REQUEST MSR.
 *
 * This bit is fetched from CPUID(EAX=6H,ECX=0H):EAX[18].
 */
#define CPUID_EAX_HWP_FAM       (1U<<18U)
/**
 * @brief A bit representing whether the processor supports HW FEEDBACK.
 *
 * This bit is fetched from CPUID(EAX=6H,ECX=0H):EAX[19].
 */
#define CPUID_EAX_HFI           (1U<<19U)
/**
 * @brief A bit representing whether the processor supports Ignoring Idle Logical Processor HWP request.
 *
 * This bit is fetched from CPUID(EAX=6H,ECX=0H):EAX[20].
 */
#define CPUID_EAX_HWP_IILP      (1U<<20U)
/**
 * @brief A bit representing whether the processor supports HWP Control MSR.
 *
 * This bit is fetched from CPUID(EAX=6H,ECX=0H):EAX[22].
 */
#define CPUID_EAX_HWP_CTL       (1U<<22U)
/**
 * @brief A bit representing whether the processor supports Intel Thread Director.
 *
 * This bit is fetched from CPUID(EAX=6H,ECX=0H):EAX[23].
 */
#define CPUID_EAX_ITD           (1U<<23U)
/**
 * @brief A bit representing whether the processor supports Hardware Feedback Notification.
 *
 * This bit is fetched from CPUID(EAX=6H,ECX=0H):EAX[24].
 */
#define CPUID_EAX_HFN           (1U<<24U)
/**
 * @brief A bit representing whether the processor supports Hardware Coordination Feedback Capability.
 *
 * This bit is fetched from CPUID(EAX=6H,ECX=0H):ECX[0].
 */
#define CPUID_ECX_HCFC          (1U<<0U)
/**
 * @brief 8 bits representing the Number of Intel Thread Director classes supported by the processor.
 *
 * These bits are fetched from CPUID(EAX=6H,ECX=0H):ECX[15:8].
 */
 #define CPUID_ECX_ITD_NOC      (0xFFU<<8U)
/**
 * @brief A bit representing whether the processor supports FSGSBASE.
 *
 * This bit is fetched from CPUID(EAX=7H,ECX=0H):EBX[0].
 */
#define CPUID_EBX_FSGSBASE      (1U<<0U)
/**
 * @brief A bit representing whether the processor supports IA32_TSC_ADJUST MSR.
 *
 * This bit is fetched from CPUID(EAX=7H,ECX=0H):EBX[1].
 */
#define CPUID_EBX_TSC_ADJ       (1U<<1U)
/**
 * @brief A bit representing whether the processor supports Intel SGX Extensions.
 *
 * This bit is fetched from CPUID(EAX=7H,ECX=0H):EBX[2].
 */
#define CPUID_EBX_SGX           (1U<<2U)
/**
 * @brief A bit representing whether the processor supports Supervisor-Mode Execution Prevention.
 *
 * This bit is fetched from CPUID(EAX=7H,ECX=0H):EBX[7].
 */
#define CPUID_EBX_SMEP          (1U<<7U)
/**
 * @brief A bit representing whether the processor supports Intel Memory Protection Extensions.
 *
 * This bit is fetched from CPUID(EAX=7H,ECX=0H):EBX[14].
 */
#define CPUID_EBX_MPX           (1U<<14U)
/**
 * @brief A bit representing whether the processor supports Supervisor-Mode Access Prevention.
 *
 * This bit is fetched from CPUID(EAX=7H,ECX=0H):EBX[20].
 */
#define CPUID_EBX_SMAP          (1U<<20U)
/**
 * @brief A bit representing whether the processor supports User-Mode Instruction Prevention.
 *
 * This bit is fetched from CPUID(EAX=7H,ECX=0H):ECX[2].
 */
#define CPUID_ECX_UMIP		(1U<<2U)
/**
 * @brief A bit representing whether the processor supports Protection Keys for user-mode pages.
 *
 * This bit is fetched from CPUID(EAX=7H,ECX=0H):ECX[3].
 */
#define CPUID_ECX_PKE		(1U<<3U)
/**
 * @brief A bit representing whether the processor supports CET shadow stack features.
 *
 * This bit is fetched from CPUID(EAX=7H,ECX=0H):ECX[7].
 */
#define CPUID_ECX_CET_SS        (1U<<7U)
/**
 * @brief A bit representing whether the processor supports TME MSRs.
 *
 * This bit is fetched from CPUID(EAX=7H,ECX=0H):ECX[13].
 */
#define CPUID_ECX_TME		(1U<<13U)
/**
 * @brief A bit representing whether the processor supports 57-bit linear addresses and five-level paging.
 *
 * This bit is fetched from CPUID(EAX=7H,ECX=0H):ECX[16].
 */
#define CPUID_ECX_LA57		(1U<<16U)
/**
 * @brief A bit representing whether the processor supports SGX Launch Configuration.
 *
 * This bit is fetched from CPUID(EAX=7H,ECX=0H):ECX[30].
 */
#define CPUID_ECX_SGX_LC        (1U<<30U)
/**
 * @brief A bit representing whether the processor supports protection keys for supervisor-mode pages.
 *
 * This bit is fetched from CPUID(EAX=7H,ECX=0H):ECX[31].
 */
#define CPUID_ECX_PKS           (1U<<31U)
/**
 * @brief A bit representing whether the processor is hybrid part.
 *
 * This bit is fetched from CPUID(EAX=7H,ECX=0H):EDX[15].
 */
#define CPUID_EDX_HYBRID        (1U<<15U)
/**
 * @brief A bit representing whether the processor supports PCONFIG.
 *
 * This bit is fetched from CPUID(EAX=7H,ECX=0H):EDX[18].
 */
#define CPUID_EDX_PCONFIG       (1U<<18U)
/**
 * @brief A bit representing whether the processor supports Architectural LBRs.
 *
 * This bit is fetched from CPUID(EAX=7H,ECX=0H):EDX[19].
 */
 #define CPUID_EDX_LBR          (1U<<19U)
 /**
 * @brief A bit representing whether the processor supports CET Indirect Branch Tracking features.
 *
 * This bit is fetched from CPUID(EAX=7H,ECX=0H):EDX[20].
 */
#define CPUID_EDX_CET_IBT       (1U<<20U)
/**
 * @brief A bit representing whether the processor supports Indirect Branch Restricted Speculation and Indirect Branch
 * Predictor Barrier.
 *
 * This bit is fetched from CPUID(EAX=7H,ECX=0H):EDX[26].
 */
#define CPUID_EDX_IBRS_IBPB     (1U<<26U)
/**
 * @brief A bit representing whether the processor supports Single Thread Indirect Branch Predictors.
 *
 * This bit is fetched from CPUID(EAX=7H,ECX=0H):EDX[27].
 */
#define CPUID_EDX_STIBP         (1U<<27U)
/**
 * @brief A bit representing whether the processor supports IA32_ARCH_CAPABILITIES MSR.
 *
 * This bit is fetched from CPUID(EAX=7H,ECX=0H):EDX[29].
 */
#define CPUID_EDX_ARCH_CAP	(1U<<29U)
/**
 * @brief A bit representing whether the processor supports 1-GByte pages.
 *
 * This bit is fetched from CPUID(EAX=80000001H):EDX[26].
 */
#define CPUID_EDX_PAGE1GB       (1U<<26U)
/**
 * @brief A bit representing whether the processor supports INVPCID instruction for system software that manages
 * process-context identifiers.
 *
 * This bit is fetched from CPUID(EAX=7H,ECX=0H):EBX[10].
 */
#define CPUID_EBX_INVPCID       (1U<<10U)
/**
 * @brief A bit representing whether the processor supports Intel Resource Director Technology Monitoring capability.
 *
 * This bit is fetched from CPUID(EAX=7H,ECX=0H):EBX[12].
 */
#define CPUID_EBX_PQM           (1U<<12U)
/**
 * @brief A bit representing whether the processor supports Intel Resource Director Technology Allocation capability.
 *
 * This bit is fetched from CPUID(EAX=7H,ECX=0H):EBX[15].
 */
#define CPUID_EBX_PQE           (1U<<15U)
/**
 * @brief A bit representing whether the processor supports Intel Processor Trace.
 *
 * This bit is fetched from CPUID(EAX=7H,ECX=0H):EBX[25].
 */
#define CPUID_EBX_PROC_TRC      (1U<<25U)
/**
 * @brief A bit representing whether the processor supports AVX versions of the Vector Neural Network Instructions.
 *
 * This bit is fetched from CPUID(EAX=7H,ECX=1H):EAX[4].
 */
#define CPUID_EAX_AVX_VNNI      (1U<<4U)
/**
 * @brief A bit representing whether Vector Neural Network Instructions supports BFLOAT16 inputs and conversion
 * instructions from IEEE single precision.
 *
 * This bit is fetched from CPUID(EAX=7H,ECX=1H):EAX[5].
 */
#define CPUID_EAX_AVX512_BF16   (1U<<5U)
/**
 * @brief A bit representing whether supports history reset via the HRESET instruction and the IA32_HRESET_ENABLE MSR.
 *
 * This bit is fetched from CPUID(EAX=7H,ECX=1H):EAX[22].
 */
 #define CPUID_EAX_HRESET      (1U<<22U)
/**
 * @brief A bit representing whether hypervisor supports CET Supervisor Shadow Stack
 *
 * This bit is fetched from CPUID(EAX=7H,ECX=1H):EDX[18].
 */
#define CPUID_EDX_CET_SSS	(1U<<18U)
/**
 * @brief A bit representing whether the processor supports XCR0.BNDREGS[3].
 *
 * This bit is fetched from CPUID(EAX=DH,ECX=0H):EAX[3].
 */
#define CPUID_EAX_XCR0_BNDREGS  (1U<<3U)
/**
 * @brief A bit representing whether the processor supports XCR0.BNDCSR[4].
 *
 * This bit is fetched from CPUID(EAX=DH,ECX=0H):EAX[4].
 */
#define CPUID_EAX_XCR0_BNDCSR   (1U<<4U)
/**
 * @brief A bit representing whether the processor supports CET user state bit of IA32_XSS MSR.
 *
 * This bit is fetched from CPUID(EAX=DH,ECX=1H):ECX[11].
 */
#define CPUID_ECX_CET_U_STATE   (1U<<11U)
/**
 * @brief A bit representing whether the processor supports CET supervisor state bit of IA32_XSS MSR.
 *
 * This bit is fetched from CPUID(EAX=DH,ECX=1H):ECX[12].
 */
#define CPUID_ECX_CET_S_STATE   (1U<<12U)
/**
 * @brief A bit representing whether Intel SGX supports the collection of SGX1 leaf functions.
 *
 * This bit is fetched from CPUID(EAX=12H,ECX=0H):EAX[0].
 */
#define CPUID_EAX_SGX1          (1U<<0U)
/**
 * @brief A bit representing whether Intel SGX supports the collection of SGX2 leaf functions.
 *
 * This bit is fetched from CPUID(EAX=12H,ECX=0H):EAX[1].
 */
#define CPUID_EAX_SGX2          (1U<<1U)
/**
 * @brief A bit representing whether the AES Key Locker instructions are fully enabled.
 *
 * This bit is fetched from CPUID(EAX=19H):EBX[0].
 */
#define CPUID_EBX_KL_AES_EN	(1U<<0U)
/**
 * @brief A bit representing whether the platform supports the Key Locker MSRs and backing up the internal wrapping
 * key.
 *
 * This bit is fetched from CPUID(EAX=19H):EBX[4].
 */
#define CPUID_EBX_KL_BACKUP_MSR	(1U<<4U)
/**
 * @brief A bit representing whether the platform supports the NoBackup parameter to LOADIWKEY.
 *
 * This bit is fetched from CPUID(EAX=19H):ECX[0].
 */
#define CPUID_ECX_KL_NOBACKUP	(1U<<0U)
/**
 * @brief A bit representing whether the platform supports the KeySource encoding of 1 (randomization of the internal
 * wrapping key)
 *
 * This bit is fetched from CPUID(EAX=19H):ECX[1].
 */
#define CPUID_ECX_KL_RANDOM_KS	(1U<<1U)
/**
 * @brief A bit representing whether the Execute Disable Bit is available.
 *
 * This bit is fetched from CPUID(EAX=80000001H):EDX[20].
 */
#define CPUID_EDX_XD_BIT_AVIL   (1U<<20U)

/* CPUID source operands */
/**
 * @brief A CPUID entry provide the vendor string.
 */
#define CPUID_VENDORSTRING      0U
/**
 * @brief A CPUID entry provide the information of version, features, and capabilities.
 */
#define CPUID_FEATURES          1U
/**
 * @brief A CPUID entry provide the information about the processor’s internal TLBs,
 * cache, and prefetch hardware.
 */
#define CPUID_TLB               2U
/**
 * @brief A CPUID entry provide the processor serial number.
 */
#define CPUID_SERIALNUM         3U
/**
 * @brief A CPUID entry provide the deterministic cache parameters for each level.
 */
#define CPUID_CACHE             4U
/**
 * @brief A CPUID entry provide the thermal and power management information.
 */
#define CPUID_THERMAL_POWER     6U
/**
 * @brief A CPUID entry provide the structured extended features.
 */
#define CPUID_EXTEND_FEATURE    7U
/**
 * @brief A CPUID entry provide the architectural performance monitoring information.
 */
#define CPUID_ARCH_PERF_MON    0xAU
/**
 * @brief A CPUID entry provide the processor extended topology enumeration.
 */
#define CPUID_EXTEND_TOPOLOGY  0xBU
/**
 * @brief A CPUID entry provide the processor extended state enumeration.
 **/
#define CPUID_XSAVE_FEATURES   0xDU
/**
 * @brief A CPUID entry provide the Intel Resource Director Technology monitoring facility enumeration.
 */
#define CPUID_RDT_MONITOR      0xFU
/**
 * @brief A CPUID entry provide the Intel Resource Director Technology allocation facility enumeration.
 */
#define CPUID_RDT_ALLOCATION   0x10U
/**
 * @brief A CPUID entry provide the Intel SGX capability enumeration.
 */
#define CPUID_SGX_CAP          0x12U
/**
 * @brief A CPUID entry provide the Intel Processor Trace capability enumeration.
 */
#define CPUID_TRACE            0x14U
/**
 * @brief A CPUID entry provide the Time Stamp Counter and Nominal Core Crystal Clock information.
 */
 #define CPUID_CLOCK           0x15U
/**
 * @brief A CPUID entry provide the Intel Processor frequency information.
 */
#define CPUID_FREQ             0x16U
/**
 * @brief A CPUID entry provide the deterministic address translation parameters.
 */
#define CPUID_ADDR_TRANS       0x18U
/**
 * @brief A CPUID entry provide the Key Locker information.
 */
#define CPUID_KEY_LOCKER       0x19U
/**
 * @brief A CPUID entry provide the native Model ID enumeration.
 */
#define CPUID_MODEL_ID         0x1AU
/**
 * @brief A CPUID entry provide the PCONFIG information.
 */
#define CPUID_PCONFIG          0x1BU
/**
 * @brief A CPUID entry provide the Last Branch Records information.
 */
#define CPUID_LAST_BRANCH_RECORD     0x1CU
/**
 * @brief A CPUID entry provide the V2 Extended Topology enumeration.
 */
#define CPUID_V2_EXTEND_TOPOLOGY     0x1FU
/**
 * @brief A CPUID entry provide the Processor History Reset information.
 */
 #define CPUID_HRESET          0x20U
/**
 * @brief A CPUID entry to get the highest value for extended processor information.
 */
#define CPUID_MAX_EXTENDED_FUNCTION  0x80000000U
/**
 * @brief A CPUID entry of extended function 1.
 **/
#define CPUID_EXTEND_FUNCTION_1      0x80000001U
/**
 * @brief A CPUID entry of extended function 2.
 **/
#define CPUID_EXTEND_FUNCTION_2      0x80000002U
/**
 * @brief A CPUID entry of extended function 3.
 **/
#define CPUID_EXTEND_FUNCTION_3      0x80000003U
/**
 * @brief A CPUID entry of extended function 4.
 **/
#define CPUID_EXTEND_FUNCTION_4      0x80000004U
/**
 * @brief A CPUID entry provide the extended cache information.
 **/
#define CPUID_EXTEND_CACHE           0x80000006U
/**
 * @brief A CPUID entry used to verify whether the processors supports Invariant TSC.
 **/
#define CPUID_EXTEND_INVA_TSC        0x80000007U
/**
 * @brief A CPUID entry used to fetch Linear/Physical Address size.
 */
#define CPUID_EXTEND_ADDRESS_SIZE    0x80000008U

/**
 * @brief Executes a CPUID instruction with the specified leaf and subleaf.
 *
 * @param[in] leaf The value to be loaded into the EAX register before executing the CPUID instruction.
 * @param[in] subleaf The value to be loaded into the ECX register before executing the CPUID instruction.
 * @param[out] eax Pointer to a variable that will store the content of the EAX register after executing the CPUID
 * instruction.
 * @param[out] ebx Pointer to a variable that will store the content of the EBX register after executing the CPUID
 * instruction.
 * @param[out] ecx Pointer to a variable that will store the content of the ECX register after executing the CPUID
 * instruction.
 * @param[out] edx Pointer to a variable that will store the content of the EDX register after executing the CPUID
 * instruction.
 *
 * @return None
 *
 * @pre eax != NULL
 * @pre ebx != NULL
 * @pre ecx != NULL
 * @pre edx != NULL
 * @post N/A
 */
static inline void cpuid_subleaf(uint32_t leaf, uint32_t subleaf,
				uint32_t *eax, uint32_t *ebx,
				uint32_t *ecx, uint32_t *edx)
{
	/* Execute CPUID instruction and save results */
	asm volatile("cpuid":"=a"(*eax), "=b"(*ebx),
			"=c"(*ecx), "=d"(*edx)
			: "a" (leaf), "c" (subleaf)
			: "memory");
}

/**
 * @}
 */
#endif /* CPUID_H_ */
