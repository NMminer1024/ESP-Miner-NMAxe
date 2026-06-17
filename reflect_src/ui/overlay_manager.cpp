#include "overlay_manager.h"
#include "ui_manager.h"
#include "../app/system_events.h"
#include "../utils/logger/logger.h"
#include "../nvs/nvs_config.h"
#include "../utils/reboot_log/reboot_log.h"
#include <SPIFFS.h>

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
    lv_obj_add_event_cb(_panel, [](lv_event_t*) {
        auto& mgr = OverlayManager::instance();
        if (!mgr._ctx.sys_evt) return;
        xEventGroupClearBits(mgr._ctx.sys_evt,
            SYS_EVENT_MINER_BLOCK_HIT |
            SYS_EVENT_MINER_HIGH_DIFF_ACHIEVED |
            SYS_EVENT_SCREEN_SAVER_TRIGGERED |
            SYS_EVENT_FIND_NEIGHBOR_TRIGGERED);
        UIManager::instance().wake_activity();
    }, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(_panel, [](lv_event_t*) {
        UIManager::instance().wake_activity();
    }, LV_EVENT_PRESSING, nullptr);

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

void OverlayManager::_gif_hide() {
    if (_gif) { lv_obj_del(_gif); _gif = nullptr; }
    _gif_shown = false;
}

void OverlayManager::_show(uint32_t accent, const char* title, const String& body) {
    if (!_panel) return;
    _gif_hide();                                                    // no GIF for non-screensaver overlays
    if (_btn_yes) { lv_obj_add_flag(_btn_yes, LV_OBJ_FLAG_HIDDEN); }
    if (_btn_no)  { lv_obj_add_flag(_btn_no,  LV_OBJ_FLAG_HIDDEN); }
    _fault_event = 0;
    lv_obj_set_style_bg_color(_panel, lv_color_hex(0x000000), 0);  // normal dark bg
    lv_obj_align(_lb_body, LV_ALIGN_CENTER, 0, 8);
    lv_obj_set_style_text_color(_lb_body, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_color(_lb_title, lv_color_hex(accent), 0);
    lv_label_set_text(_lb_title, title);
    lv_label_set_text(_lb_body, body.c_str());
    if (!_visible) {
        lv_obj_clear_flag(_panel, LV_OBJ_FLAG_HIDDEN);
        _visible = true;
    }
}

void OverlayManager::_hide() {
    _gif_hide();
    if (!_panel || !_visible) return;
    _fault_event = 0;
    lv_obj_add_flag(_panel, LV_OBJ_FLAG_HIDDEN);
    _visible = false;
}

void OverlayManager::_show_fault_overlay(bool is_oc) {
    if (!_panel) return;
    _gif_hide();
    _fault_event = is_oc ? SYS_EVENT_POWER_OC_FAULT : SYS_EVENT_POWER_OT_FAULT;

    lv_obj_set_style_bg_color(_panel, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(_panel, LV_OPA_90, 0);
    lv_obj_set_style_text_color(_lb_title, lv_color_hex(is_oc ? 0xFF5252 : 0xFF6D00), 0);
    lv_label_set_text(_lb_title, is_oc ? LV_SYMBOL_WARNING " Overcurrent Fault"
                                       : LV_SYMBOL_WARNING " Overtemp Fault");
    lv_label_set_text(_lb_body,
        is_oc ? "Reset freq & voltage to defaults,\nthen reboot?"
              : "ASIC powered down.\nCheck cooling & lower OC settings,\nthen restart.");
    lv_obj_align(_lb_body, LV_ALIGN_CENTER, 0, -8);
    lv_obj_set_style_text_align(_lb_body, LV_TEXT_ALIGN_CENTER, 0);

    if (_btn_yes == nullptr) {
        _btn_yes = lv_btn_create(_panel);
        lv_obj_set_size(_btn_yes, (LV_HOR_RES - 50) / 2, 32);
        lv_obj_align(_btn_yes, LV_ALIGN_BOTTOM_LEFT, 20, -8);
        lv_obj_add_event_cb(_btn_yes, _fault_action_yes_cb, LV_EVENT_CLICKED, this);
        lv_obj_t* lb = lv_label_create(_btn_yes);
        lv_obj_set_style_text_font(lb, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_center(lb);
    }
    if (_btn_no == nullptr) {
        _btn_no = lv_btn_create(_panel);
        lv_obj_set_size(_btn_no, (LV_HOR_RES - 50) / 2, 32);
        lv_obj_align(_btn_no, LV_ALIGN_BOTTOM_RIGHT, -20, -8);
        lv_obj_set_style_bg_color(_btn_no, lv_color_hex(0x424242), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(_btn_no, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(_btn_no, 0, LV_PART_MAIN);
        lv_obj_add_event_cb(_btn_no, _fault_action_no_cb, LV_EVENT_CLICKED, this);
        lv_obj_t* lb = lv_label_create(_btn_no);
        lv_obj_set_style_text_font(lb, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_label_set_text(lb, "Dismiss");
        lv_obj_center(lb);
    }

    lv_obj_set_style_bg_color(_btn_yes, lv_color_hex(is_oc ? 0xD32F2F : 0xFF6D00), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_btn_yes, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(_btn_yes, 0, LV_PART_MAIN);
    lv_label_set_text(lv_obj_get_child(_btn_yes, 0), is_oc ? "Reset & Reboot" : "Restart");
    lv_obj_clear_flag(_btn_yes, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(_btn_no, LV_OBJ_FLAG_HIDDEN);

    if (!_visible) {
        lv_obj_clear_flag(_panel, LV_OBJ_FLAG_HIDDEN);
        _visible = true;
    }
}

void OverlayManager::_fault_action_yes_cb(lv_event_t* e) {
    auto* self = static_cast<OverlayManager*>(lv_event_get_user_data(e));
    if (!self) return;

    bool is_oc = (self->_fault_event == SYS_EVENT_POWER_OC_FAULT);
    if (is_oc && self->_ctx.spec != nullptr) {
        nvs_config_set_u16(NVS_CONFIG_ASIC_FREQ, self->_ctx.spec->asic.default_frq);
        nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, self->_ctx.spec->asic.default_vcore);
        LOG_W("OC alert: reset freq=%u vcore=%u — rebooting...",
              self->_ctx.spec->asic.default_frq, self->_ctx.spec->asic.default_vcore);
    } else {
        LOG_W("OT alert: user confirmed restart");
    }

    reboot_intent_set_if_unset(is_oc ? REBOOT_INTENT_OVERCURRENT_FAULT
                                     : REBOOT_INTENT_OVERHEAT_VCORE,
                               is_oc ? "overlay confirmed reset+reboot after OC fault"
                                     : "overlay confirmed reboot after OT fault");
    if (self->_ctx.reboot_xsem) {
        xSemaphoreGive(self->_ctx.reboot_xsem);
    }
}

void OverlayManager::_fault_action_no_cb(lv_event_t* e) {
    auto* self = static_cast<OverlayManager*>(lv_event_get_user_data(e));
    if (!self || !self->_ctx.sys_evt) return;
    xEventGroupClearBits(self->_ctx.sys_evt, SYS_EVENT_POWER_OC_FAULT | SYS_EVENT_POWER_OT_FAULT);
}

void OverlayManager::update() {
    uint32_t now = millis();
    if (now - _last_ms < 250) return;   // self-throttle (~4 Hz)
    _last_ms = now;
    if (!_panel) return;

    uint32_t bits = _ctx.sys_evt ? xEventGroupGetBits(_ctx.sys_evt) : 0;

    // ── Priority 0: touch long-press factory-reset countdown ──
    int fcd = UIManager::instance().factory_countdown();
    if (fcd >= 0) {
        _gif_hide();
        String body = String("Keep holding to reset:\n") + fcd + " s\nRelease to cancel.";
        _show(0xFF5252, "FACTORY RESET", body);
        return;
    }

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
        _show_fault_overlay(false);
        return;
    }
    if (bits & SYS_EVENT_POWER_OC_FAULT) {
        _show_fault_overlay(true);
        return;
    }

    // ── Priority 1.2: OTA / file upload in progress ──
    if (_ctx.ota && _ctx.ota->running) {
        String body = _ctx.ota->filename;
        if (body.length()) body += "\n";
        body += String(_ctx.ota->progress) + " %";
        body += "\nDo not power off.";
        _show(0x42A5F5, "UPDATING", body);
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

    // ── Priority 4: screensaver (aphorism quote, non-black mode) ──
    if ((bits & SYS_EVENT_SCREEN_SAVER_TRIGGERED) &&
        _ctx.saver_mode && *_ctx.saver_mode != 1) {
        // Rotate to a fresh quote on entry and every ~60 s thereafter.
        if (!_aph_have || (now - _aph_last) > 60000) {
            if (_ctx.aphorism && _ctx.aphorism->mutex &&
                xSemaphoreTake(_ctx.aphorism->mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                if (!_ctx.aphorism->pool.empty()) {
                    _aph_quote  = _ctx.aphorism->pool.front().quote;
                    _aph_author = _ctx.aphorism->pool.front().author;
                    _ctx.aphorism->pool.erase(_ctx.aphorism->pool.begin());
                    _aph_have = true;
                }
                xSemaphoreGive(_ctx.aphorism->mutex);
            }
            _aph_last = now;
        }
        String body = _aph_have ? (String("\"") + _aph_quote + "\"\n\n- " + _aph_author)
                                : String("Solo mining:\nbe a friend of time.");

        // GIF background if a screensaver GIF is present in SPIFFS, else quote-only.
        bool gif_ok = false;
        if (_ctx.gif_path && SPIFFS.exists(_ctx.gif_path)) {
            if (!_gif) _gif = lv_gif_create(_panel);
            if (_gif && !_gif_shown) {
                String src = String("S:") + (_ctx.gif_path + 1);   // strip leading '/'
                lv_gif_set_src(_gif, src.c_str());
                lv_obj_align(_gif, LV_ALIGN_CENTER, 0, 0);
                lv_obj_clear_flag(_gif, LV_OBJ_FLAG_HIDDEN);
                _gif_shown = true;
            }
            gif_ok = (_gif != nullptr);
        } else {
            _gif_hide();
        }

        lv_obj_set_style_bg_color(_panel, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(_panel, LV_OPA_COVER, 0);
        lv_label_set_text(_lb_title, "");
        lv_label_set_text(_lb_body, body.c_str());
        lv_obj_set_style_text_color(_lb_body, lv_color_hex(0x66BB6A), 0);
        lv_obj_align(_lb_body, gif_ok ? LV_ALIGN_BOTTOM_MID : LV_ALIGN_CENTER, 0, gif_ok ? -6 : 8);
        if (_gif) lv_obj_move_background(_gif);   // keep quote text on top
        if (!_visible) { lv_obj_clear_flag(_panel, LV_OBJ_FLAG_HIDDEN); _visible = true; }
        return;
    }
    _aph_have = false;   // reset so the next screensaver entry pops a fresh quote

    _hide();
}
