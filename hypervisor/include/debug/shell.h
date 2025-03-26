/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef SHELL_H
#define SHELL_H

/**
 * @addtogroup debug
 *
 * @{
 */

/**
 * @file
 * @brief Shell Interface
 *
 * This file declares functions for initializing and managing a shell interface
 * within the system. These functions provide an interface for setting up and
 * interacting with a command-line shell, allowing users to execute commands
 * and perform administrative tasks.
 */

void shell_init(void);
void shell_kick(void);

/**
 * @}
 */
#endif /* SHELL_H */
