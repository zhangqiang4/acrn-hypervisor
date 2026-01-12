/*
 * Copyright (C) 2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#ifndef __USB_PMAPPER_H__
#define __USB_PMAPPER_H__

#include "usb/core.h"

struct pmapper;
int usb_pmapper_init(void);
void usb_pmapper_deinit(void);
int usb_pmapper_add(struct usb_hcd *hcd, uint8_t vport, uint8_t bus, struct usb_devpath path);
void usb_pmapper_del(struct usb_hcd *hcd);

#endif
