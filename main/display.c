/*
 * SPDX-FileCopyrightText: 2025-2026 Bruce
 * SPDX-License-Identifier: CC-BY-NC-4.0
 */

#include "display.h"
#include "board_config.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "display";

static esp_lcd_panel_handle_t s_panel;

// ── Step 0: Immediately lock BL off, RST low, CS deselected ─────────
static void early_gpio_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = BIT64(LCD_PIN_BLK) | BIT64(LCD_PIN_RST) | BIT64(LCD_PIN_CS),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&cfg);

    gpio_set_level(LCD_PIN_BLK, 0);   // backlight off
    gpio_set_level(LCD_PIN_RST, 0);   // hold in reset
    gpio_set_level(LCD_PIN_CS, 1);    // CS deselected — prevent SPI noise

    // Hard reset: RST low ≥50ms
    vTaskDelay(pdMS_TO_TICKS(50));

    // Release reset, wait 120ms for ST7789 internal power stabilization
    gpio_set_level(LCD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
}

// ── Backlight PWM (LEDC, starts at duty 0) ──────────────────────────
static void backlight_init(void)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer_cfg);

    ledc_channel_config_t ch_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = LCD_PIN_BLK,
        .duty = 0,             // keep backlight off
        .hpoint = 0,
    };
    ledc_channel_config(&ch_cfg);
}

esp_err_t display_init(void)
{
    // ── 1. Lock pins before anything else ────────────────────────────
    early_gpio_init();

    // ── 2. LEDC takes over BL pin (still duty=0) ────────────────────
    ESP_LOGI(TAG, "Initializing backlight (off)");
    backlight_init();

    // Pull SD_CS (GPIO4) high so SD card doesn't interfere on shared SPI bus
    gpio_config_t sd_cs_cfg = {
        .pin_bit_mask = BIT64(GPIO_NUM_4),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&sd_cs_cfg);
    gpio_set_level(GPIO_NUM_4, 1);

    // ── 3. SPI bus (CS is already high from early_gpio_init) ─────────
    ESP_LOGI(TAG, "Initializing SPI bus");
    const spi_bus_config_t bus_cfg = {
        .mosi_io_num = LCD_PIN_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = LCD_PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * LCD_BUF_LINES * sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(
        spi_bus_initialize(LCD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO),
        TAG, "SPI bus init failed");

    // ── 4. Panel IO (takes over CS pin) ──────────────────────────────
    ESP_LOGI(TAG, "Initializing LCD panel IO");
    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = LCD_PIN_DC,
        .cs_gpio_num = LCD_PIN_CS,
        .pclk_hz = LCD_SPI_FREQ_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    esp_lcd_panel_io_handle_t io_handle = NULL;
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_spi(LCD_SPI_HOST, &io_cfg, &io_handle),
        TAG, "LCD panel IO init failed");

    // ── 5. ST7789 panel (we already did hard reset, driver does another — fine)
    ESP_LOGI(TAG, "Initializing ST7789 panel");
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = LCD_PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_st7789(io_handle, &panel_cfg, &s_panel),
        TAG, "ST7789 panel init failed");

    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);       // sends SLPOUT + config, but NOT DISPON

    // ── 6. Explicit DISPOFF before any configuration ─────────────────
    esp_lcd_panel_disp_on_off(s_panel, false);

    esp_lcd_panel_invert_color(s_panel, true);
    // Landscape rotation: swap XY, mirror X
    esp_lcd_panel_swap_xy(s_panel, true);
    esp_lcd_panel_mirror(s_panel, true, false);
    // After swap_xy, the 34-pixel offset moves to Y axis
    esp_lcd_panel_set_gap(s_panel, 0, 34);

    // ── 7. LVGL port ─────────────────────────────────────────────────
    ESP_LOGI(TAG, "Initializing LVGL port");
    const lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_RETURN_ON_ERROR(
        lvgl_port_init(&port_cfg),
        TAG, "LVGL port init failed");

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = s_panel,
        .buffer_size = LCD_H_RES * LCD_BUF_LINES,
        .double_buffer = true,
        .hres = LCD_H_RES,
        .vres = LCD_V_RES,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy = true,
            .mirror_x = true,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = true,
            .swap_bytes = true,
        },
    };
    lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);
    if (disp == NULL) {
        ESP_LOGE(TAG, "Failed to add LVGL display");
        return ESP_FAIL;
    }

    // ── 8. Flush black frame into VRAM (display is still DISPOFF) ────
    if (lvgl_port_lock(0)) {
        lv_obj_t *scr = lv_screen_active();
        lv_obj_set_style_bg_color(scr, lv_color_hex(0x0F0F1A), 0);
        lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
        lv_refr_now(NULL);
        lvgl_port_unlock();
    }

    // ── 9. DISPON — VRAM is now solid dark, safe to show ─────────────
    esp_lcd_panel_disp_on_off(s_panel, true);

    // ── 10. Backlight on last ────────────────────────────────────────
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 150);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);

    ESP_LOGI(TAG, "Display initialized successfully (%dx%d)", LCD_H_RES, LCD_V_RES);
    return ESP_OK;
}
