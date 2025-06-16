/*
 * Project Acrn
 * Acrn-dm: virtio-spi based power management
 *
 * Copyright (C) 2019-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#ifndef	__PM_VSPI__
#define	__PM_VSPI__

#include "virtio_spi.h"

extern struct vspidev_be vspidev_pm_be;

int pm_vspi_init(struct vmctx *ctx);
void pm_vspi_deinit(struct vmctx *ctx);

#endif
