/*
 * SPDX-FileCopyrightText: 2025-2026 Bruce
 * SPDX-License-Identifier: CC-BY-NC-4.0
 */

#include "ui.h"
#include "ui_internal.h"
#include "led.h"
#include "crypto_logos.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "lvgl.h"

#include <sys/time.h>

static const char *TAG = "ui";

// ── Crypto data (populated by live price fetcher) ──────────────────
crypto_item_t g_crypto[] = {
    {"BTC", "Bitcoin",  &img_btc_logo, 0, 0, 0, 0, {0}},
    {"ETH", "Ethereum", &img_eth_logo, 0, 0, 0, 0, {0}},
    {"PAXG", "PAX Gold", &img_paxg_logo, 0, 0, 0, 0, {0}},
};

// ── Shared state ───────────────────────────────────────────────────
int  s_focus_idx = 0;
bool s_animating = false;
bool s_show_info = false;

// Loading overlay — shown until first prices arrive
lv_obj_t *s_loading_overlay;
static bool s_price_loaded[CRYPTO_COUNT];
static int  s_history_count[CRYPTO_COUNT];

// Price flash animation
static lv_timer_t *s_flash_timer;

// Price scroll animation
static int32_t s_price_scale = 100;

// Chart throttle: record one point per CHART_INTERVAL_MS
static int64_t s_last_chart_time[CRYPTO_COUNT];

// ── Main card widgets ──────────────────────────────────────────────
lv_obj_t *s_main_card;
static lv_obj_t *s_main_logo;
static lv_obj_t *s_main_sym;
static lv_obj_t *s_main_name;
static lv_obj_t *s_main_price;
static lv_obj_t *s_main_chg;
static lv_obj_t *s_main_hl;
static lv_obj_t *s_chart;
static lv_chart_series_t *s_chart_ser;

// ── Side card widgets ──────────────────────────────────────────────
lv_obj_t *s_side_card[2];
static lv_obj_t *s_side_logo[2];
static lv_obj_t *s_side_sym[2];
static lv_obj_t *s_side_price[2];
static lv_obj_t *s_side_chg[2];

// ── Boot screen ────────────────────────────────────────────────────
volatile bool s_ui_teardown = false;

static lv_obj_t *s_boot_scr;
static lv_obj_t *s_boot_label;
static lv_obj_t *s_boot_bar;

void ui_boot_show(const char *msg, int progress_pct)
{
    if (lvgl_port_lock(0)) {
        if (s_boot_scr) {
            lv_label_set_text(s_boot_label, msg);
            lv_bar_set_value(s_boot_bar, progress_pct, LV_ANIM_ON);
            lvgl_port_unlock();
            return;
        }

        lv_obj_t *scr = lv_screen_active();
        lv_obj_set_style_bg_color(scr, lv_color_hex(0x0F0F1A), 0);
        lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

        s_boot_scr = lv_obj_create(scr);
        lv_obj_set_size(s_boot_scr, LCD_H_RES, LCD_V_RES);
        lv_obj_center(s_boot_scr);
        lv_obj_set_style_bg_opa(s_boot_scr, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(s_boot_scr, 0, 0);
        lv_obj_set_style_pad_all(s_boot_scr, 0, 0);
        lv_obj_clear_flag(s_boot_scr, LV_OBJ_FLAG_SCROLLABLE);

        // Brand label
        lv_obj_t *brand = lv_label_create(s_boot_scr);
        lv_label_set_text(brand, "TokenTicker");
        lv_obj_set_style_text_color(brand, lv_color_hex(0x00E676), 0);
        lv_obj_set_style_text_font(brand, &lv_font_montserrat_24, 0);
        lv_obj_align(brand, LV_ALIGN_CENTER, 0, -24);

        // Progress bar
        s_boot_bar = lv_bar_create(s_boot_scr);
        lv_obj_set_size(s_boot_bar, 200, 8);
        lv_obj_align(s_boot_bar, LV_ALIGN_CENTER, 0, 10);
        lv_bar_set_range(s_boot_bar, 0, 100);
        lv_bar_set_value(s_boot_bar, progress_pct, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(s_boot_bar, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_boot_bar, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(s_boot_bar, 4, LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_boot_bar, lv_color_hex(0x00E676), LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(s_boot_bar, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_radius(s_boot_bar, 4, LV_PART_INDICATOR);
        lv_obj_set_style_anim_time(s_boot_bar, 300, LV_PART_INDICATOR);

        // Status label
        s_boot_label = lv_label_create(s_boot_scr);
        lv_label_set_text(s_boot_label, msg);
        lv_obj_set_style_text_color(s_boot_label, lv_color_hex(0x8A8A8A), 0);
        lv_obj_set_style_text_font(s_boot_label, &lv_font_montserrat_14, 0);
        lv_obj_align(s_boot_label, LV_ALIGN_CENTER, 0, 36);

        lvgl_port_unlock();
    }
}

void ui_boot_hide(void)
{
    if (lvgl_port_lock(0)) {
        if (s_boot_scr) {
            lv_obj_delete(s_boot_scr);
            s_boot_scr = NULL;
            s_boot_label = NULL;
            s_boot_bar = NULL;
        }
        lvgl_port_unlock();
    }
}

// ── Helpers ────────────────────────────────────────────────────────
void format_price(char *buf, size_t len, double price)
{
    if (price < 1.0) {
        snprintf(buf, len, "$%.5f", price);
        return;
    }

    int whole = (int)price;
    int frac  = (int)((price - whole) * 100 + 0.5) % 100;

    char ibuf[20];
    int pos = 0;
    int digits = 0;
    if (whole == 0) {
        ibuf[pos++] = '0';
    } else {
        int tmp = whole;
        char raw[16];
        int rlen = 0;
        while (tmp > 0) {
            raw[rlen++] = '0' + (tmp % 10);
            tmp /= 10;
            digits++;
            if (digits % 3 == 0 && tmp > 0) {
                raw[rlen++] = ',';
            }
        }
        for (int i = rlen - 1; i >= 0; i--) {
            ibuf[pos++] = raw[i];
        }
    }
    ibuf[pos] = '\0';

    snprintf(buf, len, "$%s.%02d", ibuf, frac);
}

void format_change(char *buf, size_t len, double pct)
{
    char sign = pct >= 0 ? '+' : '-';
    double a = pct < 0 ? -pct : pct;
    int w = (int)a;
    int f = (int)(a * 100) % 100;
    snprintf(buf, len, "%c%d.%02d%%", sign, w, f);
}

lv_color_t chg_color(double pct)
{
    return pct >= 0 ? lv_color_hex(0x00E676) : lv_color_hex(0xFF5252);
}

// ── Logo image ─────────────────────────────────────────────────────
static lv_obj_t *create_logo_img(lv_obj_t *parent, int size)
{
    lv_obj_t *img = lv_image_create(parent);
    lv_image_set_inner_align(img, LV_IMAGE_ALIGN_CENTER);
    lv_obj_set_size(img, size, size);
    return img;
}

static void set_logo(lv_obj_t *img, const crypto_item_t *item)
{
    lv_image_set_src(img, item->logo);
}

// ── Price flash animation ──────────────────────────────────────────
static void flash_reset_cb(lv_timer_t *timer)
{
    (void)timer;
    lv_obj_set_style_text_color(s_main_price, lv_color_hex(0xFFFFFF), 0);
    s_flash_timer = NULL;
}

static void flash_price(double old_price, double new_price)
{
    if (old_price <= 0 || old_price == new_price) return;

    lv_color_t c = (new_price > old_price)
        ? lv_color_hex(0x00E676)
        : lv_color_hex(0xFF5252);

    lv_obj_set_style_text_color(s_main_price, c, 0);

    if (s_flash_timer) {
        lv_timer_delete(s_flash_timer);
    }
    s_flash_timer = lv_timer_create(flash_reset_cb, 1000, NULL);
    lv_timer_set_repeat_count(s_flash_timer, 1);
}

// ── Price scroll animation ─────────────────────────────────────────
static void price_anim_cb(void *var, int32_t v)
{
    (void)var;
    char buf[24];
    format_price(buf, sizeof(buf), (double)v / (double)s_price_scale);
    lv_label_set_text(s_main_price, buf);
}

// ── Chart helpers ──────────────────────────────────────────────────
// Normalize double prices to int32_t chart values with maximum visual resolution.
// Maps [lo, hi] of raw prices to [0, 10000] for LVGL chart, so even tiny
// sub-cent fluctuations fill the full chart height.
#define CHART_INT_RANGE 10000

static void chart_get_bounds(const crypto_item_t *item, int count,
                             double *out_lo, double *out_hi)
{
    int start = CHART_POINTS - count;
    if (start < 0) start = 0;

    double lo = item->hist_raw[start], hi = lo;
    for (int i = start + 1; i < CHART_POINTS; i++) {
        if (item->hist_raw[i] < lo) lo = item->hist_raw[i];
        if (item->hist_raw[i] > hi) hi = item->hist_raw[i];
    }
    *out_lo = lo;
    *out_hi = hi;
}

static int32_t price_to_chart(double price, double lo, double range)
{
    if (range <= 0) return CHART_INT_RANGE / 2;
    return (int32_t)((price - lo) / range * CHART_INT_RANGE);
}

static void chart_rebuild(int idx)
{
    const crypto_item_t *item = &g_crypto[idx];
    int count = s_history_count[idx];

    if (s_chart_ser) {
        lv_chart_remove_series(s_chart, s_chart_ser);
        s_chart_ser = NULL;
    }

    lv_chart_set_point_count(s_chart, CHART_POINTS);
    s_chart_ser = lv_chart_add_series(s_chart, chg_color(item->change_pct),
                                      LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_set_style_bg_color(s_chart, chg_color(item->change_pct), 0);

    if (count < 2) {
        lv_chart_refresh(s_chart);
        return;
    }

    int start = CHART_POINTS - count;
    if (start < 0) start = 0;

    double lo, hi;
    chart_get_bounds(item, count, &lo, &hi);
    double range = hi - lo;
    // Add 25% padding on each side
    double pad = range * 0.25;
    if (pad <= 0) pad = lo * 0.001;  // 0.1% of price if flat
    lo -= pad;
    range = (hi + pad) - lo;

    lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, 0, CHART_INT_RANGE);

    for (int i = start; i < CHART_POINTS; i++) {
        lv_chart_set_next_value(s_chart, s_chart_ser,
                                price_to_chart(item->hist_raw[i], lo, range));
    }
    lv_chart_refresh(s_chart);
}

static void chart_add_point(int idx)
{
    int count = s_history_count[idx];
    if (count <= 1) return;

    // Always rebuild to keep normalization consistent with raw doubles
    chart_rebuild(idx);
}

// ── Update content ─────────────────────────────────────────────────
static void update_main_labels(void)
{
    const crypto_item_t *item = &g_crypto[s_focus_idx];
    char buf[24];

    set_logo(s_main_logo, item);
    lv_label_set_text(s_main_sym, item->symbol);
    lv_label_set_text(s_main_name, item->name);

    format_price(buf, sizeof(buf), item->price);
    lv_label_set_text(s_main_price, buf);

    format_change(buf, sizeof(buf), item->change_pct);
    lv_label_set_text(s_main_chg, buf);
    lv_obj_set_style_text_color(s_main_chg, chg_color(item->change_pct), 0);

    lv_obj_set_style_border_color(s_main_card, chg_color(item->change_pct), 0);
    lv_obj_set_style_shadow_color(s_main_card, chg_color(item->change_pct), 0);

    lv_color_t bg = item->change_pct >= 0
        ? lv_color_hex(0x0C2218)
        : lv_color_hex(0x220C0C);
    lv_obj_set_style_bg_color(s_main_card, bg, 0);

    if (item->high_24h > 0 && item->low_24h > 0) {
        char hbuf[20], lbuf[20];
        format_price(hbuf, sizeof(hbuf), item->high_24h);
        format_price(lbuf, sizeof(lbuf), item->low_24h);
        char hlbuf[48];
        snprintf(hlbuf, sizeof(hlbuf), "H:%s  L:%s", hbuf, lbuf);
        lv_label_set_text(s_main_hl, hlbuf);
    }
}

static void update_side_cards(void)
{
    int idx[2] = {
        (s_focus_idx + 1) % CRYPTO_COUNT,
        (s_focus_idx + 2) % CRYPTO_COUNT,
    };
    for (int i = 0; i < 2; i++) {
        const crypto_item_t *item = &g_crypto[idx[i]];
        char buf[24];

        set_logo(s_side_logo[i], item);
        lv_label_set_text(s_side_sym[i], item->symbol);

        format_price(buf, sizeof(buf), item->price);
        lv_label_set_text(s_side_price[i], buf);

        format_change(buf, sizeof(buf), item->change_pct);
        lv_label_set_text(s_side_chg[i], buf);
        lv_obj_set_style_text_color(s_side_chg[i], chg_color(item->change_pct), 0);

        lv_color_t bg = item->change_pct >= 0
            ? lv_color_hex(0x0C2218)
            : lv_color_hex(0x220C0C);
        lv_obj_set_style_bg_color(s_side_card[i], bg, 0);
        lv_obj_set_style_shadow_color(s_side_card[i], chg_color(item->change_pct), 0);
    }
}

// ── Coin switch ────────────────────────────────────────────────────
void switch_focus(void)
{
    if (s_animating) return;
    s_animating = true;
    s_focus_idx = (s_focus_idx + 1) % CRYPTO_COUNT;

    update_main_labels();
    update_side_cards();
    chart_rebuild(s_focus_idx);

    // Sync LED color to newly focused coin's 24h trend
    led_set_market_mood(g_crypto[s_focus_idx].change_pct >= 0);

    s_animating = false;
}

// ── Build UI ───────────────────────────────────────────────────────
static void create_main_card(lv_obj_t *parent)
{
    s_main_card = lv_obj_create(parent);
    lv_obj_set_size(s_main_card, MAIN_W, CONTENT_H);
    lv_obj_set_pos(s_main_card, MARGIN_H, MARGIN_TOP);
    lv_obj_set_style_bg_color(s_main_card, lv_color_hex(0x16213E), 0);
    lv_obj_set_style_bg_opa(s_main_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_main_card, 2, 0);
    lv_obj_set_style_radius(s_main_card, 8, 0);
    lv_obj_set_style_pad_all(s_main_card, 6, 0);
    lv_obj_clear_flag(s_main_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_style_shadow_width(s_main_card, 12, 0);
    lv_obj_set_style_shadow_spread(s_main_card, 2, 0);
    lv_obj_set_style_shadow_opa(s_main_card, LV_OPA_30, 0);

    s_main_logo = create_logo_img(s_main_card, 32);
    lv_obj_align(s_main_logo, LV_ALIGN_TOP_LEFT, 0, -2);

    s_main_sym = lv_label_create(s_main_card);
    lv_obj_set_style_text_color(s_main_sym, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_main_sym, &lv_font_montserrat_18, 0);
    lv_obj_align(s_main_sym, LV_ALIGN_TOP_LEFT, 38, 0);

    s_main_name = lv_label_create(s_main_card);
    lv_obj_set_style_text_color(s_main_name, lv_color_hex(0x8A8A8A), 0);
    lv_obj_set_style_text_font(s_main_name, &lv_font_montserrat_12, 0);
    lv_obj_align(s_main_name, LV_ALIGN_TOP_LEFT, 38, 22);

    s_main_price = lv_label_create(s_main_card);
    lv_obj_set_style_text_color(s_main_price, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_main_price, &lv_font_montserrat_20, 0);
    lv_obj_align(s_main_price, LV_ALIGN_TOP_LEFT, 0, 38);

    s_main_chg = lv_label_create(s_main_card);
    lv_obj_set_style_text_font(s_main_chg, &lv_font_montserrat_14, 0);
    lv_obj_align(s_main_chg, LV_ALIGN_TOP_RIGHT, 0, 42);

    s_main_hl = lv_label_create(s_main_card);
    lv_obj_set_style_text_color(s_main_hl, lv_color_hex(0xD0D0D0), 0);
    lv_obj_set_style_text_font(s_main_hl, &lv_font_montserrat_10, 0);
    lv_obj_align(s_main_hl, LV_ALIGN_TOP_LEFT, 0, 62);
    lv_label_set_text(s_main_hl, "");

    s_chart = lv_chart_create(s_main_card);
    lv_obj_set_size(s_chart, MAIN_W - 16, CHART_H);
    lv_obj_align(s_chart, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_chart, CHART_POINTS);
    lv_obj_set_style_bg_opa(s_chart, 30, 0);
    lv_obj_set_style_bg_grad_dir(s_chart, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_grad_color(s_chart, lv_color_hex(0x0F0F1A), 0);
    lv_obj_set_style_border_width(s_chart, 0, 0);
    lv_obj_set_style_pad_all(s_chart, 0, 0);
    lv_obj_set_style_size(s_chart, 0, 0, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(s_chart, 3, LV_PART_ITEMS);
    lv_chart_set_div_line_count(s_chart, 0, 0);
    s_chart_ser = NULL;

}

static void create_side_cards(lv_obj_t *parent)
{
    for (int i = 0; i < 2; i++) {
        int y = MARGIN_TOP + i * (SIDE_H + GAP);

        s_side_card[i] = lv_obj_create(parent);
        lv_obj_set_size(s_side_card[i], SIDE_W, SIDE_H);
        lv_obj_set_pos(s_side_card[i], SIDE_X, y);
        lv_obj_set_style_bg_color(s_side_card[i], lv_color_hex(0x111a30), 0);
        lv_obj_set_style_bg_opa(s_side_card[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(s_side_card[i], lv_color_hex(0x0F3460), 0);
        lv_obj_set_style_border_width(s_side_card[i], 1, 0);
        lv_obj_set_style_radius(s_side_card[i], 6, 0);
        lv_obj_set_style_pad_all(s_side_card[i], 4, 0);
        lv_obj_clear_flag(s_side_card[i], LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_set_style_shadow_width(s_side_card[i], 8, 0);
        lv_obj_set_style_shadow_spread(s_side_card[i], 1, 0);
        lv_obj_set_style_shadow_opa(s_side_card[i], LV_OPA_20, 0);

        s_side_logo[i] = create_logo_img(s_side_card[i], 24);
        lv_obj_align(s_side_logo[i], LV_ALIGN_TOP_LEFT, 0, -1);

        s_side_sym[i] = lv_label_create(s_side_card[i]);
        lv_obj_set_style_text_color(s_side_sym[i], lv_color_hex(0xBBBBBB), 0);
        lv_obj_set_style_text_font(s_side_sym[i], &lv_font_montserrat_14, 0);
        lv_obj_align(s_side_sym[i], LV_ALIGN_TOP_LEFT, 30, 0);

        s_side_price[i] = lv_label_create(s_side_card[i]);
        lv_obj_set_style_text_color(s_side_price[i], lv_color_hex(0xCCCCCC), 0);
        lv_obj_set_style_text_font(s_side_price[i], &lv_font_montserrat_14, 0);
        lv_obj_align(s_side_price[i], LV_ALIGN_LEFT_MID, 0, 4);

        s_side_chg[i] = lv_label_create(s_side_card[i]);
        lv_obj_set_style_text_font(s_side_chg[i], &lv_font_montserrat_12, 0);
        lv_obj_align(s_side_chg[i], LV_ALIGN_BOTTOM_LEFT, 0, 0);
    }
}

// ── Pre-fill chart history from candlestick data ────────────────────
void ui_set_chart_history(int idx, const double *prices, int count)
{
    if (idx < 0 || idx >= CRYPTO_COUNT) return;
    if (count <= 0) return;
    if (count > CHART_POINTS) count = CHART_POINTS;

    // Fill from the right side of hist_raw (newest at end)
    int start = CHART_POINTS - count;
    for (int i = 0; i < count; i++) {
        g_crypto[idx].hist_raw[start + i] = prices[i];
    }
    s_history_count[idx] = count;

    // Set last chart time to now so next sample is 30 min later
    struct timeval tv;
    gettimeofday(&tv, NULL);
    s_last_chart_time[idx] = (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;

    ESP_LOGI(TAG, "Chart history loaded for idx=%d: %d points", idx, count);
}

// ── Live price update (called from price_fetch task) ───────────────
void ui_update_price(int idx, double price, double change_pct,
                     double high_24h, double low_24h)
{
    if (s_ui_teardown) return;
    if (idx < 0 || idx >= CRYPTO_COUNT) return;

    double old_price = g_crypto[idx].price;

    g_crypto[idx].price = price;
    g_crypto[idx].change_pct = change_pct;
    g_crypto[idx].high_24h = high_24h;
    g_crypto[idx].low_24h = low_24h;

    // Chart: throttle to one point per CHART_INTERVAL_MS (30 min)
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int64_t now_ms = (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
    bool add_chart = (now_ms - s_last_chart_time[idx] >= CHART_INTERVAL_MS);
    if (add_chart) {
        for (int i = 0; i < CHART_POINTS - 1; i++) {
            g_crypto[idx].hist_raw[i] = g_crypto[idx].hist_raw[i + 1];
        }
        g_crypto[idx].hist_raw[CHART_POINTS - 1] = price;
        if (s_history_count[idx] < CHART_POINTS) {
            s_history_count[idx]++;
        }
        s_last_chart_time[idx] = now_ms;
    }
    s_price_loaded[idx] = true;

    // If UI not built yet (boot-time fetch), only store data — no LVGL ops
    if (!s_main_card) return;

    if (lvgl_port_lock(100)) {
        if (s_loading_overlay) {
            bool all_loaded = true;
            for (int i = 0; i < CRYPTO_COUNT; i++) {
                if (!s_price_loaded[i]) { all_loaded = false; break; }
            }
            if (all_loaded) {
                lv_obj_delete(s_loading_overlay);
                s_loading_overlay = NULL;
                lv_obj_clear_flag(s_main_card, LV_OBJ_FLAG_HIDDEN);
                for (int i = 0; i < 2; i++) {
                    lv_obj_clear_flag(s_side_card[i], LV_OBJ_FLAG_HIDDEN);
                }
            }
        }
        // Only refresh the cards that display this coin
        if (idx == s_focus_idx) {
            update_main_labels();
        }
        // Check if idx appears in either side card slot
        int side0 = (s_focus_idx + 1) % CRYPTO_COUNT;
        int side1 = (s_focus_idx + 2) % CRYPTO_COUNT;
        if (idx == side0 || idx == side1) {
            update_side_cards();
        }

        if (idx == s_focus_idx) {
            if (add_chart) {
                chart_add_point(idx);
            }
            flash_price(old_price, price);

            s_price_scale = (price < 1.0) ? 100000 : 100;
            int32_t old_val = (int32_t)(old_price * s_price_scale);
            int32_t new_val = (int32_t)(price * s_price_scale);
            if (old_price > 0 && old_val != new_val) {
                lv_anim_t a;
                lv_anim_init(&a);
                lv_anim_set_var(&a, s_main_price);
                lv_anim_set_values(&a, old_val, new_val);
                lv_anim_set_duration(&a, 500);
                lv_anim_set_exec_cb(&a, price_anim_cb);
                lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
                lv_anim_start(&a);
                price_anim_cb(NULL, old_val);
            }
        }

        lvgl_port_unlock();
    }

    // LED update — always runs regardless of LVGL lock success
    if (idx == s_focus_idx && s_main_card) {
        led_set_market_mood(change_pct >= 0);
        if (old_price > 0 && price != old_price) {
            led_flash_price(price > old_price);
        }
    }
}

// ── Public API ─────────────────────────────────────────────────────
void ui_init(void)
{
    temp_sensor_init();

    lvgl_port_lock(0);

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0F0F1A), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    create_main_card(scr);
    create_side_cards(scr);
    create_info_panel(scr);

    // Check if prices were already fetched during boot
    bool all_loaded = true;
    for (int i = 0; i < CRYPTO_COUNT; i++) {
        if (!s_price_loaded[i]) { all_loaded = false; break; }
    }

    if (all_loaded) {
        // Prices ready — show cards immediately, populate labels
        update_main_labels();
        update_side_cards();
        chart_rebuild(s_focus_idx);
    } else {
        // Still waiting — hide cards, show loading overlay
        lv_obj_add_flag(s_main_card, LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < 2; i++) {
            lv_obj_add_flag(s_side_card[i], LV_OBJ_FLAG_HIDDEN);
        }

        s_loading_overlay = lv_obj_create(scr);
        lv_obj_set_size(s_loading_overlay, CONTENT_W, CONTENT_H);
        lv_obj_set_pos(s_loading_overlay, MARGIN_H, MARGIN_TOP);
        lv_obj_set_style_bg_opa(s_loading_overlay, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(s_loading_overlay, 0, 0);
        lv_obj_set_style_pad_all(s_loading_overlay, 0, 0);
        lv_obj_clear_flag(s_loading_overlay, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *spinner = lv_spinner_create(s_loading_overlay);
        lv_obj_set_size(spinner, 36, 36);
        lv_obj_align(spinner, LV_ALIGN_CENTER, 0, -12);
        lv_spinner_set_anim_params(spinner, 1000, 270);
        lv_obj_set_style_arc_color(spinner, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
        lv_obj_set_style_arc_color(spinner, lv_color_hex(0x00E676), LV_PART_INDICATOR);
        lv_obj_set_style_arc_width(spinner, 3, LV_PART_MAIN);
        lv_obj_set_style_arc_width(spinner, 3, LV_PART_INDICATOR);

        lv_obj_t *loading_lbl = lv_label_create(s_loading_overlay);
        lv_label_set_text(loading_lbl, "Fetching prices...");
        lv_obj_set_style_text_color(loading_lbl, lv_color_hex(0x8A8A8A), 0);
        lv_obj_set_style_text_font(loading_lbl, &lv_font_montserrat_12, 0);
        lv_obj_align(loading_lbl, LV_ALIGN_CENTER, 0, 20);
    }

    lvgl_port_unlock();

    xTaskCreate(info_update_task, "info", 3072, NULL, 3, NULL);
}

// ── Teardown (used before entering WiFi provisioning) ──────────────
void ui_cleanup(void)
{
    // Signal background tasks to stop
    s_ui_teardown = true;

    // Wake info_update_task in case it's blocked waiting for notification
    extern TaskHandle_t s_info_task;
    if (s_info_task) {
        xTaskNotifyGive(s_info_task);
    }

    // Give info_update_task time to exit
    vTaskDelay(pdMS_TO_TICKS(200));

    if (lvgl_port_lock(0)) {
        // Delete flash timer
        if (s_flash_timer) {
            lv_timer_delete(s_flash_timer);
            s_flash_timer = NULL;
        }

        // Cancel all running animations
        lv_anim_delete(s_main_price, NULL);
        lv_anim_delete(s_main_card, NULL);
        for (int i = 0; i < 2; i++) {
            lv_anim_delete(s_side_card[i], NULL);
        }

        // Clear everything on screen
        lv_obj_t *scr = lv_screen_active();
        lv_obj_clean(scr);

        // Null all ui.c pointers (objects already destroyed by lv_obj_clean)
        s_boot_scr = NULL;
        s_boot_label = NULL;
        s_boot_bar = NULL;
        s_main_card = NULL;
        s_main_logo = NULL;
        s_main_sym = NULL;
        s_main_name = NULL;
        s_main_price = NULL;
        s_main_chg = NULL;
        s_main_hl = NULL;
        s_chart = NULL;
        s_chart_ser = NULL;
        for (int i = 0; i < 2; i++) {
            s_side_card[i] = NULL;
            s_side_logo[i] = NULL;
            s_side_sym[i] = NULL;
            s_side_price[i] = NULL;
            s_side_chg[i] = NULL;
        }
        s_loading_overlay = NULL;

        // Clear info panel pointers
        ui_info_cleanup();

        lvgl_port_unlock();
    }
}

// ── WiFi config guide screen ───────────────────────────────────────
void ui_show_wifi_config(void)
{
    if (lvgl_port_lock(0)) {
        lv_obj_t *scr = lv_screen_active();
        lv_obj_set_style_bg_color(scr, lv_color_hex(0x0F0F1A), 0);
        lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

        lv_obj_t *cont = lv_obj_create(scr);
        lv_obj_set_size(cont, LCD_H_RES, LCD_V_RES);
        lv_obj_center(cont);
        lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(cont, 0, 0);
        lv_obj_set_style_pad_all(cont, 0, 0);
        lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(cont, 4, 0);

        // Title
        lv_obj_t *title = lv_label_create(cont);
        lv_label_set_text(title, LV_SYMBOL_WIFI "  WiFi Setup");
        lv_obj_set_style_text_color(title, lv_color_hex(0x00E676), 0);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);

        // Instruction line 1
        lv_obj_t *lbl1 = lv_label_create(cont);
        lv_label_set_text(lbl1, "Connect to WiFi network:");
        lv_obj_set_style_text_color(lbl1, lv_color_hex(0x8A8A8A), 0);
        lv_obj_set_style_text_font(lbl1, &lv_font_montserrat_14, 0);

        // SSID name
        lv_obj_t *ssid_lbl = lv_label_create(cont);
        lv_label_set_text(ssid_lbl, "TokenTicker");
        lv_obj_set_style_text_color(ssid_lbl, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(ssid_lbl, &lv_font_montserrat_20, 0);

        // Instruction line 2
        lv_obj_t *lbl2 = lv_label_create(cont);
        lv_label_set_text(lbl2, "Then open in your browser:");
        lv_obj_set_style_text_color(lbl2, lv_color_hex(0x8A8A8A), 0);
        lv_obj_set_style_text_font(lbl2, &lv_font_montserrat_14, 0);

        // IP address
        lv_obj_t *ip_lbl = lv_label_create(cont);
        lv_label_set_text(ip_lbl, "192.168.4.1");
        lv_obj_set_style_text_color(ip_lbl, lv_color_hex(0x00E676), 0);
        lv_obj_set_style_text_font(ip_lbl, &lv_font_montserrat_18, 0);

        lvgl_port_unlock();
    }
}
