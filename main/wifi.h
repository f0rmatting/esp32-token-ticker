/*
 * SPDX-FileCopyrightText: 2025-2026 Bruce
 * SPDX-License-Identifier: CC-BY-NC-4.0
 */

#pragma once

#include "esp_err.h"

/**
 * Initialize Wi-Fi in STA mode and connect.
 * Blocks until connected or timeout (10s).
 * Returns ESP_OK on success.
 */
esp_err_t wifi_init_sta(void);

/**
 * Get the current IP address as a string.
 * Returns "N/A" if not connected.
 */
const char *wifi_get_ip_str(void);

/**
 * Get the connected SSID.
 * Returns "N/A" if not connected.
 */
const char *wifi_get_ssid(void);

/**
 * Get the current RSSI (signal strength) in dBm.
 * Returns 0 if not available.
 */
int wifi_get_rssi(void);
