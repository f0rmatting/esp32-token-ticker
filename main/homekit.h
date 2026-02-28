/*
 * SPDX-FileCopyrightText: 2025-2026 Bruce
 * SPDX-License-Identifier: CC-BY-NC-4.0
 */

#pragma once

void homekit_init(void);
void homekit_send_switch_press(void);       // SinglePress (0)
void homekit_send_switch_double_press(void); // DoublePress (1)
const char *homekit_get_setup_code(void);
int  homekit_get_paired_count(void);
void homekit_reset(void);
