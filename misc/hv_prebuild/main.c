/*
 * Copyright (C) 2020-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <hv_prebuild.h>

int32_t main(void)
{
	int32_t ret = 0;

	if (!sanitize_vm_config()) {
		(void)printf("VM configuration check fail!\n");
		ret = -1;
	} else {
		(void)printf("VM configuration check pass!\n");
		ret = 0;
	}
	return ret;
}
