/*
 * SPDX-FileCopyrightText: 2025-2026 Bruce
 * SPDX-License-Identifier: CC-BY-NC-4.0
 */

#pragma once

#include "esp_err.h"
#include <stddef.h>

/**
 * Start SoftAP provisioning mode (blocks until user submits or timeout).
 * Reboots the device after credentials are saved.
 */
void wifi_prov_start(void);

/**
 * Read saved WiFi credentials from NVS.
 * Returns ESP_OK if credentials exist, ESP_ERR_NOT_FOUND otherwise.
 */
esp_err_t wifi_prov_get_saved_creds(char *ssid, size_t ssid_len,
                                     char *pass, size_t pass_len);

/**
 * Read saved timezone from NVS (POSIX TZ string, e.g. "CST-8").
 * Returns ESP_OK if found, ESP_ERR_NOT_FOUND otherwise.
 */
esp_err_t wifi_prov_get_saved_tz(char *tz, size_t tz_len);
