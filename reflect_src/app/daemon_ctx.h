#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "../mining/mining_types.h"
#include "../net/wifi_ctx.h"

// ============================================================================
//  DaemonCtx — launch context for daemon_thread_entry
//
//  The daemon owns reboot/factory-reset orchestration and the stratum/ASIC
//  freeze watchdogs. It reads shared runtime state through DI handles instead
//  of the legacy g_board god-object.
// ============================================================================
struct DaemonCtx {
    SemaphoreHandle_t reboot_xsem          = nullptr;  // fire to request a reboot
    SemaphoreHandle_t recover_factory_xsem = nullptr;  // user-triggered factory reset
    SemaphoreHandle_t wifi_reconnect_xsem  = nullptr;  // wifi reconnect request
    volatile bool*    ota_running = nullptr;           // skip checks while OTA active
    volatile int*     wifi_status = nullptr;           // wl_status_t snapshot
    volatile uint8_t* bm_mode     = nullptr;           // 0=Normal, 1=Benchmark
    MinerStatus*      status      = nullptr;           // stratum_update / asic_update
    WifiConnConfig*   wifi_cfg    = nullptr;           // sta ssid/pwd for reconnect
};
