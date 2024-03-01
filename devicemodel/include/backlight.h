/*
 * Copyright (C) 2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Virtual Backlight for VMs
 *
 */
#ifndef _BACKLIGHT_H_
#define _BACKLIGHT_H_

#define MAX_BACKLIGHT_DEVICE 4

struct backlight_info {
	int32_t brightness;
	int32_t max_brightness;
	int32_t power;
	int32_t type;
	int32_t scale;
};

struct backlight_properties {
	int32_t brightness;
	int32_t power;
};

int set_backlight_brightness(char *name, int32_t brightness);

int set_backlight_power(char *name, int32_t power);

int get_backlight_brightness(char *name, int32_t *brightness);

int get_backlight_brightness_info(char *name, struct backlight_info * info);

int check_backlist_device(char *name);

#endif /* _BACKLIGHT_H_ */
