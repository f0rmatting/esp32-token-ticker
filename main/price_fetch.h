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
void price_fetch_first(void);

void price_fetch_start(void);
