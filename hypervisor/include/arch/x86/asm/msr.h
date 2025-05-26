/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @addtogroup vp-base_vmsr
 *
 * @{
 */

#ifndef MSR_H
#define MSR_H

/**
 * @file
 *
 * @brief Define MSR encoding, bits and related macros
 */
#define MSR_IA32_P5_MC_ADDR			0x00000000U /**< Machine check address */
#define MSR_IA32_P5_MC_TYPE			0x00000001U /**< Machine check error type */
#define MSR_IA32_MONITOR_FILTER_SIZE		0x00000006U /**< System coherence line size for MWAIT/MONITOR */
#define MSR_IA32_TIME_STAMP_COUNTER		0x00000010U /**< Timer-Stamp Counter */
#define MSR_IA32_PLATFORM_ID			0x00000017U /**< Platform ID */
#define MSR_IA32_APIC_BASE			0x0000001BU /**< APIC base address */
#define MSR_IA32_USER_MSR_CTL			0x0000001CU /**< User Read/Write MSR Control */
#define MSR_IA32_BARRIER			0x0000002FU /**< Memory Ordering Barrier */
#define MSR_TEST_CTL				0x00000033U /**< Test control */
#define MSR_IA32_FEATURE_CONTROL		0x0000003AU /**< Feature control */
#define MSR_IA32_TSC_ADJUST			0x0000003BU /**< Per Logical Processor TSC Adjust */
#define MSR_IA32_SPEC_CTRL			0x00000048U /**< Speculation Control */
#define MSR_IA32_PRED_CMD			0x00000049U /**< Prediction Command */
#define MSR_IA32_BIOS_UPDT_TRIG 		0x00000079U /**< BIOS Update Trigger */
#define MSR_IA32_BIOS_SIGN_ID			0x0000008BU /**< BIOS Update Signature */
#define MSR_IA32_SGXLEPUBKEYHASH0		0x0000008CU /**< Bits 63:0 of the SHA256 digest of SGX Enclave */
#define MSR_IA32_SGXLEPUBKEYHASH1		0x0000008DU /**< Bits 127:64 of the SHA256 digest of SGX Enclave */
#define MSR_IA32_SGXLEPUBKEYHASH2		0x0000008EU /**< Bits 191:128 of the SHA256 digest of SGX Enclave */
#define MSR_IA32_SGXLEPUBKEYHASH3		0x0000008FU /**< Bits 255:192 of the SHA256 digest of SGX Enclave */
#define MSR_IA32_SMM_MONITOR_CTL		0x0000009BU /**< SMM Monitor Configuration */
#define MSR_IA32_SMBASE 			0x0000009EU /**< Base address of processor's SMRAM image */
#define MSR_IA32_XAPIC_DIS_STATUS		0x000000BDU /**< xAPIC Disable Status */
#define MSR_IA32_PMC0				0x000000C1U /**< General Performance Counter 0 */
#define MSR_IA32_PMC1				0x000000C2U /**< General Performance Counter 1 */
#define MSR_IA32_PMC2				0x000000C3U /**< General Performance Counter 2 */
#define MSR_IA32_PMC3				0x000000C4U /**< General Performance Counter 3 */
#define MSR_IA32_PMC4				0x000000C5U /**< General Performance Counter 4 */
#define MSR_IA32_PMC5				0x000000C6U /**< General Performance Counter 5 */
#define MSR_IA32_PMC6				0x000000C7U /**< General Performance Counter 6 */
#define MSR_IA32_PMC7				0x000000C8U /**< General Performance Counter 7 */
#define MSR_IA32_CORE_CAPABILITIES		0x000000CFU /**< IA32 Core Capabilities Register */
#define MSR_IA32_UMWAIT_CONTROL 		0x000000E1U /**< UMWAIT Control MSR */
#define MSR_IA32_MPERF				0x000000E7U /**< TSC Frequency Clock Counter */
#define MSR_IA32_APERF				0x000000E8U /**< Actual Performance Clock Counter */
#define MSR_IA32_MTRR_CAP			0x000000FEU /**< MTRR Capability */
#define MSR_IA32_ARCH_CAPABILITIES		0x0000010AU /**< Enumeration of Architectural Features */
#define MSR_IA32_FLUSH_CMD			0x0000010BU /**< Flush Command */
#define MSR_MISC_FEATURE_ENABLES		0x00000140U /**< MISC enables for thread specific features */
#define MSR_IA32_SYSENTER_CS			0x00000174U /**< CS for SYSENTER instruction */
#define MSR_IA32_SYSENTER_ESP			0x00000175U /**< ESP for SYSENTER instruction */
#define MSR_IA32_SYSENTER_EIP			0x00000176U /**< EIP for SYSENTER instruction */
#define MSR_IA32_MCG_CAP			0x00000179U /**< Global Machine Check Capability */
#define MSR_IA32_MCG_STATUS			0x0000017AU /**< Global Machine Check Status */
#define MSR_IA32_MCG_CTL			0x0000017BU /**< Global Machine Check Control */
#define MSR_IA32_PERFEVTSEL0			0x00000186U /**< Performance Event Select Register 0 */
#define MSR_IA32_PERFEVTSEL1			0x00000187U /**< Performance Event Select Register 1 */
#define MSR_IA32_PERFEVTSEL2			0x00000188U /**< Performance Event Select Register 2 */
#define MSR_IA32_PERFEVTSEL3			0x00000189U /**< Performance Event Select Register 3 */
#define MSR_IA32_PERFEVTSEL4			0x0000018AU /**< Performance Event Select Register 4 */
#define MSR_IA32_PERFEVTSEL5			0x0000018BU /**< Performance Event Select Register 5 */
#define MSR_IA32_PERFEVTSEL6			0x0000018CU /**< Performance Event Select Register 6 */
#define MSR_IA32_PERFEVTSEL7			0x0000018DU /**< Performance Event Select Register 7 */
#define MSR_IA32_PERF_STATUS			0x00000198U /**< Current Performance Status */
#define MSR_IA32_PERF_CTL			0x00000199U /**< Performance Control MSR */
#define MSR_IA32_CLOCK_MODULATION		0x0000019AU /**< Clock Modulation Control */
#define MSR_IA32_THERM_INTERRUPT		0x0000019BU /**< Thermal Interrupt Control */
#define MSR_IA32_THERM_STATUS			0x0000019CU /**< Thermal Status Information */
#define MSR_IA32_MISC_ENABLE			0x000001A0U /**< Enable Misc. Processor Features */
#define MSR_IA32_ENERGY_PERF_BIAS		0x000001B0U /**< Performance Energy Bias Hint */
#define MSR_IA32_PACKAGE_THERM_STATUS		0x000001B1U /**< Package Thermal Status Information */
#define MSR_IA32_PACKAGE_THERM_INTERRUPT	0x000001B2U /**< Pkg Thermal Interrupt Control */
#define MSR_IA32_DEBUGCTL			0x000001D9U /**< Trace/Profile Resource Control */
#define MSR_IA32_LER_FROM_LIP			0x000001DDU /**< Last Event Record Source IP Register */
#define MSR_IA32_LER_TO_LIP			0x000001DEU /**< Last Event Record Destination IP Register */
#define MSR_IA32_LER_INFO			0x000001E0U /**< Last Event Record Info Register */
#define MSR_IA32_SMRR_PHYSBASE			0x000001F2U /**< SMRR Base Address */
#define MSR_IA32_SMRR_PHYSMASK			0x000001F3U /**< SMRR Range Mask */
#define MSR_IA32_PLATFORM_DCA_CAP		0x000001F8U /**< DCA Capability */
#define MSR_IA32_CPU_DCA_CAP			0x000001F9U /**< CPU Prefetch-Hint type */
#define MSR_IA32_DCA_0_CAP			0x000001FAU /**< DCA type 0 Status and Control register */
#define MSR_IA32_MTRR_PHYSBASE_0		0x00000200U /**< MTRR setting of base address range0 */
#define MSR_IA32_MTRR_PHYSMASK_0		0x00000201U /**< MTRR range mask of range0 */
#define MSR_IA32_MTRR_PHYSBASE_1		0x00000202U /**< MTRR setting of base address range1 */
#define MSR_IA32_MTRR_PHYSMASK_1		0x00000203U /**< MTRR range mask of range1 */
#define MSR_IA32_MTRR_PHYSBASE_2		0x00000204U /**< MTRR setting of base address range2 */
#define MSR_IA32_MTRR_PHYSMASK_2		0x00000205U /**< MTRR range mask of range2 */
#define MSR_IA32_MTRR_PHYSBASE_3		0x00000206U /**< MTRR setting of base address range3 */
#define MSR_IA32_MTRR_PHYSMASK_3		0x00000207U /**< MTRR range mask of range3 */
#define MSR_IA32_MTRR_PHYSBASE_4		0x00000208U /**< MTRR setting of base address range4 */
#define MSR_IA32_MTRR_PHYSMASK_4		0x00000209U /**< MTRR range mask of range4 */
#define MSR_IA32_MTRR_PHYSBASE_5		0x0000020AU /**< MTRR setting of base address range5 */
#define MSR_IA32_MTRR_PHYSMASK_5		0x0000020BU /**< MTRR range mask of range5 */
#define MSR_IA32_MTRR_PHYSBASE_6		0x0000020CU /**< MTRR setting of base address range6 */
#define MSR_IA32_MTRR_PHYSMASK_6		0x0000020DU /**< MTRR range mask of range6 */
#define MSR_IA32_MTRR_PHYSBASE_7		0x0000020EU /**< MTRR setting of base address range7 */
#define MSR_IA32_MTRR_PHYSMASK_7		0x0000020FU /**< MTRR range mask of range7 */
#define MSR_IA32_MTRR_PHYSBASE_8		0x00000210U /**< MTRR setting of base address range8 */
#define MSR_IA32_MTRR_PHYSMASK_8		0x00000211U /**< MTRR range mask of range8 */
#define MSR_IA32_MTRR_PHYSBASE_9		0x00000212U /**< MTRR setting of base address range9 */
#define MSR_IA32_MTRR_PHYSMASK_9		0x00000213U /**< MTRR range mask of range9 */
#define MSR_IA32_MTRR_FIX64K_00000		0x00000250U /**< MTRR setting of fixed ranger 00000H ~ 7FFFFH */
#define MSR_IA32_MTRR_FIX16K_80000		0x00000258U /**< MTRR setting of fixed ranger 80000H ~ 9FFFFH */
#define MSR_IA32_MTRR_FIX16K_A0000		0x00000259U /**< MTRR setting of fixed ranger A0000H ~ BFFFFH */
#define MSR_IA32_MTRR_FIX4K_C0000		0x00000268U /**< MTRR setting of fixed ranger C0000H ~ C7FFFH */
#define MSR_IA32_MTRR_FIX4K_C8000		0x00000269U /**< MTRR setting of fixed ranger C8000H ~ CFFFFH */
#define MSR_IA32_MTRR_FIX4K_D0000		0x0000026AU /**< MTRR setting of fixed ranger D0000H ~ D7FFFH */
#define MSR_IA32_MTRR_FIX4K_D8000		0x0000026BU /**< MTRR setting of fixed ranger D8000H ~ DFFFFH */
#define MSR_IA32_MTRR_FIX4K_E0000		0x0000026CU /**< MTRR setting of fixed ranger E0000H ~ E7FFFH */
#define MSR_IA32_MTRR_FIX4K_E8000		0x0000026DU /**< MTRR setting of fixed ranger E8000H ~ EFFFFH */
#define MSR_IA32_MTRR_FIX4K_F0000		0x0000026EU /**< MTRR setting of fixed ranger F0000H ~ F7FFFH */
#define MSR_IA32_MTRR_FIX4K_F8000		0x0000026FU /**< MTRR setting of fixed ranger F8000H ~ FFFFFH */
#define MSR_IA32_PAT				0x00000277U /**< Page attribute table MSR */
#define MSR_IA32_MC0_CTL2			0x00000280U /**< MSR to enable/disable CMCI capability for bank 0 */
#define MSR_IA32_MC1_CTL2			0x00000281U /**< MSR to enable/disable CMCI capability for bank 1 */
#define MSR_IA32_MC2_CTL2			0x00000282U /**< MSR to enable/disable CMCI capability for bank 2 */
#define MSR_IA32_MC3_CTL2			0x00000283U /**< MSR to enable/disable CMCI capability for bank 3 */
#define MSR_IA32_MC4_CTL2			0x00000284U /**< MSR to enable/disable CMCI capability for bank 4 */
#define MSR_IA32_MC5_CTL2			0x00000285U /**< MSR to enable/disable CMCI capability for bank 5 */
#define MSR_IA32_MC6_CTL2			0x00000286U /**< MSR to enable/disable CMCI capability for bank 6 */
#define MSR_IA32_MC7_CTL2			0x00000287U /**< MSR to enable/disable CMCI capability for bank 7 */
#define MSR_IA32_MC8_CTL2			0x00000288U /**< MSR to enable/disable CMCI capability for bank 8 */
#define MSR_IA32_MC9_CTL2			0x00000289U /**< MSR to enable/disable CMCI capability for bank 9 */
#define MSR_IA32_MC10_CTL2			0x0000028AU /**< MSR to enable/disable CMCI capability for bank 10 */
#define MSR_IA32_MC11_CTL2			0x0000028BU /**< MSR to enable/disable CMCI capability for bank 11 */
#define MSR_IA32_MC12_CTL2			0x0000028CU /**< MSR to enable/disable CMCI capability for bank 12 */
#define MSR_IA32_MC13_CTL2			0x0000028DU /**< MSR to enable/disable CMCI capability for bank 13 */
#define MSR_IA32_MC14_CTL2			0x0000028EU /**< MSR to enable/disable CMCI capability for bank 14 */
#define MSR_IA32_MC15_CTL2			0x0000028FU /**< MSR to enable/disable CMCI capability for bank 15 */
#define MSR_IA32_MC16_CTL2			0x00000290U /**< MSR to enable/disable CMCI capability for bank 16 */
#define MSR_IA32_MC17_CTL2			0x00000291U /**< MSR to enable/disable CMCI capability for bank 17 */
#define MSR_IA32_MC18_CTL2			0x00000292U /**< MSR to enable/disable CMCI capability for bank 18 */
#define MSR_IA32_MC19_CTL2			0x00000293U /**< MSR to enable/disable CMCI capability for bank 19 */
#define MSR_IA32_MC20_CTL2			0x00000294U /**< MSR to enable/disable CMCI capability for bank 20 */
#define MSR_IA32_MC21_CTL2			0x00000295U /**< MSR to enable/disable CMCI capability for bank 21 */
#define MSR_IA32_MC31_CTL2			0x0000029FU /**< MSR to enable/disable CMCI capability for bank 31 */
#define MSR_IA32_MTRR_DEF_TYPE			0x000002FFU /**< MTRR Memory Type Range MSR */
#define MSR_SGXOWNEREPOCH0			0x00000300U /**< Lower 64 Bit CR_SGXOWNEREPOCH */
#define MSR_SGXOWNEREPOCH1			0x00000301U /**< Upper 64 Bit CR_SGXOWNEREPOCH */
#define MSR_IA32_FIXED_CTR0			0x00000309U /**< Fixed-Function Performance Counter Register 0 */
#define MSR_IA32_FIXED_CTR1			0x0000030AU /**< Fixed-Function Performance Counter Register 1 */
#define MSR_IA32_FIXED_CTR2			0x0000030BU /**< Fixed-Function Performance Counter Register 2 */
#define MSR_IA32_PERF_CAPABILITIES		0x00000345U /**< Enumeration of performance monitoring features */
#define MSR_IA32_FIXED_CTR_CTL			0x0000038DU /**< Fixed-Function Performance Counter Control */
#define MSR_IA32_PERF_GLOBAL_STATUS		0x0000038EU /**< Global Performance Counter Status */
#define MSR_IA32_PERF_GLOBAL_CTRL		0x0000038FU /**< Global Performance Counter Control */
#define MSR_IA32_PERF_GLOBAL_OVF_CTRL		0x00000390U /**< Global Performance Counter Overflow Control */
#define MSR_IA32_PERF_GLOBAL_STATUS_SET 	0x00000391U /**< Global Performance Counter Overflow Set Control */
#define MSR_IA32_PERF_GLOBAL_INUSE		0x00000392U /**< Indicator that core perfmon interface is in use */
#define MSR_IA32_PEBS_ENABLE			0x000003F1U /**< PEBS Control MSR */
#define MSR_IA32_MC0_CTL			0x00000400U /**< Machine-check control of bank 0 */
#define MSR_IA32_MC0_STATUS			0x00000401U /**< Machine-check status of bank 0 */
#define MSR_IA32_MC0_ADDR			0x00000402U /**< Machine-check address of bank 0 */
#define MSR_IA32_MC0_MISC			0x00000403U /**< Machine-check MISC information of bank 0 */
#define MSR_IA32_MC1_CTL			0x00000404U /**< Machine-check control of bank 1 */
#define MSR_IA32_MC1_STATUS			0x00000405U /**< Machine-check status of bank 1 */
#define MSR_IA32_MC1_ADDR			0x00000406U /**< Machine-check address of bank 1 */
#define MSR_IA32_MC1_MISC			0x00000407U /**< Machine-check MISC information of bank 1 */
#define MSR_IA32_MC2_CTL			0x00000408U /**< Machine-check control of bank 2 */
#define MSR_IA32_MC2_STATUS			0x00000409U /**< Machine-check status of bank 2 */
#define MSR_IA32_MC2_ADDR			0x0000040AU /**< Machine-check address of bank 2 */
#define MSR_IA32_MC2_MISC			0x0000040BU /**< Machine-check MISC information of bank 2 */
#define MSR_IA32_MC3_CTL			0x0000040CU /**< Machine-check control of bank 3 */
#define MSR_IA32_MC3_STATUS			0x0000040DU /**< Machine-check status of bank 3 */
#define MSR_IA32_MC3_ADDR			0x0000040EU /**< Machine-check address of bank 3 */
#define MSR_IA32_MC3_MISC			0x0000040FU /**< Machine-check MISC information of bank 3 */
#define MSR_IA32_MC4_CTL			0x00000410U /**< Machine-check control of bank 4 */
#define MSR_IA32_MC4_STATUS			0x00000411U /**< Machine-check status of bank 4 */
#define MSR_IA32_MC4_ADDR			0x00000412U /**< Machine-check address of bank 4 */
#define MSR_IA32_MC4_MISC			0x00000413U /**< Machine-check MISC information of bank 4 */
#define MSR_IA32_MC5_CTL			0x00000414U /**< Machine-check control of bank 5 */
#define MSR_IA32_MC5_STATUS			0x00000415U /**< Machine-check status of bank 5 */
#define MSR_IA32_MC5_ADDR			0x00000416U /**< Machine-check address of bank 5 */
#define MSR_IA32_MC5_MISC			0x00000417U /**< Machine-check MISC information of bank 5 */
#define MSR_IA32_MC6_CTL			0x00000418U /**< Machine-check control of bank 6 */
#define MSR_IA32_MC6_STATUS			0x00000419U /**< Machine-check status of bank 6 */
#define MSR_IA32_MC6_ADDR			0x0000041AU /**< Machine-check address of bank 6 */
#define MSR_IA32_MC6_MISC			0x0000041BU /**< Machine-check MISC information of bank 6 */
#define MSR_IA32_MC7_CTL			0x0000041CU /**< Machine-check control of bank 7 */
#define MSR_IA32_MC7_STATUS			0x0000041DU /**< Machine-check status of bank 7 */
#define MSR_IA32_MC7_ADDR			0x0000041EU /**< Machine-check address of bank 7 */
#define MSR_IA32_MC7_MISC			0x0000041FU /**< Machine-check MISC information of bank 7 */
#define MSR_IA32_MC8_CTL			0x00000420U /**< Machine-check control of bank 8 */
#define MSR_IA32_MC8_STATUS			0x00000421U /**< Machine-check status of bank 8 */
#define MSR_IA32_MC8_ADDR			0x00000422U /**< Machine-check address of bank 8 */
#define MSR_IA32_MC8_MISC			0x00000423U /**< Machine-check MISC information of bank 8 */
#define MSR_IA32_MC9_CTL			0x00000424U /**< Machine-check control of bank 9 */
#define MSR_IA32_MC9_STATUS			0x00000425U /**< Machine-check status of bank 9 */
#define MSR_IA32_MC9_ADDR			0x00000426U /**< Machine-check address of bank 9 */
#define MSR_IA32_MC9_MISC			0x00000427U /**< Machine-check MISC information of bank 9 */
#define MSR_IA32_MC10_CTL			0x00000428U /**< Machine-check control of bank 10 */
#define MSR_IA32_MC10_STATUS			0x00000429U /**< Machine-check status of bank 10 */
#define MSR_IA32_MC10_ADDR			0x0000042AU /**< Machine-check address of bank 10 */
#define MSR_IA32_MC10_MISC			0x0000042BU /**< Machine-check MISC information of bank 10 */
#define MSR_IA32_MC11_CTL			0x0000042CU /**< Machine-check control of bank 11 */
#define MSR_IA32_MC11_STATUS			0x0000042DU /**< Machine-check status of bank 11 */
#define MSR_IA32_MC11_ADDR			0x0000042EU /**< Machine-check address of bank 11 */
#define MSR_IA32_MC11_MISC			0x0000042FU /**< Machine-check MISC information of bank 11 */
#define MSR_IA32_MC12_CTL			0x00000430U /**< Machine-check control of bank 12 */
#define MSR_IA32_MC12_STATUS			0x00000431U /**< Machine-check status of bank 12 */
#define MSR_IA32_MC12_ADDR			0x00000432U /**< Machine-check address of bank 12 */
#define MSR_IA32_MC12_MISC			0x00000433U /**< Machine-check MISC information of bank 12 */
#define MSR_IA32_MC13_CTL			0x00000434U /**< Machine-check control of bank 13 */
#define MSR_IA32_MC13_STATUS			0x00000435U /**< Machine-check status of bank 13 */
#define MSR_IA32_MC13_ADDR			0x00000436U /**< Machine-check address of bank 13 */
#define MSR_IA32_MC13_MISC			0x00000437U /**< Machine-check MISC information of bank 13 */
#define MSR_IA32_MC14_CTL			0x00000438U /**< Machine-check control of bank 14 */
#define MSR_IA32_MC14_STATUS			0x00000439U /**< Machine-check status of bank 14 */
#define MSR_IA32_MC14_ADDR			0x0000043AU /**< Machine-check address of bank 14 */
#define MSR_IA32_MC14_MISC			0x0000043BU /**< Machine-check MISC information of bank 14 */
#define MSR_IA32_MC15_CTL			0x0000043CU /**< Machine-check control of bank 15 */
#define MSR_IA32_MC15_STATUS			0x0000043DU /**< Machine-check status of bank 15 */
#define MSR_IA32_MC15_ADDR			0x0000043EU /**< Machine-check address of bank 15 */
#define MSR_IA32_MC15_MISC			0x0000043FU /**< Machine-check MISC information of bank 15 */
#define MSR_IA32_MC16_CTL			0x00000440U /**< Machine-check control of bank 16 */
#define MSR_IA32_MC16_STATUS			0x00000441U /**< Machine-check status of bank 16 */
#define MSR_IA32_MC16_ADDR			0x00000442U /**< Machine-check address of bank 16 */
#define MSR_IA32_MC16_MISC			0x00000443U /**< Machine-check MISC information of bank 16 */
#define MSR_IA32_MC17_CTL			0x00000444U /**< Machine-check control of bank 17 */
#define MSR_IA32_MC17_STATUS			0x00000445U /**< Machine-check status of bank 17 */
#define MSR_IA32_MC17_ADDR			0x00000446U /**< Machine-check address of bank 17 */
#define MSR_IA32_MC17_MISC			0x00000447U /**< Machine-check MISC information of bank 17 */
#define MSR_IA32_MC18_CTL			0x00000448U /**< Machine-check control of bank 18 */
#define MSR_IA32_MC18_STATUS			0x00000449U /**< Machine-check status of bank 18 */
#define MSR_IA32_MC18_ADDR			0x0000044AU /**< Machine-check address of bank 18 */
#define MSR_IA32_MC18_MISC			0x0000044BU /**< Machine-check MISC information of bank 18 */
#define MSR_IA32_MC19_CTL			0x0000044CU /**< Machine-check control of bank 19 */
#define MSR_IA32_MC19_STATUS			0x0000044DU /**< Machine-check status of bank 19 */
#define MSR_IA32_MC19_ADDR			0x0000044EU /**< Machine-check address of bank 19 */
#define MSR_IA32_MC19_MISC			0x0000044FU /**< Machine-check MISC information of bank 19 */
#define MSR_IA32_MC20_CTL			0x00000450U /**< Machine-check control of bank 20 */
#define MSR_IA32_MC20_STATUS			0x00000451U /**< Machine-check status of bank 20 */
#define MSR_IA32_MC20_ADDR			0x00000452U /**< Machine-check address of bank 20 */
#define MSR_IA32_MC20_MISC			0x00000453U /**< Machine-check MISC information of bank 20 */
#define MSR_IA32_MC21_CTL			0x00000454U /**< Machine-check control of bank 21 */
#define MSR_IA32_MC21_STATUS			0x00000455U /**< Machine-check status of bank 21 */
#define MSR_IA32_MC21_ADDR			0x00000456U /**< Machine-check address of bank 21 */
#define MSR_IA32_MC21_MISC			0x00000457U /**< Machine-check MISC information of bank 21 */
#define MSR_IA32_MC28_CTL			0x00000470U /**< Machine-check control of bank 28 */
#define MSR_IA32_MC28_STATUS			0x00000471U /**< Machine-check status of bank 28 */
#define MSR_IA32_MC28_ADDR			0x00000472U /**< Machine-check address of bank 28 */
#define MSR_IA32_MC28_MISC			0x00000473U /**< Machine-check MISC information of bank 28 */
#define MSR_IA32_VMX_BASIC			0x00000480U /**< Basic VMX Capabilities reporting */
#define MSR_IA32_VMX_PINBASED_CTLS		0x00000481U /**< Pin-Based VM-Execution Controls reporting */
#define MSR_IA32_VMX_PROCBASED_CTLS		0x00000482U /**< Primary Processor-Based VM-Execution Controls */
#define MSR_IA32_VMX_EXIT_CTLS			0x00000483U /**< Capability Reporting of Primary VM-Exit Controls */
#define MSR_IA32_VMX_ENTRY_CTLS 		0x00000484U /**< Capability Reporting of VM-Entry Controls */
#define MSR_IA32_VMX_MISC			0x00000485U /**< Miscellaneous VMX Capabilities reporting */
#define MSR_IA32_VMX_CR0_FIXED0 		0x00000486U /**< Capability Reporting of CR0 Bits Fixed to 0 */
#define MSR_IA32_VMX_CR0_FIXED1 		0x00000487U /**< Capability Reporting of CR0 Bits Fixed to 1 */
#define MSR_IA32_VMX_CR4_FIXED0 		0x00000488U /**< Capability Reporting of CR4 Bits Fixed to 0 */
#define MSR_IA32_VMX_CR4_FIXED1 		0x00000489U /**< Capability Reporting of CR4 Bits Fixed to 1 */
#define MSR_IA32_VMX_VMCS_ENUM			0x0000048AU /**< Capability Reporting of VMCS Field Enumeration */
#define MSR_IA32_VMX_PROCBASED_CTLS2		0x0000048BU /**< Secondary Processor-Based VM-Execution Controls */
#define MSR_IA32_VMX_EPT_VPID_CAP		0x0000048CU /**< Capability Reporting Register of EPT and VPID */
#define MSR_IA32_VMX_TRUE_PINBASED_CTLS 	0x0000048DU /**< Pin-Based VM-Execution Flex Controls reporting */
#define MSR_IA32_VMX_TRUE_PROCBASED_CTLS	0x0000048EU /**< Primary Processor-Based VM-Execution Flex Controls */
#define MSR_IA32_VMX_TRUE_EXIT_CTLS		0x0000048FU /**< Capability Reporting of VM-Exit Flex Controls */
#define MSR_IA32_VMX_TRUE_ENTRY_CTLS		0x00000490U /**< Capability Reporting of VM-Entry Flex Controls */
#define MSR_IA32_VMX_VMFUNC			0x00000491U /**< Capability Reporting of VM-Function Controls */
#define MSR_IA32_VMX_PROCBASED_CTLS3		0x00000492U /**< Tertiary Processor-Based VM-Execution Controls */
#define MSR_IA32_VMX_EXIT_CTLS2 		0x00000493U /**< Capability Reporting Secondary VM-Exit Controls */
#define MSR_IA32_A_PMC0 			0x000004C1U /**< Full Width Writable IA32_PMC0 Alias */
#define MSR_IA32_A_PMC1 			0x000004C2U /**< Full Width Writable IA32_PMC1 Alias */
#define MSR_IA32_A_PMC2 			0x000004C3U /**< Full Width Writable IA32_PMC2 Alias */
#define MSR_IA32_A_PMC3 			0x000004C4U /**< Full Width Writable IA32_PMC3 Alias */
#define MSR_IA32_A_PMC4 			0x000004C5U /**< Full Width Writable IA32_PMC4 Alias */
#define MSR_IA32_A_PMC5 			0x000004C6U /**< Full Width Writable IA32_PMC5 Alias */
#define MSR_IA32_A_PMC6 			0x000004C7U /**< Full Width Writable IA32_PMC6 Alias */
#define MSR_IA32_A_PMC7 			0x000004C8U /**< Full Width Writable IA32_PMC7 Alias */
#define MSR_IA32_MCG_EXT_CTL			0x000004D0U /**< Signal MCEs to a one logical processor control */
#define MSR_IA32_SGX_SVN_STATUS 		0x00000500U /**< Status and SVN Threshold of SGX Support for ACM */
#define MSR_IA32_RTIT_OUTPUT_BASE		0x00000560U /**< Trace Output Base Register */
#define MSR_IA32_RTIT_OUTPUT_MASK_PTRS		0x00000561U /**< Trace Output Mask Pointers Register */
#define MSR_IA32_RTIT_CTL			0x00000570U /**< Trace Control Register */
#define MSR_IA32_RTIT_STATUS			0x00000571U /**< Tracing Status Register */
#define MSR_IA32_RTIT_CR3_MATCH 		0x00000572U /**< Trace Filter CR3 Match Register */
#define MSR_IA32_RTIT_ADDR0_A			0x00000580U /**< Region 0 Start Address */
#define MSR_IA32_RTIT_ADDR0_B			0x00000581U /**< Region 0 End Address */
#define MSR_IA32_RTIT_ADDR1_A			0x00000582U /**< Region 1 Start Address */
#define MSR_IA32_RTIT_ADDR1_B			0x00000583U /**< Region 1 End Address */
#define MSR_IA32_RTIT_ADDR2_A			0x00000584U /**< Region 2 Start Address */
#define MSR_IA32_RTIT_ADDR2_B			0x00000585U /**< Region 2 End Address */
#define MSR_IA32_RTIT_ADDR3_A			0x00000586U /**< Region 3 Start Address */
#define MSR_IA32_RTIT_ADDR3_B			0x00000587U /**< Region 3 End Address */
#define MSR_IA32_DS_AREA			0x00000600U /**< DS Save Area */
#define MSR_IA32_U_CET				0x000006A0U /**< Configure User Mode CET */
#define MSR_IA32_S_CET				0x000006A2U /**< Configure Supervisor Mode CET */
#define MSR_IA32_PL0_SSP			0x000006A4U /**< Address to be loaded on transition to CPL 0 */
#define MSR_IA32_PL1_SSP			0x000006A5U /**< Address to be loaded on transition to CPL 1 */
#define MSR_IA32_PL2_SSP			0x000006A6U /**< Address to be loaded on transition to CPL 2 */
#define MSR_IA32_PL3_SSP			0x000006A7U /**< Address to be loaded on transition to CPL 3 */
#define MSR_IA32_INTERRUPT_SSP_TABLE_ADDR	0x000006A8U /**< Address of shadow stack pointer table */
#define MSR_IA32_TSC_DEADLINE			0x000006E0U /**< TSC Target of Local APIC's TSC Deadline Mode */
#define MSR_IA32_PKRS				0x000006E1U /**< PK permissions associated with supervisor pages */
#define MSR_IA32_PM_ENABLE			0x00000770U /**< Enable/disable HWP */
#define MSR_IA32_HWP_CAPABILITIES		0x00000771U /**< HWP Performance Range Enumeration */
#define MSR_IA32_HWP_REQUEST_PKG		0x00000772U /**< Power Management Control Hints for one package */
#define MSR_IA32_HWP_INTERRUPT			0x00000773U /**< Control HWP Native Interrupts */
#define MSR_IA32_HWP_REQUEST			0x00000774U /**< Power Management Control Hints to one Processor */
#define MSR_IA32_HWP_PECI_REQUEST_INFO		0x00000775U /**< HWP Request settings via the PECI mechanism */
#define MSR_IA32_HWP_CTL			0x00000776U /**< HWP Control register */
#define MSR_IA32_HWP_STATUS			0x00000777U /**< HWP Status register */
#define MSR_IA32_EXT_XAPICID			0x00000802U /**< x2APIC ID Register */
#define MSR_IA32_EXT_APIC_VERSION		0x00000803U /**< x2APIC Version Register */
#define MSR_IA32_EXT_APIC_TPR			0x00000808U /**< x2APIC Task Priority Register */
#define MSR_IA32_EXT_APIC_PPR			0x0000080AU /**< x2APIC Processor Priority Register */
#define MSR_IA32_EXT_APIC_EOI			0x0000080BU /**< x2APIC EOI Register */
#define MSR_IA32_EXT_APIC_LDR			0x0000080DU /**< x2APIC Logical Destination Register */
#define MSR_IA32_EXT_APIC_SIVR			0x0000080FU /**< x2APIC Spurious Interrupt Vector Register */
#define MSR_IA32_EXT_APIC_ISR0			0x00000810U /**< x2APIC In-Service Register Bits 31:0 */
#define MSR_IA32_EXT_APIC_ISR1			0x00000811U /**< x2APIC In-Service Register Bits 63:32 */
#define MSR_IA32_EXT_APIC_ISR2			0x00000812U /**< x2APIC In-Service Register Bits 95:64 */
#define MSR_IA32_EXT_APIC_ISR3			0x00000813U /**< x2APIC In-Service Register Bits 127:96 */
#define MSR_IA32_EXT_APIC_ISR4			0x00000814U /**< x2APIC In-Service Register Bits 159:128 */
#define MSR_IA32_EXT_APIC_ISR5			0x00000815U /**< x2APIC In-Service Register Bits 191:160 */
#define MSR_IA32_EXT_APIC_ISR6			0x00000816U /**< x2APIC In-Service Register Bits 223:192 */
#define MSR_IA32_EXT_APIC_ISR7			0x00000817U /**< x2APIC In-Service Register Bits 255:224 */
#define MSR_IA32_EXT_APIC_TMR0			0x00000818U /**< x2APIC Trigger Mode Register Bits 31:0 */
#define MSR_IA32_EXT_APIC_TMR1			0x00000819U /**< x2APIC Trigger Mode Register Bits 63:32 */
#define MSR_IA32_EXT_APIC_TMR2			0x0000081AU /**< x2APIC Trigger Mode Register Bits 95:64 */
#define MSR_IA32_EXT_APIC_TMR3			0x0000081BU /**< x2APIC Trigger Mode Register Bits 127:96 */
#define MSR_IA32_EXT_APIC_TMR4			0x0000081CU /**< x2APIC Trigger Mode Register Bits 159:128 */
#define MSR_IA32_EXT_APIC_TMR5			0x0000081DU /**< x2APIC Trigger Mode Register Bits 191:160 */
#define MSR_IA32_EXT_APIC_TMR6			0x0000081EU /**< x2APIC Trigger Mode Register Bits 223:192 */
#define MSR_IA32_EXT_APIC_TMR7			0x0000081FU /**< x2APIC Trigger Mode Register Bits 255:224 */
#define MSR_IA32_EXT_APIC_IRR0			0x00000820U /**< x2APIC Interrupt Request Register Bits 31:0 */
#define MSR_IA32_EXT_APIC_IRR1			0x00000821U /**< x2APIC Interrupt Request Register Bits 63:32 */
#define MSR_IA32_EXT_APIC_IRR2			0x00000822U /**< x2APIC Interrupt Request Register Bits 95:64 */
#define MSR_IA32_EXT_APIC_IRR3			0x00000823U /**< x2APIC Interrupt Request Register Bits 127:96 */
#define MSR_IA32_EXT_APIC_IRR4			0x00000824U /**< x2APIC Interrupt Request Register Bits 159:128 */
#define MSR_IA32_EXT_APIC_IRR5			0x00000825U /**< x2APIC Interrupt Request Register Bits 191:160 */
#define MSR_IA32_EXT_APIC_IRR6			0x00000826U /**< x2APIC Interrupt Request Register Bits 223:192 */
#define MSR_IA32_EXT_APIC_IRR7			0x00000827U /**< x2APIC Interrupt Request Register Bits 255:224 */
#define MSR_IA32_EXT_APIC_ESR			0x00000828U /**< x2APIC Error Status Register */
#define MSR_IA32_EXT_APIC_LVT_CMCI		0x0000082FU /**< x2APIC LVT Corrected MC Interrupt Register */
#define MSR_IA32_EXT_APIC_ICR			0x00000830U /**< x2APIC Interrupt Command Register */
#define MSR_IA32_EXT_APIC_LVT_TIMER		0x00000832U /**< x2APIC LVT Timer Interrupt Register */
#define MSR_IA32_EXT_APIC_LVT_THERMAL		0x00000833U /**< x2APIC LVT Thermal Sensor Interrupt Register */
#define MSR_IA32_EXT_APIC_LVT_PMI		0x00000834U /**< x2APIC LVT Performance Monitor Interrupt Register */
#define MSR_IA32_EXT_APIC_LVT_LINT0		0x00000835U /**< x2APIC LVT LINT0 Register */
#define MSR_IA32_EXT_APIC_LVT_LINT1		0x00000836U /**< x2APIC LVT LINT1 Register */
#define MSR_IA32_EXT_APIC_LVT_ERROR		0x00000837U /**< x2APIC LVT Error Register */
#define MSR_IA32_EXT_APIC_INIT_COUNT		0x00000838U /**< x2APIC Initial Count Register */
#define MSR_IA32_EXT_APIC_CUR_COUNT		0x00000839U /**< x2APIC Current Count Register */
#define MSR_IA32_EXT_APIC_DIV_CONF		0x0000083EU /**< x2APIC Divide Configuration Register */
#define MSR_IA32_EXT_APIC_SELF_IPI		0x0000083FU /**< x2APIC Self IPI Register */
#define MSR_IA32_DEBUG_INTERFACE		0x00000C80U /**< Silicon Debug Feature Control Register */
#define MSR_IA32_L3_QOS_CFG			0x00000C81U /**< L3 QOS Configuration */
#define MSR_IA32_L2_QOS_CFG			0x00000C82U /**< L2 QOS Configuration */
#define MSR_IA32_QM_EVTSEL			0x00000C8DU /**< Monitoring Event Select Register */
#define MSR_IA32_QM_CTR 			0x00000C8EU /**< Monitoring Counter Register */
#define MSR_IA32_PQR_ASSOC			0x00000C8FU /**< Resource Association Register */
#define MSR_IA32_L3_MASK_BASE			0x00000C90U /**< L3 CAT Mask for COS0 */
#define MSR_IA32_XSS				0x00000DA0U /**< Extended Supervisor State Mask */
#define MSR_IA32_PKG_HDC_CTL			0x00000DB0U /**< Package Level Enable/Disable HDC */
#define MSR_IA32_PM_CTL1			0x00000DB1U /**< Enable/Disable the HDC Thread Level Activity */
#define MSR_IA32_THREAD_STALL			0x00000DB2U /**< Per-Logical_Processor_ID HDC Idle Residency */
#define MSR_IA32_L2_MASK_BASE			0x00000D10U /**< L2 Class Of Service Mask */
#define MSR_IA32_MBA_MASK_BASE			0x00000D50U /**< Memory Bandwidth Allocation throttling base MSR */
#define MSR_IA32_BNDCFGS			0x00000D90U /**< Supervisor State of MPX Configuration */
#define MSR_IA32_COPY_LOCAL_TO_PLATFORM 	0x00000D91U /**< Copy Local State to Platform State */
#define MSR_IA32_COPY_PLATFORM_TO_LOCAL 	0x00000D92U /**< Copy Platform State to Local State */
#define MSR_IA32_COPY_STATUS			0x00000990U /**< Status of Local or Local to Platform Copies */
#define MSR_IA32_IWKEY_BACKUP_STATUS		0x00000991U /**< Information about IWKeyBackup Register */
#define MSR_IA32_LBR_0_INFO			0x00001200U /**< Last Branch Record 0 Additional Information */
#define MSR_IA32_LBR_31_INFO			0x0000121FU /**< Last Branch Record 31 Additional Information */
#define MSR_IA32_LBR_CTL			0x000014CEU /**< Last Branch Record Enabling and Control Register */
#define MSR_IA32_LBR_DEPTH			0x000014CFU /**< Last Branch Record Maximum Stack Depth Register */
#define MSR_IA32_LBR_0_FROM_IP			0x00001500U /**< Last Branch Record entry 0 source IP register */
#define MSR_IA32_LBR_31_FROM_IP 		0x0000151FU /**< Last Branch Record entry 31 source IP register */
#define MSR_IA32_LBR_0_TO_IP			0x00001600U /**< Last Branch Record Entry 0 Destination IP Register */
#define MSR_IA32_LBR_31_TO_IP			0x0000161FU /**< Last Branch Record Entry 31 Destination IP Register */
#define MSR_IA32_EFER				0xC0000080U /**< Extended Feature Enables MSR */
#define MSR_IA32_STAR				0xC0000081U /**< System Call Target Address */
#define MSR_IA32_LSTAR				0xC0000082U /**< IA-32e Mode System Call Target Address */
#define MSR_IA32_CSTAR				0xC0000083U /**< IA-32e Mode System Call Target Address */
#define MSR_IA32_FMASK				0xC0000084U /**< System Call Flag Mask */
#define MSR_IA32_FS_BASE			0xC0000100U /**< Map of BASE Address of FS */
#define MSR_IA32_GS_BASE			0xC0000101U /**< Map of BASE Address of GS */
#define MSR_IA32_KERNEL_GS_BASE 		0xC0000102U /**< Swap Target of BASE Address of GS */
#define MSR_IA32_TSC_AUX			0xC0000103U /**< Auxiliary TSC */

/* non-architectural MSRs */
#define MSR_EBL_CR_POWERON			0x0000002AU /**< Processor Hard Power-On Configuration */
#define MSR_EBC_SOFT_POWERON			0x0000002BU /**< Processor Soft Power-On Configuration */
#define MSR_EBC_FREQUENCY_ID			0x0000002CU /**< Processor Frequency Configuration */
#define MSR_SMI_COUNT				0x00000034U /**< SMI Counter */
#define MSR_CORE_THREAD_COUNT			0x00000035U /**< State of Enabled Core and Processor Count */
#define MSR_LASTBRANCH_0_FROM_IP		0x00000040U /**< LBR of source instruction address 0 */
#define MSR_LASTBRANCH_1_FROM_IP		0x00000041U /**< LBR of source instruction address 1 */
#define MSR_LASTBRANCH_2_FROM_IP		0x00000042U /**< LBR of source instruction address 2 */
#define MSR_LASTBRANCH_3_FROM_IP		0x00000043U /**< LBR of source instruction address 3 */
#define MSR_LASTBRANCH_4_FROM_IP		0x00000044U /**< LBR of source instruction address 4 */
#define MSR_LASTBRANCH_5_FROM_IP		0x00000045U /**< LBR of source instruction address 5 */
#define MSR_LASTBRANCH_6_FROM_IP		0x00000046U /**< LBR of source instruction address 6 */
#define MSR_LASTBRANCH_7_FROM_IP		0x00000047U /**< LBR of source instruction address 7 */
#define MSR_PPIN_CTL				0x0000004EU /**< Protected CPU Inventory Number Enable Control */
#define MSR_PPIN				0x0000004FU /**< Protected Processor Inventory Number */
#define MSR_THREAD_ID_INFO			0x00000053U /**< A Hardware Assigned ID for the Logical Processor */
#define MSR_LASTBRANCH_0_TO_LIP 		0x00000060U /**< LBR of destination instruction address 0 */
#define MSR_LASTBRANCH_1_TO_LIP 		0x00000061U /**< LBR of destination instruction address 1 */
#define MSR_LASTBRANCH_2_TO_LIP 		0x00000062U /**< LBR of destination instruction address 2 */
#define MSR_LASTBRANCH_3_TO_LIP 		0x00000063U /**< LBR of destination instruction address 3 */
#define MSR_LASTBRANCH_4_TO_LIP 		0x00000064U /**< LBR of destination instruction address 4 */
#define MSR_LASTBRANCH_5_TO_LIP 		0x00000065U /**< LBR of destination instruction address 5 */
#define MSR_LASTBRANCH_6_TO_LIP 		0x00000066U /**< LBR of destination instruction address 6 */
#define MSR_LASTBRANCH_7_TO_LIP 		0x00000067U /**< LBR of destination instruction address 7 */
#define MSR_TRACE_HUB_STH_ACPIBAR_BASE		0x00000080U /**< NPK Address Used by AET Messages */
#define MSR_FSB_FREQ				0x000000CDU /**< Scalable Bus Speed */
#define MSR_PLATFORM_INFO			0x000000CEU /**< Platform Information */
#define MSR_PKG_CST_CONFIG_CONTROL		0x000000E2U /**< C-State Configuration Control */
#define MSR_PMG_IO_CAPTURE_BASE 		0x000000E4U /**< Power Management IO Redirection in C-state */
#define MSR_UNDOCUMENTED_TJMAX			0x000000EEU /**< Pointer to RAR Action Vector */
#define MSR_BBL_CR_CTL				0x00000119U /**< Control Register for programming L2 commands */
#define MSR_BBL_CR_CTL3 			0x0000011EU /**< Control Register 3 for the L2 Cache */
#define MSR_FEATURE_CONFIG			0x0000013CU /**< AES Configuration (RW-L) */
#define MSR_SMM_MCA_CAP 			0x0000017DU /**< Enhanced SMM Capabilities */
#define MSR_ERROR_CONTROL			0x0000017FU /**< MC Bank Error Configuration */
#define MSR_THERM2_CTL				0x0000019DU /**< Thermal Monitor 2 Control */
#define MSR_PLATFORM_BRV			0x000001A1U /**< Platform Feature Requirements */
#define MSR_TEMPERATURE_TARGET			0x000001A2U /**< Temperature Target */
#define MSR_MISC_FEATURE_CONTROL		0x000001A4U /**< Miscellaneous Feature Control */
#define MSR_OFFCORE_RSP_0			0x000001A6U /**< Offcore Response Event Select Register */
#define MSR_OFFCORE_RSP_1			0x000001A7U /**< Offcore Response Event Select Register */
#define MSR_MISC_PWR_MGMT			0x000001AAU /**< Miscellaneous Power Management Control */
#define MSR_TURBO_POWER_CURRENT_LIMIT		0x000001ACU /**< Tubro Power Current Limit */
#define MSR_TURBO_RATIO_LIMIT			0x000001ADU /**< Maximum Ratio Limit of Turbo Mode */
#define MSR_TURBO_GROUP_CORECNT 		0x000001AEU /**< Group Size of Active Cores for Turbo Mode */
#define MSR_TURBO_RATIO_LIMIT2			0x000001AFU /**< Maximum Ratio Limit of Turbo Mode */
#define MSR_LBR_SELECT				0x000001C8U /**< Last Branch Record Filtering Select Register */
#define MSR_LASTBRANCH_TOS			0x000001DAU /**< Last Branch Record Stack TOS */
#define MSR_LASTBRANCH_0			0x000001DBU /**< Last Branch Record 0 */
#define MSR_LASTBRANCH_1			0x000001DCU /**< Last Branch Record 1 */
#define MSR_LASTBRANCH_2			0x000001DDU /**< Last Branch Record 2 */
#define MSR_LASTBRANCH_3			0x000001DEU /**< Last Branch Record 3 */
#define MSR_PRMRR_PHYS_BASE			0x000001F4U /**< PRMRR Physical Base Control Register */
#define MSR_PRMRR_PHYS_MASK			0x000001F5U /**< PRMRR Physical Mask Control Register */
#define MSR_PRMRR_VALID_CONFIG			0x000001FBU /**< Valid PRMRR Configurations */
#define MSR_POWER_CTL				0x000001FCU /**< Power Control Register */
#define MSR_UNCORE_PRMRR_PHYS_BASE		0x000002F4U /**< Uncore PRMRR range base */
#define MSR_UNCORE_PRMRR_PHYS_MASK		0x000002F5U /**< Uncore PRMRR range base mask */

#define MSR_BR_DETECT_CTRL			0x00000350U /**< Branch Monitoring Global Control */
#define MSR_BR_DETECT_STATUS			0x00000351U /**< Branch Monitoring Global Status */
#define MSR_UNCORE_PERF_GLOBAL_OVF_CTRL 	0x00000393U /**< Uncore perf global overflow control */
#define MSR_UNCORE_FIXED_CTR0			0x00000394U /**< Uncore Fixed Counter Control */
#define MSR_UNCORE_FIXED_CTR_CTRL		0x00000395U /**< Uncore U-box perfmon fixed counter control MSR */
#define MSR_UNCORE_ADDR_OPCODE_MATCH		0x00000396U /**< Uncore C-Box Configuration Information */
#define MSR_UNCORE_PMC0 			0x000003B0U /**< Uncore Arb Unit, Performance Counter 0 */
#define MSR_UNCORE_PMC1 			0x000003B1U /**< Uncore Arb Unit, Performance Counter 1 */
#define MSR_UNCORE_PMC2 			0x000003B2U /**< Uncore Arb Unit, Performance Counter 2 */
#define MSR_UNCORE_PMC3 			0x000003B3U /**< Uncore Arb Unit, Performance Counter 3 */
#define MSR_UNCORE_PMC4 			0x000003B4U /**< Uncore Arb Unit, Performance Counter 4 */
#define MSR_UNCORE_PMC5 			0x000003B5U /**< Uncore Arb Unit, Performance Counter 5 */
#define MSR_UNCORE_PMC6 			0x000003B6U /**< Uncore Arb Unit, Performance Counter 6 */
#define MSR_UNCORE_PMC7 			0x000003B7U /**< Uncore Arb Unit, Performance Counter 7 */
#define MSR_UNCORE_PERFEVTSEL0			0x000003C0U /**< Uncore Performance Event Select Register 0 */
#define MSR_UNCORE_PERFEVTSEL1			0x000003C1U /**< Uncore Performance Event Select Register 1 */
#define MSR_UNCORE_PERFEVTSEL2			0x000003C2U /**< Uncore Performance Event Select Register 2 */
#define MSR_UNCORE_PERFEVTSEL3			0x000003C3U /**< Uncore Performance Event Select Register 3 */
#define MSR_UNCORE_PERFEVTSEL4			0x000003C4U /**< Uncore Performance Event Select Register 4 */
#define MSR_UNCORE_PERFEVTSEL5			0x000003C5U /**< Uncore Performance Event Select Register 5 */
#define MSR_UNCORE_PERFEVTSEL6			0x000003C6U /**< Uncore Performance Event Select Register 6 */
#define MSR_UNCORE_PERFEVTSEL7			0x000003C7U /**< Uncore Performance Event Select Register 7 */
#define MSR_PEBS_LD_LAT 			0x000003F6U /**< PEBS Threshold latency value */
#define MSR_PEBS_FRONTEND			0x000003F7U /**< FrontEnd Precise Event Condition Select */
#define MSR_PKG_C2_RESIDENCY			0x000003F8U /**< Package C3 Residency Counter */
#define MSR_PKG_C4_RESIDENCY			0x000003F9U /**< Package C4 Residency Counter */
#define MSR_PKG_C6_RESIDENCY			0x000003FAU /**< Package C6 Residency Counter */
#define MSR_CORE_C3_RESIDENCY			0x000003FCU /**< CORE C3 Residency Counter */
#define MSR_CORE_C6_RESIDENCY			0x000003FDU /**< CORE C6 Residency Counter */
#define MSR_CORE_C7_RESIDENCY			0x000003FEU /**< CORE C7 Residency Counter */
#define MSR_SMM_FEATURE_CONTROL 		0x000004E0U /**< Enhanced SMM Feature Control */
#define MSR_SMM_DELAYED 			0x000004E2U /**< SMM Delayed */
#define MSR_SMM_BLOCKED 			0x000004E3U /**< SMM Blocked */
#define MSR_RAPL_POWER_UNIT			0x00000606U /**< Unit Multipliers used in RAPL Interfaces */
#define MSR_PKGC3_IRTL				0x0000060AU /**< Package C3 Interrupt Response Limit */
#define MSR_PKGC_IRTL1				0x0000060BU /**< Package C6/C7S Interrupt Response Limit 1 */
#define MSR_PKGC_IRTL2				0x0000060CU /**< Package C6/C7S Interrupt Response Limit 2 */
#define MSR_ATOM_PKG_C2_RESIDENCY		0x0000060DU /**< Package C2 Residency Counter */
#define MSR_PKG_POWER_LIMIT			0x00000610U /**< PKG RAPL Power Limit Control */
#define MSR_PKG_ENERGY_STATUS			0x00000611U /**< PKG Energy Status */
#define MSR_PKG_PERF_STATUS			0x00000613U /**< PKG Perf Status */
#define MSR_PKG_POWER_INFO			0x00000614U /**< PKG RAPL Parameters */
#define MSR_DRAM_POWER_LIMIT			0x00000618U /**< DRAM RAPL Power Limit Control */
#define MSR_DRAM_ENERGY_STATUS			0x00000619U /**< DRAM Energy Status */
#define MSR_DRAM_PERF_STATUS			0x0000061BU /**< DRAM Performance Throttling Status */
#define MSR_DRAM_POWER_INFO			0x0000061CU /**< DRAM RAPL Parameters */
#define MSR_PCIE_PLL_RATIO			0x0000061EU /**< Configuration of PCIE PLL Relative to BCLK */
#define MSR_UNCORE_RATIO_LIMIT			0x00000620U /**< Uncore Ratio Limit */
#define MSR_PKG_C8_RESIDENCY			0x00000630U /**< Package C8 Residency Counter */
#define MSR_PKG_C9_RESIDENCY			0x00000631U /**< Package C9 Residency Counter */
#define MSR_PKG_C10_RESIDENCY			0x00000632U /**< Package C10 Residency Counter */
#define MSR_PKGC8_IRTL				0x00000633U /**< Package C8 Interrupt Response Time Limit */
#define MSR_PKGC9_IRTL				0x00000634U /**< Package C9 Interrupt Response Time Limit */
#define MSR_PKGC10_IRTL 			0x00000635U /**< Package C10 Interrupt Response Time Limit */
#define MSR_PP0_POWER_LIMIT			0x00000638U /**< PP0 RAPL Power Limit Control */
#define MSR_PP0_ENERGY_STATUS			0x00000639U /**< PP0 Energy Status */
#define MSR_PP0_POLICY				0x0000063AU /**< PP0 Balance Policy */
#define MSR_PP1_POWER_LIMIT			0x00000640U /**< PP1 RAPL Power Limit Control */
#define MSR_PP1_ENERGY_STATUS			0x00000641U /**< PP1 Energy Status */
#define MSR_PP1_POLICY				0x00000642U /**< PP1 Balance Policy */
#define MSR_CONFIG_TDP_NOMINAL			0x00000648U /**< Base TDP Ratio */
#define MSR_CONFIG_TDP_LEVEL1			0x00000649U /**< ConfigTDP Level 1 ratio and power level */
#define MSR_CONFIG_TDP_LEVEL2			0x0000064AU /**< ConfigTDP Level 2 ratio and power level */
#define MSR_CONFIG_TDP_CONTROL			0x0000064BU /**< ConfigTDP Control */
#define MSR_TURBO_ACTIVATION_RATIO		0x0000064CU /**< Turbo Activation Ratio */
#define MSR_PLATFORM_ENERGY_COUNTER		0x0000064DU /**< Platform Energy Counter */
#define MSR_PPERF				0x0000064EU /**< Productive Performance Count */
#define MSR_ATOM_CORE_PERF_LIMIT_REASONS	0X0000064FU /**< Frequency Clipping in Processor Cores */
#define MSR_PKG_HDC_CONFIG			0x00000652U /**< PKG HDC Configuration */
#define MSR_CORE_HDC_RESIDENCY			0x00000653U /**< Core HDC Idle Residency */
#define MSR_PKG_HDC_SHALLOW_RESIDENCY		0x00000655U /**< Accumulate the cycles the package was in C2 state */
#define MSR_PKG_HDC_DEEP_RESIDENCY		0x00000656U /**< Package Cx HDC Idle Residency */
#define MSR_WEIGHTED_CORE_C0			0x00000658U /**< Core-count Weighted C0 Residency */
#define MSR_ANY_CORE_C0 			0x00000659U /**< Any Core C0 Residency */
#define MSR_ANY_GFXE_C0 			0x0000065AU /**< Any Graphics Engine C0 Residency */
#define MSR_CORE_GFXE_OVERLAP_C0		0x0000065BU /**< Core and Graphics Engine Overlapped C0 Residency */
#define MSR_PLATFORM_POWER_LIMIT		0x0000065CU /**< Platform Power Limit Control */
#define MSR_CORE_C1_RESIDENCY			0x00000660U /**< Core C1 Residency Counter */
#define MSR_MC6_RESIDENCY_COUNTER		0x00000664U /**< Module C6 Residency Counter */
#define MSR_CC6_DEMOTION_POLICY_CONFIG		0x00000668U /**< Core C6 Demotion Policy Config MSR */
#define MSR_MC6_DEMOTION_POLICY_CONFIG		0x00000669U /**< Module C6 Demotion Policy Config MSR */
#define MSR_ATOM_PKG_POWER_INFO 		0x0000066EU /**< PKG RAPL Parameter */
#define MSR_RING_PERF_LIMIT_REASONS		0x000006B1U /**< Frequency Clipping in the Ring Interconnect */
#define MSR_CORE_PERF_LIMIT_REASONS		0x00000690U /**< Frequency Clipping in Processor Cores */
#define MSR_LASTBRANCH_31_FROM_IP		0x0000069FU /**< Last Branch Record 31 From IP */
#define MSR_GRAPHICS_PERF_LIMIT_REASONS 	0x000006B0U /**< Frequency Clipping in the Processor Graphics */
#define MSR_LASTBRANCH_0_TO_IP  		0x000006C0U /**< LBR Record 0 of destination instruction */
#define MSR_LASTBRANCH_31_TO_IP 		0x000006DFU /**< LBR Record 31 of destination instruction */
#define MSR_IA32_TME_CAPABILITY 		0x00000981U /**< Memory Encryption Capability MSR */
#define MSR_IA32_TME_ACTIVATE			0x00000982U /**< Memory Encryption Activation MSR */
#define MSR_IA32_TME_EXCLUDE_MASK		0x00000983U /**< Memory Encryption Exclude Mask */
#define MSR_IA32_TME_EXCLUDE_BASE		0x00000984U /**< Memory Encryption Exclude Base */
#define MSR_CORE_MKTME_ACTIVATION		0x000009FFU /**< MSR to read TME_ACTIVATE[MK_TME_KEYID_BITS] */
#define MSR_IA32_L2_QOS_MASK_0			0x00000D10U /**< L2 CAT Mask for COS0 */
#define MSR_IA32_L2_QOS_MASK_1			0x00000D11U /**< L2 CAT Mask for COS1 */
#define MSR_IA32_L2_QOS_MASK_2			0x00000D12U /**< L2 CAT Mask for COS2 */
#define MSR_IA32_L2_QOS_MASK_3			0x00000D13U /**< L2 CAT Mask for COS3 */
#define MSR_LASTBRANCH_INFO_0			0x00000DC0U /**< Last Branch Record 0 Additional Information */
#define MSR_LASTBRANCH_INFO_31			0x00000DDFU /**< Last Branch Record 31 Additional Information */
#define MSR_R0_PMON_BOX_STATUS			0x00000E01U /**< Uncore R-box 0 perfmon local box status MSR */
#define MSR_EMON_L3_CTR_CTL0			0x000107CCU /**< GBUSQ Event Control and Counter Register 0 */
#define MSR_EMON_L3_CTR_CTL1			0x000107CDU /**< GBUSQ Event Control and Counter Register 1 */
#define MSR_EMON_L3_CTR_CTL2			0x000107CEU /**< GSNPQ Event Control and Counter Register 0 */
#define MSR_EMON_L3_CTR_CTL3			0x000107CFU /**< GSNPQ Event Control and Counter Register 1 */
#define MSR_EMON_L3_CTR_CTL4			0x000107D0U /**< FSB Event Control and Counter Register 0 */
#define MSR_EMON_L3_CTR_CTL5			0x000107D1U /**< FSB Event Control and Counter Register 1 */
#define MSR_EMON_L3_CTR_CTL6			0x000107D2U /**< FSB Event Control and Counter Register 2 */
#define MSR_EMON_L3_CTR_CTL7			0x000107D3U /**< FSB Event Control and Counter Register 3 */
#define IA32_HW_FEEDBACK_PTR			0x000017D0U /**< Hardware Feedback Interface Pointer */
#define IA32_HW_FEEDBACK_CONFIG 		0x000017D1U /**< Hardware Feedback Interface Configuration */
#define IA32_THREAD_FEEDBACK_CHAR		0x000017D2U /**< Thread Feedback Characteristics */
#define IA32_HW_FEEDBACK_THREAD_CONFIG		0x000017D4U /**< Hardware Feedback Thread Configuration */
#define IA32_HRESET_ENABLE			0x000017DAU /**< History Reset Enable */

#ifdef PROFILING_ON
#define MSR_CORE_LASTBRANCH_TOS 		0x000001C9U /**< LBR Stack TOS */
#define MSR_CORE_LASTBRANCH_0_FROM_IP		0x00000680U /**< LBR Record 0 for source instruction address */
#define MSR_CORE_LASTBRANCH_0_TO_IP		0x000006C0U /**< LBR Record 0 for destination instruction address */
#endif

#define MSR_LNC_BIOS_CACHE_AS_RAM		0x000002E0U /**< Configure CAR MSR */

/* EFER bits */
#define MSR_IA32_EFER_SCE_BIT			(1UL << 0U) /**< SYSCALL Enable */
#define MSR_IA32_EFER_LME_BIT			(1UL << 8U) /**< IA32e mode enable */
#define MSR_IA32_EFER_LMA_BIT			(1UL << 10U) /**< IA32e mode active */
#define MSR_IA32_EFER_NXE_BIT			(1UL << 11U) /**< Execute Disable Bit Enable */

/* FEATURE CONTROL bits */
#define MSR_IA32_FEATURE_CONTROL_LOCK		(1U << 0U) /**< Lock bit */
#define MSR_IA32_FEATURE_CONTROL_VMX_IN_SMX	(1U << 1U) /**< Enable VMX inside SMX operation */
#define MSR_IA32_FEATURE_CONTROL_VMX_OUT_SMX	(1U << 2U) /**< Enable VMX outside SMX operation */
#define MSR_IA32_FEATURE_CONTROL_SENTERS_MSK	(0x7F00U) /**< SENTER Local Function Enables */
#define MSR_IA32_FEATURE_CONTROL_SENTER 	(1U << 15U) /**< SENTER Global Enable */
#define MSR_IA32_FEATURE_CONTROL_SGX_LC 	(1U << 17U) /**< SGX Launch Control Enable */
#define MSR_IA32_FEATURE_CONTROL_SGX_GE 	(1U << 18U) /**< SGX Global Enable */

/* PAT memory type definitions */
#define PAT_MEM_TYPE_UC 			0x00UL	/**< PAT Uncached Type*/
#define PAT_MEM_TYPE_WC 			0x01UL	/**< PAT Write-combining Type */
#define PAT_MEM_TYPE_WT 			0x04UL	/**< PAT Write-through Type */
#define PAT_MEM_TYPE_WP 			0x05UL	/**< PAT Write-protected Type */
#define PAT_MEM_TYPE_WB 			0x06UL	/**< PAT Write-back Type */
#define PAT_MEM_TYPE_UCM			0x07UL	/**< PAT Uncached-minus Type */

/* MISC_ENABLE bits: architectural */
#define MSR_IA32_MISC_ENABLE_FAST_STRING	(1UL << 0U) /**< Fast-Strings Enable */
#define MSR_IA32_MISC_ENABLE_TCC		(1UL << 3U) /**< Automatic Thermal Control Circuit Enable */
#define MSR_IA32_MISC_ENABLE_PMA		(1UL << 7U) /**< Performance Monitoring Available */
#define MSR_IA32_MISC_ENABLE_BTS_UNAVAIL	(1UL << 11U) /**< Branch Trace Storage Unavailable */
#define MSR_IA32_MISC_ENABLE_PEBS_UNAVAIL	(1UL << 12U) /**< Processor Event Based Sampling (PEBS) Unavailable */
#define MSR_IA32_MISC_ENABLE_TM2_ENABLE 	(1UL << 13U) /**< TM2 Enable */
#define MSR_IA32_MISC_ENABLE_EIST		(1UL << 16U) /**< Enhanced Intel SpeedStep Technology Enable */
#define MSR_IA32_MISC_ENABLE_MONITOR_ENA	(1UL << 18U) /**< ENABLE MONITOR FSM */
#define MSR_IA32_MISC_ENABLE_LIMIT_CPUID	(1UL << 22U) /**< Limit CPUID Maxval */
#define MSR_IA32_MISC_ENABLE_XTPR		(1UL << 23U) /**< xTPR Message Disable */
#define MSR_IA32_MISC_ENABLE_XD_DISABLE 	(1UL << 34U) /**< XD Bit Disable */

/* DEBUGCTL bits */
#define MSR_IA32_DEBUGCTL_ENABLE_LBR		(1UL << 0U) /**< LBR Enable */
#define MSR_IA32_DEBUGCTL_FREEZE_LBRS_ON_PMI	(1UL << 11U) /**< Freeze LBR stackon a PMI request */
#define MSR_IA32_DEBUGCTL_RSV_BITS		(0xFFFFFFFFFFFF0038UL) /**< Reserved bits in DEBUGCTL MSR */

/* MSR_IA32_XSS bits */
#define MSR_IA32_XSS_PT 			(1UL << 8U) /**< PT State */
#define MSR_IA32_XSS_CET_U			(1UL << 11U) /**< CET_U State */
#define MSR_IA32_XSS_CET_S			(1UL << 12U) /**< CET_S State */
#define MSR_IA32_XSS_HDC			(1UL << 13U) /**< HDC State */

#define MSR_IA32_MISC_UNRESTRICTED_GUEST	(1U<<5U) /**< Unrestricted Guest Support */
#define MSR_IA32_VMX_BASIC_ADDR_WIDTH		(1UL << 48U) /**< Width of physical address used by VMX */
#define PAT_FIELD_RSV_BITS			(0xF8UL) /**< 5 high-order bits in every field are reserved */

#define MSR_TEST_CTL_GP_UCLOCK			(1UL << 28U) /**< UC lock causes a \#GP(0) */
#define MSR_TEST_CTL_AC_SPLITLOCK		(1UL << 29U) /**< Split lock causes an \#AC(0) */
#define MSR_TEST_CTL_DISABLE_LOCK_ASSERTION	(1UL << 31U) /**< Disable Lock Assertion */

#ifndef ASSEMBLER

/**
 * @brief Check whether PAT memory type is valid
 *
 * Refer to Section 12.12 PAGE ATTRIBUTE TABLE (PAT), Vol.3 SDM 325462-078US for details.
 *
 * @param[in] x Target memory type to check
 *
 * @return true if the memory type is valid for PAT, otherwise false.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline bool is_pat_mem_type_invalid(uint64_t x)
{
	return (((x & PAT_FIELD_RSV_BITS) != 0UL) || ((x & 0x6UL) == 0x2UL));
}

/**
 * @brief Check whether it's x2APIC MSR
 *
 * Valid x2APIC MSRs are in range: [0x800U, 0x900U].
 *
 * @param[in] msr Target MSR to check
 *
 * @return true if the MSR is a valid x2APIC MSR, otherwise false.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline bool is_x2apic_msr(uint32_t msr)
{
	/*
	 * if msr is in the range of x2APIC MSRs
	 */
	return ((msr >= 0x800U) && (msr < 0x900U));
}

struct acrn_vcpu;

void init_msr_emulation(struct acrn_vcpu *vcpu);
uint32_t vmsr_get_guest_msr_index(uint32_t msr);
int32_t rdmsr_vmexit_handler(struct acrn_vcpu *vcpu);
int32_t wrmsr_vmexit_handler(struct acrn_vcpu *vcpu);

#endif /* ASSEMBLER */

/**
 * @brief Macro to define default setting of PAT MSR
 *
 * The MSR field values are referenced by paging structure fields: PAT, PCD and PWT to determine
 * the memory type of the target physical memory that the paging entry points to.
 * Refer to section 12.12 PAGE ATTRIBUTE TABLE, Vol.3 SDM 325462-078US for details.
 */
#define PAT_POWER_ON_VALUE	(PAT_MEM_TYPE_WB + \
	(PAT_MEM_TYPE_WT << 8U) + \
	(PAT_MEM_TYPE_UCM << 16U) + \
	(PAT_MEM_TYPE_UC << 24U) + \
	(PAT_MEM_TYPE_WB << 32U) + \
	(PAT_MEM_TYPE_WT << 40U) + \
	(PAT_MEM_TYPE_UCM << 48U) + \
	(PAT_MEM_TYPE_UC << 56U))

/**
 * @brief Macro to define a PAT MSR value with all memory types to UC
 *
 * When guest sets CR0.CD == 1, HV shall set all memory types to UC for guest PAT MSR.
 */
#define PAT_ALL_UC_VALUE (PAT_MEM_TYPE_UC + \
	(PAT_MEM_TYPE_UC << 8U) + \
	(PAT_MEM_TYPE_UC << 16U) + \
	(PAT_MEM_TYPE_UC << 24U) + \
	(PAT_MEM_TYPE_UC << 32U) + \
	(PAT_MEM_TYPE_UC << 40U) + \
	(PAT_MEM_TYPE_UC << 48U) + \
	(PAT_MEM_TYPE_UC << 56U))

/* MTRR memory type definitions */
#define MTRR_MEM_TYPE_UC			0x00UL	/**< MTRR Uncached Type */
#define MTRR_MEM_TYPE_WC			0x01UL	/**< MTRR Write-combining Type */
#define MTRR_MEM_TYPE_WT			0x04UL	/**< MTRR Write-through Type */
#define MTRR_MEM_TYPE_WP			0x05UL	/**< MTRR Write-protected Type */
#define MTRR_MEM_TYPE_WB			0x06UL	/**< MTRR Write-back Type */

/* misc. MTRR flag definitions */
#define MTRR_FIX_ENABLE 			0x400U	/**< Fixed range MTRR enable bit of IA32_MTRR_DEF_TYPE MSR */
#define MTRR_VALID				0x800U	/**< Valid bit of IA32_MTRR_PHYSMASKn MSR */
#define MTRR_VARIABLE_CNT			0xFFU   /**< Number of variable MTRRs mask of IA32_MTRRCAP MSR */

/* MSR_IA32_MTRR_DEF_TYPE bits */
#define MSR_IA32_MTRR_DEF_TYPE_TYPE		0xFFUL  /**< Default memory type mask of IA32_MTRR_DEF_TYPE MSR */
#define MSR_IA32_MTRR_DEF_TYPE_ENABLE		0x800UL /**< MTRR enable bit of IA32_MTRR_DEF_TYPE MSR */
#define MSR_IA32_MTRR_DEF_TYPE_RES		0xFFFFFFFFFFFFF300UL /**< Reserved bits of IA32_MTRR_DEF_TYPE MSR */

/* SPEC & PRED bit */
#define SPEC_ENABLE_IBRS			(1U << 0U) /**< Indirect Branch Restricted Speculation (IBRS) */
#define SPEC_ENABLE_STIBP			(1U << 1U) /**< Single Thread Indirect Branch Predictors (STIBP) */
#define SPEC_RRSBA_DIS_S			(1U << 6U) /**< Disables RRSBA behavior for CPL0/1/2 if set */
#define PRED_SET_IBPB				(1U << 0U) /**< Indirect Branch Prediction Barrier (IBPB) */

#define IA32_LEGACY_XAPIC_DISABLED		(1U << 0U) /**< Attempts to clear IA32_APIC_BASE.EXTD fails if set */

/* IA32 ARCH Capabilities bit */
#define IA32_ARCH_CAP_RDCL_NO			(1UL << 0U) /**< CPU not susceptible to Rogue Data Cache Load */
#define IA32_ARCH_CAP_IBRS_ALL			(1UL << 1U) /**< The processor supports enhanced IBRS. */
#define IA32_ARCH_CAP_RSBA			(1UL << 2U) /**< The processor supports RSB Alternate. */
#define IA32_ARCH_CAP_SKIP_L1DFL_VMENTRY	(1UL << 3U) /**< HV need not flush the L1D on VM entry if set. */
#define IA32_ARCH_CAP_SSB_NO			(1UL << 4U) /**< CPU not susceptible to Speculative Store Bypass. */
#define IA32_ARCH_CAP_MDS_NO			(1UL << 5U) /**< CPU not susceptible to MDS */
#define IA32_ARCH_CAP_IF_PSCHANGE_MC_NO 	(1UL << 6U) /**< CPU not susceptible to MC error due to TLB flush. */
#define IA32_ARCH_CAP_RESTRICTED_RSBA		(1UL << 19U) /**< CPU may have RRSBA alternate prediction behavior */
#define IA32_ARCH_CAP_XAPIC_DIS_STATUS		(1UL << 21U) /**< Enumerates that XAPIC_DISABLE_STATUS MSR exists. */

#define IA32_L1D_FLUSH				(1UL << 0U) /**< Writeback and invalidate the L1 data cache. */

/* PLATFORM INFO bits */
#define MSR_PLATFORM_INFO_MAX_NON_TURBO_LIM_RATIO_MASK	(0x000000000000ff00UL)	/**< Maximum Non-Turbo Ratio */
#define MSR_PLATFORM_INFO_MAX_EFFICIENCY_RATIO_MASK	(0x0000ff0000000000UL)	/**< Maximum Efficiency Ratio */
#define MSR_PLATFORM_INFO_MIN_OPERATING_RATIO_MASK	(0x00ff000000000000UL)	/**< Minimum Operating Ratio */
#define MSR_PLATFORM_INFO_SAMPLE_PART			(1UL << 27U) /**< Programmable Sample Part */

#define MSR_IA32_HWP_STATUS_RSV_BITS		(~0x3DUL) /**< Reserved bits of HWP_STATUS MSR */
#define MSR_IA32_HWP_REQUEST_RSV_BITS		(0x7FFF80000000000UL) /**< Reserved bits of HWP_REQUEST MSR */
#define MSR_IA32_HWP_REQUEST_PKG_CTL		(1UL << 42U) /**< Package_Control */

/* Machine Check bits */
#define	MSR_IA32_MCG_CAP_COUNT		0x000000ffUL /**< Number of hardware unit error-reporting banks */
#define	MSR_IA32_MCG_CAP_CTL_P		0x00000100UL /**< Control MSR present */
#define	MSR_IA32_MCG_CAP_EXT_P		0x00000200UL /**< Extended MSRs present */
#define	MSR_IA32_MCG_CAP_CMCI_P		0x00000400UL /**< Corrected MC error counting/signaling extension present */
#define	MSR_IA32_MCG_CAP_TES_P		0x00000800UL /**< Threshold-based error status present */
#define	MSR_IA32_MCG_CAP_EXT_CNT	0x00ff0000UL /**< The number of extended MC state registers present. */
#define	MSR_IA32_MCG_CAP_SER_P		0x01000000UL /**< Software error recovery support present */
#define	MSR_IA32_MCG_CAP_EMC_P		0x02000000UL /**< Enhanced Machine Check Capability */
#define	MSR_IA32_MCG_CAP_ELOG_P		0x04000000UL /**< Extended error logging */
#define	MSR_IA32_MCG_CAP_LMCE_P		0x08000000UL /**< Local machine check exception */
#define	MSR_IA32_MCG_STATUS_RIPV	0x00000001UL /**< Restart IP valid */
#define	MSR_IA32_MCG_STATUS_EIPV	0x00000002UL /**< Error IP valid */
#define	MSR_IA32_MCG_STATUS_MCIP	0x00000004UL /**< Machine check in progress */
#define	MSR_IA32_MCG_CTL_ENABLE		0xffffffffffffffffUL /**< Machine Check Feature Enable mask */
#define	MSR_IA32_MCG_CTL_DISABLE	0x0000000000000000UL /**< Machine Check Feature Disable mask */

/**
 * @brief Calculate IA32_MC_CTL MSR address for bank x
 *
 * @param[in] x Target bank index
 *
 * @return Target IA32_MC_CTL MSR address of bank x
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark Bank index should be a valid value for the platform.
 */
#define	MSR_IA32_MC_CTL(x)		(MSR_IA32_MC0_CTL + (x) * 4)

/**
 * @brief Calculate MSR_IA32_MC_STATUS MSR address for bank x
 *
 * @param[in] x Target bank index
 *
 * @return Target MSR_IA32_MC_STATUS MSR address of bank x
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark Bank index should be a valid value for the platform.
 */
#define	MSR_IA32_MC_STATUS(x)		(MSR_IA32_MC0_STATUS + (x) * 4)

/**
 * @brief Calculate MSR_IA32_MC_ADDR MSR address for bank x
 *
 * @param[in] x Target bank index
 *
 * @return Target MSR_IA32_MC_ADDR MSR address of bank x
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark Bank index should be a valid value for the platform.
 */
#define	MSR_IA32_MC_ADDR(x)		(MSR_IA32_MC0_ADDR + (x) * 4)

/**
 * @brief Calculate MSR_IA32_MC_MISC MSR address for bank x
 *
 * @param[in] x Target bank index
 *
 * @return Target MSR_IA32_MC_MISC MSR address of bank x
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark Bank index should be a valid value for the platform.
 */
#define	MSR_IA32_MC_MISC(x)		(MSR_IA32_MC0_MISC + (x) * 4)

/**
 * @brief Calculate MSR_IA32_MC_CTL2 MSR address for bank x
 *
 * @param[in] x Target bank index
 *
 * @return Target MSR_IA32_MC_CTL2 MSR address of bank x
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark Bank index should be a valid value for the platform.
 */
#define	MSR_IA32_MC_CTL2(x)		(MSR_IA32_MC0_CTL2 + (x) * 1)

#define	MSR_IA32_MC_STATUS_MCA_ERROR	0x000000000000ffffUL /**< MCA (MC Architecture) error code field */
#define	MSR_IA32_MC_STATUS_MODEL_ERROR	0x00000000ffff0000UL /**< Model-specific error code field */
#define	MSR_IA32_MC_STATUS_OTHER_INFO	0x01ffffff00000000UL /**< Reserved, Error Status and Other fields */
#define	MSR_IA32_MC_STATUS_COR_COUNT	0x001fffc000000000UL /**< Corrected Error Count */
#define	MSR_IA32_MC_STATUS_TES_STATUS	0x0060000000000000UL /**< Threshold-based error status */
#define	MSR_IA32_MC_STATUS_AR		0x0080000000000000UL /**< Recovery action required for UCR error */
#define	MSR_IA32_MC_STATUS_S		0x0100000000000000UL /**< Signaling an uncorrected recoverable (UCR) error */
#define	MSR_IA32_MC_STATUS_PCC		0x0200000000000000UL /**< Processor context corrupt */
#define	MSR_IA32_MC_STATUS_ADDRV	0x0400000000000000UL /**< IA32_MCi_ADDR register valid */
#define	MSR_IA32_MC_STATUS_MISCV	0x0800000000000000UL /**< IA32_MCi_MISC register valid */
#define	MSR_IA32_MC_STATUS_EN		0x1000000000000000UL /**< Error enabled */
#define	MSR_IA32_MC_STATUS_UC		0x2000000000000000UL /**< Error uncorrected */
#define	MSR_IA32_MC_STATUS_OVER		0x4000000000000000UL /**< Machine-check overflow */
#define	MSR_IA32_MC_STATUS_VAL		0x8000000000000000UL /**< IA32_MCi_STATUS register valid */
#define	MSR_IA32_MC_MISC_RA_LSB		0x000000000000003fUL /**< Recoverable Address LSB */
#define	MSR_IA32_MC_MISC_ADDRESS_MODE	0x00000000000001c0UL /**< Address Mode */
#define	MSR_IA32_MC_CTL2_THRESHOLD	0x0000000000007fffUL /**< Corrected error count threshold */
#define	MSR_IA32_MC_CTL2_CMCI_EN	0x0000000040000000UL /**< Corrected error interrupt enable */

#endif /* MSR_H */
/**
 * @}
 */
