/*
 * SPDX-FileCopyrightText: 2025-2026 Bruce
 * SPDX-License-Identifier: CC-BY-NC-4.0
 */

#include "price_fetch.h"
#include "ui.h"
#include "ui_internal.h"

#include "esp_http_client.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_system.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>
#include <stdlib.h>

static const char *TAG = "price_fetch";

#define MAX_RESP_LEN          1024
#define MAX_HIST_RESP_LEN     10240
#define TICKER_TIMEOUT_MS     5000
#define HISTORY_TIMEOUT_MS    15000
#define MAX_RETRIES           2

static const char *s_pairs[] = {
    "BTC_USDT",
    "ETH_USDT",
    "PAXG_USDT",
    "SUI_USDT",
};
#define PAIR_COUNT 4

static esp_http_client_handle_t s_client;

typedef struct {
    char *buf;
    int   len;
    int   cap;
} resp_buf_t;

static char s_resp_buf[MAX_RESP_LEN];
static resp_buf_t s_resp = { .buf = s_resp_buf, .len = 0, .cap = MAX_RESP_LEN };

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    resp_buf_t *resp = (resp_buf_t *)evt->user_data;
    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (resp->len + evt->data_len < resp->cap - 1) {
            memcpy(resp->buf + resp->len, evt->data, evt->data_len);
            resp->len += evt->data_len;
        }
        break;
    default:
        break;
    }
    return ESP_OK;
}

static bool parse_ticker(int idx, const char *pair, const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return false;

    bool ok = false;
    cJSON *item = cJSON_IsArray(root) ? cJSON_GetArrayItem(root, 0) : root;

    if (item) {
        cJSON *j_last = cJSON_GetObjectItem(item, "last");
        cJSON *j_chg  = cJSON_GetObjectItem(item, "change_percentage");
        cJSON *j_high = cJSON_GetObjectItem(item, "high_24h");
        cJSON *j_low  = cJSON_GetObjectItem(item, "low_24h");

        const char *s_last = j_last ? cJSON_GetStringValue(j_last) : NULL;
        const char *s_chg  = j_chg  ? cJSON_GetStringValue(j_chg)  : NULL;
        const char *s_high = j_high ? cJSON_GetStringValue(j_high) : NULL;
        const char *s_low  = j_low  ? cJSON_GetStringValue(j_low)  : NULL;

        if (s_last && s_chg) {
            ui_update_price(idx, atof(s_last), atof(s_chg), 
                            s_high ? atof(s_high) : 0, s_low ? atof(s_low) : 0);
            ok = true;
        }
    }
    cJSON_Delete(root);
    return ok;
}

static bool ensure_client(int timeout_ms)
{
    if (s_client) {
        esp_http_client_set_timeout_ms(s_client, timeout_ms);
        return true;
    }
    esp_http_client_config_t cfg = {
        .url = "https://api.gateio.ws/api/v4/spot/tickers",
        .event_handler = http_event_handler,
        .user_data = &s_resp,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = timeout_ms,
        .keep_alive_enable = true,
    };
    s_client = esp_http_client_init(&cfg);
    if (!s_client) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return false;
    }
    return true;
}

static void reset_client(void)
{
    if (s_client) {
        esp_http_client_close(s_client);
        esp_http_client_cleanup(s_client);
        s_client = NULL;
    }
}

static bool fetch_ticker(int idx)
{
    char url[128];
    snprintf(url, sizeof(url), "https://api.gateio.ws/api/v4/spot/tickers?currency_pair=%s", s_pairs[idx]);

    for (int retry = 0; retry <= MAX_RETRIES; retry++) {
        if (!ensure_client(TICKER_TIMEOUT_MS)) return false;
        esp_http_client_set_url(s_client, url);
        s_resp.len = 0;

        esp_err_t err = esp_http_client_perform(s_client);
        int status = esp_http_client_get_status_code(s_client);

        if (err == ESP_OK && status == 200) {
            s_resp.buf[s_resp.len] = '\0';
            return parse_ticker(idx, s_pairs[idx], s_resp.buf);
        }

        if (status == 429) {
            ESP_LOGW(TAG, "Rate limited (429), backoff...");
            vTaskDelay(pdMS_TO_TICKS(5000)); // Wait longer on rate limit
        } else {
            ESP_LOGW(TAG, "Fetch %s failed (err=%d, status=%d), retry %d", s_pairs[idx], err, status, retry);
            reset_client();
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    return false;
}

static void price_fetch_task(void *arg)
{
    (void)arg;
    while (1) {
        for (int i = 0; i < PAIR_COUNT; i++) {
            fetch_ticker(i);
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

void price_fetch_history(void)
{
    // Memory Defense: need ~10KB buffer + ~15KB cJSON overhead + HTTP/TLS state
    uint32_t free_heap = esp_get_free_heap_size();
    ESP_LOGI(TAG, "History sync: free heap = %lu bytes", (unsigned long)free_heap);
    if (free_heap < MAX_HIST_RESP_LEN * 5) {
        ESP_LOGE(TAG, "Low memory (%lu), skipping history sync", (unsigned long)free_heap);
        return;
    }

    char *buf = malloc(MAX_HIST_RESP_LEN);
    if (!buf) {
        ESP_LOGE(TAG, "Failed to alloc history buffer");
        return;
    }

    // Temp price array on heap to avoid 384 bytes on app_main stack
    double *prices = (double *)malloc(CHART_POINTS * sizeof(double));
    if (!prices) {
        free(buf);
        return;
    }

    resp_buf_t saved = s_resp;
    s_resp = (resp_buf_t){ .buf = buf, .len = 0, .cap = MAX_HIST_RESP_LEN };

    for (int i = 0; i < PAIR_COUNT; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));   // Yield to watchdog between coins

        bool ok = false;
        // Boot phase: only 1 retry with shorter timeout to avoid watchdog
        for (int retry = 0; retry <= 1; retry++) {
            if (!ensure_client(8000)) break;   // 8s timeout (< watchdog)
            char url[160];
            snprintf(url, sizeof(url), "https://api.gateio.ws/api/v4/spot/candlesticks?currency_pair=%s&interval=30m&limit=%d",
                     s_pairs[i], CHART_POINTS);
            esp_http_client_set_url(s_client, url);
            s_resp.len = 0;

            vTaskDelay(pdMS_TO_TICKS(10));    // Yield before blocking HTTP

            esp_err_t err = esp_http_client_perform(s_client);
            int status = esp_http_client_get_status_code(s_client);

            vTaskDelay(pdMS_TO_TICKS(10));    // Yield after HTTP

            if (err == ESP_OK && status == 200) {
                s_resp.buf[s_resp.len] = '\0';
                cJSON *root = cJSON_Parse(s_resp.buf);
                if (root && cJSON_IsArray(root)) {
                    int n = cJSON_GetArraySize(root);
                    if (n > CHART_POINTS) n = CHART_POINTS;
                    for (int j = 0; j < n; j++) {
                        cJSON *candle = cJSON_GetArrayItem(root, j);
                        cJSON *close = cJSON_GetArrayItem(candle, 2);
                        const char *s_close = close ? cJSON_GetStringValue(close) : NULL;
                        prices[j] = s_close ? atof(s_close) : 0;
                    }
                    ui_set_chart_history(i, prices, n);
                    ok = true;
                }
                cJSON_Delete(root);
                if (ok) break;
            } else {
                ESP_LOGW(TAG, "History %s failed (err=%d, status=%d), retry %d",
                         s_pairs[i], err, status, retry);
                reset_client();
                vTaskDelay(pdMS_TO_TICKS(500));  // Short backoff + yield
            }
        }
        if (!ok) {
            ESP_LOGW(TAG, "Skipping history for %s", s_pairs[i]);
        }
    }
    s_resp = saved;
    free(prices);
    free(buf);
}

void price_fetch_first(void)
{
    for (int i = 0; i < PAIR_COUNT; i++) fetch_ticker(i);
}

void price_fetch_start(void)
{
    xTaskCreate(price_fetch_task, "price", 8192, NULL, 4, NULL);
}
