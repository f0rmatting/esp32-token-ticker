/*
 * SPDX-FileCopyrightText: 2025-2026 Bruce
 * SPDX-License-Identifier: CC-BY-NC-4.0
 */

#include "led.h"
#include "board_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "led";
static led_strip_handle_t s_strip;
static TaskHandle_t s_led_task;

// Shared state (written by API, read by task)
static volatile bool s_flash_up;       // direction for price tick flash
static volatile bool s_mood_up = false; // 24h direction for breathing color

// Breathing parameters
#define BREATH_MIN     20
#define BREATH_MAX     80
#define BREATH_STEP_MS 40
#define BREATH_STEPS   40
// Full cycle: 40 steps × 40ms × 2 directions = 3.2s

#define FLASH_BRIGHTNESS 200

// ── Pixel helper ────────────────────────────────────────────────────
static inline void set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    // Hardware has R/G channels swapped
    led_strip_set_pixel(s_strip, 0, g, r, b);
    led_strip_refresh(s_strip);
}

// ── Price tick flash: bright pulse ×3 ────────────────────────────────
static void do_flash(bool up)
{
    uint8_t r_max = up ? 0  : FLASH_BRIGHTNESS;
    uint8_t g_max = up ? FLASH_BRIGHTNESS : 0;

    for (int rep = 0; rep < 3; rep++) {
        // Fade in
        for (int b = 0; b <= 10; b++) {
            set_rgb(r_max * b / 10, g_max * b / 10, 0);
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        // Fade out
        for (int b = 10; b >= 0; b--) {
            set_rgb(r_max * b / 10, g_max * b / 10, 0);
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        vTaskDelay(pdMS_TO_TICKS(60));
    }
    // No set_mood_color() — breathing loop takes over
}

// ── LED task: breathing loop with flash interrupts ───────────────────
static void led_task(void *arg)
{
    (void)arg;

    int step = 0;
    bool rising = true;

    while (1) {
        // Compute current breathing brightness (triangle wave)
        uint8_t brightness = rising
            ? BREATH_MIN + (BREATH_MAX - BREATH_MIN) * step / BREATH_STEPS
            : BREATH_MAX - (BREATH_MAX - BREATH_MIN) * step / BREATH_STEPS;

        bool up = s_mood_up;
        uint8_t r = up ? 0 : brightness;
        uint8_t g = up ? brightness : 0;
        set_rgb(r, g, 0);

        // Wait for step interval or flash notification
        uint32_t notified = ulTaskNotifyTake(pdTRUE,
                                             pdMS_TO_TICKS(BREATH_STEP_MS));
        if (notified) {
            do_flash(s_flash_up);
            // After flash, continue breathing from current position
        }

        // Advance breathing step
        step++;
        if (step >= BREATH_STEPS) {
            step = 0;
            rising = !rising;
        }
    }
}

// ── Public API ──────────────────────────────────────────────────────
esp_err_t led_init(void)
{
    led_strip_config_t strip_cfg = {
        .strip_gpio_num = LED_STRIP_GPIO,
        .max_leds = LED_STRIP_NUM,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };

    led_strip_rmt_config_t rmt_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };

    ESP_RETURN_ON_ERROR(
        led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip),
        TAG, "LED strip init failed");

    led_strip_clear(s_strip);

    xTaskCreate(led_task, "led", 3072, NULL, 2, &s_led_task);
    ESP_LOGI(TAG, "WS2812B LED initialized on GPIO%d", LED_STRIP_GPIO);
    return ESP_OK;
}

void led_set_market_mood(bool is_24h_up)
{
    s_mood_up = is_24h_up;
}

void led_flash_price(bool went_up)
{
    s_flash_up = went_up;
    if (s_led_task) {
        xTaskNotifyGive(s_led_task);
    }
}
