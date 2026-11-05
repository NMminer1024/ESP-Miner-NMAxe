#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/event_groups.h>
#include "runtime_state.h"                 // BenchmarkState
#include "../mining/mining_types.h"        // AsicMinerClass, MinerStatus
#include "../drivers/power/power_ctx.h"    // PowerTelemetry
#include "../drivers/power/power_hal.h"    // AxePowerHal (OC/OT fault check)
#include "../drivers/temp/temp_ctx.h"     // TempState
#include "../drivers/fan/fan.h"           // fan_status_t
#include <vector>

struct BoardSpecConfig;  // fwd — board.h not needed for a pointer member

// ============================================================================
//  BenchmarkCtx — launch context for benchmark_thread_entry
//
//  Auto frequency/vcore sweep: samples hashrate at each (freq, vcore) point,
//  persists stable results to NVS, and HOT-SWITCHES to the next round in place
//  (no per-round reboot). Applies the best result to Normal mode when the
//  sweep completes.
// ============================================================================
struct BenchmarkCtx {
    volatile uint8_t* bm_mode = nullptr;        // 0=Normal (idle), 1=Benchmark (run); flipped live by the web API
    BenchmarkState*   bm = nullptr;             // UI overlay progress sink
    AsicMinerClass*   miner = nullptr;          // get_asic_small_cores / get_asic_count / frequency hot-switch
    MinerStatus*      status = nullptr;         // live hashrate
    const PowerTelemetry* pwr = nullptr;        // measured vbus/ibus/vcore
    AxePowerHal*      power = nullptr;          // OC/OT fault status (HAL)
    const TempState*  temp = nullptr;           // sampled asic/vcore temps
    // Per-fan runtime status. Only the duty percentage is used: a result records
    // whether cooling was already saturated at that operating point, which is what
    // makes a point unsuitable for 24/7 use. fan id 0 = ASIC, id 1 = Vcore/VRM.
    const std::vector<fan_status_t>* fans = nullptr;
    BoardSpecConfig*  spec = nullptr;           // board defaults + live req_frq/req_vcore (hot-switch target)
    EventGroupHandle_t init_evt = nullptr;      // INIT_EVENT_MINER_READY gate
};
