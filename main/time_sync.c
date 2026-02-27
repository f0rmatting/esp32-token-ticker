/*
 * SPDX-FileCopyrightText: 2025-2026 Bruce
 * SPDX-License-Identifier: CC-BY-NC-4.0
 */

#include "time_sync.h"
#include "wifi_prov.h"

#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <time.h>
#include <string.h>

static const char *TAG = "time_sync";

// Re-sync every 30 minutes
#define RESYNC_INTERVAL_MS  (30 * 60 * 1000)

static void time_resync_task(void *arg)
{
    (void)arg;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(RESYNC_INTERVAL_MS));
        ESP_LOGI(TAG, "Periodic SNTP re-sync...");
        esp_err_t ret = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15000));
        if (ret == ESP_OK) {
            time_t now;
            struct tm ti;
            time(&now);
            localtime_r(&now, &ti);
            ESP_LOGI(TAG, "Time re-synced: %04d-%02d-%02d %02d:%02d:%02d",
                     ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
                     ti.tm_hour, ti.tm_min, ti.tm_sec);
        } else {
            ESP_LOGW(TAG, "Re-sync timed out");
        }
    }
}

esp_err_t time_sync_init(void)
{
    // Read timezone from NVS, default to UTC if not configured
    char tz_str[64] = "UTC0";
    if (wifi_prov_get_saved_tz(tz_str, sizeof(tz_str)) == ESP_OK) {
        ESP_LOGI(TAG, "Using saved timezone: %s", tz_str);
    } else {
        ESP_LOGI(TAG, "No saved timezone, defaulting to UTC");
    }
    setenv("TZ", tz_str, 1);
    tzset();

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    config.sync_cb = NULL;
    esp_err_t ret = esp_netif_sntp_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SNTP init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "SNTP initialized, waiting for time sync...");

    // Wait up to 15s for time to be set
    ret = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15000));
    if (ret == ESP_OK) {
        time_t now;
        struct tm ti;
        time(&now);
        localtime_r(&now, &ti);
        ESP_LOGI(TAG, "Time synced: %04d-%02d-%02d %02d:%02d:%02d",
                 ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
                 ti.tm_hour, ti.tm_min, ti.tm_sec);
    } else {
        ESP_LOGW(TAG, "Time sync timed out, clock may be inaccurate");
    }

    // Start periodic re-sync task
    xTaskCreate(time_resync_task, "time_resync", 3072, NULL, 3, NULL);

    return ESP_OK;
}
