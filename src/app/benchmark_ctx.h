#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/event_groups.h>
#include "runtime_state.h"                 // BenchmarkState
#include "../mining/mining_types.h"        // AsicMinerClass, MinerStatus
#include "../drivers/power/power_ctx.h"    // PowerTelemetry
#include "../drivers/temp/temp_ctx.h"     // TempState

// ============================================================================
//  BenchmarkCtx — launch context for benchmark_thread_entry
//
//  Auto frequency/vcore sweep: samples hashrate at each (freq, vcore) point,
//  persists stable results to NVS, and reboots to the next round. Applies the
//  best result to Normal mode when the sweep completes.
// ============================================================================
struct BenchmarkCtx {
    volatile uint8_t* bm_mode = nullptr;        // 0=Normal (exit), 1=Benchmark (run)
    BenchmarkState*   bm = nullptr;             // UI overlay progress sink
    AsicMinerClass*   miner = nullptr;          // get_asic_small_cores / get_asic_count
    MinerStatus*      status = nullptr;         // live hashrate
    const PowerTelemetry* pwr = nullptr;        // measured vbus/ibus/vcore
    const TempState*  temp = nullptr;           // sampled asic/vcore temps
    SemaphoreHandle_t reboot_xsem = nullptr;
    EventGroupHandle_t init_evt = nullptr;      // INIT_EVENT_MINER_READY gate
};
