/*
 * SPDX-FileCopyrightText: 2025-2026 Bruce
 * SPDX-License-Identifier: CC-BY-NC-4.0
 */

#pragma once

/**
 * Show boot/loading screen with logo and progress bar.
 * Call after display_init(). Updates text/progress if already showing.
 * progress_pct: 0-100
 */
void ui_boot_show(const char *msg, int progress_pct);

/**
 * Remove the boot screen.
 */
void ui_boot_hide(void);

/**
 * Update price data for a crypto item.
 * idx: 0=BTC, 1=ETH, 2=PAXG
 * Thread-safe (acquires LVGL lock internally).
 */
void ui_update_price(int idx, double price, double change_pct,
                     double high_24h, double low_24h);

/**
 * Pre-fill chart history with candlestick close prices.
 * prices[] should be in chronological order (oldest first).
 * Called during boot before ui_init().
 */
void ui_set_chart_history(int idx, const double *prices, int count);

/**
 * Create the TokenTicker UI using LVGL.
 * Must be called after display_init().
 */
void ui_init(void);

/**
 * Tear down the entire UI safely (stop tasks, clear screen, null pointers).
 * Used before entering WiFi provisioning mode.
 */
void ui_cleanup(void);

/**
 * Show a clean WiFi configuration guide screen.
 * Call after ui_cleanup() to display provisioning instructions.
 */
void ui_show_wifi_config(void);

/**
 * Initialize BOOT button (single/double/long press).
 * Can be called early (before ui_init) so long-press provisioning
 * works during WiFi connection phase.
 */
void btn_init(void);
