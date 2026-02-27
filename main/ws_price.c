/*
 * SPDX-FileCopyrightText: 2025-2026 Bruce
 * SPDX-License-Identifier: CC-BY-NC-4.0
 */

#include "ws_price.h"
#include "ui.h"

#include "esp_websocket_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"

#include <string.h>
#include <sys/time.h>

static const char *TAG = "ws_price";

static const char *s_pairs[] = {
    "BTC_USDT",
    "ETH_USDT",
    "PAXG_USDT",
};
#define PAIR_COUNT 3

static esp_websocket_client_handle_t s_client;
static esp_timer_handle_t s_debounce_timer;
static esp_timer_handle_t s_ping_timer;

static volatile int s_active_idx  = -1;   // coin currently subscribed
static volatile int s_pending_idx = -1;   // coin waiting for debounce
static volatile bool s_connected  = false;

// ── Send helpers ──────────────────────────────────────────────────────
static void send_json(const char *channel, const char *event,
                      const char *payload)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);

    char msg[192];
    if (payload) {
        snprintf(msg, sizeof(msg),
                 "{\"time\":%ld,\"channel\":\"%s\",\"event\":\"%s\","
                 "\"payload\":[\"%s\"]}",
                 (long)tv.tv_sec, channel, event, payload);
    } else {
        snprintf(msg, sizeof(msg),
                 "{\"time\":%ld,\"channel\":\"%s\"}",
                 (long)tv.tv_sec, channel);
    }
    esp_websocket_client_send_text(s_client, msg, strlen(msg), pdMS_TO_TICKS(2000));
}

static void send_subscribe(int idx)
{
    if (idx < 0 || idx >= PAIR_COUNT || !s_connected) return;
    ESP_LOGI(TAG, "subscribe %s", s_pairs[idx]);
    send_json("spot.tickers", "subscribe", s_pairs[idx]);
    s_active_idx = idx;
}

static void send_unsubscribe(int idx)
{
    if (idx < 0 || idx >= PAIR_COUNT || !s_connected) return;
    ESP_LOGI(TAG, "unsubscribe %s", s_pairs[idx]);
    send_json("spot.tickers", "unsubscribe", s_pairs[idx]);
    s_active_idx = -1;
}

// ── 30s ping ─────────────────────────────────────────────────────────
static void ping_timer_cb(void *arg)
{
    (void)arg;
    if (s_connected) {
        send_json("spot.ping", NULL, NULL);
    }
}

// ── 2s debounce for coin switch ──────────────────────────────────────
static void debounce_timer_cb(void *arg)
{
    (void)arg;
    int idx = s_pending_idx;
    if (idx >= 0 && idx < PAIR_COUNT) {
        send_subscribe(idx);
    }
}

// ── Parse incoming message ───────────────────────────────────────────
static void handle_data(const char *data, int len)
{
    cJSON *root = cJSON_ParseWithLength(data, len);
    if (!root) return;

    cJSON *j_channel = cJSON_GetObjectItem(root, "channel");
    cJSON *j_event   = cJSON_GetObjectItem(root, "event");

    if (!j_channel || !j_event ||
        strcmp(cJSON_GetStringValue(j_channel), "spot.tickers") != 0 ||
        strcmp(cJSON_GetStringValue(j_event), "update") != 0) {
        cJSON_Delete(root);
        return;
    }

    cJSON *result = cJSON_GetObjectItem(root, "result");
    if (!result) { cJSON_Delete(root); return; }

    cJSON *j_last = cJSON_GetObjectItem(result, "last");
    cJSON *j_chg  = cJSON_GetObjectItem(result, "change_percentage");
    cJSON *j_high = cJSON_GetObjectItem(result, "high_24h");
    cJSON *j_low  = cJSON_GetObjectItem(result, "low_24h");

    if (!j_last || !j_chg) { cJSON_Delete(root); return; }

    double price      = atof(cJSON_GetStringValue(j_last));
    double change_pct = atof(cJSON_GetStringValue(j_chg));
    double high_24h   = j_high ? atof(cJSON_GetStringValue(j_high)) : price;
    double low_24h    = j_low  ? atof(cJSON_GetStringValue(j_low))  : price;

    // Compute latency: server time vs local time
    cJSON *j_time = cJSON_GetObjectItem(root, "time");
    int latency_ms = 0;
    if (j_time) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        long server_sec = (long)cJSON_GetNumberValue(j_time);
        latency_ms = (int)((tv.tv_sec - server_sec) * 1000 +
                           tv.tv_usec / 1000);
        if (latency_ms < 0) latency_ms = 0;
    }

    int idx = s_active_idx;
    if (idx >= 0) {
        ESP_LOGD(TAG, "WS %s: $%.2f %+.2f%% lat=%dms",
                 s_pairs[idx], price, change_pct, latency_ms);
        ui_update_price_ws(idx, price, change_pct, high_24h, low_24h,
                           latency_ms);
    }

    cJSON_Delete(root);
}

// ── WebSocket event handler ──────────────────────────────────────────
static void ws_event_handler(void *arg, esp_event_base_t base,
                             int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WS connected");
        s_connected = true;
        // Re-subscribe on reconnect
        if (s_active_idx >= 0) {
            send_subscribe(s_active_idx);
        } else if (s_pending_idx >= 0) {
            send_subscribe(s_pending_idx);
        }
        break;

    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "WS disconnected");
        s_connected = false;
        break;

    case WEBSOCKET_EVENT_DATA:
        if (data->op_code == 0x01 && data->data_len > 0 &&
            data->payload_offset == 0 &&
            data->payload_len == data->data_len) {
            handle_data(data->data_ptr, data->data_len);
        }
        break;

    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "WS error");
        break;

    default:
        break;
    }
}

// ── Public API ───────────────────────────────────────────────────────
void ws_price_start(int focus_idx)
{
    if (s_client) return;  // already running

    s_pending_idx = focus_idx;

    esp_websocket_client_config_t cfg = {
        .uri                  = "wss://api.gateio.ws/ws/v4/",
        .crt_bundle_attach    = esp_crt_bundle_attach,
        .task_stack           = 6144,
        .buffer_size          = 1024,
        .reconnect_timeout_ms = 5000,
    };

    s_client = esp_websocket_client_init(&cfg);
    if (!s_client) {
        ESP_LOGE(TAG, "WS client init failed");
        return;
    }

    esp_websocket_register_events(s_client, WEBSOCKET_EVENT_ANY,
                                  ws_event_handler, NULL);
    esp_websocket_client_start(s_client);

    // Debounce timer (one-shot, not started yet)
    esp_timer_create_args_t debounce_args = {
        .callback = debounce_timer_cb,
        .name     = "ws_debounce",
    };
    esp_timer_create(&debounce_args, &s_debounce_timer);

    // 30s ping timer
    esp_timer_create_args_t ping_args = {
        .callback = ping_timer_cb,
        .name     = "ws_ping",
    };
    esp_timer_create(&ping_args, &s_ping_timer);
    esp_timer_start_periodic(s_ping_timer, 30 * 1000 * 1000);  // 30s in µs

    ESP_LOGI(TAG, "WS client started, will subscribe to idx=%d", focus_idx);
}

void ws_price_switch(int new_idx)
{
    if (!s_client) return;
    if (new_idx < 0 || new_idx >= PAIR_COUNT) return;

    // Unsubscribe current immediately
    int old = s_active_idx;
    if (old >= 0) {
        send_unsubscribe(old);
    }

    s_pending_idx = new_idx;

    // Reset debounce: subscribe after 2s of stability
    esp_timer_stop(s_debounce_timer);
    esp_timer_start_once(s_debounce_timer, 2 * 1000 * 1000);  // 2s in µs
}

void ws_price_stop(void)
{
    if (s_ping_timer) {
        esp_timer_stop(s_ping_timer);
        esp_timer_delete(s_ping_timer);
        s_ping_timer = NULL;
    }
    if (s_debounce_timer) {
        esp_timer_stop(s_debounce_timer);
        esp_timer_delete(s_debounce_timer);
        s_debounce_timer = NULL;
    }
    if (s_client) {
        esp_websocket_client_close(s_client, pdMS_TO_TICKS(2000));
        esp_websocket_client_destroy(s_client);
        s_client = NULL;
    }
    s_connected   = false;
    s_active_idx  = -1;
    s_pending_idx = -1;
    ESP_LOGI(TAG, "WS client stopped");
}

int ws_price_get_active_idx(void)
{
    return s_active_idx;
}
