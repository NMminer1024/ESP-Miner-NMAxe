#pragma once
#include <Arduino.h>
#include "lvgl.h"
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include "../mining/mining_types.h"   // MinerStatus, MinerRuntimeState
#include "../app/runtime_state.h"     // BenchmarkState

// ============================================================================
//  OverlayCtx — dependencies the overlay layer reads (LVGL thread, read-only).
// ============================================================================
struct OverlayCtx {
    volatile uint8_t*  bm_mode = nullptr;   // benchmark mode flag
    BenchmarkState*    bm      = nullptr;   // live sweep progress
    MinerStatus*       status  = nullptr;   // runtime_state / user_paused
    EventGroupHandle_t sys_evt = nullptr;   // OC/OT fault bits
};

// ============================================================================
//  OverlayManager — single top-layer panel shown above the page tileview.
//
//  Priority (highest first): OC/OT power alert > benchmark progress >
//  mining-paused. Driven from the LVGL thread via update() (self-throttled).
// ============================================================================
class OverlayManager {
public:
    static OverlayManager& instance();
    void init(const OverlayCtx& ctx);
    void update();   // call from LVGL thread (render loop)

private:
    OverlayManager() = default;
    void _build();
    void _show(uint32_t accent, const char* title, const String& body);
    void _hide();

    OverlayCtx _ctx;
    lv_obj_t*  _panel = nullptr;
    lv_obj_t*  _lb_title = nullptr;
    lv_obj_t*  _lb_body  = nullptr;
    bool       _visible = false;
    uint32_t   _last_ms = 0;
    uint32_t   _find_start = 0;   // find-me blink start (0 = inactive)
};
