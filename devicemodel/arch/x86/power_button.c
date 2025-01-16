/*
 * Copyright (C) 2019-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <errno.h>

#include "vmmapi.h"
#include "acpi.h"
#include "mevent.h"
#include "monitor.h"
#include "log.h"

#define POWER_BUTTON_NAME	"power_button"

static bool monitor_run;

static int
vm_stop_handler(void *arg)
{
	if (!arg)
		return -EINVAL;

	inject_power_button_event(arg);
	return 0;
}

static int
vm_suspend_handler(void *arg)
{
	/*
	 * Invoke vm_stop_handler directly in here since suspend of User VM is
	 * set by User VM power button setting.
	 */
	return vm_stop_handler(arg);
}

static struct monitor_vm_ops vm_ops = {
	.stop = vm_stop_handler,
	.suspend = vm_suspend_handler,
};

void
power_button_init(struct vmctx *ctx)
{
	/*
	 * Suspend or shutdown User VM by acrnctl suspend and
	 * stop command.
	 */
	if (!monitor_run && monitor_register_vm_ops(&vm_ops, ctx, POWER_BUTTON_NAME) == 0) {
		monitor_run = true;
	} else {
		pr_err("power_button: failed to register vm ops\n");
	}
}

void
power_button_deinit(struct vmctx *ctx __attribute__((unused)))
{
	monitor_run = false;
}
