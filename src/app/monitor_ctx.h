#pragma once
#include <Arduino.h>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/event_groups.h>
#include "../mining/mining_types.h"        // MinerStatus, AsicMinerClass, BoardSpecConfig
#include "../drivers/power/power_hal.h"    // AxePowerHal
#include "../drivers/power/power_ctx.h"    // PowerTelemetry
#include "../drivers/temp/temp_ctx.h"     // TempState
#include "../drivers/fan/fan.h"           // fan_status_t

// ============================================================================
//  MonitorCtx — launch context for monitor_thread_entry
//
//  The monitor owns: NTP/time sync, per-second telemetry sampling, hashrate
//  recalculation + efficiency, the safety watchdogs (temp throttle, fan/power/
//  hashrate → reboot), history queue, and NVS persistence. UI page-rolling and
//  backlight control are handled by the reflect UI framework, not here.
// ============================================================================
struct MonitorCtx {
    AxePowerHal*     power  = nullptr;       // measured rail readers
    BoardSpecConfig* spec   = nullptr;       // mutable: req_vcore throttle + ui.hashrate_dist_page
    AsicMinerClass*  miner  = nullptr;       // calculate_hashrate()
    MinerStatus*     status = nullptr;       // shared mining runtime state
    PowerTelemetry*  pwr    = nullptr;       // measured vbus/ibus/vcore sink
    const TempState* temp   = nullptr;       // sampled asic/vcore temps
    std::vector<fan_status_t>* fan_status = nullptr;

    volatile int*      wifi_status = nullptr;  // wl_status_t snapshot
    volatile int*      wifi_rssi   = nullptr;  // RSSI sink
    volatile uint64_t* utc  = nullptr;         // shared UTC seconds (time domain)
    String*            tz   = nullptr;         // timezone string, e.g. "8.0"
    volatile bool*     ota_running = nullptr;
    volatile uint8_t*  bm_mode = nullptr;

    SemaphoreHandle_t  reboot_xsem       = nullptr;
    SemaphoreHandle_t  nvs_save_xsem     = nullptr;
    SemaphoreHandle_t  force_config_xsem = nullptr;
    EventGroupHandle_t sys_evt = nullptr;
};
