/*
 * SPDX-FileCopyrightText: 2025-2026 Bruce
 * SPDX-License-Identifier: CC-BY-NC-4.0
 */

#include <stdio.h>
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_system.h"
#include "display.h"
#include "wifi.h"
#include "time_sync.h"
#include "ui.h"
#include "led.h"
#include "price_fetch.h"
#include "token_config.h"
#include "homekit.h"

static const char *TAG = "main";

static void power_management_init(void)
{
#if CONFIG_PM_ENABLE
    // Disable light_sleep_enable because it gates the LEDC clock used for backlight
    esp_pm_config_t pm_config = {
#if defined(CONFIG_IDF_TARGET_ESP32S3)
        .max_freq_mhz = 240,
        .min_freq_mhz = 80,
#else
        .max_freq_mhz = 160,
        .min_freq_mhz = 40,
#endif
        .light_sleep_enable = false
    };
    ESP_ERROR_CHECK(esp_pm_configure(&pm_config));
    ESP_LOGI(TAG, "Power management initialized (DFS only)");
#endif
}

// Defined in ui_info.c — capture before WiFi/TLS allocations for accuracy
extern uint32_t s_heap_total;

void app_main(void)
{
    ESP_LOGI(TAG, "Starting TokenTicker");

    s_heap_total = esp_get_free_heap_size();

    power_management_init();

    esp_err_t ret = display_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Display init failed: %s", esp_err_to_name(ret));
        return;
    }

    token_config_load();
    led_init();
    btn_init();  // early init so long-press provisioning works during WiFi connect

    // Show boot screen while connecting
    ui_boot_show("Initializing...", 10);

    ui_boot_show("Connecting WiFi...", 30);
    ret = wifi_init_sta();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WiFi not connected, continuing with mock data");
    } else {
        ui_boot_show("Syncing time...", 40);
        time_sync_init();

        ui_boot_show("Fetching prices...", 50);
        price_fetch_first();

        ui_boot_show("Starting HomeKit...", 90);
        homekit_init();
    }

    ui_boot_show("Loading UI...", 95);

    // Remove boot screen, build main UI
    ui_boot_hide();
    ui_init();

    // Always start polling — if WiFi reconnects later, fetches will succeed
    price_fetch_start();

    ESP_LOGI(TAG, "TokenTicker UI ready");
}
