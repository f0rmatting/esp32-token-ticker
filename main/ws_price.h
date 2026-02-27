/*
 * SPDX-FileCopyrightText: 2025-2026 Bruce
 * SPDX-License-Identifier: CC-BY-NC-4.0
 */

#pragma once

/**
 * Start WebSocket connection and subscribe to the given coin index.
 * idx: 0=BTC, 1=ETH, 2=PAXG
 */
void ws_price_start(int focus_idx);

/**
 * Switch subscription: unsubscribe old coin, 2s debounce, subscribe new.
 * During the transition ws_price_get_active_idx() returns -1.
 */
void ws_price_switch(int new_idx);

/**
 * Stop WebSocket client and free resources.
 */
void ws_price_stop(void);

/**
 * Return the coin index currently receiving WS updates, or -1 if inactive.
 */
int ws_price_get_active_idx(void);
