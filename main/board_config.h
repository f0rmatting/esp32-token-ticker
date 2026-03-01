/*
 * SPDX-FileCopyrightText: 2025-2026 Bruce
 * SPDX-License-Identifier: CC-BY-NC-4.0
 */

#pragma once

#include "sdkconfig.h"

// ── Common parameters (shared across all targets) ───────────────────

// Display resolution (1.47" LCD, landscape after swap_xy)
#define LCD_H_RES           320
#define LCD_V_RES           172

// LVGL buffer size
#define LCD_BUF_LINES       40

// WS2812B RGB LED count
#define LED_STRIP_NUM       1

// ── Target-specific pin assignments ─────────────────────────────────

#if defined(CONFIG_IDF_TARGET_ESP32C6)
// ── Waveshare ESP32-C6-LCD-1.47 (ST7789) ────────────────────────────

#define LCD_SPI_HOST        SPI2_HOST
#define LCD_SPI_FREQ_HZ     (80 * 1000 * 1000)   // IOMUX pins → 80 MHz OK

// SPI pins (shared with SD card)
#define LCD_PIN_MOSI        GPIO_NUM_6
#define LCD_PIN_CLK         GPIO_NUM_7
#define LCD_PIN_CS          GPIO_NUM_14

// Control pins
#define LCD_PIN_DC          GPIO_NUM_15
#define LCD_PIN_RST         GPIO_NUM_21
#define LCD_PIN_BLK         GPIO_NUM_22

// WS2812B RGB LED
#define LED_STRIP_GPIO      GPIO_NUM_8

// BOOT button (Key1)
#define BTN_BOOT_GPIO       GPIO_NUM_9

#elif defined(CONFIG_IDF_TARGET_ESP32S3)
// ── Waveshare ESP32-S3-Touch-LCD-1.47 (JD9853) ─────────────────────

#define LCD_SPI_HOST        SPI2_HOST
#define LCD_SPI_FREQ_HZ     (80 * 1000 * 1000)

// SPI pins
#define LCD_PIN_MOSI        GPIO_NUM_39
#define LCD_PIN_CLK         GPIO_NUM_38
#define LCD_PIN_CS          GPIO_NUM_21

// Control pins
#define LCD_PIN_DC          GPIO_NUM_45
#define LCD_PIN_RST         GPIO_NUM_40
#define LCD_PIN_BLK         GPIO_NUM_46

// WS2812B RGB LED (no dedicated LED on Touch variant; set to NC)
#define LED_STRIP_GPIO      GPIO_NUM_NC

// BOOT button
#define BTN_BOOT_GPIO       GPIO_NUM_0

// I2C bus (touch controller AXS5106)
#define TP_I2C_SDA          GPIO_NUM_42
#define TP_I2C_SCL          GPIO_NUM_41
#define TP_PIN_RST          GPIO_NUM_48
#define TP_PIN_INT          GPIO_NUM_47

#else
#error "Unsupported target: define pins for your board in board_config.h"
#endif
