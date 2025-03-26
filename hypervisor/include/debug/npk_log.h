/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef NPK_LOG_H
#define NPK_LOG_H

/**
 * @addtogroup debug
 *
 * @{
 */

/**
 * @file
 * @brief NPK Logging Interface
 *
 * This file declares functions for setting up and managing NPK (North Peak PCI device)
 * logging. These functions provide an interface for configuring logging parameters,
 * writing log entries, and determining the necessity of logging based on severity.
 * They are used to facilitate efficient and persistent logging within the system.
 */

struct hv_npk_log_param;

void npk_log_setup(struct hv_npk_log_param *param);
void npk_log_write(const char *buf, size_t len);

bool npk_need_log(uint32_t severity);
void npk_log(char *buffer);

/**
 * @}
 */
#endif /* NPK_LOG_H */
