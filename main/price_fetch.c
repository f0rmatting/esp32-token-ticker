/*
 * SPDX-FileCopyrightText: 2025-2026 Bruce
 * SPDX-License-Identifier: CC-BY-NC-4.0
 */

#include "price_fetch.h"
#include "ui.h"
#include "ui_internal.h"
#include "token_config.h"
#include "homekit.h"

#include "esp_http_client.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

#include <string.h>
#include <stdlib.h>

static const char *TAG = "price_fetch";

#define MAX_RESP_LEN          1024
#define MAX_HIST_RESP_LEN     10240
#define TICKER_TIMEOUT_MS     3000
#define HISTORY_TIMEOUT_MS    10000
#define MAX_RETRIES           2

// ── Polling intervals ──────────────────────────────────────────────
#define FOCUS_POLL_MS          10000   // focused token: 10s
#define BG_POLL_MS             600000  // background tokens: 10min
#define FOCUS_SWITCH_DELAY_MS  3000    // delay after switching focus
#define LOOP_TICK_MS           2000    // main loop step: 2s


// ── Price alert: surge/crash detection for focused coin ─────────────
#define ALERT_SURGE_PCT    5.0   // +5% 24h change → surge
#define ALERT_CRASH_PCT   -5.0   // -5% 24h change → crash
#define ALERT_RESET_PCT    3.0   // must come back within ±3% to re-arm

static bool s_surge_sent;
static bool s_crash_sent;

static void check_price_alert(int idx, double change_pct)
{
    if (idx != s_focus_idx) return;

    if (!s_surge_sent && change_pct >= ALERT_SURGE_PCT) {
        s_surge_sent = true;
        homekit_send_switch_press();
        ESP_LOGW(TAG, "SURGE alert: %s %.1f%%", g_crypto[idx].symbol, change_pct);
    } else if (s_surge_sent && change_pct < ALERT_RESET_PCT) {
        s_surge_sent = false;
    }

    if (!s_crash_sent && change_pct <= ALERT_CRASH_PCT) {
        s_crash_sent = true;
        homekit_send_switch_double_press();
        ESP_LOGW(TAG, "CRASH alert: %s %.1f%%", g_crypto[idx].symbol, change_pct);
    } else if (s_crash_sent && change_pct > -ALERT_RESET_PCT) {
        s_crash_sent = false;
    }
}

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
            double chg = atof(s_chg);
            ui_update_price(idx, atof(s_last), chg,
                            s_high ? atof(s_high) : 0, s_low ? atof(s_low) : 0);
            check_price_alert(idx, chg);
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
    const char *pair = g_crypto[idx].pair;
    if (!pair) {
        // Stablecoin with no trading pair — report $1.00
        ui_update_price(idx, 1.0, 0.0, 1.0, 1.0);
        return true;
    }

    char url[128];
    snprintf(url, sizeof(url), "https://api.gateio.ws/api/v4/spot/tickers?currency_pair=%s", pair);

    for (int retry = 0; retry <= MAX_RETRIES; retry++) {
        if (!ensure_client(TICKER_TIMEOUT_MS)) return false;
        esp_http_client_set_url(s_client, url);
        s_resp.len = 0;

        esp_err_t err = esp_http_client_perform(s_client);
        int status = esp_http_client_get_status_code(s_client);

        if (err == ESP_OK && status == 200) {
            s_resp.buf[s_resp.len] = '\0';
            return parse_ticker(idx, pair, s_resp.buf);
        }

        if (status == 429) {
            ESP_LOGW(TAG, "Rate limited (429), backoff...");
            vTaskDelay(pdMS_TO_TICKS(5000));
        } else {
            ESP_LOGW(TAG, "Fetch %s failed (err=%d, status=%d), retry %d", pair, err, status, retry);
            reset_client();
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    return false;
}

// ── Focus-aware polling state ──────────────────────────────────────
static int64_t s_last_fetch_ms[MAX_TOKENS];      // per-token last fetch time
static volatile int s_pending_focus = -1;         // pending focus idx (-1=none)
static volatile int64_t s_focus_change_ms = 0;    // timestamp of last focus switch

// ── Background chart history loading ────────────────────────────────
static volatile int s_chart_priority = -1;      // user-requested coin, or -1
static bool s_chart_loaded[MAX_TOKENS];          // per-coin loaded flag

// Static buffers for chart history — reused across calls, avoids heap fragmentation
static char   s_hist_buf[MAX_HIST_RESP_LEN];
static double s_hist_prices[CHART_POINTS];

static bool fetch_one_history(int idx)
{
    const char *pair = g_crypto[idx].pair;
    if (!pair) return true;  // stablecoin, no chart needed

    resp_buf_t saved = s_resp;
    s_resp = (resp_buf_t){ .buf = s_hist_buf, .len = 0, .cap = MAX_HIST_RESP_LEN };

    bool ok = false;
    for (int retry = 0; retry <= 1; retry++) {
        if (!ensure_client(HISTORY_TIMEOUT_MS)) break;
        char url[160];
        snprintf(url, sizeof(url),
                 "https://api.gateio.ws/api/v4/spot/candlesticks?"
                 "currency_pair=%s&interval=30m&limit=%d",
                 pair, CHART_POINTS);
        esp_http_client_set_url(s_client, url);
        s_resp.len = 0;

        esp_err_t err = esp_http_client_perform(s_client);
        int status = esp_http_client_get_status_code(s_client);

        if (err == ESP_OK && status == 200) {
            s_resp.buf[s_resp.len] = '\0';
            cJSON *root = cJSON_Parse(s_resp.buf);
            if (root && cJSON_IsArray(root)) {
                int n = cJSON_GetArraySize(root);
                if (n > CHART_POINTS) n = CHART_POINTS;
                for (int j = 0; j < n; j++) {
                    cJSON *candle = cJSON_GetArrayItem(root, j);
                    if (!candle) continue;
                    cJSON *close = cJSON_GetArrayItem(candle, 2);
                    const char *s_close = close ? cJSON_GetStringValue(close) : NULL;
                    s_hist_prices[j] = s_close ? atof(s_close) : 0;
                }
                ui_set_chart_history(idx, s_hist_prices, n);
                ok = true;
            }
            cJSON_Delete(root);
            if (ok) break;
        } else {
            ESP_LOGW(TAG, "History %s failed (err=%d, status=%d), retry %d",
                     pair, err, status, retry);
            reset_client();
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }

    s_resp = saved;
    return ok;
}

/* Pick next coin whose chart is not yet loaded.
 * Priority goes to s_chart_priority if set, otherwise sequential. */
static int pick_next_chart(void)
{
    int prio = s_chart_priority;
    if (prio >= 0 && prio < g_active_count && !s_chart_loaded[prio])
        return prio;
    for (int i = 0; i < g_active_count; i++) {
        if (!s_chart_loaded[i]) return i;
    }
    return -1;  // all loaded
}

static void price_fetch_task(void *arg)
{
    (void)arg;

    while (1) {
        int64_t now = esp_timer_get_time() / 1000;

        // 1. Check pending focus switch (3s delay expired & still current focus)
        int pf = s_pending_focus;
        if (pf >= 0) {
            if (pf != s_focus_idx) {
                s_pending_focus = -1;   // switched away, cancel
            } else if (now - s_focus_change_ms >= FOCUS_SWITCH_DELAY_MS) {
                fetch_ticker(pf);
                s_last_fetch_ms[pf] = now;
                s_pending_focus = -1;
                ESP_LOGI(TAG, "Focus switch fetch: %s", g_crypto[pf].symbol);
            }
        }

        // 2. Load one pending chart per cycle
        int ci = pick_next_chart();
        if (ci >= 0) {
            if (fetch_one_history(ci)) {
                s_chart_loaded[ci] = true;
                ESP_LOGI(TAG, "Chart loaded: %s", g_crypto[ci].symbol);
            }
        }

        // 3. Focused token: poll every FOCUS_POLL_MS
        int focus = s_focus_idx;
        if (now - s_last_fetch_ms[focus] >= FOCUS_POLL_MS) {
            fetch_ticker(focus);
            s_last_fetch_ms[focus] = now;
        }

        // 4. Background tokens: poll every BG_POLL_MS, max 1 per loop
        for (int i = 0; i < g_active_count; i++) {
            if (i == focus) continue;
            if (now - s_last_fetch_ms[i] >= BG_POLL_MS) {
                fetch_ticker(i);
                s_last_fetch_ms[i] = now;
                break;   // only 1 background token per loop
            }
        }

        vTaskDelay(pdMS_TO_TICKS(LOOP_TICK_MS));
    }
}

void price_fetch_prioritize_chart(int idx)
{
    if (idx >= 0 && idx < g_active_count) {
        s_chart_priority = idx;
    }
}

void price_fetch_on_focus_change(int new_idx)
{
    s_pending_focus = new_idx;
    s_focus_change_ms = esp_timer_get_time() / 1000;
}

void price_fetch_first(void)
{
    /* Fetch all current prices */
    for (int i = 0; i < g_active_count; i++) fetch_ticker(i);

    /* Record fetch time so background task doesn't re-fetch immediately */
    int64_t now = esp_timer_get_time() / 1000;
    for (int i = 0; i < g_active_count; i++) s_last_fetch_ms[i] = now;

    /* Pre-load charts for first 2 tokens so UI enters with chart ready */
    for (int i = 0; i < 2 && i < g_active_count; i++) {
        if (fetch_one_history(i)) {
            s_chart_loaded[i] = true;
            ESP_LOGI(TAG, "Boot chart ready: %s", g_crypto[i].symbol);
        }
    }
}

void price_fetch_start(void)
{
    xTaskCreate(price_fetch_task, "price", 6144, NULL, 4, NULL);
}
