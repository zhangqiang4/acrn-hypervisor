/*
 * Copyright (C) 2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <dxe.h>
#include "diagnostics.h"

__unused int32_t initialize_diagnostics(__unused uint64_t routine_mask, __unused void *data)
{
	return 0;
}

__unused int32_t run_diagnostics(uint64_t routine_mask, void *data)
{
	int32_t ret = 0;

	if (routine_mask & BIT(DIAG_ENABLE_CRC)) {
		ret = telltale_enable_crc(data);
		if (ret)
			return ret;
	}
	if (routine_mask & BIT(DIAG_DISABLE_CRC)) {
		ret = telltale_disable_crc(data);
		if (ret)
			return ret;
	}
	if (routine_mask & BIT(DIAG_GET_CRC)) {
		ret = telltale_get_crc(data);
		if (ret)
			return ret;
	}

	return 0;
}
