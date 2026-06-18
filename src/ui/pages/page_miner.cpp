#include "ui/pages/page_miner.h"
#include "app/application.h"
#include "board/board.h"
#include "ui/assets/fonts.h"

void PageMinerBase::destroy() {
    auto& m = AppState::instance().miner;
    m.hashrate.unsubscribe(_on_text);
    m.hashrate_unit.unsubscribe(_on_text);
    m.blk_hit.unsubscribe(_on_text);
    m.price.unsubscribe(_on_text);
    m.ver.unsubscribe(_on_text);
    m.power.unsubscribe(_on_text);
    m.ip.unsubscribe(_on_text);
    m.uptime_hms.unsubscribe(_on_text);
    m.uptime_day.unsubscribe(_on_text);
    m.diff.unsubscribe(_on_text);
    m.shares.unsubscribe(_on_text);
    m.temp.unsubscribe(_on_text);
    m.fan.unsubscribe(_on_text);
    m.utc_time.unsubscribe(_on_text);
    m.diff_symbol_color.unsubscribe(_on_color);
    m.share_symbol_color.unsubscribe(_on_color);
    m.temp_symbol_color.unsubscribe(_on_color);
    m.fan_symbol_color.unsubscribe(_on_color);
    m.wifi_color.unsubscribe(_on_color);
    m.swarm_bd.unsubscribe(_on_text);
    m.swarm_hr.unsubscribe(_on_text);
    m.swarm_workers.unsubscribe(_on_text);

    _lb_hashrate = _lb_hr_unit = _lb_blk_hit = _lb_price = _lb_ver = nullptr;
    _lb_power = _lb_ip = _lb_uptime_hms = _lb_uptime_day = nullptr;
    _lb_diff = _lb_share = _lb_temp = _lb_fan = _lb_utc_time = _lb_wifi_symb = nullptr;
    _lb_diff_symb = _lb_share_symb = _lb_temp_symb = _lb_fan_symb = nullptr;
    _lb_swarm_bd = _lb_swarm_hr = _lb_swarm_wk = nullptr;
}

void PageMinerBase::_finish_create() {
    auto& m = AppState::instance().miner;
    m.hashrate.subscribe(_on_text, &_lb_hashrate);
    m.hashrate_unit.subscribe(_on_text, &_lb_hr_unit);
    m.blk_hit.subscribe(_on_text, &_lb_blk_hit);
    m.price.subscribe(_on_text, &_lb_price);
    m.ver.subscribe(_on_text, &_lb_ver);
    m.power.subscribe(_on_text, &_lb_power);
    m.ip.subscribe(_on_text, &_lb_ip);
    m.uptime_hms.subscribe(_on_text, &_lb_uptime_hms);
    m.uptime_day.subscribe(_on_text, &_lb_uptime_day);
    m.diff.subscribe(_on_text, &_lb_diff);
    m.shares.subscribe(_on_text, &_lb_share);
    m.temp.subscribe(_on_text, &_lb_temp);
    m.fan.subscribe(_on_text, &_lb_fan);
    m.utc_time.subscribe(_on_text, &_lb_utc_time);
    m.diff_symbol_color.subscribe(_on_color, &_lb_diff_symb);
    m.share_symbol_color.subscribe(_on_color, &_lb_share_symb);
    m.temp_symbol_color.subscribe(_on_color, &_lb_temp_symb);
    m.fan_symbol_color.subscribe(_on_color, &_lb_fan_symb);
    m.wifi_color.subscribe(_on_color, &_lb_wifi_symb);
    m.swarm_bd.subscribe(_on_text, &_lb_swarm_bd);
    m.swarm_hr.subscribe(_on_text, &_lb_swarm_hr);
    m.swarm_workers.subscribe(_on_text, &_lb_swarm_wk);
}

void PageMinerBase::_on_update() {
    auto& app = MinerApp::instance();
    auto& m = AppState::instance().miner;

    static uint32_t last_share_cnt = 0;
    static bool last_share_cnt_init = false;
    static bool share_color_toggle = false;
    static uint32_t last_share_toggle_ms = 0;
    static uint32_t share_blink_until_ms = 0;
    static bool diff_color_toggle = false;
    static uint32_t last_diff_toggle_ms = 0;
    static bool fan_color_toggle = false;
    static uint32_t last_fan_toggle_ms = 0;

    const MinerStatus* st = app.status();
    if (!st) {
        return;
    }

    if (st->diff.last != 0.0) {
        uint32_t now = millis();
        if (last_diff_toggle_ms == 0 || now - last_diff_toggle_ms >= 500) {
            diff_color_toggle = !diff_color_toggle;
            last_diff_toggle_ms = now;
        }
        uint32_t diff_color = 0xA9A9A9;
        if (st->diff.last == st->diff.best_session) diff_color = diff_color_toggle ? 0x00FF00 : 0xA9A9A9;
        else if (st->diff.last == st->diff.best_ever) diff_color = diff_color_toggle ? 0xFFA500 : 0xA9A9A9;
        m.diff_symbol_color = diff_color;
    }

    uint32_t temp_color = 0x00FF00;
    if (app.spec().pwr.temp_limit.high <= app.temp().vcore) temp_color = 0xFF0000;
    else if (app.spec().pwr.temp_limit.medium <= app.temp().vcore) temp_color = 0xFFA500;
    m.temp_symbol_color = temp_color;

    uint32_t share_color = 0xA9A9A9;
    if (!last_share_cnt_init) {
        last_share_cnt = st->share_accepted;
        last_share_cnt_init = true;
    } else if (last_share_cnt != st->share_accepted) {
        last_share_cnt = st->share_accepted;
        share_blink_until_ms = millis() + 2000;
        share_color_toggle = false;
        last_share_toggle_ms = 0;
    }
    if (share_blink_until_ms != 0 && (int32_t)(share_blink_until_ms - millis()) > 0) {
        uint32_t now = millis();
        if (last_share_toggle_ms == 0 || now - last_share_toggle_ms >= 500) {
            share_color_toggle = !share_color_toggle;
            last_share_toggle_ms = now;
        }
        share_color = share_color_toggle ? 0x00FF00 : 0xA9A9A9;
    } else {
        share_blink_until_ms = 0;
        share_color_toggle = false;
        share_color = 0xA9A9A9;
    }
    m.share_symbol_color = share_color;

    uint32_t fan_color = 0xA9A9A9;
    if (!app.fan_status().empty() && app.fan_status()[0].rpm > 0) {
        uint32_t now = millis();
        if (last_fan_toggle_ms == 0 || now - last_fan_toggle_ms >= 500) {
            fan_color_toggle = !fan_color_toggle;
            last_fan_toggle_ms = now;
        }
        fan_color = fan_color_toggle ? 0x00FF00 : 0xA9A9A9;
    }
    m.fan_symbol_color = fan_color;

    if (_lb_blk_hit) {
        const bool is_pp = app.spec().name == BOARD_NMQAXE_PLUS_PLUS_NAME;
        if (st->hits <= 9) {
            lv_obj_set_style_text_font(_lb_blk_hit, is_pp ? &ds_digib_font_56 : &ds_digib_font_56, LV_PART_MAIN);
            lv_obj_align(_lb_blk_hit, LV_ALIGN_TOP_MID, is_pp ? 20 : 6, is_pp ? 65 : 36);
        } else if (st->hits <= 99) {
            if (is_pp) {
                lv_obj_set_style_text_font(_lb_blk_hit, &ds_digib_font_38, LV_PART_MAIN);
                lv_obj_align(_lb_blk_hit, LV_ALIGN_TOP_MID, 20, 73);
            } else {
                lv_obj_set_style_text_font(_lb_blk_hit, &ds_digib_font_28, LV_PART_MAIN);
                lv_obj_align(_lb_blk_hit, LV_ALIGN_TOP_MID, 8, 47);
            }
        }
    }
}

void PageMinerBase::_on_text(const String& v, void* ctx) {
    if (!ctx) return;
    lv_obj_t** lbl = static_cast<lv_obj_t**>(ctx);
    if (*lbl) lv_label_set_text(*lbl, v.c_str());
}

void PageMinerBase::_on_color(const uint32_t& v, void* ctx) {
    if (!ctx) return;
    lv_obj_t** lbl = static_cast<lv_obj_t**>(ctx);
    if (*lbl) lv_obj_set_style_text_color(*lbl, lv_color_hex(v), 0);
}
