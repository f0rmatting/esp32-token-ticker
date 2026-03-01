/*
 * SPDX-FileCopyrightText: 2025-2026 Bruce
 * SPDX-License-Identifier: CC-BY-NC-4.0
 */

#include "ui_internal.h"
#include "wifi.h"
#include "homekit.h"

LV_FONT_DECLARE(font_mono_10)
LV_FONT_DECLARE(font_mono_12)
LV_FONT_DECLARE(font_mono_14)
LV_FONT_DECLARE(font_mono_18)

#include "driver/temperature_sensor.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"

#include <time.h>

static const char *TAG = "ui_info";

#define SLIDE_DUR 300

TaskHandle_t s_info_task;

// ── Info panel widgets ─────────────────────────────────────────────
static temperature_sensor_handle_t s_temp_sensor;
uint32_t s_heap_total;
static lv_obj_t *s_info_panel;
static lv_obj_t *s_info_time;
static lv_obj_t *s_info_temp_arc;
static lv_obj_t *s_info_temp_lbl;
static lv_obj_t *s_info_heap_arc;
static lv_obj_t *s_info_heap_lbl;
static lv_obj_t *s_info_ip;
static lv_obj_t *s_rssi_bars[4];
static lv_obj_t *s_info_mode;
static lv_obj_t *s_info_setup;

// ── Helper: create a styled card inside the info panel ─────────────
static lv_obj_t *info_card(lv_obj_t *parent, int x, int y, int w, int h,
                           lv_color_t accent)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, w, h);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x16213E), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, accent, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, 6, 0);
    lv_obj_set_style_shadow_width(card, 10, 0);
    lv_obj_set_style_shadow_spread(card, 1, 0);
    lv_obj_set_style_shadow_color(card, accent, 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_20, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

void create_info_panel(lv_obj_t *parent)
{
    s_info_panel = lv_obj_create(parent);
    lv_obj_set_size(s_info_panel, LCD_H_RES, LCD_V_RES);
    lv_obj_set_pos(s_info_panel, 0, 0);
    lv_obj_set_style_bg_color(s_info_panel, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_info_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_info_panel, 0, 0);
    lv_obj_set_style_pad_all(s_info_panel, 0, 0);
    lv_obj_clear_flag(s_info_panel, LV_OBJ_FLAG_SCROLLABLE);

    // ── Time card (top, wide) ──────────────────────────────────────
    #define TIME_CARD_H 34
    lv_color_t cyan = lv_color_hex(0x00BCD4);
    lv_obj_t *time_card = info_card(s_info_panel,
        MARGIN_H, MARGIN_TOP, CONTENT_W, TIME_CARD_H, cyan);

    s_info_time = lv_label_create(time_card);
    lv_label_set_text(s_info_time, "--:--:--");
    lv_obj_set_style_text_color(s_info_time, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_info_time, &font_mono_18, 0);
    lv_obj_align(s_info_time, LV_ALIGN_CENTER, 0, 0);

    // ── System card (bottom-left): temp arc + heap arc ──────────────
    int bot_y = MARGIN_TOP + TIME_CARD_H + GAP;
    int bot_h = CONTENT_H - TIME_CARD_H - GAP;
    int half_w = (CONTENT_W - GAP) / 2;

    lv_color_t orange = lv_color_hex(0xFF9800);
    lv_obj_t *sys_card = info_card(s_info_panel,
        MARGIN_H, bot_y, half_w, bot_h, orange);

    lv_obj_t *sys_title = lv_label_create(sys_card);
    lv_label_set_text(sys_title, "SYSTEM");
    lv_obj_set_style_text_color(sys_title, orange, 0);
    lv_obj_set_style_text_font(sys_title, &font_mono_10, 0);
    lv_obj_align(sys_title, LV_ALIGN_TOP_MID, 0, 0);

    // Arc common dimensions — fill available card space
    int arc_size = 64;
    int arc_y = 20;              // offset below title (spaced from SYSTEM label)
    int cw = half_w - 12;       // content width inside padding (132)
    int left_cx = cw / 4;       // 33 — center of left half
    int right_cx = cw * 3 / 4;  // 99 — center of right half

    // ── Temperature arc (left) ─────────────────────────────────────
    s_info_temp_arc = lv_arc_create(sys_card);
    lv_obj_set_size(s_info_temp_arc, arc_size, arc_size);
    lv_obj_set_pos(s_info_temp_arc, left_cx - arc_size / 2, arc_y);
    lv_arc_set_rotation(s_info_temp_arc, 270);
    lv_arc_set_range(s_info_temp_arc, 0, 80);
    lv_arc_set_value(s_info_temp_arc, 0);
    lv_arc_set_bg_angles(s_info_temp_arc, 0, 360);
    lv_obj_remove_flag(s_info_temp_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s_info_temp_arc, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_info_temp_arc, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_info_temp_arc, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_info_temp_arc, lv_color_hex(0x00FF88), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(s_info_temp_arc, true, LV_PART_INDICATOR);
    lv_obj_set_style_opa(s_info_temp_arc, LV_OPA_TRANSP, LV_PART_KNOB);

    s_info_temp_lbl = lv_label_create(s_info_temp_arc);
    lv_label_set_text(s_info_temp_lbl, "--");
    lv_obj_set_style_text_color(s_info_temp_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_info_temp_lbl, &font_mono_12, 0);
    lv_obj_center(s_info_temp_lbl);

    lv_obj_t *temp_tag = lv_label_create(sys_card);
    lv_label_set_text(temp_tag, "TEMP");
    lv_obj_set_style_text_color(temp_tag, lv_color_hex(0x8A8A8A), 0);
    lv_obj_set_style_text_font(temp_tag, &font_mono_10, 0);
    lv_obj_align_to(temp_tag, s_info_temp_arc, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);

    // ── Heap arc (right) ───────────────────────────────────────────
    s_info_heap_arc = lv_arc_create(sys_card);
    lv_obj_set_size(s_info_heap_arc, arc_size, arc_size);
    lv_obj_set_pos(s_info_heap_arc, right_cx - arc_size / 2, arc_y);
    lv_arc_set_rotation(s_info_heap_arc, 270);
    lv_arc_set_range(s_info_heap_arc, 0, 100);
    lv_arc_set_value(s_info_heap_arc, 0);
    lv_arc_set_bg_angles(s_info_heap_arc, 0, 360);
    lv_obj_remove_flag(s_info_heap_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s_info_heap_arc, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_info_heap_arc, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_info_heap_arc, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_info_heap_arc, lv_color_hex(0x00FF88), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(s_info_heap_arc, true, LV_PART_INDICATOR);
    lv_obj_set_style_opa(s_info_heap_arc, LV_OPA_TRANSP, LV_PART_KNOB);

    s_info_heap_lbl = lv_label_create(s_info_heap_arc);
    lv_label_set_text(s_info_heap_lbl, "--");
    lv_obj_set_style_text_color(s_info_heap_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_info_heap_lbl, &font_mono_12, 0);
    lv_obj_center(s_info_heap_lbl);

    lv_obj_t *heap_tag = lv_label_create(sys_card);
    lv_label_set_text(heap_tag, "HEAP");
    lv_obj_set_style_text_color(heap_tag, lv_color_hex(0x8A8A8A), 0);
    lv_obj_set_style_text_font(heap_tag, &font_mono_10, 0);
    lv_obj_align_to(heap_tag, s_info_heap_arc, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);

    // ── Network card (bottom-right) ────────────────────────────────
    lv_color_t green = lv_color_hex(0x00FF88);
    lv_obj_t *net_card = info_card(s_info_panel,
        MARGIN_H + half_w + GAP, bot_y, half_w, bot_h, green);

    lv_obj_t *net_title = lv_label_create(net_card);
    lv_label_set_text(net_title, "NETWORK");
    lv_obj_set_style_text_color(net_title, green, 0);
    lv_obj_set_style_text_font(net_title, &font_mono_10, 0);
    lv_obj_align(net_title, LV_ALIGN_TOP_LEFT, 0, 0);

    // ── Signal strength bars (top-right, 4 ascending bars) ─────────
    static const int bar_h[] = {3, 6, 9, 12};
    int bar_max = 12;
    int bar_w = 3;
    int bar_gap = 5;
    int bars_total_w = 4 * bar_w + 3 * (bar_gap - bar_w);  // 18px
    int bar_x0 = half_w - 18 - bars_total_w;  // right-align with margin
    for (int i = 0; i < 4; i++) {
        s_rssi_bars[i] = lv_obj_create(net_card);
        lv_obj_set_size(s_rssi_bars[i], bar_w, bar_h[i]);
        lv_obj_set_pos(s_rssi_bars[i], bar_x0 + i * bar_gap, bar_max - bar_h[i]);
        lv_obj_set_style_bg_color(s_rssi_bars[i], lv_color_hex(0x2A2A2A), 0);
        lv_obj_set_style_bg_opa(s_rssi_bars[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_rssi_bars[i], 0, 0);
        lv_obj_set_style_radius(s_rssi_bars[i], 1, 0);
        lv_obj_set_style_pad_all(s_rssi_bars[i], 0, 0);
        lv_obj_clear_flag(s_rssi_bars[i], LV_OBJ_FLAG_SCROLLABLE);
    }

    s_info_ip = lv_label_create(net_card);
    lv_label_set_text(s_info_ip, "");
    lv_obj_set_style_text_color(s_info_ip, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_info_ip, &font_mono_14, 0);
    lv_obj_align(s_info_ip, LV_ALIGN_TOP_MID, 0, 24);

    s_info_mode = lv_label_create(net_card);
    lv_label_set_text(s_info_mode, "");
    lv_obj_set_style_text_color(s_info_mode, lv_color_hex(0xA0A0A0), 0);
    lv_obj_set_style_text_font(s_info_mode, &font_mono_12, 0);
    lv_obj_align(s_info_mode, LV_ALIGN_TOP_MID, 0, 44);


    // ── HomeKit status ─────────────────────────────────────────────
    s_info_setup = lv_label_create(net_card);
    lv_label_set_text(s_info_setup, "");
    lv_obj_set_style_text_color(s_info_setup, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_info_setup, &font_mono_14, 0);
    lv_obj_align(s_info_setup, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    lv_obj_add_flag(s_info_panel, LV_OBJ_FLAG_HIDDEN);
}

// ── Slide animation helpers ────────────────────────────────────────
static void anim_x_cb(void *var, int32_t v)
{
    lv_obj_set_x((lv_obj_t *)var, v);
}

static void slide_to_info_done(lv_anim_t *a)
{
    (void)a;
    lv_obj_add_flag(s_main_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_side_viewport, LV_OBJ_FLAG_HIDDEN);
    s_animating = false;
}

static void slide_to_crypto_done(lv_anim_t *a)
{
    (void)a;
    lv_obj_add_flag(s_info_panel, LV_OBJ_FLAG_HIDDEN);
    s_animating = false;
}

static void update_rssi_bars(int rssi)
{
    int level;
    if (rssi >= -65)      level = 4;
    else if (rssi >= -75) level = 3;
    else if (rssi >= -85) level = 2;
    else if (rssi >= -95) level = 1;
    else                  level = 0;

    lv_color_t active   = lv_color_hex(0x00FF88);
    lv_color_t inactive = lv_color_hex(0x2A2A2A);
    for (int i = 0; i < 4; i++) {
        lv_obj_set_style_bg_color(s_rssi_bars[i],
            (i < level) ? active : inactive, 0);
    }
}

void toggle_info_panel(void)
{
    if (s_animating || s_loading_overlay) return;
    s_animating = true;
    s_show_info = !s_show_info;

    // Wake the info update task when panel becomes visible
    if (s_show_info && s_info_task) {
        xTaskNotifyGive(s_info_task);
    }

    lv_anim_t a;

    if (s_show_info) {
        // Refresh network info on show
        lv_label_set_text(s_info_ip, wifi_get_ip_str());

        update_rssi_bars(wifi_get_rssi());

        // Wi-Fi channel + protocol
        {
            wifi_ap_record_t ap;
            if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
                const char *proto = "Wi-Fi 4";
#if SOC_WIFI_HE_SUPPORT
                if (ap.phy_11ax) proto = "Wi-Fi 6";
                else
#endif
                if (ap.phy_11n) proto = "Wi-Fi 4";
                else if (ap.phy_11g) proto = "11g";
                else proto = "11b";

                const char *band = (ap.primary <= 14) ? "2.4G" : "5G";
                char mode_buf[32];
                snprintf(mode_buf, sizeof(mode_buf), "%s %s Ch:%d",
                         proto, band, ap.primary);
                lv_label_set_text(s_info_mode, mode_buf);
            }
        }

        // HomeKit status
        if (homekit_get_paired_count() > 0) {
            lv_label_set_text(s_info_setup, "HomeKit Paired");
        } else {
            char hk_buf[32];
            snprintf(hk_buf, sizeof(hk_buf), "SETUP %s", homekit_get_setup_code());
            lv_label_set_text(s_info_setup, hk_buf);
        }

        // Prepare info panel off-screen right
        lv_obj_set_x(s_info_panel, LCD_H_RES);
        lv_obj_clear_flag(s_info_panel, LV_OBJ_FLAG_HIDDEN);

        // Slide crypto panel + ticker out to the left
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_main_panel);
        lv_anim_set_values(&a, 0, -LCD_H_RES);
        lv_anim_set_duration(&a, SLIDE_DUR);
        lv_anim_set_exec_cb(&a, anim_x_cb);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
        lv_anim_start(&a);

        lv_anim_init(&a);
        lv_anim_set_var(&a, s_side_viewport);
        lv_anim_set_values(&a, SIDE_X, -LCD_H_RES);
        lv_anim_set_duration(&a, SLIDE_DUR);
        lv_anim_set_exec_cb(&a, anim_x_cb);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
        lv_anim_start(&a);

        // Slide info panel in from the right
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_info_panel);
        lv_anim_set_values(&a, LCD_H_RES, 0);
        lv_anim_set_duration(&a, SLIDE_DUR);
        lv_anim_set_exec_cb(&a, anim_x_cb);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
        lv_anim_set_completed_cb(&a, slide_to_info_done);
        lv_anim_start(&a);
    } else {
        // Prepare crypto panel + ticker off-screen right
        lv_obj_set_x(s_main_panel, LCD_H_RES);
        lv_obj_clear_flag(s_main_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_x(s_side_viewport, LCD_H_RES);
        lv_obj_clear_flag(s_side_viewport, LV_OBJ_FLAG_HIDDEN);

        // Slide info panel out to the left
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_info_panel);
        lv_anim_set_values(&a, 0, -LCD_H_RES);
        lv_anim_set_duration(&a, SLIDE_DUR);
        lv_anim_set_exec_cb(&a, anim_x_cb);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
        lv_anim_set_completed_cb(&a, slide_to_crypto_done);
        lv_anim_start(&a);

        // Slide crypto panel + ticker in from the right
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_main_panel);
        lv_anim_set_values(&a, LCD_H_RES, 0);
        lv_anim_set_duration(&a, SLIDE_DUR);
        lv_anim_set_exec_cb(&a, anim_x_cb);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
        lv_anim_start(&a);

        lv_anim_init(&a);
        lv_anim_set_var(&a, s_side_viewport);
        lv_anim_set_values(&a, LCD_H_RES, SIDE_X);
        lv_anim_set_duration(&a, SLIDE_DUR);
        lv_anim_set_exec_cb(&a, anim_x_cb);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
        lv_anim_start(&a);
    }
}

// ── Background task: updates time, temperature, heap, RSSI ─────────

void info_update_task(void *arg)
{
    (void)arg;
    s_info_task = xTaskGetCurrentTaskHandle();
    while (1) {
        if (s_ui_teardown) {
            s_info_task = NULL;
            vTaskDelete(NULL);
            return;
        }
        if (!s_show_info) {
            // Sleep until notified by toggle_info_panel()
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));

        char time_buf[12] = "";

        // Time
        time_t now;
        struct tm ti;
        time(&now);
        localtime_r(&now, &ti);
        if (ti.tm_year + 1900 >= 2024) {
            snprintf(time_buf, sizeof(time_buf), "%02d:%02d:%02d",
                     ti.tm_hour, ti.tm_min, ti.tm_sec);
        }

        // Chip temperature → arc value (range 0-80°C)
        int temp_val = 0;
        char temp_txt[24] = "--";
        if (s_temp_sensor) {
            float celsius = 0;
            if (temperature_sensor_get_celsius(s_temp_sensor, &celsius) == ESP_OK) {
                temp_val = (int)(celsius + 0.5f);
                if (temp_val < 0) temp_val = 0;
                if (temp_val > 80) temp_val = 80;
                int w = (int)celsius;
                int f = ((int)(celsius * 10)) % 10;
                if (f < 0) f = -f;
                snprintf(temp_txt, sizeof(temp_txt), "%d.%d", w, f);
            }
        }
        // Temp color: green < 55, yellow 55-70, red > 70
        lv_color_t temp_color;
        if (temp_val < 55) {
            temp_color = lv_color_hex(0x00FF88);
        } else if (temp_val < 70) {
            temp_color = lv_color_hex(0xFFEB3B);
        } else {
            temp_color = lv_color_hex(0xFF3366);
        }

        // Free heap → usage percentage
        uint32_t free_heap = esp_get_free_heap_size();
        int used_pct = (s_heap_total > 0)
            ? 100 - (int)(free_heap * 100 / s_heap_total)
            : 0;
        if (used_pct < 0) used_pct = 0;
        if (used_pct > 100) used_pct = 100;
        char heap_txt[12];
        snprintf(heap_txt, sizeof(heap_txt), "%d%%", used_pct);
        // Heap color: green < 70%, yellow 70-85%, red > 85%
        lv_color_t heap_color;
        if (used_pct < 70) {
            heap_color = lv_color_hex(0x00FF88);
        } else if (used_pct < 85) {
            heap_color = lv_color_hex(0xFFEB3B);
        } else {
            heap_color = lv_color_hex(0xFF3366);
        }

        // RSSI
        int rssi = wifi_get_rssi();

        if (lvgl_port_lock(100)) {
            if (time_buf[0]) lv_label_set_text(s_info_time, time_buf);
            // Temp arc
            lv_arc_set_value(s_info_temp_arc, temp_val);
            lv_obj_set_style_arc_color(s_info_temp_arc, temp_color, LV_PART_INDICATOR);
            lv_label_set_text(s_info_temp_lbl, temp_txt);
            // Heap arc
            lv_arc_set_value(s_info_heap_arc, used_pct);
            lv_obj_set_style_arc_color(s_info_heap_arc, heap_color, LV_PART_INDICATOR);
            lv_label_set_text(s_info_heap_lbl, heap_txt);
            update_rssi_bars(rssi);
            // HomeKit status
            if (homekit_get_paired_count() > 0) {
                lv_label_set_text(s_info_setup, "HomeKit Paired");
            } else {
                char hk_buf[32];
                snprintf(hk_buf, sizeof(hk_buf), "SETUP %s", homekit_get_setup_code());
                lv_label_set_text(s_info_setup, hk_buf);
            }
            lvgl_port_unlock();
        }
    }
}

void temp_sensor_init(void)
{
    temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    esp_err_t ret = temperature_sensor_install(&cfg, &s_temp_sensor);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Temp sensor install failed: %s", esp_err_to_name(ret));
        return;
    }
    temperature_sensor_enable(s_temp_sensor);
    ESP_LOGI(TAG, "Temperature sensor initialized");
}

void ui_info_cleanup(void)
{
    s_info_panel = NULL;
    s_info_time = NULL;
    s_info_temp_arc = NULL;
    s_info_temp_lbl = NULL;
    s_info_heap_arc = NULL;
    s_info_heap_lbl = NULL;
    s_info_ip = NULL;
    for (int i = 0; i < 4; i++) s_rssi_bars[i] = NULL;
    s_info_mode = NULL;
    s_info_setup = NULL;
}
