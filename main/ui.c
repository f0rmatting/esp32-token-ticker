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

LV_FONT_DECLARE(font_mono_10)
LV_FONT_DECLARE(font_mono_12)
LV_FONT_DECLARE(font_mono_14)
LV_FONT_DECLARE(font_mono_18)
LV_FONT_DECLARE(font_mono_20)
LV_FONT_DECLARE(font_mono_24)

static const char *TAG = "ui";

// ── Crypto data (populated by live price fetcher) ──────────────────
crypto_item_t g_crypto[] = {
    {"BTC", "Bitcoin",  &img_btc_logo, 0, 0, 0, 0, {0}},
    {"ETH", "Ethereum", &img_eth_logo, 0, 0, 0, 0, {0}},
    {"PAXG", "PAX Gold", &img_paxg_logo, 0, 0, 0, 0, {0}},
    {"SUI", "Sui",      &img_sui_logo, 0, 0, 0, 0, {0}},
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

// ── Main panel widgets ──────────────────────────────────────────────
lv_obj_t *s_main_panel;
static lv_obj_t *s_main_logo;
static lv_obj_t *s_main_sym;
static lv_obj_t *s_main_price;
static lv_obj_t *s_chg_pill;
static lv_obj_t *s_chg_label;
static lv_obj_t *s_chart;
static lv_chart_series_t *s_chart_ser;

// ── Side card widgets (marquee) ─────────────────────────────────────
lv_obj_t *s_side_viewport;
#define SIDE_SLOTS 7                 // enough cards to fill viewport during full scroll
static lv_obj_t *s_side_card[SIDE_SLOTS];
static lv_obj_t *s_side_logo[SIDE_SLOTS];
static lv_obj_t *s_side_sym[SIDE_SLOTS];
static lv_obj_t *s_side_price[SIDE_SLOTS];
static lv_obj_t *s_side_chg[SIDE_SLOTS];
static int  s_side_coin[SIDE_SLOTS]; // crypto index shown on each slot
static int  s_side_count;            // how many non-focused coins
static lv_timer_t *s_scroll_timer;

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
        lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

        s_boot_scr = lv_obj_create(scr);
        lv_obj_set_size(s_boot_scr, LCD_H_RES, LCD_V_RES);
        lv_obj_center(s_boot_scr);
        lv_obj_set_style_bg_opa(s_boot_scr, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(s_boot_scr, 0, 0);
        lv_obj_set_style_pad_all(s_boot_scr, 0, 0);
        lv_obj_clear_flag(s_boot_scr, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *brand = lv_label_create(s_boot_scr);
        lv_label_set_text(brand, "TokenTicker");
        lv_obj_set_style_text_color(brand, lv_color_hex(0x00FF88), 0);
        lv_obj_set_style_text_font(brand, &font_mono_24, 0);
        lv_obj_align(brand, LV_ALIGN_CENTER, 0, -24);

        s_boot_bar = lv_bar_create(s_boot_scr);
        lv_obj_set_size(s_boot_bar, 200, 8);
        lv_obj_align(s_boot_bar, LV_ALIGN_CENTER, 0, 10);
        lv_bar_set_range(s_boot_bar, 0, 100);
        lv_bar_set_value(s_boot_bar, progress_pct, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(s_boot_bar, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_boot_bar, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(s_boot_bar, 4, LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_boot_bar, lv_color_hex(0x00FF88), LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(s_boot_bar, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_radius(s_boot_bar, 4, LV_PART_INDICATOR);
        lv_obj_set_style_anim_time(s_boot_bar, 300, LV_PART_INDICATOR);

        s_boot_label = lv_label_create(s_boot_scr);
        lv_label_set_text(s_boot_label, msg);
        lv_obj_set_style_text_color(s_boot_label, lv_color_hex(0x8A8A8A), 0);
        lv_obj_set_style_text_font(s_boot_label, &font_mono_14, 0);
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

void format_compact_price(char *buf, size_t len, double price)
{
    if (price >= 100000) {
        snprintf(buf, len, "$%dk", (int)(price / 1000));
    } else if (price >= 1000) {
        snprintf(buf, len, "$%.2fk", price / 1000.0);
    } else if (price >= 1.0) {
        snprintf(buf, len, "$%.2f", price);
    } else {
        snprintf(buf, len, "$%.4f", price);
    }
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
    return pct >= 0 ? lv_color_hex(0x00FF88) : lv_color_hex(0xFF3366);
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
        ? lv_color_hex(0x00FF88)
        : lv_color_hex(0xFF3366);

    lv_obj_set_style_text_color(s_main_price, c, 0);

    if (s_flash_timer) {
        lv_timer_delete(s_flash_timer);
    }
    s_flash_timer = lv_timer_create(flash_reset_cb, 1000, NULL);
    lv_timer_set_repeat_count(s_flash_timer, 1);
}

// ── Pill breathing animation ───────────────────────────────────────
static void pill_breathe_cb(void *var, int32_t v)
{
    lv_obj_set_style_bg_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
    lv_obj_set_style_shadow_opa((lv_obj_t *)var, (lv_opa_t)(v / 2), 0);
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

    if (count < 2) {
        lv_chart_refresh(s_chart);
        return;
    }

    int start = CHART_POINTS - count;
    if (start < 0) start = 0;

    double lo, hi;
    chart_get_bounds(item, count, &lo, &hi);
    double range = hi - lo;
    double pad = range * 0.25;
    if (pad <= 0) pad = lo * 0.001;
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
    chart_rebuild(idx);
}

// ── Side card marquee ───────────────────────────────────────────────
static void update_one_side(int slot, int coin_idx)
{
    const crypto_item_t *item = &g_crypto[coin_idx];
    char buf[24];

    set_logo(s_side_logo[slot], item);
    lv_label_set_text(s_side_sym[slot], item->symbol);

    format_compact_price(buf, sizeof(buf), item->price);
    lv_label_set_text(s_side_price[slot], buf);

    format_change(buf, sizeof(buf), item->change_pct);
    lv_label_set_text(s_side_chg[slot], buf);
    lv_obj_set_style_text_color(s_side_chg[slot], chg_color(item->change_pct), 0);

    s_side_coin[slot] = coin_idx;
}

// Container that holds all slot cards — we animate its Y to scroll
static lv_obj_t *s_side_strip;

static void marquee_anim_cb(void *var, int32_t v)
{
    lv_obj_set_y((lv_obj_t *)var, v);
}

static void start_marquee(void);

static void marquee_ready_cb(lv_anim_t *a)
{
    (void)a;
    // Snap back and restart — seamless because duplicate card was at bottom
    lv_obj_set_y(s_side_strip, 0);
    start_marquee();
}

static void start_marquee(void)
{
    if (s_side_count <= 0) return;

    // Scroll by exactly count * ROW_H so duplicate card aligns at top
    int scroll_dist = s_side_count * SIDE_ROW_H;
    // Duration: 3 seconds per card
    int duration = s_side_count * 3000;

    lv_anim_delete(s_side_strip, marquee_anim_cb);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_side_strip);
    lv_anim_set_values(&a, 0, -scroll_dist);
    lv_anim_set_duration(&a, duration);
    lv_anim_set_exec_cb(&a, marquee_anim_cb);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_set_completed_cb(&a, marquee_ready_cb);
    lv_anim_start(&a);
}

static void rebuild_side_coins(void)
{
    // Collect non-focused coin indices
    int coins[CRYPTO_COUNT];
    s_side_count = 0;
    for (int i = 0; i < CRYPTO_COUNT; i++) {
        if (i != s_focus_idx) coins[s_side_count++] = i;
    }
    if (s_side_count == 0) return;

    // Fill slots with repeated coins to cover scroll_dist + viewport
    // scroll_dist = s_side_count * SIDE_ROW_H, need strip >= scroll_dist + SIDE_H
    int need_px = s_side_count * SIDE_ROW_H + SIDE_H;
    int total_slots = (need_px + SIDE_ROW_H - 1) / SIDE_ROW_H;
    if (total_slots > SIDE_SLOTS) total_slots = SIDE_SLOTS;

    for (int i = 0; i < total_slots; i++) {
        update_one_side(i, coins[i % s_side_count]);
        lv_obj_set_pos(s_side_card[i], 0, i * SIDE_ROW_H);
        lv_obj_clear_flag(s_side_card[i], LV_OBJ_FLAG_HIDDEN);
    }
    // Hide unused slots
    for (int i = total_slots; i < SIDE_SLOTS; i++) {
        lv_obj_add_flag(s_side_card[i], LV_OBJ_FLAG_HIDDEN);
    }

    // Resize strip to fit all visible cards
    lv_obj_set_height(s_side_strip, total_slots * SIDE_ROW_H);

    // Reset strip position and restart marquee
    lv_obj_set_y(s_side_strip, 0);
    start_marquee();
}

// ── Update content ─────────────────────────────────────────────────
static void update_main_labels(void)
{
    const crypto_item_t *item = &g_crypto[s_focus_idx];
    char buf[24];

    set_logo(s_main_logo, item);
    lv_label_set_text(s_main_sym, item->symbol);

    format_price(buf, sizeof(buf), item->price);
    lv_label_set_text(s_main_price, buf);

    format_change(buf, sizeof(buf), item->change_pct);
    lv_label_set_text(s_chg_label, buf);
    lv_obj_set_style_bg_color(s_chg_pill, chg_color(item->change_pct), 0);
    lv_obj_set_style_shadow_color(s_chg_pill, chg_color(item->change_pct), 0);
}

static void update_side_cards(void)
{
    // Update data for currently visible slots
    for (int i = 0; i <= s_side_count && i < SIDE_SLOTS; i++) {
        int ci = s_side_coin[i];
        if (ci >= 0 && ci < CRYPTO_COUNT)
            update_one_side(i, ci);
    }
}

// ── Coin switch ────────────────────────────────────────────────────
void switch_focus(void)
{
    if (s_animating) return;
    s_animating = true;
    s_focus_idx = (s_focus_idx + 1) % CRYPTO_COUNT;

    update_main_labels();
    rebuild_side_coins();
    chart_rebuild(s_focus_idx);

    led_set_market_mood(g_crypto[s_focus_idx].change_pct >= 0);

    s_animating = false;
}

// ── Build UI ───────────────────────────────────────────────────────
static void create_main_panel(lv_obj_t *parent)
{
    s_main_panel = lv_obj_create(parent);
    lv_obj_set_size(s_main_panel, LCD_H_RES, LCD_V_RES);
    lv_obj_set_pos(s_main_panel, 0, 0);
    lv_obj_set_style_bg_opa(s_main_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_main_panel, 0, 0);
    lv_obj_set_style_pad_all(s_main_panel, 0, 0);
    lv_obj_clear_flag(s_main_panel, LV_OBJ_FLAG_SCROLLABLE);

    // ── Header (y=4): [Logo 32px] SYM (18pt) ─────────────────────
    s_main_logo = create_logo_img(s_main_panel, 32);
    lv_obj_set_pos(s_main_logo, MARGIN_H, MARGIN_TOP);

    s_main_sym = lv_label_create(s_main_panel);
    lv_obj_set_style_text_color(s_main_sym, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_main_sym, &font_mono_18, 0);
    lv_obj_set_pos(s_main_sym, MARGIN_H + 38, MARGIN_TOP + 6);

    // ── Price (y=42) ──────────────────────────────────────────────
    s_main_price = lv_label_create(s_main_panel);
    lv_obj_set_style_text_color(s_main_price, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_main_price, &font_mono_24, 0);
    lv_obj_set_pos(s_main_price, MARGIN_H, 42);

    // ── Pill badge (header row, right of side cards) ────────────
    s_chg_pill = lv_obj_create(s_main_panel);
    lv_obj_set_size(s_chg_pill, LV_SIZE_CONTENT, PILL_H);
    lv_obj_set_style_radius(s_chg_pill, PILL_RADIUS, 0);
    lv_obj_set_style_bg_opa(s_chg_pill, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_chg_pill, 0, 0);
    lv_obj_set_style_pad_left(s_chg_pill, 10, 0);
    lv_obj_set_style_pad_right(s_chg_pill, 10, 0);
    lv_obj_set_style_pad_top(s_chg_pill, 4, 0);
    lv_obj_set_style_pad_bottom(s_chg_pill, 4, 0);
    lv_obj_set_style_shadow_width(s_chg_pill, 16, 0);
    lv_obj_set_style_shadow_spread(s_chg_pill, 2, 0);
    lv_obj_clear_flag(s_chg_pill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_chg_pill, LV_ALIGN_TOP_RIGHT,
                 -(MARGIN_H + SIDE_W + GAP),
                 MARGIN_TOP + (32 - PILL_H) / 2);

    s_chg_label = lv_label_create(s_chg_pill);
    lv_obj_set_style_text_color(s_chg_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_chg_label, &font_mono_14, 0);
    lv_obj_center(s_chg_label);

    // Breathing glow animation
    lv_anim_t pa;
    lv_anim_init(&pa);
    lv_anim_set_var(&pa, s_chg_pill);
    lv_anim_set_values(&pa, 120, 255);
    lv_anim_set_duration(&pa, 1500);
    lv_anim_set_playback_duration(&pa, 1500);
    lv_anim_set_repeat_count(&pa, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&pa, pill_breathe_cb);
    lv_anim_set_path_cb(&pa, lv_anim_path_ease_in_out);
    lv_anim_start(&pa);

    // ── Chart (left of side cards, large) ─────────────────────────
    int chart_w = SIDE_X - GAP - MARGIN_H;
    s_chart = lv_chart_create(s_main_panel);
    lv_obj_set_size(s_chart, chart_w, CHART_H);
    lv_obj_set_pos(s_chart, MARGIN_H, CHART_Y);
    lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_chart, CHART_POINTS);
    lv_obj_set_style_bg_color(s_chart, lv_color_hex(0x0D0D1E), 0);
    lv_obj_set_style_bg_opa(s_chart, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_dir(s_chart, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_grad_color(s_chart, lv_color_hex(0x000000), 0);
    lv_obj_set_style_radius(s_chart, 6, 0);
    lv_obj_set_style_border_width(s_chart, 0, 0);
    lv_obj_set_style_pad_all(s_chart, 4, 0);
    lv_obj_set_style_size(s_chart, 0, 0, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(s_chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_line_opa(s_chart, LV_OPA_COVER, LV_PART_ITEMS);
    lv_chart_set_div_line_count(s_chart, 0, 0);
    s_chart_ser = NULL;
}

static void create_side_cards(lv_obj_t *parent)
{
    // Viewport: clips the scrolling strip
    s_side_viewport = lv_obj_create(parent);
    lv_obj_set_size(s_side_viewport, SIDE_W, SIDE_H);
    lv_obj_set_pos(s_side_viewport, SIDE_X, MARGIN_TOP);
    lv_obj_set_style_bg_opa(s_side_viewport, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_side_viewport, 0, 0);
    lv_obj_set_style_pad_all(s_side_viewport, 0, 0);
    lv_obj_clear_flag(s_side_viewport, LV_OBJ_FLAG_SCROLLABLE);

    // Strip container — holds all slot cards, animated as one unit
    s_side_strip = lv_obj_create(s_side_viewport);
    lv_obj_set_size(s_side_strip, SIDE_W, SIDE_SLOTS * SIDE_ROW_H);
    lv_obj_set_pos(s_side_strip, 0, 0);
    lv_obj_set_style_bg_opa(s_side_strip, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_side_strip, 0, 0);
    lv_obj_set_style_pad_all(s_side_strip, 0, 0);
    lv_obj_clear_flag(s_side_strip, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < SIDE_SLOTS; i++) {
        s_side_card[i] = lv_obj_create(s_side_strip);
        lv_obj_set_size(s_side_card[i], SIDE_W, SIDE_ROW_H - 12);
        lv_obj_set_pos(s_side_card[i], 0, i * SIDE_ROW_H);
        lv_obj_set_style_bg_color(s_side_card[i], lv_color_hex(0x0A0A14), 0);
        lv_obj_set_style_bg_opa(s_side_card[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_side_card[i], 0, 0);
        lv_obj_set_style_radius(s_side_card[i], 6, 0);
        lv_obj_set_style_pad_left(s_side_card[i], 6, 0);
        lv_obj_set_style_pad_right(s_side_card[i], 4, 0);
        lv_obj_set_style_pad_top(s_side_card[i], 10, 0);
        lv_obj_set_style_pad_bottom(s_side_card[i], 10, 0);
        lv_obj_clear_flag(s_side_card[i], LV_OBJ_FLAG_SCROLLABLE);

        // Row 1: Logo + Symbol (top)
        s_side_logo[i] = create_logo_img(s_side_card[i], 18);
        lv_obj_align(s_side_logo[i], LV_ALIGN_TOP_LEFT, 0, 0);

        s_side_sym[i] = lv_label_create(s_side_card[i]);
        lv_obj_set_style_text_color(s_side_sym[i], lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(s_side_sym[i], &font_mono_14, 0);
        lv_obj_align(s_side_sym[i], LV_ALIGN_TOP_LEFT, 22, 1);

        // Row 2: Price (middle)
        s_side_price[i] = lv_label_create(s_side_card[i]);
        lv_label_set_long_mode(s_side_price[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_width(s_side_price[i], SIDE_W - 10);
        lv_obj_set_style_text_color(s_side_price[i], lv_color_hex(0xCCCCCC), 0);
        lv_obj_set_style_text_font(s_side_price[i], &font_mono_14, 0);
        lv_obj_align(s_side_price[i], LV_ALIGN_LEFT_MID, 0, 0);

        // Row 3: Change % (bottom)
        s_side_chg[i] = lv_label_create(s_side_card[i]);
        lv_label_set_long_mode(s_side_chg[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_width(s_side_chg[i], SIDE_W - 10);
        lv_obj_set_style_text_font(s_side_chg[i], &font_mono_14, 0);
        lv_obj_align(s_side_chg[i], LV_ALIGN_BOTTOM_LEFT, 0, 0);

        s_side_coin[i] = -1;
    }

    rebuild_side_coins();
}

// ── Pre-fill chart history from candlestick data ────────────────────
void ui_set_chart_history(int idx, const double *prices, int count)
{
    if (idx < 0 || idx >= CRYPTO_COUNT) return;
    if (count <= 0) return;
    if (count > CHART_POINTS) count = CHART_POINTS;

    int start = CHART_POINTS - count;
    for (int i = 0; i < count; i++) {
        g_crypto[idx].hist_raw[start + i] = prices[i];
    }
    s_history_count[idx] = count;

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

    if (!s_main_panel) return;

    if (lvgl_port_lock(100)) {
        if (s_loading_overlay) {
            bool all_loaded = true;
            for (int i = 0; i < CRYPTO_COUNT; i++) {
                if (!s_price_loaded[i]) { all_loaded = false; break; }
            }
            if (all_loaded) {
                lv_obj_delete(s_loading_overlay);
                s_loading_overlay = NULL;
                lv_obj_clear_flag(s_main_panel, LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_flag(s_side_viewport, LV_OBJ_FLAG_HIDDEN);
                update_main_labels();
                rebuild_side_coins();
                chart_rebuild(s_focus_idx);
            }
        }

        if (idx == s_focus_idx) {
            update_main_labels();
        }

        for (int i = 0; i < SIDE_SLOTS; i++) {
            if (s_side_coin[i] == idx) {
                update_one_side(i, idx);
            }
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

    if (idx == s_focus_idx && s_main_panel) {
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
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    create_main_panel(scr);
    create_side_cards(scr);
    create_info_panel(scr);

    bool all_loaded = true;
    for (int i = 0; i < CRYPTO_COUNT; i++) {
        if (!s_price_loaded[i]) { all_loaded = false; break; }
    }

    if (all_loaded) {
        update_main_labels();
        update_side_cards();
        chart_rebuild(s_focus_idx);
    } else {
        lv_obj_add_flag(s_main_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_side_viewport, LV_OBJ_FLAG_HIDDEN);

        s_loading_overlay = lv_obj_create(scr);
        lv_obj_set_size(s_loading_overlay, LCD_H_RES, LCD_V_RES);
        lv_obj_set_pos(s_loading_overlay, 0, 0);
        lv_obj_set_style_bg_opa(s_loading_overlay, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(s_loading_overlay, 0, 0);
        lv_obj_set_style_pad_all(s_loading_overlay, 0, 0);
        lv_obj_clear_flag(s_loading_overlay, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *spinner = lv_spinner_create(s_loading_overlay);
        lv_obj_set_size(spinner, 36, 36);
        lv_obj_align(spinner, LV_ALIGN_CENTER, 0, -12);
        lv_spinner_set_anim_params(spinner, 1000, 270);
        lv_obj_set_style_arc_color(spinner, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
        lv_obj_set_style_arc_color(spinner, lv_color_hex(0x00FF88), LV_PART_INDICATOR);
        lv_obj_set_style_arc_width(spinner, 3, LV_PART_MAIN);
        lv_obj_set_style_arc_width(spinner, 3, LV_PART_INDICATOR);

        lv_obj_t *loading_lbl = lv_label_create(s_loading_overlay);
        lv_label_set_text(loading_lbl, "Fetching prices...");
        lv_obj_set_style_text_color(loading_lbl, lv_color_hex(0x8A8A8A), 0);
        lv_obj_set_style_text_font(loading_lbl, &font_mono_12, 0);
        lv_obj_align(loading_lbl, LV_ALIGN_CENTER, 0, 20);
    }

    lvgl_port_unlock();

    xTaskCreate(info_update_task, "info", 3072, NULL, 3, NULL);
}

// ── Teardown (used before entering WiFi provisioning) ──────────────
void ui_cleanup(void)
{
    s_ui_teardown = true;

    extern TaskHandle_t s_info_task;
    if (s_info_task) {
        xTaskNotifyGive(s_info_task);
    }

    vTaskDelay(pdMS_TO_TICKS(200));

    if (lvgl_port_lock(0)) {
        if (s_flash_timer) {
            lv_timer_delete(s_flash_timer);
            s_flash_timer = NULL;
        }
        if (s_scroll_timer) {
            lv_timer_delete(s_scroll_timer);
            s_scroll_timer = NULL;
        }

        lv_anim_delete(s_main_price, NULL);
        lv_anim_delete(s_chg_pill, NULL);
        lv_anim_delete(s_main_panel, NULL);
        if (s_side_strip) lv_anim_delete(s_side_strip, marquee_anim_cb);

        lv_obj_t *scr = lv_screen_active();
        lv_obj_clean(scr);

        s_boot_scr = NULL;
        s_boot_label = NULL;
        s_boot_bar = NULL;
        s_main_panel = NULL;
        s_main_logo = NULL;
        s_main_sym = NULL;
        s_main_price = NULL;
        s_chg_pill = NULL;
        s_chg_label = NULL;
        s_chart = NULL;
        s_chart_ser = NULL;
        s_side_viewport = NULL;
        s_side_strip = NULL;
        for (int i = 0; i < SIDE_SLOTS; i++) {
            s_side_card[i] = NULL;
            s_side_logo[i] = NULL;
            s_side_sym[i] = NULL;
            s_side_price[i] = NULL;
            s_side_chg[i] = NULL;
        }
        s_loading_overlay = NULL;

        ui_info_cleanup();

        lvgl_port_unlock();
    }
}

// ── WiFi config guide screen ───────────────────────────────────────
void ui_show_wifi_config(void)
{
    if (lvgl_port_lock(0)) {
        lv_obj_t *scr = lv_screen_active();
        lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
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

        lv_obj_t *title = lv_label_create(cont);
        lv_label_set_text(title, "WiFi Setup");
        lv_obj_set_style_text_color(title, lv_color_hex(0x00FF88), 0);
        lv_obj_set_style_text_font(title, &font_mono_20, 0);

        lv_obj_t *lbl1 = lv_label_create(cont);
        lv_label_set_text(lbl1, "Connect to WiFi network:");
        lv_obj_set_style_text_color(lbl1, lv_color_hex(0x8A8A8A), 0);
        lv_obj_set_style_text_font(lbl1, &font_mono_14, 0);

        lv_obj_t *ssid_lbl = lv_label_create(cont);
        lv_label_set_text(ssid_lbl, "TokenTicker");
        lv_obj_set_style_text_color(ssid_lbl, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(ssid_lbl, &font_mono_20, 0);

        lv_obj_t *lbl2 = lv_label_create(cont);
        lv_label_set_text(lbl2, "Then open in your browser:");
        lv_obj_set_style_text_color(lbl2, lv_color_hex(0x8A8A8A), 0);
        lv_obj_set_style_text_font(lbl2, &font_mono_14, 0);

        lv_obj_t *ip_lbl = lv_label_create(cont);
        lv_label_set_text(ip_lbl, "192.168.4.1");
        lv_obj_set_style_text_color(ip_lbl, lv_color_hex(0x00FF88), 0);
        lv_obj_set_style_text_font(ip_lbl, &font_mono_18, 0);

        lvgl_port_unlock();
    }
}
