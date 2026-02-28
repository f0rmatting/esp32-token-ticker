/*
 * SPDX-FileCopyrightText: 2025-2026 Bruce
 * SPDX-License-Identifier: CC-BY-NC-4.0
 */

#include "ui_internal.h"
#include "board_config.h"
#include "wifi_prov.h"
#include "homekit.h"

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"

static const char *TAG = "button";

static volatile bool s_btn_pressed = false;

#define LONG_PRESS_MS     3000
#define LONG_PRESS_POLL   50

static void IRAM_ATTR btn_isr_handler(void *arg)
{
    s_btn_pressed = true;
}

static bool btn_is_held(void)
{
    return gpio_get_level(BTN_BOOT_GPIO) == 0; // active low
}

static void btn_task(void *arg)
{
    (void)arg;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(50));
        if (s_btn_pressed) {
            s_btn_pressed = false;

            // Check for long press: poll GPIO for 3 seconds
            bool long_pressed = true;
            int polls = LONG_PRESS_MS / LONG_PRESS_POLL;
            for (int i = 0; i < polls; i++) {
                vTaskDelay(pdMS_TO_TICKS(LONG_PRESS_POLL));
                if (!btn_is_held()) {
                    long_pressed = false;
                    break;
                }
            }

            if (long_pressed) {
                // Long press 3s -> reset WiFi + HomeKit, enter provisioning
                ESP_LOGI(TAG, "Long press detected, entering config mode");
                homekit_reset();
                wifi_prov_start(); // blocks forever (reboots after config)
                continue;
            }

            // Button was released before 3s — check double click
            // Clear any interrupt that fired during long-press polling
            s_btn_pressed = false;
            vTaskDelay(pdMS_TO_TICKS(300));
            if (s_btn_pressed) {
                // Double click -> toggle info panel
                s_btn_pressed = false;
                if (lvgl_port_lock(100)) {
                    toggle_info_panel();
                    lvgl_port_unlock();
                }
            } else {
                // Single click
                if (s_show_info) {
                    // Info panel: send HomeKit switch event to Apple Home
                    homekit_send_switch_press();
                } else {
                    // Crypto view: switch focused coin
                    if (lvgl_port_lock(100)) {
                        switch_focus();
                        lvgl_port_unlock();
                    }
                }
            }
        }
    }
}

void btn_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = BIT64(BTN_BOOT_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&cfg);
    esp_err_t ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "ISR service install failed: %s", esp_err_to_name(ret));
        return;
    }
    gpio_isr_handler_add(BTN_BOOT_GPIO, btn_isr_handler, NULL);
    ESP_LOGI(TAG, "BOOT button on GPIO%d", BTN_BOOT_GPIO);
    xTaskCreate(btn_task, "btn", 2048, NULL, 5, NULL);
}
