/*
 * SPDX-FileCopyrightText: 2025-2026 Bruce
 * SPDX-License-Identifier: CC-BY-NC-4.0
 */

#pragma once

#include "esp_err.h"

/**
 * Initialize SPI bus, ST7789 LCD panel, backlight, and LVGL port.
 * After this call, LVGL is ready for UI creation.
 */
esp_err_t display_init(void);
