/*
 * SPDX-FileCopyrightText: 2025-2026 Bruce
 * SPDX-License-Identifier: CC-BY-NC-4.0
 */

#pragma once

#include "board_config.h"
#include "lvgl.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ── Layout constants ────────────────────────────────────────────────
#define MARGIN_H        12          // horizontal padding for content
#define MARGIN_TOP      8
#define GAP             8
#define CONTENT_W       (LCD_H_RES - 2 * MARGIN_H)     // 296
#define CONTENT_H       (LCD_V_RES - 2 * MARGIN_TOP)   // 164

// Main panel (left) + side panel (right)
#define SIDE_W      80
#define SIDE_X      (LCD_H_RES - MARGIN_H - SIDE_W)            // 228
#define SIDE_H      CONTENT_H                                   // 156
#define SIDE_ROW_H  90                                          // card row height for marquee (incl. gap)
#define CHART_Y     76
#define CHART_H     (LCD_V_RES - CHART_Y)                      // 96
#define PILL_H      26
#define PILL_RADIUS 13

#define CHART_POINTS      48
#define CHART_INTERVAL_MS (30 * 60 * 1000)   // 30 min between chart samples

#define CRYPTO_COUNT 4

// ── Crypto data ────────────────────────────────────────────────────
typedef struct {
    const char *symbol;
    const char *name;
    const lv_image_dsc_t *logo;
    double      price;
    double      change_pct;
    double      high_24h;
    double      low_24h;
    double      hist_raw[CHART_POINTS];   // raw price history (full precision)
} crypto_item_t;

// ── Shared state (defined in ui.c) ─────────────────────────────────
extern crypto_item_t g_crypto[];
extern int  s_focus_idx;
extern bool s_animating;
extern bool s_show_info;
extern lv_obj_t *s_loading_overlay;

// Main panel + side cards (defined in ui.c, needed by ui_info.c for slide)
extern lv_obj_t *s_main_panel;
extern lv_obj_t *s_side_viewport;

// ── Helpers (defined in ui.c) ──────────────────────────────────────
lv_color_t chg_color(double pct);
void format_price(char *buf, size_t len, double price);
void format_compact_price(char *buf, size_t len, double price);
void format_change(char *buf, size_t len, double pct);

// ── Teardown flag (set by ui_cleanup, checked by background tasks) ──
extern volatile bool s_ui_teardown;

// ── Cross-module functions ─────────────────────────────────────────
// ui.c
void switch_focus(void);

// ui_info.c
void create_info_panel(lv_obj_t *parent);
void toggle_info_panel(void);
void temp_sensor_init(void);
void info_update_task(void *arg);
void ui_info_cleanup(void);

// button.c — btn_init() declared in ui.h (called early from app_main)
