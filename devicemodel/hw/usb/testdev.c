/*
 * Copyright (C) 2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#include <string.h>
#include <libusb.h>

#define pr_prefix "usb: test-dev: "
#include "usb/core.h"
#include "log.h"

struct usb_testdev_data {

};

static void usb_transfer_testdev(struct usb_device *dev, struct usb_transfer *xfer)
{
}

static bool usb_match_testdev(const char *opts)
{
	return !strncmp(opts, "usb-testdev", 11);
}

static int usb_init_testdev(struct usb_device *dev, const char *opts)
{

	return 0;
}

static void usb_deinit_testdev(struct usb_device *dev)
{

}

static int usb_reset_testdev(struct usb_device *dev)
{
	return -1;
}

struct usb_device_backend usb_backend_testdev = {
	.name = "usb-testdev",
	.dev_priv_size = sizeof(struct usb_testdev_data),
	.xfer_priv_size = 0,
	.reset = usb_reset_testdev,
	.transfer = usb_transfer_testdev,
	.match = usb_match_testdev,
	.init = usb_init_testdev,
	.deinit = usb_deinit_testdev,
};
