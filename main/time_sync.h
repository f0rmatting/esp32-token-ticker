/*
 * SPDX-FileCopyrightText: 2025-2026 Bruce
 * SPDX-License-Identifier: CC-BY-NC-4.0
 */

#pragma once

#include "esp_err.h"

/**
 * Initialize SNTP time synchronization.
 * Call after Wi-Fi is connected. Sets timezone to CST-8 (China).
 */
esp_err_t time_sync_init(void);
