/*
 * Copyright (C) 2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <dxe.h>
#include <pci.h>
#include <asm/pgtable.h>

uint64_t get_mmcfg_base(__unused uint16_t bdf)
{
	struct pci_mmcfg_region *mmcfg = get_mmcfg_region();
	return (uint64_t)hpa2hva(mmcfg->address);
}

int32_t register_diagnostics_on_msi(__unused uint16_t bdf,
		__unused int32_t (*cb)(void *data), __unused void *data)
{
	return 0;
}

int32_t unregister_diagnostics_on_msi(__unused uint16_t bdf)
{
	return 0;
}

int32_t register_diagnostics_on_msix(__unused uint16_t bdf, __unused uint32_t vector,
		__unused int32_t (*cb)(void *data), __unused void *data)
{
	return 0;
}

int32_t unregister_diagnostics_on_msix(__unused uint16_t bdf, __unused uint32_t vector)
{
	return 0;
}

