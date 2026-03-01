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

#if defined(CONFIG_IDF_TARGET_ESP32S3)
#include "esp_lcd_jd9853.h"
#include "driver/i2c_master.h"
#include "esp_lcd_touch_axs5106.h"
#endif

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

    // Release reset, wait 120ms for LCD internal power stabilization
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

#if defined(CONFIG_IDF_TARGET_ESP32C6)
    // Pull SD_CS (GPIO4) high so SD card doesn't interfere on shared SPI bus
    gpio_config_t sd_cs_cfg = {
        .pin_bit_mask = BIT64(GPIO_NUM_4),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&sd_cs_cfg);
    gpio_set_level(GPIO_NUM_4, 1);
#endif

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

    // ── 5. LCD panel ─────────────────────────────────────────────────
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = LCD_PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };

#if defined(CONFIG_IDF_TARGET_ESP32S3)
    ESP_LOGI(TAG, "Initializing JD9853 panel");
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_jd9853(io_handle, &panel_cfg, &s_panel),
        TAG, "JD9853 panel init failed");
#else
    ESP_LOGI(TAG, "Initializing ST7789 panel");
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_st7789(io_handle, &panel_cfg, &s_panel),
        TAG, "ST7789 panel init failed");
#endif

    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);

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

#if defined(CONFIG_IDF_TARGET_ESP32S3)
    // ── 7b. Touch panel (AXS5106 via I2C) ────────────────────────────
    ESP_LOGI(TAG, "Initializing touch panel");
    i2c_master_bus_config_t i2c_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = 0,
        .scl_io_num = TP_I2C_SCL,
        .sda_io_num = TP_I2C_SDA,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t i2c_bus = NULL;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&i2c_cfg, &i2c_bus), TAG, "I2C bus init failed");

    i2c_device_config_t tp_dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ESP_LCD_TOUCH_IO_I2C_AXS5106_ADDRESS,
        .scl_speed_hz = 400000,
    };
    i2c_master_dev_handle_t tp_dev = NULL;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(i2c_bus, &tp_dev_cfg, &tp_dev), TAG, "I2C add touch device failed");

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = LCD_V_RES,    // portrait native: 172
        .y_max = LCD_H_RES,    // portrait native: 320
        .rst_gpio_num = TP_PIN_RST,
        .int_gpio_num = GPIO_NUM_NC,   // polling mode for diagnosis
        .flags = {
            // Landscape 90°: swap XY to match display orientation
            .swap_xy = 1,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };
    esp_lcd_touch_handle_t touch_handle = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_axs5106(tp_dev, &tp_cfg, &touch_handle), TAG, "Touch init failed");

    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = disp,
        .handle = touch_handle,
    };
    lvgl_port_add_touch(&touch_cfg);
    ESP_LOGI(TAG, "Touch panel initialized");
#endif

    // ── 8. Flush black frame into VRAM (display is still DISPOFF) ────
    if (lvgl_port_lock(0)) {
        lv_obj_t *scr = lv_screen_active();
        lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
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

// ── Backlight control ─────────────────────────────────────────────

void display_set_backlight(int brightness)
{
    if (brightness < 0) brightness = 0;
    if (brightness > 255) brightness = 255;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, brightness);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}
