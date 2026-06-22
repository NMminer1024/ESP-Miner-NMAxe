#pragma once
#include <Arduino.h>
#include "lvgl.h"
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include "../mining/mining_types.h"   // MinerStatus, MinerRuntimeState
#include "../app/runtime_state.h"     // BenchmarkState
#include "../app/aphorism_ctx.h"      // AphorismState
#include "../board/board.h"           // BoardSpecConfig

// ============================================================================
//  OverlayCtx — dependencies the overlay layer reads (LVGL thread, read-only).
// ============================================================================
struct OverlayCtx {
    volatile uint8_t*  bm_mode = nullptr;   // benchmark mode flag
    BenchmarkState*    bm      = nullptr;   // live sweep progress
    MinerStatus*       status  = nullptr;   // runtime_state / user_paused
    EventGroupHandle_t sys_evt = nullptr;   // OC/OT fault bits
    SemaphoreHandle_t  reboot_xsem = nullptr;
    OtaState*          ota     = nullptr;   // firmware-update progress
    BoardSpecConfig*   spec    = nullptr;   // defaults for OC reset path
    AphorismState*     aphorism   = nullptr;  // screensaver quote pool
    const uint8_t*     saver_mode = nullptr;  // 0=gif/quote, 1=black
    const char*        gif_path   = nullptr;  // "S:screen_saver_*.gif" (nullptr=none)
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
    void _reset_layout();

    void _gif_hide();
    void _show_factory_overlay(int countdown_sec);
    void _show_setup_overlay(int countdown_sec);
    void _show_rebooting_overlay(const char* title);
    void _show_ota_overlay(uint32_t now);
    void _show_benchmark_overlay();
    void _show_footer_ip(lv_coord_t y, bool large_font = false);

    OverlayCtx _ctx;
    lv_obj_t*  _panel = nullptr;
    lv_obj_t*  _lb_title = nullptr;
    lv_obj_t*  _lb_body  = nullptr;
    lv_obj_t*  _lb_aux   = nullptr;
    lv_obj_t*  _lb_aux2  = nullptr;
    lv_obj_t*  _btn_yes  = nullptr;
    lv_obj_t*  _btn_no   = nullptr;
    lv_obj_t*  _bar = nullptr;
    lv_obj_t*  _bm_rows[7] = {nullptr};
    lv_obj_t*  _bm_ip = nullptr;
    lv_obj_t*  _gif = nullptr;     // lazy lv_gif for the screensaver (mode 0)
    bool       _gif_shown = false;
    bool       _visible = false;
    bool       _screensaver_fading = false;
    bool       _ota_overlay_active = false;
    bool       _ota_rebooting = false;
    uint32_t   _last_ms = 0;
    uint32_t   _find_start = 0;   // find-me blink start (0 = inactive)
    uint32_t   _screensaver_fade_start = 0;
    uint32_t   _ota_dismiss_at = 0;
    uint32_t   _ota_last_update = 0;
    EventBits_t _fault_event = 0;
    // screensaver aphorism rotation
    String     _aph_quote, _aph_author;
    bool       _aph_have = false;
    uint32_t   _aph_last = 0;

    void _show_fault_overlay(bool is_oc);
    static void _fault_action_yes_cb(lv_event_t* e);
    static void _fault_action_no_cb(lv_event_t* e);
};
