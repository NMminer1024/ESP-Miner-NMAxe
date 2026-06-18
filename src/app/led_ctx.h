#pragma once
#include <Arduino.h>
#include "../board/board.h"            // BoardSpecConfig
#include "runtime_state.h"             // PreferenceState
#include "../stratum/stratum.h"        // StratumClass
#include "../mining/mining_types.h"    // MinerStatus

// ============================================================================
//  LedCtx — launch context for led_thread_entry
//
//  Drives WiFi/pool/sys status LEDs (NMAXE/Gamma) or the NeoPixel light show
//  (NMQAXE++). Pulls live state through DI instead of the g_board god-object.
// ============================================================================
struct LedCtx {
    const BoardSpecConfig* spec = nullptr;       // led pins + board name
    PreferenceState* pref    = nullptr;          // led.enable / led.sleep
    StratumClass*    stratum = nullptr;          // pool subscription state
    MinerStatus*     status  = nullptr;          // hashrate (breathing speed)
    volatile int*    wifi_status  = nullptr;     // wl_status_t snapshot
    volatile bool*   ota_running  = nullptr;     // turn off LEDs during OTA
    volatile int*    ota_progress = nullptr;     // OTA progress bar (NeoPixel)
};
