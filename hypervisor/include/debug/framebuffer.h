/*
 * Copyright (C) 2021 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

void fb_init(bool early_boot);
bool fb_need_log();
void fb_log(char *buffer);

#endif /* FRAMEBUFFER_H */
