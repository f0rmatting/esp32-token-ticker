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
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>
#include <stdlib.h>

static const char *TAG = "price_fetch";

#define MAX_RESP_LEN      1024
#define MAX_HIST_RESP_LEN 8192

static const char *s_pairs[] = {
    "BTC_USDT",
    "ETH_USDT",
    "PAXG_USDT",
};
#define PAIR_COUNT 3

// ── HTTP response buffer ────────────────────────────────────────────
typedef struct {
    char  buf[MAX_RESP_LEN];
    int   len;
} resp_buf_t;

static resp_buf_t s_resp;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    resp_buf_t *resp = (resp_buf_t *)evt->user_data;

    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (resp->len + evt->data_len < MAX_RESP_LEN) {
            memcpy(resp->buf + resp->len, evt->data, evt->data_len);
            resp->len += evt->data_len;
        }
        break;
    default:
        break;
    }
    return ESP_OK;
}

// ── Parse ticker JSON and update UI ─────────────────────────────────
static bool parse_ticker(int idx, const char *pair, const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        ESP_LOGE(TAG, "JSON parse failed for %s", pair);
        return false;
    }

    bool ok = false;
    cJSON *item = NULL;

    if (cJSON_IsArray(root)) {
        item = cJSON_GetArrayItem(root, 0);
    } else if (cJSON_IsObject(root)) {
        item = root;
    }

    if (item) {
        cJSON *j_last = cJSON_GetObjectItem(item, "last");
        cJSON *j_chg  = cJSON_GetObjectItem(item, "change_percentage");
        cJSON *j_high = cJSON_GetObjectItem(item, "high_24h");
        cJSON *j_low  = cJSON_GetObjectItem(item, "low_24h");

        if (j_last && j_chg) {
            double price      = atof(cJSON_GetStringValue(j_last));
            double change_pct = atof(cJSON_GetStringValue(j_chg));
            double high_24h   = j_high ? atof(cJSON_GetStringValue(j_high)) : price;
            double low_24h    = j_low  ? atof(cJSON_GetStringValue(j_low))  : price;

            ESP_LOGI(TAG, "%s: $%.2f  %+.2f%%  H:%.2f L:%.2f",
                     pair, price, change_pct, high_24h, low_24h);
            ui_update_price(idx, price, change_pct, high_24h, low_24h);
            ok = true;
        }
    }

    if (!ok) {
        ESP_LOGE(TAG, "Missing fields in response for %s", pair);
    }

    cJSON_Delete(root);
    return ok;
}

// ── Create persistent HTTP client ───────────────────────────────────
static esp_http_client_handle_t create_client(void)
{
    esp_http_client_config_t cfg = {
        .url               = "https://api.gateio.ws/api/v4/spot/tickers?currency_pair=BTC_USDT",
        .event_handler     = http_event_handler,
        .user_data         = &s_resp,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms        = 8000,
        .keep_alive_enable = true,
    };
    return esp_http_client_init(&cfg);
}

// ── FreeRTOS task ───────────────────────────────────────────────────
static void price_fetch_task(void *arg)
{
    (void)arg;

    esp_http_client_handle_t client = create_client();
    if (!client) {
        ESP_LOGE(TAG, "HTTP client init failed");
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        for (int i = 0; i < PAIR_COUNT; i++) {
            char url[128];
            snprintf(url, sizeof(url),
                     "https://api.gateio.ws/api/v4/spot/tickers?currency_pair=%s",
                     s_pairs[i]);

            esp_http_client_set_url(client, url);
            s_resp.len = 0;

            esp_err_t err = esp_http_client_perform(client);
            int status = esp_http_client_get_status_code(client);

            if (err != ESP_OK || status != 200) {
                ESP_LOGE(TAG, "HTTP failed for %s: err=%s status=%d",
                         s_pairs[i], esp_err_to_name(err), status);
                // Connection might be broken — recreate client
                if (err != ESP_OK) {
                    esp_http_client_cleanup(client);
                    vTaskDelay(pdMS_TO_TICKS(2000));
                    client = create_client();
                    if (!client) {
                        ESP_LOGE(TAG, "HTTP client re-init failed");
                        vTaskDelay(pdMS_TO_TICKS(10000));
                        client = create_client();
                    }
                    break;  // restart the for loop with fresh connection
                }
                continue;
            }

            s_resp.buf[s_resp.len] = '\0';
            parse_ticker(i, s_pairs[i], s_resp.buf);
        }
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

// ── Heap-allocated response buffer for large responses ───────────────
typedef struct {
    char *buf;
    int   len;
    int   cap;
} heap_resp_t;

static esp_err_t heap_event_handler(esp_http_client_event_t *evt)
{
    heap_resp_t *resp = (heap_resp_t *)evt->user_data;

    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (resp->len + evt->data_len < resp->cap) {
            memcpy(resp->buf + resp->len, evt->data, evt->data_len);
            resp->len += evt->data_len;
        }
        break;
    default:
        break;
    }
    return ESP_OK;
}

// ── Fetch 24h candlestick history to pre-fill charts ─────────────────
void price_fetch_history(void)
{
    char *buf = malloc(MAX_HIST_RESP_LEN);
    if (!buf) {
        ESP_LOGE(TAG, "Failed to allocate history buffer");
        return;
    }

    heap_resp_t resp = { .buf = buf, .len = 0, .cap = MAX_HIST_RESP_LEN };

    esp_http_client_config_t cfg = {
        .url               = "https://api.gateio.ws/api/v4/spot/candlesticks",
        .event_handler     = heap_event_handler,
        .user_data         = &resp,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms        = 10000,
        .keep_alive_enable = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        ESP_LOGE(TAG, "HTTP client init failed for history");
        free(buf);
        return;
    }

    for (int i = 0; i < PAIR_COUNT; i++) {
        char url[160];
        snprintf(url, sizeof(url),
                 "https://api.gateio.ws/api/v4/spot/candlesticks"
                 "?currency_pair=%s&interval=30m&limit=%d",
                 s_pairs[i], CHART_POINTS);

        esp_http_client_set_url(client, url);
        resp.len = 0;

        esp_err_t err = esp_http_client_perform(client);
        int status = esp_http_client_get_status_code(client);

        if (err != ESP_OK || status != 200) {
            ESP_LOGE(TAG, "History fetch failed for %s: err=%s status=%d",
                     s_pairs[i], esp_err_to_name(err), status);
            continue;
        }

        resp.buf[resp.len] = '\0';

        // Parse candlestick array: [[ts, vol, close, high, low, open, base_vol, closed], ...]
        cJSON *root = cJSON_Parse(resp.buf);
        if (!root || !cJSON_IsArray(root)) {
            ESP_LOGE(TAG, "History JSON parse failed for %s", s_pairs[i]);
            cJSON_Delete(root);
            continue;
        }

        int n = cJSON_GetArraySize(root);
        if (n > CHART_POINTS) n = CHART_POINTS;

        double prices[CHART_POINTS];
        for (int j = 0; j < n; j++) {
            cJSON *candle = cJSON_GetArrayItem(root, j);
            if (candle && cJSON_IsArray(candle)) {
                cJSON *close = cJSON_GetArrayItem(candle, 2);  // close price
                prices[j] = close ? atof(cJSON_GetStringValue(close)) : 0;
            }
        }

        ui_set_chart_history(i, prices, n);
        ESP_LOGI(TAG, "History loaded for %s: %d candles", s_pairs[i], n);

        cJSON_Delete(root);
    }

    esp_http_client_cleanup(client);
    free(buf);
    ESP_LOGI(TAG, "Chart history pre-fill completed");
}

void price_fetch_first(void)
{
    esp_http_client_handle_t client = create_client();
    if (!client) {
        ESP_LOGE(TAG, "HTTP client init failed for first fetch");
        return;
    }

    for (int i = 0; i < PAIR_COUNT; i++) {
        char url[128];
        snprintf(url, sizeof(url),
                 "https://api.gateio.ws/api/v4/spot/tickers?currency_pair=%s",
                 s_pairs[i]);

        esp_http_client_set_url(client, url);
        s_resp.len = 0;

        esp_err_t err = esp_http_client_perform(client);
        int status = esp_http_client_get_status_code(client);

        if (err != ESP_OK || status != 200) {
            ESP_LOGE(TAG, "First fetch failed for %s: err=%s status=%d",
                     s_pairs[i], esp_err_to_name(err), status);
            continue;
        }

        s_resp.buf[s_resp.len] = '\0';
        parse_ticker(i, s_pairs[i], s_resp.buf);
    }

    esp_http_client_cleanup(client);
    ESP_LOGI(TAG, "First price fetch completed");
}

void price_fetch_start(void)
{
    xTaskCreate(price_fetch_task, "price", 8192, NULL, 4, NULL);
    ESP_LOGI(TAG, "Price fetch task started");
}
