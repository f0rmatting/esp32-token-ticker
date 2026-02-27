/*
 * SPDX-FileCopyrightText: 2025-2026 Bruce
 * SPDX-License-Identifier: CC-BY-NC-4.0
 */

#pragma once

#include <stdbool.h>
#include "esp_err.h"

/**
 * Initialize WS2812B LED strip.
 */
esp_err_t led_init(void);

/**
 * Set the idle breathing color based on 24h change direction.
 * true = 24h up (green breathing), false = 24h down (red breathing).
 */
void led_set_market_mood(bool is_24h_up);

/**
 * Flash LED to indicate a price tick.
 * Bright green (up) or red (down) for ~2s, then resumes breathing.
 */
void led_flash_price(bool went_up);
