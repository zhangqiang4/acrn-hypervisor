/*
 * Copyright (C) 2018-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef CPUFEATURES_H
#define CPUFEATURES_H
/**
 * @addtogroup hwmgmt_hw-caps  hwmgmt.hw-caps
 *
 * @{
 */

/**
 * @file
 * @brief Declaration of macros for CPU features in the hwmgmt.hw-caps
 *
 * This file declares all macros to index CPU features in the CPUID registers.
 *
 */

/* Intel-defined CPU features, CPUID level 0x00000001 (ECX)*/
/** @brief Index of the SSE3 feature, indicated by ECX bit 0 of CPUID level 0x00000001 */
#define X86_FEATURE_SSE3            ((FEAT_1_ECX << 5U) +  0U)
/** @brief Index of the PCLMUL feature, indicated by ECX bit 1 of CPUID level 0x00000001 */
#define X86_FEATURE_PCLMUL          ((FEAT_1_ECX << 5U) +  1U)
/** @brief Index of the DTES64 feature, indicated by ECX bit 2 of CPUID level 0x00000001 */
#define X86_FEATURE_DTES64          ((FEAT_1_ECX << 5U) +  2U)
/** @brief Index of the MONITOR feature, indicated by ECX bit 3 of CPUID level 0x00000001 */
#define X86_FEATURE_MONITOR         ((FEAT_1_ECX << 5U) +  3U)
/** @brief Index of the DS-CPL feature, indicated by ECX bit 4 of CPUID level 0x00000001 */
#define X86_FEATURE_DS_CPL          ((FEAT_1_ECX << 5U) +  4U)
/** @brief Index of the VMX feature, indicated by ECX bit 5 of CPUID level 0x00000001 */
#define X86_FEATURE_VMX             ((FEAT_1_ECX << 5U) +  5U)
/** @brief Index of the SMX feature, indicated by ECX bit 6 of CPUID level 0x00000001 */
#define X86_FEATURE_SMX             ((FEAT_1_ECX << 5U) +  6U)
/** @brief Index of the EST feature, indicated by ECX bit 7 of CPUID level 0x00000001 */
#define X86_FEATURE_EST             ((FEAT_1_ECX << 5U) +  7U)
/** @brief Index of the TM2 feature, indicated by ECX bit 8 of CPUID level 0x00000001 */
#define X86_FEATURE_TM2             ((FEAT_1_ECX << 5U) +  8U)
/** @brief Index of the SSSE3 feature, indicated by ECX bit 9 of CPUID level 0x00000001 */
#define X86_FEATURE_SSSE3           ((FEAT_1_ECX << 5U) +  9U)
/** @brief Index of the CID feature, indicated by ECX bit 10 of CPUID level 0x00000001 */
#define X86_FEATURE_CID             ((FEAT_1_ECX << 5U) + 10U)
/** @brief Index of the FMA feature, indicated by ECX bit 12 of CPUID level 0x00000001 */
#define X86_FEATURE_FMA             ((FEAT_1_ECX << 5U) + 12U)
/** @brief Index of the CX16 feature, indicated by ECX bit 13 of CPUID level 0x00000001 */
#define X86_FEATURE_CX16            ((FEAT_1_ECX << 5U) + 13U)
/** @brief Index of the ETPRD feature, indicated by ECX bit 14 of CPUID level 0x00000001 */
#define X86_FEATURE_ETPRD           ((FEAT_1_ECX << 5U) + 14U)
/** @brief Index of the PDCM feature, indicated by ECX bit 15 of CPUID level 0x00000001 */
#define X86_FEATURE_PDCM            ((FEAT_1_ECX << 5U) + 15U)
/** @brief Index of the PCID feature, indicated by ECX bit 17 of CPUID level 0x00000001 */
#define X86_FEATURE_PCID            ((FEAT_1_ECX << 5U) + 17U)
/** @brief Index of the DCA feature, indicated by ECX bit 18 of CPUID level 0x00000001 */
#define X86_FEATURE_DCA             ((FEAT_1_ECX << 5U) + 18U)
/** @brief Index of the SSE4.1 feature, indicated by ECX bit 19 of CPUID level 0x00000001 */
#define X86_FEATURE_SSE4_1          ((FEAT_1_ECX << 5U) + 19U)
/** @brief Index of the SSE4.2 feature, indicated by ECX bit 20 of CPUID level 0x00000001 */
#define X86_FEATURE_SSE4_2          ((FEAT_1_ECX << 5U) + 20U)
/** @brief Index of the X2APIC feature, indicated by ECX bit 21 of CPUID level 0x00000001 */
#define X86_FEATURE_X2APIC          ((FEAT_1_ECX << 5U) + 21U)
/** @brief Index of the MOVBE feature, indicated by ECX bit 22 of CPUID level 0x00000001 */
#define X86_FEATURE_MOVBE           ((FEAT_1_ECX << 5U) + 22U)
/** @brief Index of the POPCNT feature, indicated by ECX bit 23 of CPUID level 0x00000001 */
#define X86_FEATURE_POPCNT          ((FEAT_1_ECX << 5U) + 23U)
/** @brief Index of the TSC DEADLINE feature, indicated by ECX bit 24 of CPUID level 0x00000001 */
#define X86_FEATURE_TSC_DEADLINE    ((FEAT_1_ECX << 5U) + 24U)
/** @brief Index of the AES feature, indicated by ECX bit 25 of CPUID level 0x00000001 */
#define X86_FEATURE_AES             ((FEAT_1_ECX << 5U) + 25U)
/** @brief Index of the XSAVE feature, indicated by ECX bit 26 of CPUID level 0x00000001 */
#define X86_FEATURE_XSAVE           ((FEAT_1_ECX << 5U) + 26U)
/** @brief Index of the OSXSAVE feature, indicated by ECX bit 27 of CPUID level 0x00000001 */
#define X86_FEATURE_OSXSAVE         ((FEAT_1_ECX << 5U) + 27U)
/** @brief Index of the AVX feature, indicated by ECX bit 28 of CPUID level 0x00000001 */
#define X86_FEATURE_AVX             ((FEAT_1_ECX << 5U) + 28U)
/** @brief Index of the RDRAND feature, indicated by ECX bit 30 of CPUID level 0x00000001 */
#define X86_FEATURE_RDRAND          ((FEAT_1_ECX << 5U) + 30U)

/* Intel-defined CPU features, CPUID level 0x00000001 (EDX)*/
/** @brief Index of the FPU feature, indicated by EDX bit 0 of CPUID level 0x00000001 */
#define X86_FEATURE_FPU             ((FEAT_1_EDX << 5U) +  0U)
/** @brief Index of the VME feature, indicated by EDX bit 1 of CPUID level 0x00000001 */
#define X86_FEATURE_VME             ((FEAT_1_EDX << 5U) +  1U)
/** @brief Index of the DE feature, indicated by EDX bit 2 of CPUID level 0x00000001 */
#define X86_FEATURE_DE              ((FEAT_1_EDX << 5U) +  2U)
/** @brief Index of the PSE feature, indicated by EDX bit 3 of CPUID level 0x00000001 */
#define X86_FEATURE_PSE             ((FEAT_1_EDX << 5U) +  3U)
/** @brief Index of the TSC feature, indicated by EDX bit 4 of CPUID level 0x00000001 */
#define X86_FEATURE_TSC             ((FEAT_1_EDX << 5U) +  4U)
/** @brief Index of the MSR feature, indicated by EDX bit 5 of CPUID level 0x00000001 */
#define X86_FEATURE_MSR             ((FEAT_1_EDX << 5U) +  5U)
/** @brief Index of the PAE feature, indicated by EDX bit 6 of CPUID level 0x00000001 */
#define X86_FEATURE_PAE             ((FEAT_1_EDX << 5U) +  6U)
/** @brief Index of the MCE feature, indicated by EDX bit 7 of CPUID level 0x00000001 */
#define X86_FEATURE_MCE             ((FEAT_1_EDX << 5U) +  7U)
/** @brief Index of the CX8 feature, indicated by EDX bit 8 of CPUID level 0x00000001 */
#define X86_FEATURE_CX8             ((FEAT_1_EDX << 5U) +  8U)
/** @brief Index of the APIC feature, indicated by EDX bit 9 of CPUID level 0x00000001 */
#define X86_FEATURE_APIC            ((FEAT_1_EDX << 5U) +  9U)
/** @brief Index of the SEP feature, indicated by EDX bit 11 of CPUID level 0x00000001 */
#define X86_FEATURE_SEP             ((FEAT_1_EDX << 5U) + 11U)
/** @brief Index of the MTRR feature, indicated by EDX bit 12 of CPUID level 0x00000001 */
#define X86_FEATURE_MTRR            ((FEAT_1_EDX << 5U) + 12U)
/** @brief Index of the PGE feature, indicated by EDX bit 13 of CPUID level 0x00000001 */
#define X86_FEATURE_PGE             ((FEAT_1_EDX << 5U) + 13U)
/** @brief Index of the MCA feature, indicated by EDX bit 14 of CPUID level 0x00000001 */
#define X86_FEATURE_MCA             ((FEAT_1_EDX << 5U) + 14U)
/** @brief Index of the CMOV feature, indicated by EDX bit 15 of CPUID level 0x00000001 */
#define X86_FEATURE_CMOV            ((FEAT_1_EDX << 5U) + 15U)
/** @brief Index of the PAT feature, indicated by EDX bit 16 of CPUID level 0x00000001 */
#define X86_FEATURE_PAT             ((FEAT_1_EDX << 5U) + 16U)
/** @brief Index of the PSE36 feature, indicated by EDX bit 17 of CPUID level 0x00000001 */
#define X86_FEATURE_PSE36           ((FEAT_1_EDX << 5U) + 17U)
/** @brief Index of the PSN feature, indicated by EDX bit 18 of CPUID level 0x00000001 */
#define X86_FEATURE_PSN             ((FEAT_1_EDX << 5U) + 18U)
/** @brief Index of the CLF feature, indicated by EDX bit 19 of CPUID level 0x00000001 */
#define X86_FEATURE_CLF             ((FEAT_1_EDX << 5U) + 19U)
/** @brief Index of the DTES feature, indicated by EDX bit 21 of CPUID level 0x00000001 */
#define X86_FEATURE_DTES            ((FEAT_1_EDX << 5U) + 21U)
/** @brief Index of the ACPI feature, indicated by EDX bit 22 of CPUID level 0x00000001 */
#define X86_FEATURE_ACPI            ((FEAT_1_EDX << 5U) + 22U)
/** @brief Index of the MMX feature, indicated by EDX bit 23 of CPUID level 0x00000001 */
#define X86_FEATURE_MMX             ((FEAT_1_EDX << 5U) + 23U)
/** @brief Index of the FXSR feature, indicated by EDX bit 24 of CPUID level 0x00000001 */
#define X86_FEATURE_FXSR            ((FEAT_1_EDX << 5U) + 24U)
/** @brief Index of the SSE feature, indicated by EDX bit 25 of CPUID level 0x00000001 */
#define X86_FEATURE_SSE             ((FEAT_1_EDX << 5U) + 25U)
/** @brief Index of the SSE2 feature, indicated by EDX bit 26 of CPUID level 0x00000001 */
#define X86_FEATURE_SSE2            ((FEAT_1_EDX << 5U) + 26U)
/** @brief Index of the SS feature, indicated by EDX bit 27 of CPUID level 0x00000001 */
#define X86_FEATURE_SS              ((FEAT_1_EDX << 5U) + 27U)
/** @brief Index of the HTT feature, indicated by EDX bit 28 of CPUID level 0x00000001 */
#define X86_FEATURE_HTT             ((FEAT_1_EDX << 5U) + 28U)
/** @brief Index of the TM1 feature, indicated by EDX bit 29 of CPUID level 0x00000001 */
#define X86_FEATURE_TM1             ((FEAT_1_EDX << 5U) + 29U)
/** @brief Index of the IA64 feature, indicated by EDX bit 30 of CPUID level 0x00000001 */
#define X86_FEATURE_IA64            ((FEAT_1_EDX << 5U) + 30U)
/** @brief Index of the PBE feature, indicated by EDX bit 31 of CPUID level 0x00000001 */
#define X86_FEATURE_PBE             ((FEAT_1_EDX << 5U) + 31U)

/* Intel-defined CPU features, CPUID.(EAX=7H,ECX=0H) (EBX)*/
/** @brief Index of the TSC_ADJUST feature, indicated by EBX bit 1 of CPUID.(EAX=7H,ECX=0H) */
#define X86_FEATURE_TSC_ADJ         ((FEAT_7_0_EBX << 5U) +  1U)
/** @brief Index of the SGX feature, indicated by EBX bit 2 of CPUID.(EAX=7H,ECX=0H) */
#define X86_FEATURE_SGX             ((FEAT_7_0_EBX << 5U) +  2U)
/** @brief Index of the SMEP feature, indicated by EBX bit 7 of CPUID.(EAX=7H,ECX=0H) */
#define X86_FEATURE_SMEP            ((FEAT_7_0_EBX << 5U) +  7U)
/** @brief Index of the ERMS feature, indicated by EBX bit 9 of CPUID.(EAX=7H,ECX=0H) */
#define X86_FEATURE_ERMS            ((FEAT_7_0_EBX << 5U) +  9U)
/** @brief Index of the INVPCID feature, indicated by EBX bit 10 of CPUID.(EAX=7H,ECX=0H) */
#define X86_FEATURE_INVPCID         ((FEAT_7_0_EBX << 5U) + 10U)
/** @brief Index of the RDT_A feature, indicated by EBX bit 15 of CPUID.(EAX=7H,ECX=0H) */
#define X86_FEATURE_RDT_A           ((FEAT_7_0_EBX << 5U) + 15U)
/** @brief Index of the SMAP feature, indicated by EBX bit 20 of CPUID.(EAX=7H,ECX=0H) */
#define X86_FEATURE_SMAP            ((FEAT_7_0_EBX << 5U) + 20U)
/** @brief Index of the CLFLUSHOPT feature, indicated by EBX bit 23 of CPUID.(EAX=7H,ECX=0H) */
#define X86_FEATURE_CLFLUSHOPT      ((FEAT_7_0_EBX << 5U) + 23U)

/* Intel-defined CPU features, CPUID.(EAX=7H,ECX=0H) (ECX)*/
/** @brief Index of the WAITPKG feature, indicated by ECX bit 5 of CPUID.(EAX=7H,ECX=0H) */
#define X86_FEATURE_WAITPKG         ((FEAT_7_0_ECX << 5U) +  5U)
/** @brief Index of the KEYLOCKER feature, indicated by ECX bit 23 of CPUID.(EAX=7H,ECX=0H) */
#define X86_FEATURE_KEYLOCKER       ((FEAT_7_0_ECX << 5U) + 23U)

/* Intel-defined CPU features, CPUID.(EAX=7H,ECX=0H) (EDX)*/
/** @brief Index of the MD_CLEAR feature, indicated by EDX bit 10 of CPUID.(EAX=7H,ECX=0H) */
#define X86_FEATURE_MDS_CLEAR       ((FEAT_7_0_EDX << 5U) + 10U)
/** @brief Index of the HYBRID feature, indicated by EDX bit 15 of CPUID.(EAX=7H,ECX=0H) */
#define X86_FEATURE_HYBRID          ((FEAT_7_0_EDX << 5U) + 15U)
/** @brief Index of the IBRS and IBPB feature, indicated by EDX bit 26 of CPUID.(EAX=7H,ECX=0H) */
#define X86_FEATURE_IBRS_IBPB       ((FEAT_7_0_EDX << 5U) + 26U)
/** @brief Index of the STIBP feature, indicated by EDX bit 27 of CPUID.(EAX=7H,ECX=0H) */
#define X86_FEATURE_STIBP           ((FEAT_7_0_EDX << 5U) + 27U)
/** @brief Index of the L1D_FLUSH feature, indicated by EDX bit 28 of CPUID.(EAX=7H,ECX=0H) */
#define X86_FEATURE_L1D_FLUSH       ((FEAT_7_0_EDX << 5U) + 28U)
/** @brief Index of the ARCH_CAPABILITIES MSR feature, indicated by EDX bit 29 of CPUID.(EAX=7H,ECX=0H) */
#define X86_FEATURE_ARCH_CAP        ((FEAT_7_0_EDX << 5U) + 29U)
/** @brief Index of the CORE_CAPABILITIES MSR feature, indicated by EDX bit 30 of CPUID.(EAX=7H,ECX=0H) */
#define X86_FEATURE_CORE_CAP        ((FEAT_7_0_EDX << 5U) + 30U)
/** @brief Index of the SSBD feature, indicated by EDX bit 31 of CPUID.(EAX=7H,ECX=0H) */
#define X86_FEATURE_SSBD            ((FEAT_7_0_EDX << 5U) + 31U)

/* Intel-defined CPU features, CPUID.(EAX=7H,ECX=2H) (EDX)*/
/** @brief Index of the RRSBA_CTRL feature, indicated by EDX bit 2 of CPUID.(EAX=7H,ECX=2H) */
#define X86_FEATURE_RRSBA_CTRL      ((FEAT_7_2_EDX << 5U) + 2U)

/* Intel-defined CPU features, CPUID level 0x80000001 (EDX)*/
/** @brief Index of the NX feature, indicated by EDX bit 20 of CPUID level 0x80000001 */
#define X86_FEATURE_NX              ((FEAT_8000_0001_EDX << 5U) + 20U)
/** @brief Index of the PAGE1GB feature, indicated by EDX bit 26 of CPUID level 0x80000001 */
#define X86_FEATURE_PAGE1GB         ((FEAT_8000_0001_EDX << 5U) + 26U)
/** @brief Index of the LM feature, indicated by EDX bit 29 of CPUID level 0x80000001 */
#define X86_FEATURE_LM              ((FEAT_8000_0001_EDX << 5U) + 29U)

/* Intel-defined CPU features, CPUID level 0x80000007 (EDX)*/
/** @brief Index of the INVA_TSC feature, indicated by EDX bit 8 of CPUID level 0x80000007 */
#define X86_FEATURE_INVA_TSC        ((FEAT_8000_0007_EDX << 5U) + 8U)

/* Intel-defined CPU features, CPUID.(EAX=DH,ECX=1H) (EAX) */
/** @brief Index of the compaction extensions feature, indicated by EAX bit 1 of CPUID.(EAX=DH,ECX=1H) */
#define X86_FEATURE_COMPACTION_EXT  ((FEAT_D_1_EAX << 5U) + 1U)
/** @brief Index of the XSAVES feature, indicated by EAX bit 3 of CPUID.(EAX=DH,ECX=1H) */
#define X86_FEATURE_XSAVES          ((FEAT_D_1_EAX << 5U) + 3U)

/**
 * @}
 */
#endif /* CPUFEATURES_H */
