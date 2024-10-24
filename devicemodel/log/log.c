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
#include  <time.h>
#include  <sys/time.h>

#include "dm_string.h"
#include "log.h"


DECLARE_LOGGER_SECTION();

extern char *vmname;
static char *level_strs[] = {
	/* log level starts from 1 */
	"",
	"ERRO",
	"WARN",
	"NOTICE",
	"INFO",
	"DEBUG",
};
#define PREFIX_MAX_LEN 50

#define LOG_DEBUG_DOMAIN_MAX 8
static char *domains[LOG_DEBUG_DOMAIN_MAX];
static size_t domain_cnt;
static bool domain_all;
bool debug_log = false;

bool domain_selected(const char *domain_prefix)
{
	/* suppress logs from domains we don't care */
	if (domain_all) {
		return true;
	} else {
		for (int i = 0; i < domain_cnt; i++) {
			if (!strncmp(domain_prefix, domains[i], strlen(domains[i]))) {
				return true;
			}
		}
		return false;
	}
}

/*
 * --logger_setting: console,level=4;disk,level=4;kmsg,level=3[;debug_domains=<all|a,b,c>]
 * the setting param is from acrn-dm input, will be parsed here
 */
int init_logger_setting(const char *opt)
{
	char *orig, *str, *elem, *name, *level;
	uint32_t lvl_val;
	int error = 0;
	struct logger_ops **pp_logger, *plogger;

	orig = str = strdup(opt);
	if (!str) {
		fprintf(stderr, "%s: strdup returns NULL\n", __func__);
		return -1;
	}

	for (elem = strsep(&str, ";"); elem != NULL; elem = strsep(&str, ";")) {
		if (!strncmp(elem, "debug_domains=", 14)) {
			debug_log = true;
			elem += 14;
			if (!strcmp(elem, "all")) {
				domain_all = true;
				continue;
			}

			for (char *domain = strsep(&elem, ","); domain; domain = strsep(&elem, ",")) {
				if (domain_cnt < LOG_DEBUG_DOMAIN_MAX) {
					domains[domain_cnt++] = strdup(domain);
				} else {
					fprintf(stderr, "logger setting error: too many debug domains!\n");
				}
			}
			printf("logger: debug domains:");
			for (int i = 0; i < domain_cnt; i++) {
				printf(i == 0 ? "%s": ",%s", domains[i]);
			}
			printf("\n");
		} else {
			name = strsep(&elem, ",");
			level = elem;

			if ((strncmp(level, "level=", 6) != 0) || (dm_strtoui(level + 6, &level, 10, &lvl_val))) {
				fprintf(stderr, "logger setting param error: %s, please check!\n", elem);
				error = -1;
				break;
			}

			printf("logger: name=%s, level=%d\n", name, lvl_val);

			plogger = NULL;
			FOR_EACH_LOGGER(pp_logger) {
				plogger = *pp_logger;
				if (strcmp(name, plogger->name) == 0) {
					if (plogger->init)
						plogger->init(true, (uint8_t)lvl_val);

					break;
				}
			}

			if (plogger == NULL) {
				fprintf(stderr, "there is no logger: %s found in DM, please check!\n", name);
				error = -1;
				break;
			}
		}
	}

	free(orig);
	return error;
}

void deinit_loggers(void)
{
	struct logger_ops **pp_logger, *plogger;

	FOR_EACH_LOGGER(pp_logger) {
		plogger = *pp_logger;
		if (plogger->deinit)
			plogger->deinit();
	}

	for (int i = 0; i < domain_cnt; i++) {
		free(domains[i]);
	}
}

static void fill_logger_prefix_str(uint8_t level, char *prefix_str, uint32_t size)
{
	struct timeval tv;
	struct tm *lt;
	int rc;

	gettimeofday(&tv, NULL);
	lt = localtime(&tv.tv_sec);

	/* add the log prefix, e.g. [2024-09-23 03:53:39.737][VM1][DEBUG] */
	rc = snprintf(prefix_str, size, "[%4d-%02d-%02d %02d:%02d:%02d.%03ld][%s][%s]",
		lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday, lt->tm_hour, lt->tm_min, lt->tm_sec,
		tv.tv_usec / 1000, vmname? vmname : "UNINITVM", level_strs[level]);
	if(rc < 0 || rc >= size)
		memcpy(prefix_str, "acrn-dm:", strlen("acrn-dm:"));

	return;
}

void output_log(uint8_t level, const char *fmt, ...)
{
	va_list args;
	struct logger_ops **pp_logger, *logger;
	char prefix_str[PREFIX_MAX_LEN] = " ";

	/* check each logger flag and level, to output */
	FOR_EACH_LOGGER(pp_logger) {
		logger = *pp_logger;
		if (logger->is_enabled() && (level <= logger->get_log_level()) && (logger->output)) {
			if(prefix_str[0] == ' ')
				fill_logger_prefix_str(level, prefix_str, PREFIX_MAX_LEN);
			va_start(args, fmt);
			logger->output(prefix_str, fmt, args);
			va_end(args);
		}
	}
}

/* console setting and its API interface */
static uint8_t console_log_level = DEFAULT_LOG_LEVEL;
static bool console_enabled = true;

static bool is_console_enabled(void)
{
	return console_enabled;
}

static uint8_t get_console_log_level(void)
{
	return console_log_level;
}

static int init_console_setting(bool enable, uint8_t log_level)
{
	console_enabled = enable;
	console_log_level = log_level;

	return 0;
}

static void write_to_console(const char *prefix_str, const char *fmt, va_list args)
{
	int len;
	char *buf;

	len = vasprintf(&buf, fmt, args);
	if (len < 0)
		return;
	printf("%s %s", prefix_str, buf);
	free(buf);
}

static struct logger_ops logger_console = {
	.name = "console",
	.is_enabled = is_console_enabled,
	.get_log_level = get_console_log_level,
	.init = init_console_setting,
	.output = write_to_console,
};


DEFINE_LOGGER_DEVICE(logger_console);
