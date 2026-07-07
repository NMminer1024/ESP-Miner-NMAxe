#pragma once
#include <Arduino.h>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/event_groups.h>

#include "../mining/mining_types.h"      // MinerStatus, ConnInfo, AsicMinerClass, StratumClass, BoardSpecConfig, AxePowerHal
#include "../market/market_ctx.h"        // MarketClass
#include "../net/wifi_ctx.h"             // WifiState, WifiConnConfig
#include "../net/swarm_ctx.h"            // SwarmState, NeighborState
#include "../drivers/power/power_ctx.h"  // PowerTelemetry
#include "../drivers/temp/temp_ctx.h"    // TempState
#include "../drivers/fan/fan.h"          // fan_status_t
#include "../app/runtime_state.h"        // OtaState, PreferenceState, TimeState, BenchmarkState

// ============================================================================
//  WebCtx — launch context for webserver_thread_entry (HTTP + WebSocket)
//
//  Replaces the god-object board_sal_t* the legacy http_server.cpp reached
//  into. Every cross-domain dependency the request handlers touch is injected
//  explicitly here; a single module-global `g_web` (set once at thread start)
//  lets the AsyncWebServer free-function handlers reach the context without a
//  per-request capture.
// ============================================================================
struct WebCtx {
    // ── Mining / hardware instances ─────────────────────────────────────────
    AsicMinerClass*  miner   = nullptr;   // g_board.miner->
    StratumClass*    stratum = nullptr;   // g_board.stratum->
    MarketClass*     market  = nullptr;   // g_board.market->
    AxePowerHal*     power   = nullptr;   // g_board.power->

    // ── Shared state structs ────────────────────────────────────────────────
    BoardSpecConfig* spec     = nullptr;  // g_board.info.spec
    MinerStatus*     status   = nullptr;  // g_board.status.miner
    ConnInfo*        conn     = nullptr;  // g_board.info.connection
    WifiState*       wifi     = nullptr;  // g_board.status.wifi
    WifiConnConfig*  wifi_cfg = nullptr;  // hostname / sta ssid+pwd / ap ssid
    PowerTelemetry*  pwr      = nullptr;  // g_board.status.power
    TempState*       temp     = nullptr;  // g_board.status.temp
    TimeState*       time     = nullptr;  // g_board.status.time.format
    OtaState*        ota      = nullptr;  // g_board.status.ota
    PreferenceState* pref     = nullptr;  // g_board.status.preference
    NeighborState*   neighbor = nullptr;  // g_board.status.neighbor
    std::vector<fan_status_t>* fan_status = nullptr;  // g_board.status.fan.list

    // ── Scalars / sync primitives ───────────────────────────────────────────
    volatile uint8_t*  bm_mode = nullptr; // g_board.status.bm_mode
    volatile uint64_t* utc     = nullptr; // g_board.status.time.utc
    String*            tz      = nullptr; // g_board.status.time.tz (writable)
    String*            coin_price     = nullptr;  // g_board.info.base.coin_price (writable)
    String*            coin_watchlist = nullptr;  // g_board.info.base.coin_watchlist (writable)
    String             fw_version;        // g_board.info.base.fw_version

    EventGroupHandle_t sys_evt  = nullptr;
    EventGroupHandle_t init_evt = nullptr;
    SemaphoreHandle_t  reboot_xsem            = nullptr;
    SemaphoreHandle_t  brightness_update_xsem = nullptr;
};

// Module-global context pointer, set once at the start of webserver_thread_entry
// before any route is registered. The free-function request handlers read it.
extern WebCtx* g_web;
