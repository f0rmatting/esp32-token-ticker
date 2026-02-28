/*
 * SPDX-FileCopyrightText: 2025-2026 Bruce
 * SPDX-License-Identifier: CC-BY-NC-4.0
 */

#include "homekit.h"

#include <hap.h>
#include <hap_apple_servs.h>
#include <hap_apple_chars.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include <stdio.h>

static const char *TAG = "homekit";

static hap_char_t *s_switch_event_char;
static char s_setup_code[12];

static int acc_identify(hap_acc_t *ha)
{
    ESP_LOGI(TAG, "Accessory identified");
    return HAP_SUCCESS;
}

void homekit_init(void)
{
    hap_init(HAP_TRANSPORT_WIFI);

    /* Derive unique serial number from chip MAC address */
    static char serial[13];
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(serial, sizeof(serial), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    hap_acc_cfg_t cfg = {
        .name = "TokenTicker",
        .manufacturer = "Bitcoin Go Too Moon",
        .model = "TokenTicker-C6",
        .serial_num = serial,
        .fw_rev = "1.0.1",
        .hw_rev = "1.0",
        .pv = "1.1.0",
        .identify_routine = acc_identify,
        .cid = HAP_CID_PROGRAMMABLE_SWITCH,
    };

    hap_acc_t *accessory = hap_acc_create(&cfg);
    if (!accessory) {
        ESP_LOGE(TAG, "Failed to create accessory");
        return;
    }

    hap_serv_t *service = hap_serv_stateless_programmable_switch_create(0);
    if (!service) {
        ESP_LOGE(TAG, "Failed to create switch service");
        hap_acc_delete(accessory);
        return;
    }

    hap_serv_add_char(service, hap_char_name_create("Button"));
    hap_acc_add_serv(accessory, service);

    s_switch_event_char = hap_serv_get_char_by_uuid(service,
                            HAP_CHAR_UUID_PROGRAMMABLE_SWITCH_EVENT);

    hap_add_accessory(accessory);

    /* Derive unique 8-digit setup code from MAC (format: XXX-XX-XXX) */
    uint32_t code = (mac[0] << 16) | (mac[3] << 8) | (mac[4] ^ mac[5]);
    code = (code % 99999999) + 1;  /* 00000001 – 99999999 */
    snprintf(s_setup_code, sizeof(s_setup_code), "%03lu-%02lu-%03lu",
             code / 100000, (code / 1000) % 100, code % 1000);
    hap_set_setup_code(s_setup_code);

    /* Derive 4-char setup ID from MAC: pick 4 alphanumeric chars */
    static const char alphanum[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static char setup_id[5];
    setup_id[0] = alphanum[mac[2] % 36];
    setup_id[1] = alphanum[mac[3] % 36];
    setup_id[2] = alphanum[mac[4] % 36];
    setup_id[3] = alphanum[mac[5] % 36];
    setup_id[4] = '\0';
    hap_set_setup_id(setup_id);

    int ret = hap_start();
    if (ret != HAP_SUCCESS) {
        ESP_LOGE(TAG, "hap_start() failed: %d", ret);
        return;
    }
    ESP_LOGW(TAG, "HomeKit ready (setup code: %s)", s_setup_code);
}

const char *homekit_get_setup_code(void)
{
    return s_setup_code;
}

int homekit_get_paired_count(void)
{
    return hap_get_paired_controller_count();
}

void homekit_reset(void)
{
    /* Stop HAP server first (httpd, mDNS, event loop) so the provisioning
     * HTTP server can bind to port 80 without conflict */
    hap_stop();

    /* Erase HAP NVS namespaces synchronously (hap_reset_to_factory is async
     * and reboots before wifi_prov_start can run) */
    nvs_handle_t h;
    if (nvs_open("hap_ctrl", NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
    if (nvs_open("hap_main", NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGW(TAG, "HomeKit stopped and pairing data cleared");
}

static void homekit_send_event(uint8_t event)
{
    if (!s_switch_event_char) return;
    hap_val_t val = { .u = event };
    hap_char_update_val(s_switch_event_char, &val);
}

void homekit_send_switch_press(void)
{
    homekit_send_event(0);
    ESP_LOGI(TAG, "Switch SinglePress event sent");
}

void homekit_send_switch_double_press(void)
{
    homekit_send_event(1);
    ESP_LOGI(TAG, "Switch DoublePress event sent");
}
