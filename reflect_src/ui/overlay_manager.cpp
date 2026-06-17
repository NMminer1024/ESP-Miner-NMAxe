#include "overlay_manager.h"
#include "../app/system_events.h"
#include "../utils/logger/logger.h"

OverlayManager& OverlayManager::instance() {
    static OverlayManager mgr;
    return mgr;
}

void OverlayManager::init(const OverlayCtx& ctx) {
    _ctx = ctx;
    _build();
}

void OverlayManager::_build() {
    // Full-screen panel on the top layer (above the page tileview).
    _panel = lv_obj_create(lv_layer_top());
    lv_obj_set_size(_panel, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(_panel, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_panel, 0, 0);
    lv_obj_set_style_pad_all(_panel, 6, 0);
    lv_obj_clear_flag(_panel, LV_OBJ_FLAG_SCROLLABLE);

    _lb_title = lv_label_create(_panel);
    lv_obj_set_style_text_font(_lb_title, &lv_font_montserrat_20, 0);
    lv_label_set_text(_lb_title, "");
    lv_obj_align(_lb_title, LV_ALIGN_TOP_MID, 0, 8);

    _lb_body = lv_label_create(_panel);
    lv_label_set_long_mode(_lb_body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(_lb_body, LV_PCT(92));
    lv_obj_set_style_text_font(_lb_body, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_lb_body, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(_lb_body, "");
    lv_obj_align(_lb_body, LV_ALIGN_CENTER, 0, 8);

    lv_obj_add_flag(_panel, LV_OBJ_FLAG_HIDDEN);
    _visible = false;
}

void OverlayManager::_show(uint32_t accent, const char* title, const String& body) {
    if (!_panel) return;
    lv_obj_set_style_bg_color(_panel, lv_color_hex(0x000000), 0);  // normal dark bg
    lv_obj_set_style_text_color(_lb_title, lv_color_hex(accent), 0);
    lv_label_set_text(_lb_title, title);
    lv_label_set_text(_lb_body, body.c_str());
    if (!_visible) {
        lv_obj_clear_flag(_panel, LV_OBJ_FLAG_HIDDEN);
        _visible = true;
    }
}

void OverlayManager::_hide() {
    if (!_panel || !_visible) return;
    lv_obj_add_flag(_panel, LV_OBJ_FLAG_HIDDEN);
    _visible = false;
}

void OverlayManager::update() {
    uint32_t now = millis();
    if (now - _last_ms < 250) return;   // self-throttle (~4 Hz)
    _last_ms = now;
    if (!_panel) return;

    uint32_t bits = _ctx.sys_evt ? xEventGroupGetBits(_ctx.sys_evt) : 0;

    // ── Priority 0: find-me (blink the whole screen ~6 s to locate device) ──
    if (bits & SYS_EVENT_FIND_NEIGHBOR_TRIGGERED) {
        if (_find_start == 0) _find_start = now;
        if (now - _find_start < 6000) {
            bool on = ((now / 250) & 1);
            lv_obj_set_style_bg_color(_panel, lv_color_hex(on ? 0xFFFFFF : 0x000000), 0);
            lv_obj_set_style_text_color(_lb_title, lv_color_hex(on ? 0x000000 : 0xFFFFFF), 0);
            lv_obj_set_style_text_color(_lb_body,  lv_color_hex(on ? 0x000000 : 0xFFFFFF), 0);
            lv_label_set_text(_lb_title, "FIND ME");
            lv_label_set_text(_lb_body, "Locating this miner...");
            if (!_visible) { lv_obj_clear_flag(_panel, LV_OBJ_FLAG_HIDDEN); _visible = true; }
            return;
        }
        _find_start = 0;
        lv_obj_set_style_text_color(_lb_body, lv_color_hex(0xFFFFFF), 0);  // restore body color
        if (_ctx.sys_evt) xEventGroupClearBits(_ctx.sys_evt, SYS_EVENT_FIND_NEIGHBOR_TRIGGERED);
    } else {
        _find_start = 0;
    }

    // ── Priority 1: power faults ──
    if (bits & SYS_EVENT_POWER_OT_FAULT) {
        _show(0xFF5252, "OVER-TEMP", "ASIC/VRM over-temperature.\nMining throttled for safety.");
        return;
    }
    if (bits & SYS_EVENT_POWER_OC_FAULT) {
        _show(0xFF5252, "OVER-CURRENT", "Power overcurrent fault.\nCheck PSU / load.");
        return;
    }

    // ── Priority 1.5: celebrations (cleared by a button press) ──
    if (bits & SYS_EVENT_MINER_BLOCK_HIT) {
        _show(0xFFD700, "BLOCK FOUND!", "Congratulations!\nYou solved a block!");
        return;
    }
    if (bits & SYS_EVENT_MINER_HIGH_DIFF_ACHIEVED) {
        String body = "New best difficulty!";
        if (_ctx.status) body += String("\nBest: ") + String(_ctx.status->diff.best_ever, 0);
        _show(0x00E5FF, "NEW BEST!", body);
        return;
    }

    // ── Priority 2: benchmark sweep in progress ──
    if (_ctx.bm_mode && *_ctx.bm_mode && _ctx.bm) {
        const BenchmarkState& b = *_ctx.bm;
        String body;
        body  = String("Freq ") + b.cur_freq + " MHz  Vcore " + b.cur_vcore + " mV\n";
        body += String(b.in_stab ? "Stabilizing" : "Sampling");
        if (b.phase_total) body += String("  ") + b.phase_elapsed + "/" + b.phase_total + "s";
        body += "\n";
        body += String("HR ") + String(b.avg_hr_ghs, 0) + "/" + String(b.exp_hr_ghs, 0) + " GH/s\n";
        body += String("ASIC ") + String(b.asic_temp, 0) + "C  VRM " + String(b.vcore_temp, 0) + "C";
        if (b.eta_sec) body += String("\nETA ") + (b.eta_sec / 60) + "m" + (b.eta_sec % 60) + "s";
        _show(0xEE7D30, "BENCHMARK", body);
        return;
    }

    // ── Priority 3: mining paused / error ──
    if (_ctx.status) {
        MinerRuntimeState st = _ctx.status->runtime_state;
        bool paused_like = _ctx.status->user_paused ||
                           st == MINER_RUNTIME_PAUSING || st == MINER_RUNTIME_PAUSED ||
                           st == MINER_RUNTIME_RESUMING || st == MINER_RUNTIME_ERROR;
        if (paused_like) {
            String body = String("State: ") + miner_runtime_state_to_string(st);
            if (_ctx.status->user_paused) body += "\nPaused by user";
            _show(0xFFC107, "MINING PAUSED", body);
            return;
        }
    }

    _hide();
}
