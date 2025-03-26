/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef DBG_CMD_H
#define DBG_CMD_H

/**
 * @addtogroup debug
 *
 * @{
 */

/**
 * @file
 * @brief Debug Command Handler Interface
 *
 * This file declares the interface for handling debug commands within the system.
 * It provides a function to process and execute commands received through a debug
 * interface, allowing for dynamic interaction and control during debugging sessions.
 */
bool handle_dbg_cmd(const char *cmd, int32_t len);

/**
 * @}
 */
#endif /* DBG_CMD_H */
