/*
 * Copyright (C) 2018-2024 Intel Corporation.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef	HOST_PM_H
#define	HOST_PM_H

#include <acrn_common.h>

/**
 * @defgroup hwmgmt_pm hwmgmt.pm
 * @ingroup hwmgmt
 * @brief Host power management.
 *
 * This module is implement of host power management functions, by which the hypervisor can
 * manage the host machine's ACPI Sx states, reboot and power/performance states.
 *
 * @{
 */

/**
 * @file arch/x86/wakeup.S
 *
 * @brief Assembly functions for enter/restore S3.
 *
 * This file implements 2 functions:
 *  - asm_enter_s3()
 *  - restore_s3_context()
 * Please see documentation of host_pm.h for detail.
 */

/**
 * @file
 *
 * @brief Host power management header.
 */

/**
 * @brief The bit index of SLP_TYPx in PM1 control registers.
 * Defined in ACPI spec 4.8.3.2.1.
 */
#define	BIT_SLP_TYPx	10U

/**
 * @brief The bit index of SLP_EN in PM1 control registers.
 * Defined in ACPI spec 4.8.3.2.1.
 */
#define	BIT_SLP_EN	13U

/**
 * @brief The bit index of WAK_STS in PM1 status registers.
 * Defined in ACPI spec 4.8.3.1.1.
 */
#define	BIT_WAK_STS	15U

/**
 * @brief Warm reset code for CF9 registers.
 * CF9 register is used when ACPI reset info is not presented.
 */
#define CF9_RESET_WARM	0x6U

/**
 * @brief Cold reset code for CF9 registers.
 * CF9 register is used when ACPI reset info is not presented.
 */
#define CF9_RESET_COLD	0xEU

/**
 * @brief Host CPU ACPI power state information.
 *
 * This data struct is used to describe host CPU ACPI performance states (Px)
 * and power states (Cx). Extracted from host ACPI table.
 */
struct cpu_state_info {
	uint8_t			 	px_cnt;	/**< Count of Px entries. */
	const struct acrn_pstate_data	*px_data; /**< Pointer to the Px entries. */
	uint8_t			 	cx_cnt;	/**< Count of Cx entries. */
	const struct acrn_cstate_data	*cx_data; /**< Pointer to the Cx entries. */
};

/**
 * @brief Host CPU ACPI state table.
 *
 * This data struct is used to describe host CPU ACPI state information.
 */
struct cpu_state_table {
	char			model_name[64]; /**< Name of the CPU model. */
	struct cpu_state_info	state_info; /**< CPU ACPI power state information. */
};

/**
 * @brief Host ACPI reset register info.
 *
 * This data struct is used to describe host CPU ACPI reset register information,
 * which is extracted from host ACPI table.
 */
struct acpi_reset_reg {
	struct acrn_acpi_generic_address reg; /**< ACPI reset register information for system reset. */
	uint8_t val; /**< Value to be written to the reset register when initiating system reset. */
};

struct pm_s_state_data *get_host_sstate_data(void);
void host_enter_s3(void);
void shutdown_system(void);
void save_pm1_reg_val(uint32_t pm1a_cnt_val, uint32_t pm1b_cnt_val);
void do_acpi_sx(const struct pm_s_state_data *sstate_data, uint32_t pm1a_cnt_val, uint32_t pm1b_cnt_val);

/**
 * @brief Put the host into S3.
 *
 * This is the S3 enter low-level routine. It saves the CPU context to the global variable cpu_ctx
 * and enter S3 state.
 *
 * Implemented by assembly code.
 *
 * @param[in] sstate_data Stores host Sx management info data.
 * @param[in] pm1a_cnt_val PM1a_CNT value that will decide the system Sx type.
 * @param[in] pm1b_cnt_val PM1b_CNT value that will decide the system Sx type.
 *
 * @return None
 *
 * @pre sstate_data != NULL
 */
extern void asm_enter_s3(const struct pm_s_state_data *sstate_data, uint32_t pm1a_cnt_val, uint32_t pm1b_cnt_val);

/**
 * @brief Restore S3 context.
 *
 * This is the S3 wakeup routine. It restores CPU context and returns to the next instruction
 * of the instruction that puts the system into S3.
 *
 * When system resume from S3, trampoline_start64 will jump to restore_s3_context
 * after setup temporary stack.
 *
 * Implemented by assembly code.
 *
 * @return None
 */
extern void restore_s3_context(void);
struct acpi_reset_reg *get_host_reset_reg_data(void);
void reset_host(bool warm);
void init_frequency_policy(void);
void apply_frequency_policy(void);
extern struct cpu_context cpu_ctx;
void restore_msrs(void);
#ifdef CONFIG_S0IX_SUPPORT
void hypervisor_s0ix_enter(void);
bool is_hypervisor_s0ix_confirmed(void);
void hypervisor_s0ix_resume(void);
bool is_s0ix_detected(void);
int16_t get_s0ix_resume_cpu_id(void);
void tpm2_pm_init(void *tpm2_mmio_pa);
#endif

#endif	/* HOST_PM_H */

/**
 * @}
 */
