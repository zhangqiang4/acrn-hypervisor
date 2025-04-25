/*
 * Copyright (C) 2018-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <logmsg.h>

/**
 * @addtogroup hwmgmt_security
 *
 * @{
 */

/**
 * @file
 * @brief This file implements callback function __stack_chk_fail()
 * for stack protector feature of hwmgmt.security module.
 */

void __stack_chk_fail(void);
/**
 * @brief Callback function for stack protector.
 *
 * This callback function stops the execution of program and print debug messages.
 *
 * GCC will emit extra code to check for buffer overflow in some functions if -fstack-protector-strong/
 * -fstack-protector is enabled when compiling. This function shall be executed if buffer overflow check
 * fails.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark -fstack-protector-strong/-fstack_protector flag shall be enabled when compiling.

 */
void __stack_chk_fail(void)
{
	ASSERT(false, "stack check fails in HV\n");
}

/**
 * @}
 */
