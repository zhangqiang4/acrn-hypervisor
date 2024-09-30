/*
 * Copyright (C) 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <sys/param.h>
#include <sys/stat.h>
#include <errno.h>
#include <paths.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <fcntl.h>
#include <dirent.h>
#include <time.h>
#include <pthread.h>

#include "dm.h"
#include "log.h"

#define DISK_PREFIX  "disk_log: "

#define LOG_PATH_NODE "/var/log/acrn-dm/"
#define LOG_NAME_PREFIX  "%s_log_"
#define LOG_NAME_FMT  "%s%s_log_%d" /* %s-->vm1/vm2..., %d-->1/2/3/4... */
#define LOG_DELIMITER "\n\n----------------new vm instance------------------\n\n"

#define FILE_NAME_LENGTH  96

#define LOG_SIZE_LIMIT    0x200000 /* one log file size limit */
#define LOG_FILES_COUNT   8

static FILE *disk_file = NULL;
static uint32_t cur_log_size;
static uint16_t cur_file_index;

static uint8_t disk_log_level = LOG_DEBUG;
static bool disk_log_enabled = false;
static pthread_mutex_t disk_write_lock = PTHREAD_MUTEX_INITIALIZER;

#define INDEX_AFTER(a, b) ((short int)b - (short int)a < 0)

static bool is_disk_log_enabled(void)
{
	return disk_log_enabled;
}

static uint8_t get_disk_log_level(void)
{
	return disk_log_level;
}

static int probe_disk_log_file(void)
{
	char file_name[FILE_NAME_LENGTH];
	struct dirent *pdir;
	struct stat st;
	int length;
	uint16_t index = 0, tmp;
	bool is_first_file = true;
	DIR *dir;

	if (stat(LOG_PATH_NODE, &st)) {
		if (system("mkdir -p " LOG_PATH_NODE) < 0) {
			printf(DISK_PREFIX"create path: %s failed! Error: %s\n",
				LOG_PATH_NODE, strerror(errno));
			return -1;
		}
	}

	dir = opendir(LOG_PATH_NODE);
	if (!dir) {
		printf(DISK_PREFIX" open %s failed! Error: %s\n",
			LOG_PATH_NODE, strerror(errno));
		return -1;
	}

	snprintf(file_name, FILE_NAME_LENGTH - 1, LOG_NAME_PREFIX, vmname);
	length = strlen(file_name);

	while ((pdir = readdir(dir)) != NULL) {
		if (!(pdir->d_type & DT_REG))
			continue;

		if (strncmp(pdir->d_name, file_name, length) != 0)
			continue;

		tmp = (uint16_t)atoi(pdir->d_name + length);
		if (is_first_file) {
			is_first_file = false;
			index = tmp;
		} else if (INDEX_AFTER(tmp, index)) {
			index = tmp;
		}
	}
	closedir(dir);

	snprintf(file_name, FILE_NAME_LENGTH - 1, LOG_NAME_FMT, LOG_PATH_NODE, vmname, index);
	disk_file = fopen(file_name, "a+");
	if (disk_file == NULL) {
		printf(DISK_PREFIX" open %s failed! Error: %s\n", file_name, strerror(errno));
		return -1;
	}
	if (chmod(file_name, 0644)) {
		printf(DISK_PREFIX" chmod %s failed! Error: %s\n", file_name, strerror(errno));
	}

	if (fprintf(disk_file, LOG_DELIMITER) < 0) {
		printf(DISK_PREFIX" write %s failed! Error: %s\n", file_name, strerror(errno));
		return -1;
	}

	stat(file_name, &st);
	cur_log_size = st.st_size;
	cur_file_index = index;

	return 0;
}

static int init_disk_logger(bool enable, uint8_t log_level)
{
	disk_log_enabled = enable;
	disk_log_level = log_level;

	return 1;
}

static void deinit_disk_logger(void)
{
	if (disk_file != NULL) {
		disk_log_enabled = false;

		fflush(disk_file);
		fclose(disk_file);
		disk_file = NULL;
	}
}

static void write_to_disk(const char *fmt, va_list args)
{
	char file_name[FILE_NAME_LENGTH];
	char *buf;
	int len;
	int write_cnt;
	struct timespec times = {0, 0};
	struct tm *lt;
	time_t tt;


	if ((disk_file == NULL) && disk_log_enabled) {
		/**
		 * usually this probe just be called once in DM whole life; but we need use vmname in
		 * probe_disk_log_file, it can't be called in init_disk_logger for vmname not inited then,
		 * so call it here.
		 */
		if (probe_disk_log_file() < 0) {
			disk_log_enabled = false;
			return;
		}
	}

	len = vasprintf(&buf, fmt, args);
	if (len < 0)
		return;

	time(&tt);
	lt = localtime(&tt);
	clock_gettime(CLOCK_MONOTONIC, &times);

	write_cnt = fprintf(disk_file, "[%4d-%02d-%02d %02d:%02d:%02d][%5lu.%06lu] %s",
		lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday, lt->tm_hour, lt->tm_min, lt->tm_sec,
		times.tv_sec, times.tv_nsec / 1000, buf);
	fflush(disk_file);

	if (write_cnt < 0) {
		printf(DISK_PREFIX"write disk failed");
		free(buf);
		fclose(disk_file);
		disk_file = NULL;
		return;
	}
	free(buf);

	cur_log_size += write_cnt;
	if (cur_log_size > LOG_SIZE_LIMIT) {

		cur_file_index++;

		/* remove the first old log file, to add a new one */
		snprintf(file_name, FILE_NAME_LENGTH - 1, LOG_NAME_FMT,
			LOG_PATH_NODE, vmname, (uint16_t)(cur_file_index - LOG_FILES_COUNT));
		remove(file_name);

		snprintf(file_name, FILE_NAME_LENGTH - 1, LOG_NAME_FMT,
			LOG_PATH_NODE, vmname, cur_file_index);

		fclose(disk_file);
		disk_file = fopen(file_name, "a+");
		if (disk_file == NULL) {
			printf(DISK_PREFIX" open %s failed! Error: %s\n", file_name, strerror(errno));
			return;
		}
		if (chmod(file_name, 0644)) {
			printf(DISK_PREFIX" chmod %s failed! Error: %s\n", file_name, strerror(errno));
		}
		cur_log_size = 0;
	}
}

static void write_to_disk_lock(const char *fmt, va_list args)
{
	pthread_mutex_lock(&disk_write_lock);
	write_to_disk(fmt, args);
	pthread_mutex_unlock(&disk_write_lock);
}

static struct logger_ops logger_disk = {
	.name = "disk",
	.is_enabled = is_disk_log_enabled,
	.get_log_level = get_disk_log_level,
	.init = init_disk_logger,
	.deinit = deinit_disk_logger,
	.output = write_to_disk_lock,
};

DEFINE_LOGGER_DEVICE(logger_disk);
