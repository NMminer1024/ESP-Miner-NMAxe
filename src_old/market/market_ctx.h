#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include "market/market.h"

#define MINER_MARKET_UPDATE_INTERVAL    (1000 * 30)   // ms between market refresh cycles

// ============================================================================
//  MarketCtx — launch context for market_thread_entry
//
//  Replaces board_sal_t cross-references: the thread reads wifi/ota/screensaver
//  state through DI handles and the coin symbols configured at boot.
// ============================================================================
struct MarketCtx {
    MarketClass*       market      = nullptr;   // crypto price client instance
    volatile int*      wifi_status = nullptr;   // WifiState::status (wl_status_t)
    volatile bool*     ota_running = nullptr;   // shared OTA flag
    EventGroupHandle_t init_evt    = nullptr;   // INIT_EVENT_WIFI_STA_CONNECTED gate
    EventGroupHandle_t sys_evt     = nullptr;   // SYS_EVENT_SCREEN_SAVER_TRIGGERED
    String             coin_price;              // main display symbol, e.g. "BTC"
    String             coin_watchlist;          // comma-separated, e.g. "BTC,ETH,LTC"
};
