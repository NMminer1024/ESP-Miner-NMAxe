#include "ui/pages/page_clock.h"

#include "app/application.h"
#include "board/board.h"
#include "utils/helper.h"

void PageClockBase::destroy() {
    _parent = nullptr;
    _lb_time = nullptr;
    _lb_ampm = nullptr;
    _lb_date = nullptr;
    _lb_hr = nullptr;
    _lb_hr_unit = nullptr;
    _lb_hits = nullptr;
    _lb_hits_unit = nullptr;
    _lb_price = nullptr;
    _last_sync_ms = 0;
    _last_price_text = "";
}

void PageClockBase::_sync_hashrate() {
    if (!_lb_hr || !_lb_hr_unit) {
        return;
    }

    const MinerStatus* st = MinerApp::instance().status();
    if (!st) {
        return;
    }

    String hr_value = formatNumber(st->hashrate._3m, 3);
    String hr_unit = (st->hashrate._3m > 0 && hr_value.length() > 0)
        ? (String(hr_value.charAt(hr_value.length() - 1)) + "H/s")
        : "";
    String hr_num = (hr_value.length() > 0)
        ? hr_value.substring(0, hr_value.length() - 1)
        : hr_value;

    lv_label_set_text(_lb_hr, hr_num.c_str());
    lv_label_set_text(_lb_hr_unit, hr_unit.c_str());
}

void PageClockBase::_sync_time_and_date() {
    auto& clock = AppState::instance().clock;
    String time_text = clock.time_str.text.get();
    String date_text = clock.date_str.text.get();
    String hms = time_text;
    String ampm;

    int sep = time_text.lastIndexOf(' ');
    if (sep > 0) {
        hms = time_text.substring(0, sep);
        ampm = time_text.substring(sep + 1);
    }

    if (hms.length() > 0 && hms.charAt(0) == '0') {
        hms.remove(0, 1);
    }

    if (_lb_time) {
        lv_label_set_text(_lb_time, hms.c_str());
        lv_coord_t width = lv_txt_get_width(hms.c_str(), hms.length(),
                                            lv_obj_get_style_text_font(_lb_time, LV_PART_MAIN),
                                            0, LV_TEXT_FLAG_NONE);
        lv_obj_set_width(_lb_time, width);

        if (_lb_ampm) {
            lv_label_set_text(_lb_ampm, ampm.c_str());
            lv_coord_t ampm_width = lv_txt_get_width(ampm.c_str(), ampm.length(),
                                                     lv_obj_get_style_text_font(_lb_ampm, LV_PART_MAIN),
                                                     0, LV_TEXT_FLAG_NONE);
            lv_obj_set_width(_lb_ampm, ampm_width);
            const BoardSpecConfig& spec = MinerApp::instance().spec();
            if (spec.name == BOARD_NMQAXE_PLUS_PLUS_NAME) {
                lv_obj_align(_lb_ampm, LV_ALIGN_CENTER, width / 1.7f, -32);
            } else {
                lv_obj_align(_lb_ampm, LV_ALIGN_CENTER, width / 1.5f, 2);
            }
        }
    }

    if (_lb_date) {
        lv_label_set_text(_lb_date, date_text.c_str());
        lv_coord_t width = lv_txt_get_width(date_text.c_str(), date_text.length(),
                                            lv_obj_get_style_text_font(_lb_date, LV_PART_MAIN),
                                            0, LV_TEXT_FLAG_NONE);
        lv_obj_set_width(_lb_date, width);
    }
}

void PageClockBase::_sync_hits() {
    if (!_lb_hits) {
        return;
    }

    const MinerStatus* st = MinerApp::instance().status();
    if (!st) {
        return;
    }

    if (st->hits < 10) {
        lv_obj_align(_lb_hits, LV_ALIGN_TOP_RIGHT, -10, 0);
    } else {
        lv_obj_align(_lb_hits, LV_ALIGN_TOP_RIGHT, -30, 0);
    }

    String hits_text = String((unsigned)st->hits);
    lv_obj_set_width(_lb_hits, 75);
    lv_label_set_text(_lb_hits, hits_text.c_str());
}

void PageClockBase::_sync_price() {
    if (!_lb_price) {
        return;
    }

    String price_text = AppState::instance().clock.price.text.get();
    uint32_t color = 0x808080;
    if (!price_text.isEmpty() && _last_price_text.length() > 0 && price_text != _last_price_text) {
        color = 0x00FF00;
    }
    _last_price_text = price_text;

    lv_obj_set_style_text_color(_lb_price, lv_color_hex(color), LV_PART_MAIN);
    lv_label_set_text(_lb_price, price_text.c_str());
}

void PageClockBase::_on_update() {
    if (!_parent) {
        return;
    }

    uint32_t now = millis();
    if (now - _last_sync_ms < 1000) {
        return;
    }
    _last_sync_ms = now;

    _sync_hashrate();
    _sync_time_and_date();
    _sync_hits();
    _sync_price();
}
