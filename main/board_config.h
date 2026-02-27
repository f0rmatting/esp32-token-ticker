/*
 * SPDX-FileCopyrightText: 2025-2026 Bruce
 * SPDX-License-Identifier: CC-BY-NC-4.0
 */

#pragma once

// SPI host
#define LCD_SPI_HOST        SPI2_HOST

// SPI pins (shared with SD card)
#define LCD_PIN_MOSI        GPIO_NUM_6
#define LCD_PIN_CLK         GPIO_NUM_7
#define LCD_PIN_CS          GPIO_NUM_14

// Control pins
#define LCD_PIN_DC          GPIO_NUM_15
#define LCD_PIN_RST         GPIO_NUM_21
#define LCD_PIN_BLK         GPIO_NUM_22

// Display resolution (1.47" ST7789, landscape)
#define LCD_H_RES           320
#define LCD_V_RES           172

// SPI clock frequency
#define LCD_SPI_FREQ_HZ     (80 * 1000 * 1000)

// LVGL buffer size
#define LCD_BUF_LINES       40

// WS2812B RGB LED
#define LED_STRIP_GPIO      GPIO_NUM_8
#define LED_STRIP_NUM       1

// BOOT button (Key1)
#define BTN_BOOT_GPIO       GPIO_NUM_9
