/*
 * SPDX-FileCopyrightText: 2025-2026 Bruce
 * SPDX-License-Identifier: CC-BY-NC-4.0
 */

#pragma once

/**
 * Start the background task that fetches live crypto prices
 * from the Gate.io API every 10 seconds.
 * Call after WiFi is connected.
 */
/**
 * Synchronous first fetch of all prices during boot.
 * Populates g_crypto[] so UI can show prices immediately.
 * Call before ui_init().
 */
/**
 * Set priority coin for background chart history loading.
 * Called from UI when user switches focus or at init.
 */
void price_fetch_prioritize_chart(int idx);

/**
 * Notify price_fetch that the focused token changed.
 * After a 3s delay, the new token will be fetched immediately.
 * Rapid successive calls cancel previous pending fetches.
 */
void price_fetch_on_focus_change(int new_idx);

void price_fetch_first(void);

void price_fetch_start(void);
