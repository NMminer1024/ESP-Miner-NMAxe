#include "overlay_manager.h"
#include "ui_manager.h"
#include "../app/application.h"
#include "../app/system_events.h"
#include "../utils/logger/logger.h"
#include "../nvs/nvs_config.h"
#include "../utils/reboot_log/reboot_log.h"
#include "assets/fonts.h"
#include <SPIFFS.h>
#include <cstring>

namespace {
constexpr lv_opa_t kOverlayBgOpa = LV_OPA_90;
}

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
    lv_obj_set_style_bg_opa(_panel, kOverlayBgOpa, 0);
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

    _lb_aux = lv_label_create(_panel);
    lv_obj_set_style_text_color(_lb_aux, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(_lb_aux, "");
    lv_obj_add_flag(_lb_aux, LV_OBJ_FLAG_HIDDEN);

    _lb_aux2 = lv_label_create(_panel);
    lv_obj_set_style_text_color(_lb_aux2, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(_lb_aux2, "");
    lv_obj_add_flag(_lb_aux2, LV_OBJ_FLAG_HIDDEN);

    _bar = lv_bar_create(_panel);
    lv_bar_set_range(_bar, 0, 100);
    lv_obj_add_flag(_bar, LV_OBJ_FLAG_HIDDEN);

    lv_obj_add_flag(_panel, LV_OBJ_FLAG_HIDDEN);
    _visible = false;
}

void OverlayManager::_reset_layout() {
    if (!_panel) return;
    lv_obj_set_style_bg_color(_panel, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(_panel, kOverlayBgOpa, 0);
    lv_obj_set_style_pad_all(_panel, 6, 0);

    if (_lb_title) {
        lv_obj_clear_flag(_lb_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_font(_lb_title, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(_lb_title, lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(_lb_title, LV_ALIGN_TOP_MID, 0, 8);
    }
    if (_lb_body) {
        lv_obj_clear_flag(_lb_body, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_width(_lb_body, LV_PCT(92));
        lv_obj_set_style_text_font(_lb_body, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(_lb_body, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_align(_lb_body, LV_TEXT_ALIGN_LEFT, 0);
        lv_label_set_long_mode(_lb_body, LV_LABEL_LONG_WRAP);
        lv_obj_align(_lb_body, LV_ALIGN_CENTER, 0, 8);
    }
    if (_lb_aux) {
        lv_obj_set_width(_lb_aux, LV_SIZE_CONTENT);
        lv_obj_set_style_text_align(_lb_aux, LV_TEXT_ALIGN_LEFT, 0);
        lv_label_set_long_mode(_lb_aux, LV_LABEL_LONG_WRAP);
        lv_label_set_text(_lb_aux, "");
        lv_obj_add_flag(_lb_aux, LV_OBJ_FLAG_HIDDEN);
    }
    if (_lb_aux2) {
        lv_obj_set_width(_lb_aux2, LV_SIZE_CONTENT);
        lv_obj_set_style_text_align(_lb_aux2, LV_TEXT_ALIGN_LEFT, 0);
        lv_label_set_long_mode(_lb_aux2, LV_LABEL_LONG_WRAP);
        lv_label_set_text(_lb_aux2, "");
        lv_obj_add_flag(_lb_aux2, LV_OBJ_FLAG_HIDDEN);
    }
    if (_bar) {
        lv_bar_set_value(_bar, 0, LV_ANIM_OFF);
        lv_obj_add_flag(_bar, LV_OBJ_FLAG_HIDDEN);
    }
    if (_bm_ip) {
        lv_label_set_text(_bm_ip, "");
        lv_obj_add_flag(_bm_ip, LV_OBJ_FLAG_HIDDEN);
    }
    for (lv_obj_t*& row : _bm_rows) {
        if (row) {
            lv_label_set_text(row, "");
            lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void OverlayManager::_gif_hide() {
    if (_gif) { lv_obj_del(_gif); _gif = nullptr; }
    _gif_shown = false;
}

void OverlayManager::_show_footer_ip(lv_coord_t y, bool large_font) {
    if (!_panel) return;
    if (_bm_ip == nullptr) {
        _bm_ip = lv_label_create(_panel);
    }

    const MinerApp& app = MinerApp::instance();
    WifiState* wifi = app.wifi();
    String ip_text = (wifi && wifi->ip != IPAddress(0, 0, 0, 0))
        ? wifi->ip.toString()
        : String();

    if (ip_text.isEmpty()) {
        lv_label_set_text(_bm_ip, "");
        lv_obj_add_flag(_bm_ip, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_clear_flag(_bm_ip, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_width(_bm_ip, LV_HOR_RES);
    lv_obj_set_style_text_font(_bm_ip, large_font ? &Inconsolata_22 : (LV_VER_RES <= 135 ? &Inconsolata_14 : &Inconsolata_16), 0);
    lv_obj_set_style_text_color(_bm_ip, lv_color_hex(0x4ADE80), 0);
    lv_obj_set_style_text_align(_bm_ip, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(_bm_ip, LV_LABEL_LONG_CLIP);
    lv_label_set_text(_bm_ip, ip_text.c_str());
    lv_obj_set_pos(_bm_ip, 0, y);
}

void OverlayManager::_show(uint32_t accent, const char* title, const String& body) {
    if (!_panel) return;
    _reset_layout();
    _gif_hide();                                                    // no GIF for non-screensaver overlays
    if (_btn_yes) { lv_obj_add_flag(_btn_yes, LV_OBJ_FLAG_HIDDEN); }
    if (_btn_no)  { lv_obj_add_flag(_btn_no,  LV_OBJ_FLAG_HIDDEN); }
    _fault_event = 0;
    lv_obj_set_style_bg_color(_panel, lv_color_hex(0x000000), 0);  // normal dark bg
    lv_obj_set_style_bg_opa(_panel, kOverlayBgOpa, 0);
    lv_obj_align(_lb_body, LV_ALIGN_CENTER, 0, 8);
    lv_obj_set_style_text_color(_lb_body, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_color(_lb_title, lv_color_hex(accent), 0);
    lv_label_set_text(_lb_title, title);
    lv_label_set_text(_lb_body, body.c_str());
    _show_footer_ip(LV_VER_RES <= 135 ? 114 : 212);
    if (!_visible) {
        lv_obj_clear_flag(_panel, LV_OBJ_FLAG_HIDDEN);
        _visible = true;
    }
}

void OverlayManager::_show_factory_overlay(int countdown_sec) {
    if (!_panel) return;
    const bool is_small = LV_VER_RES <= 135;
    const lv_coord_t reminder_y = is_small ? 6 : 10;
    const lv_coord_t countdown_y = is_small ? 16 : 0;
    _reset_layout();
    _gif_hide();
    if (_btn_yes) { lv_obj_add_flag(_btn_yes, LV_OBJ_FLAG_HIDDEN); }
    if (_btn_no)  { lv_obj_add_flag(_btn_no,  LV_OBJ_FLAG_HIDDEN); }
    _fault_event = 0;

    lv_obj_set_style_bg_opa(_panel, kOverlayBgOpa, 0);
    lv_obj_add_flag(_lb_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(_lb_body, LV_OBJ_FLAG_HIDDEN);

    lv_obj_clear_flag(_lb_aux, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_font(_lb_aux, is_small ? &ds_digib_font_56 : &ds_digib_font_120, 0);
    lv_obj_set_style_text_color(_lb_aux, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text_fmt(_lb_aux, "%d", countdown_sec);
    lv_obj_align(_lb_aux, LV_ALIGN_CENTER, 0, countdown_y);

    lv_obj_clear_flag(_lb_aux2, LV_OBJ_FLAG_HIDDEN);
    const char* reminder = is_small ? "Recover to\nfactory settings" : "Recover to factory settings...";
    lv_obj_set_width(_lb_aux2, is_small ? (LV_HOR_RES - 16) : LV_HOR_RES);
    lv_obj_set_style_text_font(_lb_aux2, is_small ? &lv_font_montserrat_16 : &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(_lb_aux2, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(_lb_aux2, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(_lb_aux2, is_small ? LV_LABEL_LONG_WRAP : LV_LABEL_LONG_SCROLL);
    lv_label_set_text(_lb_aux2, reminder);
    lv_obj_align(_lb_aux2, LV_ALIGN_TOP_MID, 0, reminder_y);
    _show_footer_ip(is_small ? 114 : 212);

    if (!_visible) {
        lv_obj_clear_flag(_panel, LV_OBJ_FLAG_HIDDEN);
        _visible = true;
    }
}

void OverlayManager::_show_setup_overlay(int countdown_sec) {
    if (!_panel) return;
    const bool is_small = LV_VER_RES <= 135;
    const lv_coord_t reminder_y = is_small ? 6 : 10;
    const lv_coord_t countdown_y = is_small ? 16 : 0;
    _reset_layout();
    _gif_hide();
    if (_btn_yes) { lv_obj_add_flag(_btn_yes, LV_OBJ_FLAG_HIDDEN); }
    if (_btn_no)  { lv_obj_add_flag(_btn_no,  LV_OBJ_FLAG_HIDDEN); }
    _fault_event = 0;

    lv_obj_set_style_bg_opa(_panel, kOverlayBgOpa, 0);
    lv_obj_add_flag(_lb_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(_lb_body, LV_OBJ_FLAG_HIDDEN);

    lv_obj_clear_flag(_lb_aux, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_font(_lb_aux, is_small ? &ds_digib_font_56 : &ds_digib_font_120, 0);
    lv_obj_set_style_text_color(_lb_aux, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text_fmt(_lb_aux, "%d", countdown_sec);
    lv_obj_align(_lb_aux, LV_ALIGN_CENTER, 0, countdown_y);

    lv_obj_clear_flag(_lb_aux2, LV_OBJ_FLAG_HIDDEN);
    const char* reminder = is_small ? "Reboot to\nsetup mode" : "Reboot to setup mode...";
    lv_obj_set_width(_lb_aux2, is_small ? (LV_HOR_RES - 16) : LV_HOR_RES);
    lv_obj_set_style_text_font(_lb_aux2, is_small ? &lv_font_montserrat_16 : &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(_lb_aux2, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(_lb_aux2, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(_lb_aux2, is_small ? LV_LABEL_LONG_WRAP : LV_LABEL_LONG_SCROLL);
    lv_label_set_text(_lb_aux2, reminder);
    lv_obj_align(_lb_aux2, LV_ALIGN_TOP_MID, 0, reminder_y);
    _show_footer_ip(is_small ? 114 : 212);

    if (!_visible) {
        lv_obj_clear_flag(_panel, LV_OBJ_FLAG_HIDDEN);
        _visible = true;
    }
}

void OverlayManager::_show_rebooting_overlay(const char* title) {
    if (!_panel) return;
    const bool is_small = LV_VER_RES <= 135;
    const lv_coord_t title_y = is_small ? -16 : -16;
    const lv_coord_t reboot_y = is_small ? 20 : 20;
    _reset_layout();
    _gif_hide();
    if (_btn_yes) { lv_obj_add_flag(_btn_yes, LV_OBJ_FLAG_HIDDEN); }
    if (_btn_no)  { lv_obj_add_flag(_btn_no,  LV_OBJ_FLAG_HIDDEN); }
    _fault_event = 0;

    lv_obj_set_style_bg_opa(_panel, kOverlayBgOpa, 0);
    lv_obj_add_flag(_lb_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(_lb_body, LV_OBJ_FLAG_HIDDEN);

    lv_obj_clear_flag(_lb_aux, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_width(_lb_aux, LV_HOR_RES - (is_small ? 20 : 28));
    lv_obj_set_style_text_font(_lb_aux, is_small ? &lv_font_montserrat_16 : &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(_lb_aux, lv_color_hex(is_small ? 0xD8D8D8 : 0xE8E8E8), 0);
    lv_obj_set_style_text_align(_lb_aux, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(_lb_aux, title);
    lv_label_set_long_mode(_lb_aux, LV_LABEL_LONG_WRAP);
    lv_obj_align(_lb_aux, LV_ALIGN_CENTER, 0, title_y);

    lv_obj_clear_flag(_lb_aux2, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_width(_lb_aux2, LV_HOR_RES - (is_small ? 20 : 28));
    lv_obj_set_style_text_font(_lb_aux2, is_small ? &lv_font_montserrat_16 : &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(_lb_aux2, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(_lb_aux2, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(_lb_aux2, "rebooting...");
    lv_label_set_long_mode(_lb_aux2, LV_LABEL_LONG_WRAP);
    lv_obj_align(_lb_aux2, LV_ALIGN_CENTER, 0, reboot_y);
    _show_footer_ip(is_small ? 114 : 212);

    if (!_visible) {
        lv_obj_clear_flag(_panel, LV_OBJ_FLAG_HIDDEN);
        _visible = true;
    }
}

void OverlayManager::_show_benchmark_overlay() {
    if (!_panel || !_ctx.bm || !_ctx.status) return;

    const BenchmarkState& b = *_ctx.bm;
    const bool is_large = LV_HOR_RES >= 300;
    const MinerApp& app = MinerApp::instance();
    const uint32_t now = millis();
    const bool first_build = !_visible || lv_obj_has_flag(_lb_title, LV_OBJ_FLAG_HIDDEN) ||
                             std::strcmp(lv_label_get_text(_lb_title), "[ BENCHMARK ]") != 0;
    static uint32_t last_bm_ms = 0;
    if (_visible && last_bm_ms != 0 && (now - last_bm_ms < 1000)) {
        return;
    }
    last_bm_ms = now;

    if (first_build) {
        _reset_layout();
        _gif_hide();
        if (_btn_yes) { lv_obj_add_flag(_btn_yes, LV_OBJ_FLAG_HIDDEN); }
        if (_btn_no)  { lv_obj_add_flag(_btn_no,  LV_OBJ_FLAG_HIDDEN); }
        _fault_event = 0;

        lv_obj_set_style_bg_color(_panel, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(_panel, kOverlayBgOpa, 0);
        lv_obj_set_style_pad_all(_panel, 0, 0);

        lv_obj_clear_flag(_lb_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_font(_lb_title, is_large ? &Inconsolata_22 : &Inconsolata_16, 0);
        lv_obj_set_style_text_color(_lb_title, lv_color_hex(0x22D3EE), 0);
        lv_obj_set_width(_lb_title, LV_HOR_RES);
        lv_obj_set_style_text_align(_lb_title, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(_lb_title, 0, is_large ? 4 : 2);
        lv_label_set_text(_lb_title, "[ BENCHMARK ]");

        lv_obj_add_flag(_lb_body, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(_lb_aux, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(_lb_aux2, LV_OBJ_FLAG_HIDDEN);

        if (_bm_ip == nullptr) {
            _bm_ip = lv_label_create(_panel);
        }
        lv_obj_clear_flag(_bm_ip, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_font(_bm_ip, is_large ? &Inconsolata_22 : &Inconsolata_16, 0);
        lv_obj_set_style_text_color(_bm_ip, lv_color_hex(0x4ADE80), 0);
        lv_obj_set_width(_bm_ip, LV_HOR_RES);
        lv_obj_set_style_text_align(_bm_ip, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(_bm_ip, 0, is_large ? 186 : 97);

        const int row_count = is_large ? 7 : 4;
        const int row_y_large[7] = {28, 50, 72, 94, 116, 138, 160};
        const int row_y_small[4] = {21, 40, 59, 78};
        for (int i = 0; i < row_count; i++) {
            if (_bm_rows[i] == nullptr) {
                _bm_rows[i] = lv_label_create(_panel);
            }
            lv_obj_clear_flag(_bm_rows[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_text_font(_bm_rows[i], is_large ? &Inconsolata_18 : &Inconsolata_16, 0);
            lv_obj_set_style_text_color(_bm_rows[i], lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_pos(_bm_rows[i], is_large ? 8 : 4, is_large ? row_y_large[i] : row_y_small[i]);
        }
        for (int i = row_count; i < 7; i++) {
            if (_bm_rows[i]) lv_obj_add_flag(_bm_rows[i], LV_OBJ_FLAG_HIDDEN);
        }

        lv_obj_clear_flag(_bar, LV_OBJ_FLAG_HIDDEN);
        lv_bar_set_range(_bar, 0, 100);
        if (is_large) {
            lv_obj_set_size(_bar, (lv_coord_t)(LV_HOR_RES - 24), 8);
            lv_obj_set_pos(_bar, 12, 218);
        } else {
            lv_obj_set_size(_bar, (lv_coord_t)(LV_HOR_RES - 16), 8);
            lv_obj_set_pos(_bar, 8, 118);
        }
    }

    auto fmt_time = [](char* buf, size_t len, uint32_t s) {
        if      (s >= 3600) snprintf(buf, len, "%uh%02um", (unsigned)(s / 3600), (unsigned)((s % 3600) / 60));
        else if (s >= 60)   snprintf(buf, len, "%um%02us", (unsigned)(s / 60),   (unsigned)(s % 60));
        else                snprintf(buf, len, "%us",       (unsigned)s);
    };

    uint16_t freq_total = (b.freq_step > 0) ? ((b.freq_max - b.freq_min) / b.freq_step + 1) : 1;
    uint16_t freq_idx   = (b.freq_step > 0) ? ((b.cur_freq  - b.freq_min) / b.freq_step + 1) : 1;
    uint16_t vc_total   = (b.vcore_step > 0) ? ((b.vcore_max - b.vcore_min) / b.vcore_step + 1) : 1;
    uint16_t vc_idx     = (b.vcore_step > 0) ? ((b.cur_vcore - b.vcore_min) / b.vcore_step + 1) : 1;
    uint32_t phase_remaining = (b.phase_total > b.phase_elapsed) ? (b.phase_total - b.phase_elapsed) : 0;
    char eta_str[16];
    fmt_time(eta_str, sizeof(eta_str), b.eta_sec);
    int pct = (b.phase_total > 0) ? (int)(100u * b.phase_elapsed / b.phase_total) : 0;
    if (pct > 100) pct = 100;
    const PowerTelemetry& pwr = app.pwr_tele();
    float pwr_w = (float)pwr.vbus / 1000.0f * (float)pwr.ibus / 1000.0f;

    char buf[64];
    if (is_large) {
        snprintf(buf, sizeof(buf), "  F/V/P  : %uMHz/%umV/%.1fW", b.cur_freq, b.cur_vcore, pwr_w);
        lv_label_set_text(_bm_rows[0], buf);

        snprintf(buf, sizeof(buf), "  Temp   : %.0fC / %.0fC", b.vcore_temp, b.asic_temp);
        lv_label_set_text(_bm_rows[1], buf);

        snprintf(buf, sizeof(buf), "  Phase  : %s", b.in_stab ? "Warmup    " : "Sampling ");
        lv_label_set_text(_bm_rows[2], buf);
        lv_obj_set_style_text_color(_bm_rows[2], b.in_stab ? lv_color_hex(0xFACC15) : lv_color_hex(0x60A5FA), 0);

        snprintf(buf, sizeof(buf), "  Left   : %lus", (unsigned long)phase_remaining);
        lv_label_set_text(_bm_rows[3], buf);

        if (b.in_stab) snprintf(buf, sizeof(buf), "  Avg/Exp: (Stab) / %.0f GH/s", b.exp_hr_ghs);
        else           snprintf(buf, sizeof(buf), "  Avg/Exp: %.0f / %.0f GH/s", b.avg_hr_ghs, b.exp_hr_ghs);
        lv_label_set_text(_bm_rows[4], buf);
        lv_obj_set_style_text_color(_bm_rows[4], lv_color_hex(0x4ADE80), 0);

        snprintf(buf, sizeof(buf), "  Round  : F%u/%u  V%u/%u", freq_idx, freq_total, vc_idx, vc_total);
        lv_label_set_text(_bm_rows[5], buf);
        lv_obj_set_style_text_color(_bm_rows[5], lv_color_hex(0xAAAAAA), 0);

        snprintf(buf, sizeof(buf), "  ETA    : ~%s (max)", eta_str);
        lv_label_set_text(_bm_rows[6], buf);
        lv_obj_set_style_text_color(_bm_rows[6], lv_color_hex(0xAAAAAA), 0);
    } else {
        snprintf(buf, sizeof(buf), "  F/V/P: %uM/%umV/%.0fW", b.cur_freq, b.cur_vcore, pwr_w);
        lv_label_set_text(_bm_rows[0], buf);

        snprintf(buf, sizeof(buf), "  Temp : %.0fC / %.0fC", b.asic_temp, b.vcore_temp);
        lv_label_set_text(_bm_rows[1], buf);

        if (b.in_stab) snprintf(buf, sizeof(buf), "  Avg  : --/%.0f GH/s", b.exp_hr_ghs);
        else           snprintf(buf, sizeof(buf), "  Avg  : %.0f/%.0f GH/s", b.avg_hr_ghs, b.exp_hr_ghs);
        lv_label_set_text(_bm_rows[2], buf);

        snprintf(buf, sizeof(buf), "  Round: F%u/%u V%u/%u ~%s", freq_idx, freq_total, vc_idx, vc_total, eta_str);
        lv_label_set_text(_bm_rows[3], buf);
    }

    _show_footer_ip(is_large ? 186 : 97, is_large);
    lv_bar_set_value(_bar, pct, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(_bar, b.in_stab ? lv_color_hex(0xFACC15) : lv_color_hex(0x60A5FA), LV_PART_INDICATOR);

    if (!_visible) {
        lv_obj_clear_flag(_panel, LV_OBJ_FLAG_HIDDEN);
        _visible = true;
    }
}

void OverlayManager::_show_ota_overlay(uint32_t now) {
    if (!_panel || !_ctx.ota) return;
    const bool is_small = LV_VER_RES <= 135;

    if (!_ctx.ota->running) {
        if (_ota_overlay_active && _ctx.ota->progress >= 100) {
            _ota_overlay_active = false;
            _ota_dismiss_at = 0;
            _ota_last_update = 0;
            _ota_rebooting = true;
        } else if (_ota_dismiss_at != 0 && now >= _ota_dismiss_at) {
            _ota_dismiss_at = 0;
            _ota_last_update = 0;
            _ota_overlay_active = false;
            _ota_rebooting = false;
            _hide();
        }
        return;
    }

    const bool is_complete = (_ctx.ota->progress >= 100);
    if (!is_complete && _ota_last_update != 0 && (now - _ota_last_update) < 1000) {
        return;
    }

    if (!_ota_overlay_active) {
        _reset_layout();
        _gif_hide();
        if (_btn_yes) { lv_obj_add_flag(_btn_yes, LV_OBJ_FLAG_HIDDEN); }
        if (_btn_no)  { lv_obj_add_flag(_btn_no,  LV_OBJ_FLAG_HIDDEN); }
        _fault_event = 0;
        _ota_dismiss_at = 0;

        lv_obj_set_style_bg_opa(_panel, kOverlayBgOpa, 0);
        lv_obj_set_style_pad_all(_panel, 0, 0);

        lv_obj_clear_flag(_lb_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_font(_lb_title, is_small ? &lv_font_montserrat_14 : &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(_lb_title, lv_color_hex(0xFFFFFF), 0); 
        lv_obj_align(_lb_title, LV_ALIGN_CENTER, 0, is_small ? -34 : -30);

        lv_obj_add_flag(_lb_body, LV_OBJ_FLAG_HIDDEN);

        lv_obj_clear_flag(_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(_bar, (lv_coord_t)(LV_HOR_RES * 0.81f), 5);
        lv_obj_align(_bar, LV_ALIGN_CENTER, 0, is_small ? -2 : 0);

        lv_obj_clear_flag(_lb_aux, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_width(_lb_aux, LV_SIZE_CONTENT);
        lv_obj_set_style_text_font(_lb_aux, is_small ? &lv_font_montserrat_12 : &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(_lb_aux, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_pos(_lb_aux, 0, 10);

        lv_obj_clear_flag(_lb_aux2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_width(_lb_aux2, is_small ? LV_PCT(88) : LV_PCT(92));
        lv_obj_set_style_text_font(_lb_aux2, is_small ? &lv_font_montserrat_12 : &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(_lb_aux2, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_align(_lb_aux2, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(_lb_aux2, LV_LABEL_LONG_WRAP);
        lv_label_set_text(_lb_aux2, "Do not power off during update.");
        lv_obj_align(_lb_aux2, LV_ALIGN_CENTER, 0, is_small ? 38 : 28);
        _show_footer_ip(is_small ? 114 : 212);

        if (!_visible) {
            lv_obj_clear_flag(_panel, LV_OBJ_FLAG_HIDDEN);
            _visible = true;
        }
        _ota_overlay_active = true;
        _ota_rebooting = false;
    }

    lv_label_set_text(_lb_title, _ctx.ota->filename.c_str());
    lv_bar_set_value(_bar, _ctx.ota->progress, is_complete ? LV_ANIM_OFF : LV_ANIM_OFF);
    lv_obj_clear_flag(_lb_aux, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text_fmt(_lb_aux, "%d%%", _ctx.ota->progress);
    lv_obj_update_layout(_lb_aux);

    lv_coord_t bar_x = lv_obj_get_x(_bar);
    lv_coord_t bar_y = lv_obj_get_y(_bar);
    lv_coord_t bar_w = lv_obj_get_width(_bar);
    lv_coord_t label_w = lv_obj_get_width(_lb_aux);
    lv_coord_t label_x = bar_x + (lv_coord_t)(bar_w * _ctx.ota->progress / 100.0f) - label_w / 2;
    lv_coord_t min_x = bar_x;
    lv_coord_t max_x = bar_x + bar_w - label_w;
    if (label_x < min_x) label_x = min_x;
    if (label_x > max_x) label_x = max_x;
    lv_obj_set_pos(_lb_aux, label_x, bar_y + (is_small ? 8 : 10));
    lv_obj_move_foreground(_lb_aux);
    _show_footer_ip(is_small ? 114 : 212);

    _ota_last_update = now;
}

void OverlayManager::_hide() {
    _gif_hide();
    if (!_panel || !_visible) return;
    _fault_event = 0;
    _ota_dismiss_at = 0;
    _ota_overlay_active = false;
    _ota_rebooting = false;
    lv_obj_add_flag(_panel, LV_OBJ_FLAG_HIDDEN);
    _visible = false;
}

void OverlayManager::_show_fault_overlay(bool is_oc) {
    if (!_panel) return;
    _reset_layout();
    _gif_hide();
    _fault_event = is_oc ? SYS_EVENT_POWER_OC_FAULT : SYS_EVENT_POWER_OT_FAULT;

    lv_obj_set_style_bg_color(_panel, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(_panel, kOverlayBgOpa, 0);
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
    _show_footer_ip(LV_VER_RES <= 135 ? 114 : 212);

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
    if (UIManager::instance().setup_rebooting()) {
        _show_rebooting_overlay("Entering setup mode");
        return;
    }

    if (UIManager::instance().factory_rebooting()) {
        _show_rebooting_overlay("Recovering factory settings");
        return;
    }

    int scd = UIManager::instance().setup_countdown();
    if (scd >= 0) {
        _show_setup_overlay(scd);
        return;
    }

    int fcd = UIManager::instance().factory_countdown();
    if (fcd >= 0) {
        _show_factory_overlay(fcd);
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
    if (_ota_rebooting) {
        _show_rebooting_overlay("OTA update complete");
        return;
    }

    if (_ctx.ota && (_ctx.ota->running || _ota_dismiss_at != 0 || (_ota_overlay_active && _ctx.ota->progress >= 100))) {
        _show_ota_overlay(now);
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
    if (_ctx.bm && _ctx.bm->active) {
        size_t cur = UIManager::instance().current();
        if (cur >= (size_t)UIPageId::MINER && cur <= (size_t)UIPageId::SETTING_SWARM) {
            _show_benchmark_overlay();
            return;
        }
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
        lv_obj_set_style_bg_opa(_panel, kOverlayBgOpa, 0);
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
