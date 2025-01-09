/*
 * Copyright (C) 2019-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @addtogroup vp-base_vm_config
 *
 * @{
 */

/**
 * @file
 * @brief PCI device helper utilities header
 *
 * This file is the header for helper utility initializing ACRN device
 * configuration.
 */

#ifndef PCI_DEV_H_
#define PCI_DEV_H_

#include <asm/vm_config.h>

struct pci_pdev;
struct acrn_vm_pci_dev_config *init_one_dev_config(struct pci_pdev *pdev);

#endif /* PCI_DEV_H_ */

/**
 * @}
 */
