/*
 * Copyright (C) 2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Virtual Backlight for VMs
 *
 */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include "log.h"
#include "vdisplay.h"

#define BACKLIGHT_PATH  "/sys/class/backlight"

int
check_backlist_device(char *name)
{
	char bl_path[256];
	int ret = 0;
	if (!name)
		return -1;
	snprintf(bl_path, sizeof(bl_path), "%s/%s", BACKLIGHT_PATH, name);
	ret = access(bl_path, F_OK);
	if (ret < 0) {
		return -1;
	}
	return 0;
}

static int
sysfs_read_property_file(const char *fname, char *buf, size_t sz)
{
	int fd;
	int rc;

	if (!buf || !sz)
		return -EINVAL;

	fd = open(fname, O_RDONLY);
	if (fd < 0) {
		pr_err("open failed %s %d\n", fname, errno);
		return -1;
	}

	rc = read(fd, buf, sz);

	close(fd);
	return rc;
}

static int
sysfs_read_property_int32(const char *fname, int32_t *int32_property)
{
	char buf[32] = {0};
	long int res;

	if (sysfs_read_property_file(fname, buf, sizeof(buf) - 1) < 0)
		return -1;

	res = strtol(buf, NULL, 10);
	if (errno == ERANGE)
		return -1;

	*int32_property = res;

	return 0;
}

static int
sysfs_write_property_file(const char *fname, char *buf, size_t sz)
{
	int fd;
	int rc;

	if (!buf || !sz)
		return -EINVAL;

	fd = open(fname, O_WRONLY);
	if (fd < 0) {
		pr_err("open failed %s %d\n", fname, errno);
		return -1;
	}

	rc = write(fd, buf, sz);

	close(fd);
	return rc;
}

static int
sysfs_write_property_int32(const char *fname, int32_t int32_property)
{
	char buf[32] = {0};

	snprintf(buf, sizeof(buf), "%d\n", int32_property); 
	if (sysfs_write_property_file(fname, buf, strlen(buf)) < 0)
		return -1;

	return 0;
}


int
set_backlight_brightness(char *name, int32_t brightness)
{
	char bl_path[256];

	snprintf(bl_path, sizeof(bl_path), "%s/%s/brightness", BACKLIGHT_PATH,
			name);

	return sysfs_write_property_int32(bl_path, brightness);
}

int
set_backlight_power(char *name, int32_t power)
{
	char bl_path[256];

	snprintf(bl_path, sizeof(bl_path), "%s/%s/bl_power", BACKLIGHT_PATH,
			name);
	return sysfs_write_property_int32(bl_path, power);
}

int
get_backlight_brightness(char *name, int32_t *brightness)
{
	char bl_path[256];

	snprintf(bl_path, sizeof(bl_path), "%s/%s/actual_brightness", BACKLIGHT_PATH,
			name);
	return sysfs_read_property_int32(bl_path, brightness);
}

enum backlight_type {
	BACKLIGHT_RAW = 1,
	BACKLIGHT_PLATFORM,
	BACKLIGHT_FIRMWARE,
	BACKLIGHT_TYPE_MAX,
};

enum backlight_scale {
	BACKLIGHT_SCALE_UNKNOWN = 0,
	BACKLIGHT_SCALE_LINEAR,
	BACKLIGHT_SCALE_NON_LINEAR,
};

static uint32_t
backlight_type(char *buf)
{
	uint32_t ret = BACKLIGHT_RAW;
	if (!buf)
		return ret;
	if (strncmp(buf, "raw", strlen("raw")) == 0) {
		return BACKLIGHT_RAW;
	} else if (strncmp(buf, "firmware", strlen("firmware")) == 0) {
		return BACKLIGHT_FIRMWARE;
	} else if (strncmp(buf, "platform", strlen("platform")) == 0) {
		return BACKLIGHT_PLATFORM;
	} else {
		return ret;
	}
	return ret;
}

static uint32_t
backlight_scale(char *buf)
{
	uint32_t ret = BACKLIGHT_SCALE_UNKNOWN;
	if (!buf)
		return ret;
	if (strncmp(buf, "unknown", strlen("unknown")) == 0) {
		return BACKLIGHT_SCALE_UNKNOWN;
	} else if (strncmp(buf, "linear", strlen("linear")) == 0) {
		return BACKLIGHT_SCALE_LINEAR;
	} else if (strncmp(buf, "non-linear", strlen("non-linear")) == 0) {
		return BACKLIGHT_SCALE_NON_LINEAR;
	} else {
		return ret;
	}
	return ret;
}

int
get_backlight_brightness_info(char *name, struct backlight_info * info)
{
	char bl_path[256];
	int32_t brightness = 0;
	int32_t max_brightness = 100;
	int32_t power = 0;
	int32_t type = 1;
	int32_t scale = 0;
	char buf_type[32] = {0};
	char buf_scale[32] = {0};

	snprintf(bl_path, sizeof(bl_path), "%s/%s/brightness", BACKLIGHT_PATH,
			name);
	sysfs_read_property_int32(bl_path, &brightness);
	info->brightness = brightness;
	snprintf(bl_path, sizeof(bl_path), "%s/%s/max_brightness", BACKLIGHT_PATH,
			name);
	sysfs_read_property_int32(bl_path, &max_brightness);
	info->max_brightness = max_brightness;
	snprintf(bl_path, sizeof(bl_path), "%s/%s/bl_power", BACKLIGHT_PATH,
			name);
	sysfs_read_property_int32(bl_path, &power);
	info->power = power;
	snprintf(bl_path, sizeof(bl_path), "%s/%s/type", BACKLIGHT_PATH,
			name);
	sysfs_read_property_file(bl_path, buf_type, sizeof(buf_type) -1);
	type = backlight_type(buf_type);
	info->type = type;
	snprintf(bl_path, sizeof(bl_path), "%s/%s/scale", BACKLIGHT_PATH,
			name);
	sysfs_read_property_file(bl_path, buf_scale, sizeof(buf_scale) -1);
	scale = backlight_scale(buf_scale);
	info->scale = scale;
	return 0;
}
