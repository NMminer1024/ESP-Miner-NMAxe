#pragma once
#include <Arduino.h>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// ============================================================================
//  Aphorism — motivational quote pool fetched from zenquotes.io, shown as the
//  (non-black) screensaver. Replaces legacy board.status.aphorism.
// ============================================================================
struct AphorismQuote {
    String quote;
    String author;
    String keyword;   // matched keyword (debug/telemetry)
};

struct AphorismState {
    std::vector<AphorismQuote> pool;          // producer appends, consumer pops front
    SemaphoreHandle_t          mutex = nullptr;
};

// ============================================================================
//  AphorismCtx — launch context for aphorism_thread_entry (producer).
// ============================================================================
struct AphorismCtx {
    AphorismState*  state       = nullptr;
    volatile int*   wifi_status = nullptr;   // wl_status_t (skip fetch if not connected)
    volatile bool*  ota_running = nullptr;   // skip fetch during OTA
    EventGroupHandle_t init_evt = nullptr;   // INIT_EVENT_WIFI_STA_CONNECTED gate
};
