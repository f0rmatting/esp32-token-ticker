/*
 * SPDX-FileCopyrightText: 2025-2026 Bruce
 * SPDX-License-Identifier: CC-BY-NC-4.0
 */

#include <stdio.h>
#include "esp_log.h"
#include "display.h"
#include "wifi.h"
#include "time_sync.h"
#include "ui.h"
#include "led.h"
#include "price_fetch.h"
#include "ws_price.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting TokenTicker");

    esp_err_t ret = display_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Display init failed: %s", esp_err_to_name(ret));
        return;
    }

    led_init();
    btn_init();  // early init so long-press provisioning works during WiFi connect

    // Show boot screen while connecting
    ui_boot_show("Initializing...", 10);

    ui_boot_show("Connecting WiFi...", 30);
    ret = wifi_init_sta();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WiFi not connected, continuing with mock data");
    } else {
        ui_boot_show("Syncing time...", 50);
        time_sync_init();

        ui_boot_show("Fetching prices...", 70);
        price_fetch_first();
    }

    ui_boot_show("Loading UI...", 90);

    // Remove boot screen, build main UI
    ui_boot_hide();
    ui_init();

    // Start live price polling (works even if WiFi failed — will retry)
    if (ret == ESP_OK) {
        price_fetch_start();
        ws_price_start(0);
    }

    ESP_LOGI(TAG, "TokenTicker UI ready");
}
