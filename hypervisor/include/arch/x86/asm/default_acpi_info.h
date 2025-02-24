
/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef DEFAULT_ACPI_INFO_H
#define DEFAULT_ACPI_INFO_H
/**
 * @addtogroup hwmgmt_hw-caps
 * @{
 */
/**
 * @file
 * @brief Default ACPI configuration definitions for x86 architecture.
 *
 * This header file contains default definitions for various ACPI configurations that are unlikely to change. These
 * definitions can be overridden by the platform_acpi_info.h file using \#undef with an offline tool if necessary.
 */
/* APIC */
#define LAPIC_BASE		0xFEE00000UL    /**< Base address of local APIC */

#define IOAPIC0_BASE		0xFEC00000UL    /**< Base address of IOAPIC 0 */
#define IOAPIC1_BASE		0UL             /**< Base address of IOAPIC 1 */

/* pm sstate data */
#define PM1A_EVT_SPACE_ID	SPACE_SYSTEM_IO /**< Space ID for PM1A event register */
#define PM1A_EVT_BIT_WIDTH	0x20U           /**< Bit width of PM1A event register */
#define PM1A_EVT_BIT_OFFSET	0U              /**< Bit offset of PM1A event register */

#define PM1B_EVT_SPACE_ID	SPACE_SYSTEM_IO /**< Space ID for PM1B event register */
#define PM1B_EVT_BIT_WIDTH	0U              /**< Bit width of PM1B event register */
#define PM1B_EVT_BIT_OFFSET	0U              /**< Bit offset of PM1B event register */
#define PM1B_EVT_ACCESS_SIZE	0U              /**< Access size of PM1B event register */
#define PM1B_EVT_ADDRESS	0UL             /**< Address of PM1B event register */

#define PM1A_CNT_SPACE_ID	SPACE_SYSTEM_IO /**< Space ID for PM1A control register */
#define PM1A_CNT_BIT_WIDTH	0x10U           /**< Bit width of PM1A control register */
#define PM1A_CNT_BIT_OFFSET	0U              /**< Bit offset of PM1A control register */
#define PM1A_CNT_ACCESS_SIZE	2U              /**< Access size of PM1A control register */

#define PM1B_CNT_SPACE_ID	SPACE_SYSTEM_IO /**< Space ID for PM1B control register */
#define PM1B_CNT_BIT_WIDTH	0U              /**< Bit width of PM1B control register */
#define PM1B_CNT_BIT_OFFSET	0U              /**< Bit offset of PM1B control register */
#define PM1B_CNT_ACCESS_SIZE	0U              /**< Access size of PM1B control register */
#define PM1B_CNT_ADDRESS	0UL             /**< Address of PM1B control register */

#define S3_PKG_VAL_PM1A		0x05U           /**< Value to write to SLP_TYPx register to enter S3 state */
#define S3_PKG_VAL_PM1B		0U              /**< Value to write to SLP_TYPx register to enter S3 state */
#define S3_PKG_RESERVED		0U              /**< Reserved field in SLP_TYPx register */

#define S5_PKG_VAL_PM1A		0x07U           /**< Value to write to SLP_TYPx register to enter S5 state */
#define S5_PKG_VAL_PM1B		0U              /**< Value to write to SLP_TYPx register to enter S5 state */
#define S5_PKG_RESERVED		0U              /**< Reserved field in SLP_TYPx register */

/* reset register */
#define RESET_REGISTER_BIT_WIDTH	0x08U   /**< Bit width of reset register */
#define RESET_REGISTER_BIT_OFFSET	0U      /**< Bit offset of reset register */
#define RESET_REGISTER_ACCESS_SIZE	0x01U   /**< Access size of reset register */

/**
 * @}
 */
#endif	/* DEFAULT_ACPI_INFO_H */
