/*
 * SPDX-FileCopyrightText: 2025-2026 Bruce
 * SPDX-License-Identifier: CC-BY-NC-4.0
 */

#include "token_config.h"
#include "ui_internal.h"
#include "crypto_logos.h"

#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

#include <string.h>

static const char *TAG = "token_cfg";

#define NVS_NAMESPACE   "wifi_cfg"
#define NVS_KEY_TOKENS  "tokens"

const token_registry_entry_t g_token_registry[TOTAL_AVAILABLE] = {
    { "bitcoin",   "BTC",  "Bitcoin",    "BTC_USDT",  &img_bitcoin_logo   },
    { "ethereum",  "ETH",  "Ethereum",   "ETH_USDT",  &img_ethereum_logo  },
    { "paxg",      "PAXG", "PAX Gold",   "PAXG_USDT", &img_paxg_logo      },
    { "chainbase", "C",    "Chainbase",  "C_USDT",    &img_chainbase_logo },
    { "sui",       "SUI",  "Sui",        "SUI_USDT",  &img_sui_logo       },
    { "doge",      "DOGE", "Dogecoin",   "DOGE_USDT", &img_doge_logo      },
    { "solana",    "SOL",  "Solana",     "SOL_USDT",  &img_solana_logo    },
    { "tron",      "TRX",  "Tron",       "TRX_USDT",  &img_tron_logo      },
    { "usdc",      "USDC", "USD Coin",   "USDC_USDT", &img_usdc_logo      },
    { "usdt",      "USDT", "Tether",     NULL,         &img_usdt_logo      },
};

int g_active_count = 0;

static const token_registry_entry_t *find_by_id(const char *id)
{
    for (int i = 0; i < TOTAL_AVAILABLE; i++) {
        if (strcmp(g_token_registry[i].id, id) == 0)
            return &g_token_registry[i];
    }
    return NULL;
}

void token_config_load(void)
{
    char csv[128] = {0};
    nvs_handle_t h;

    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(csv);
        if (nvs_get_str(h, NVS_KEY_TOKENS, csv, &len) != ESP_OK || len <= 1) {
            csv[0] = '\0';
        }
        nvs_close(h);
    }

    if (csv[0] == '\0') {
        strncpy(csv, DEFAULT_TOKENS, sizeof(csv) - 1);
    }

    g_active_count = 0;
    memset(g_crypto, 0, sizeof(crypto_item_t) * MAX_TOKENS);

    char *saveptr;
    char *tok = strtok_r(csv, ",", &saveptr);
    while (tok && g_active_count < MAX_TOKENS) {
        const token_registry_entry_t *entry = find_by_id(tok);
        if (entry) {
            g_crypto[g_active_count].symbol = entry->symbol;
            g_crypto[g_active_count].name   = entry->name;
            g_crypto[g_active_count].logo   = entry->logo;
            g_crypto[g_active_count].pair   = entry->pair;
            g_active_count++;
        } else {
            ESP_LOGW(TAG, "Unknown token ID: %s", tok);
        }
        tok = strtok_r(NULL, ",", &saveptr);
    }

    ESP_LOGI(TAG, "Loaded %d active tokens", g_active_count);
}

void token_config_save(const char *csv)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, NVS_KEY_TOKENS, csv);
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGI(TAG, "Tokens saved: %s", csv);
    }
}
