/*
 * Copyright (C) 2020-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef NOTIFY_H
#define NOTIFY_H

/**
 * @addtogroup hwmgmt_smpcall hwmgmt.smpcall
 *
 * @{
 */

/**
 * @brief Public x86 APIs for SMP function call mechanism.
 */


/**
 * @brief Type of SMP call function callback.
 *
 * The callback takes a generic pointer to implementation specific data.
 */
typedef void (*smp_call_func_t)(void *data);

/**
 * @brief SMP call handler function and associated data
 *
 * It's used to define the per-cpu SMP call handler. The SMP call invoker sets a handler for
 * target processor and the target processor invokes the handler.
 */
struct smp_call_info_data {
	smp_call_func_t func;   /**< The function to execute */
	void *data;             /**< The data for the function */
};

struct acrn_vm;
void smp_call_function(uint64_t mask, smp_call_func_t func, void *data);

void setup_notification(void);
void handle_smp_call(void);
void setup_pi_notification(void);

/**
 * @}
 */
#endif
