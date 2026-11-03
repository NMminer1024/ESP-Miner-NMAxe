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
//  state through DI handles.
//
//  NOTE: the coin symbols are deliberately NOT stored here. They live in NVS and
//  are re-read by the market thread on every refresh cycle, so a change made in
//  the web UI takes effect without a reboot. Holding a copy in this struct would
//  reintroduce the stale-symbol bug (UI shows the new coin next to the old
//  coin's price) that the NVS re-read was added to fix.
// ============================================================================
struct MarketCtx {
    MarketClass*       market      = nullptr;   // crypto price client instance
    volatile int*      wifi_status = nullptr;   // WifiState::status (wl_status_t)
    volatile bool*     ota_running = nullptr;   // shared OTA flag
    EventGroupHandle_t init_evt    = nullptr;   // INIT_EVENT_WIFI_STA_CONNECTED gate
    EventGroupHandle_t sys_evt     = nullptr;   // SYS_EVENT_SCREEN_SAVER_TRIGGERED
};
