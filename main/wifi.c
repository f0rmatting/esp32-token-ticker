/*
 * SPDX-FileCopyrightText: 2025-2026 Bruce
 * SPDX-License-Identifier: CC-BY-NC-4.0
 */

#include "wifi.h"
#include "wifi_prov.h"

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/timers.h"
#include "nvs_flash.h"

#include <string.h>

static const char *TAG = "wifi";

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1
#define WIFI_BOOT_RETRY     3     // fast retries during initial connect
#define WIFI_RECONNECT_MS   10000 // backoff for background reconnects

static EventGroupHandle_t s_wifi_event_group;
static int  s_retry_count = 0;
static bool s_initial_connect = true;  // true until first successful connect
static char s_ip_str[20] = "N/A";

static void reconnect_timer_cb(TimerHandle_t timer)
{
    (void)timer;
    ESP_LOGI(TAG, "Attempting reconnect...");
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        // connect() failed synchronously — schedule another attempt
        ESP_LOGW(TAG, "Reconnect call failed (%s), retrying in %ds",
                 esp_err_to_name(err), WIFI_RECONNECT_MS / 1000);
        xTimerStart(timer, 0);
    }
    // If connect() succeeded (async), a DISCONNECTED or GOT_IP event
    // will follow. On DISCONNECTED the timer is restarted by event_handler.
}

static TimerHandle_t s_reconnect_timer;

static void event_handler(void *arg, esp_event_base_t base,
                           int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_initial_connect) {
            // During boot: fast retries, then signal failure
            if (s_retry_count < WIFI_BOOT_RETRY) {
                esp_wifi_connect();
                s_retry_count++;
                ESP_LOGI(TAG, "Retrying connection (%d/%d)", s_retry_count, WIFI_BOOT_RETRY);
            } else {
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                ESP_LOGW(TAG, "Boot connection failed after %d retries", WIFI_BOOT_RETRY);
            }
        } else {
            // After boot: keep trying with backoff
            ESP_LOGW(TAG, "WiFi disconnected, reconnecting in %ds",
                     WIFI_RECONNECT_MS / 1000);
            if (s_reconnect_timer) {
                xTimerStart(s_reconnect_timer, 0);
            }
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Connected, IP: %s", s_ip_str);
        s_retry_count = 0;
        s_initial_connect = false;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_init_sta(void)
{
    // Init NVS (required by Wi-Fi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "NVS init failed");

    s_wifi_event_group = xEventGroupCreate();

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "Netif init failed");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "Event loop failed");
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "WiFi init failed");

    esp_event_handler_instance_t h_wifi, h_ip;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        &event_handler, NULL, &h_wifi);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        &event_handler, NULL, &h_ip);

    // Read credentials from NVS; if none, enter provisioning mode
    char nvs_ssid[33] = {0}, nvs_pass[65] = {0};
    if (wifi_prov_get_saved_creds(nvs_ssid, sizeof(nvs_ssid),
                                   nvs_pass, sizeof(nvs_pass)) != ESP_OK) {
        ESP_LOGW(TAG, "No WiFi credentials in NVS, entering config mode");
        wifi_prov_start(); // blocks, reboots after user configures
    }

    wifi_config_t wifi_cfg = {0};
    memcpy(wifi_cfg.sta.ssid, nvs_ssid, sizeof(wifi_cfg.sta.ssid));
    memcpy(wifi_cfg.sta.password, nvs_pass, sizeof(wifi_cfg.sta.password));

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "Set mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg), TAG, "Set config failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "WiFi start failed");

    // ── Power Optimization: Enable Modem-Sleep ────────────────────
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    ESP_LOGI(TAG, "WiFi Power Save enabled (Modem-Sleep)");

    // Create one-shot reconnect timer (used after boot for backoff)
    s_reconnect_timer = xTimerCreate("wifi_rc", pdMS_TO_TICKS(WIFI_RECONNECT_MS),
                                     pdFALSE, NULL, reconnect_timer_cb);

    ESP_LOGI(TAG, "Connecting to %s ...", nvs_ssid);

    // Wait for connection or failure (6s timeout)
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(6000));

    if (bits & WIFI_CONNECTED_BIT) {
        return ESP_OK;
    }

    // Switch to background reconnect mode so the timer keeps retrying
    s_initial_connect = false;
    if (s_reconnect_timer) {
        xTimerStart(s_reconnect_timer, 0);
    }

    ESP_LOGW(TAG, "WiFi connection timed out, will keep retrying in background");
    return ESP_ERR_TIMEOUT;
}

const char *wifi_get_ip_str(void)
{
    return s_ip_str;
}

const char *wifi_get_ssid(void)
{
    static char ssid_buf[33] = "N/A";
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        strncpy(ssid_buf, (const char *)ap.ssid, sizeof(ssid_buf) - 1);
        ssid_buf[sizeof(ssid_buf) - 1] = '\0';
    }
    return ssid_buf;
}

int wifi_get_rssi(void)
{
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        return ap.rssi;
    }
    return 0;
}
