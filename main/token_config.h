/*
 * SPDX-FileCopyrightText: 2025-2026 Bruce
 * SPDX-License-Identifier: CC-BY-NC-4.0
 */

#pragma once

#include "lvgl.h"

#define MAX_TOKENS       6    // user selects 4–6
#define TOTAL_AVAILABLE  10
#define DEFAULT_TOKENS   "bitcoin,ethereum,paxg,chainbase,sui"

typedef struct {
    const char *id;           // CoinGecko-style ID, used as NVS key
    const char *symbol;       // ticker symbol shown in UI
    const char *name;         // full name
    const char *pair;         // Gate.io trading pair (NULL = stablecoin, skip fetch)
    const lv_image_dsc_t *logo;
} token_registry_entry_t;

extern const token_registry_entry_t g_token_registry[TOTAL_AVAILABLE];
extern int g_active_count;

void token_config_load(void);           // NVS → g_crypto[]
void token_config_save(const char *csv); // comma-separated IDs → NVS
