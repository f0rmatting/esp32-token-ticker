/*
 * AXS5106 touch controller driver
 * Based on Waveshare ESP32-S3-Touch-LCD-1.47 demo
 */

#include <stdio.h>
#include "esp_lcd_touch_axs5106.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "esp_lcd_touch.h"

static const char *TAG = "axs5106";

#define TOUCH_AXS5106_DATA_REG  (0x01)

static i2c_master_dev_handle_t s_dev_handle;

/*******************************************************************************
 * Function definitions
 *******************************************************************************/
static esp_err_t esp_lcd_touch_axs5106_read_data(esp_lcd_touch_handle_t tp);
static bool esp_lcd_touch_axs5106_get_xy(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y, uint16_t *strength, uint8_t *point_num, uint8_t max_point_num);
static esp_err_t esp_lcd_touch_axs5106_del(esp_lcd_touch_handle_t tp);

/*******************************************************************************
 * I2C communication — separate transmit + receive (matching demo)
 *******************************************************************************/
static esp_err_t touch_i2c_read(uint8_t reg, uint8_t *data, uint8_t len)
{
    esp_err_t ret = i2c_master_transmit(s_dev_handle, &reg, 1, 100);
    if (ret != ESP_OK) {
        return ret;
    }
    return i2c_master_receive(s_dev_handle, data, len, 100);
}

/*******************************************************************************
 * Public API
 *******************************************************************************/
esp_err_t esp_lcd_touch_new_i2c_axs5106(i2c_master_dev_handle_t dev_handle, const esp_lcd_touch_config_t *config, esp_lcd_touch_handle_t *out_touch)
{
    esp_err_t ret = ESP_OK;
    s_dev_handle = dev_handle;
    assert(config != NULL);
    assert(out_touch != NULL);

    esp_lcd_touch_handle_t tp = heap_caps_calloc(1, sizeof(esp_lcd_touch_t), MALLOC_CAP_DEFAULT);
    ESP_GOTO_ON_FALSE(tp, ESP_ERR_NO_MEM, err, TAG, "no mem for AXS5106 controller");

    tp->read_data = esp_lcd_touch_axs5106_read_data;
    tp->get_xy = esp_lcd_touch_axs5106_get_xy;
    tp->del = esp_lcd_touch_axs5106_del;

    tp->data.lock.owner = portMUX_FREE_VAL;

    memcpy(&tp->config, config, sizeof(esp_lcd_touch_config_t));

    /* Prepare pin for touch interrupt */
    if (tp->config.int_gpio_num != GPIO_NUM_NC)
    {
        const gpio_config_t int_gpio_config = {
            .mode = GPIO_MODE_INPUT,
            .intr_type = (tp->config.levels.interrupt ? GPIO_INTR_POSEDGE : GPIO_INTR_NEGEDGE),
            .pin_bit_mask = BIT64(tp->config.int_gpio_num),
        };
        ret = gpio_config(&int_gpio_config);
        ESP_GOTO_ON_ERROR(ret, err, TAG, "INT GPIO config failed");

        if (tp->config.interrupt_callback)
        {
            esp_lcd_touch_register_interrupt_callback(tp, tp->config.interrupt_callback);
        }
    }

    /* Prepare pin for touch controller reset */
    if (tp->config.rst_gpio_num != GPIO_NUM_NC)
    {
        const gpio_config_t rst_gpio_config = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = BIT64(tp->config.rst_gpio_num),
        };
        ret = gpio_config(&rst_gpio_config);
        ESP_GOTO_ON_ERROR(ret, err, TAG, "RST GPIO config failed");
    }

    /* Reset controller (10ms timing, matching demo) */
    if (tp->config.rst_gpio_num != GPIO_NUM_NC)
    {
        gpio_set_level(tp->config.rst_gpio_num, tp->config.levels.reset);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(tp->config.rst_gpio_num, !tp->config.levels.reset);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGI(TAG, "AXS5106 touch controller initialized");

err:
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error (0x%x)! Touch controller AXS5106 initialization failed!", ret);
        if (tp)
        {
            esp_lcd_touch_axs5106_del(tp);
            tp = NULL;
        }
    }

    *out_touch = tp;
    return ret;
}

/*******************************************************************************
 * Read touch data from controller
 *******************************************************************************/
static esp_err_t esp_lcd_touch_axs5106_read_data(esp_lcd_touch_handle_t tp)
{
    uint8_t data[14] = {0};

    assert(tp != NULL);

    esp_err_t err = touch_i2c_read(TOUCH_AXS5106_DATA_REG, data, 14);
    if (err != ESP_OK)
    {
        /* Don't propagate — esp_lvgl_port uses ESP_ERROR_CHECK which aborts */
        portENTER_CRITICAL(&tp->data.lock);
        tp->data.points = 0;
        portEXIT_CRITICAL(&tp->data.lock);
        return ESP_OK;
    }

    uint8_t points = data[1] & 0x0F;

    if (points == 0)
    {
        return ESP_OK;
    }

    if (points > 2) points = 2;

    portENTER_CRITICAL(&tp->data.lock);
    tp->data.points = points;

    for (size_t i = 0; i < points; i++)
    {
        tp->data.coords[i].x = ((uint16_t)(data[2 + i * 6] & 0x0F)) << 8 | data[3 + i * 6];
        tp->data.coords[i].y = ((uint16_t)(data[4 + i * 6] & 0x0F)) << 8 | data[5 + i * 6];
    }
    portEXIT_CRITICAL(&tp->data.lock);

    return ESP_OK;
}

/*******************************************************************************
 * Get touch coordinates
 *******************************************************************************/
static bool esp_lcd_touch_axs5106_get_xy(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y, uint16_t *strength, uint8_t *point_num, uint8_t max_point_num)
{
    assert(tp != NULL);
    assert(x != NULL);
    assert(y != NULL);
    assert(point_num != NULL);
    assert(max_point_num > 0);

    portENTER_CRITICAL(&tp->data.lock);

    *point_num = (tp->data.points > max_point_num ? max_point_num : tp->data.points);

    for (size_t i = 0; i < *point_num; i++)
    {
        x[i] = tp->data.coords[i].x;
        y[i] = tp->data.coords[i].y;

        if (strength)
        {
            strength[i] = tp->data.coords[i].strength;
        }
    }

    tp->data.points = 0;

    portEXIT_CRITICAL(&tp->data.lock);

    return (*point_num > 0);
}

/*******************************************************************************
 * Delete
 *******************************************************************************/
static esp_err_t esp_lcd_touch_axs5106_del(esp_lcd_touch_handle_t tp)
{
    assert(tp != NULL);

    if (tp->config.int_gpio_num != GPIO_NUM_NC)
    {
        gpio_reset_pin(tp->config.int_gpio_num);
        if (tp->config.interrupt_callback)
        {
            gpio_isr_handler_remove(tp->config.int_gpio_num);
        }
    }

    if (tp->config.rst_gpio_num != GPIO_NUM_NC)
    {
        gpio_reset_pin(tp->config.rst_gpio_num);
    }

    free(tp);
    return ESP_OK;
}
