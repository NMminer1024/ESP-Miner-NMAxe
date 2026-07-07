#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/event_groups.h>
#include "../board/board.h"   // BoardSpecConfig (btn pins)

// UI navigation hooks — injected by the app so the button domain stays fully
// decoupled from the UI framework (no hard link dependency). nullptr = no-op.
typedef void (*ui_nav_fn)();

// ============================================================================
//  ButtonCtx — launch context for button_thread_entry
//
//  Boot button: short/double click -> UI page nav; long press -> force config.
//  User button: long press -> factory reset. UI navigation is routed through
//  injected nav hooks; control actions through DI semaphores.
// ============================================================================
struct ButtonCtx {
    const BoardSpecConfig* spec = nullptr;        // btn.boot_pin / btn.user_pin
    EventGroupHandle_t init_evt = nullptr;        // INIT_EVENT_MINER_READY gate
    EventGroupHandle_t sys_evt  = nullptr;        // overlay-dismiss event clears
    SemaphoreHandle_t  force_config_xsem = nullptr;
    SemaphoreHandle_t  recover_factory_xsem = nullptr;
    volatile bool*     ota_running = nullptr;     // suppress ticks during OTA
    ui_nav_fn          on_next_page = nullptr;    // boot single click
    ui_nav_fn          on_prev_page = nullptr;    // boot double click
    ui_nav_fn          on_activity  = nullptr;    // wake / reset inactivity timer
};
