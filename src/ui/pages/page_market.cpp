#include "ui/pages/page_market.h"

#include "app/application.h"
#include "market/market.h"
#include "utils/helper.h"
#include "../../version.h"

void PageMarketBase::destroy() {
    _parent = nullptr;
    _bg = nullptr;
    _lb_hr = nullptr;
    _lb_hr_unit = nullptr;
    _lb_version = nullptr;
    _last_update_ms = 0;
    _last_switch_ms = 0;
    _max_per_page = 0;
    _total_pages = 1;
    _cur_page = 0;
    _inited = false;
    _lb_names.clear();
    _lb_dollar_signs.clear();
    _lb_prices.clear();
    _lb_changes.clear();
}

String PageMarketBase::_format_price(float price) {
    String price_str;
    int decimals = (price < 0.01f) ? 7 : (price < 1.0f) ? 5 : 2;
    if (price >= 1000.0f) {
        int int_part = (int)price;
        float dec_part = price - int_part;
        String int_str = String(int_part);
        String fmt_int;
        int len = int_str.length();
        for (int j = 0; j < len; ++j) {
            if (j > 0 && (len - j) % 3 == 0) {
                fmt_int += ",";
            }
            fmt_int += int_str.charAt(j);
        }
        price_str = fmt_int + String(dec_part, decimals).substring(1);
    } else {
        price_str = String(price, decimals);
    }
    return price_str;
}

void PageMarketBase::_init_labels() {
    if (_inited || !_parent || !_coin_font || !_hr_font || !_hr_unit_font || !_version_font) {
        return;
    }

    lv_coord_t font_h = lv_font_get_line_height(_coin_font);
    lv_coord_t hr_h = lv_font_get_line_height(_hr_font);
    lv_coord_t avail_h = _H - 2 - hr_h - 4;
    _max_per_page = (uint16_t)(avail_h / font_h);
    if (_max_per_page == 0) {
        _max_per_page = 1;
    }

    for (uint16_t i = 0; i < _max_per_page; ++i) {
        lv_coord_t y = 2 + (lv_coord_t)(i * font_h);

        lv_obj_t* ln = lv_label_create(_parent);
        lv_obj_set_style_text_font(ln, _coin_font, 0);
        lv_obj_set_style_text_color(ln, lv_color_hex(0x00D7FF), 0);
        lv_obj_set_pos(ln, 3, y);
        lv_obj_set_width(ln, _col_dollar_x - 6);
        lv_label_set_long_mode(ln, LV_LABEL_LONG_DOT);
        lv_label_set_text(ln, "");
        _lb_names.push_back(ln);

        lv_obj_t* ld = lv_label_create(_parent);
        lv_obj_set_style_text_font(ld, _coin_font, 0);
        lv_obj_set_style_text_color(ld, lv_color_hex(0x00FF7F), 0);
        lv_obj_set_pos(ld, _col_dollar_x, y);
        lv_obj_set_width(ld, _col_price_x - _col_dollar_x - 1);
        lv_label_set_long_mode(ld, LV_LABEL_LONG_DOT);
        lv_label_set_text(ld, "$");
        _lb_dollar_signs.push_back(ld);

        lv_obj_t* lp = lv_label_create(_parent);
        lv_obj_set_style_text_font(lp, _coin_font, 0);
        lv_obj_set_style_text_color(lp, lv_color_hex(0x00FF7F), 0);
        lv_obj_set_pos(lp, _col_price_x, y);
        lv_obj_set_width(lp, _col_change_x - _col_price_x - 2);
        lv_obj_set_style_text_align(lp, LV_TEXT_ALIGN_RIGHT, 0);
        lv_label_set_long_mode(lp, LV_LABEL_LONG_DOT);
        lv_label_set_text(lp, "");
        _lb_prices.push_back(lp);

        lv_obj_t* lc = lv_label_create(_parent);
        lv_obj_set_style_text_font(lc, _coin_font, 0);
        lv_obj_set_style_text_color(lc, lv_color_hex(0x00FF7F), 0);
        lv_obj_set_pos(lc, _col_change_x, y);
        lv_obj_set_width(lc, _W - _col_change_x - 2);
        lv_obj_set_style_text_align(lc, LV_TEXT_ALIGN_RIGHT, 0);
        lv_label_set_long_mode(lc, LV_LABEL_LONG_DOT);
        lv_label_set_text(lc, "");
        _lb_changes.push_back(lc);
    }

    lv_coord_t width = lv_txt_get_width("kH/s", strlen("kH/s"), _hr_unit_font, 0, LV_TEXT_FLAG_NONE);

    _lb_hr_unit = lv_label_create(_parent);
    lv_obj_set_style_text_font(_lb_hr_unit, _hr_unit_font, 0);
    lv_obj_set_style_text_color(_lb_hr_unit, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_width(_lb_hr_unit, width + 5);
    lv_label_set_text(_lb_hr_unit, "---");
    lv_obj_align(_lb_hr_unit, LV_ALIGN_BOTTOM_RIGHT, 0, -3);

    _lb_hr = lv_label_create(_parent);
    lv_obj_set_style_text_font(_lb_hr, _hr_font, 0);
    lv_obj_set_style_text_color(_lb_hr, lv_color_hex(0xEE7D30), 0);
    lv_obj_set_width(_lb_hr, 110);
    lv_obj_set_style_text_align(_lb_hr, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(_lb_hr, "--");
    lv_obj_align(_lb_hr, LV_ALIGN_BOTTOM_RIGHT, -width - 8, 0);

    _lb_version = lv_label_create(_parent);
    lv_obj_set_style_text_font(_lb_version, _version_font, 0);
    lv_obj_set_style_text_color(_lb_version, lv_color_hex(0x888888), 0);
    lv_label_set_text(_lb_version, "");
    lv_obj_align(_lb_version, LV_ALIGN_BOTTOM_LEFT, _version_x, _version_y);

    _last_switch_ms = millis();
    _inited = true;
}

void PageMarketBase::_sync_footer() {
    const MinerStatus* st = MinerApp::instance().status();
    if (!st || !_lb_hr || !_lb_hr_unit || !_lb_version) {
        return;
    }

    String hr_val = formatNumber(st->hashrate._3m, 3);
    String hr_unit = (st->hashrate._3m > 0 && hr_val.length() > 0)
        ? (String(hr_val.charAt(hr_val.length() - 1)) + "H/s")
        : "";
    String hr_num = (hr_val.length() > 0)
        ? hr_val.substring(0, hr_val.length() - 1)
        : hr_val;

    lv_label_set_text(_lb_hr, hr_num.c_str());
    lv_label_set_text(_lb_hr_unit, hr_unit.c_str());
    lv_label_set_text(_lb_version, BOARD_CURRENT_FW_VERSION);
}

void PageMarketBase::_sync_watchlist_page() {
    MarketClass* market = MinerApp::instance().market();
    if (!market) {
        return;
    }

    const auto& wl = market->get_sorted_watchlist();
    if (wl.empty()) {
        return;
    }

    size_t total_coins = wl.size();
    _total_pages = (uint16_t)((total_coins + _max_per_page - 1) / _max_per_page);
    if (_total_pages == 0) {
        _total_pages = 1;
    }
    if (_cur_page >= _total_pages) {
        _cur_page = 0;
    }

    auto it = wl.cbegin();
    for (uint16_t skip = 0; skip < _cur_page * _max_per_page && it != wl.cend(); ++skip) {
        ++it;
    }

    for (uint16_t i = 0; i < _max_per_page; ++i) {
        if (it == wl.cend()) {
            if (i < _lb_names.size()) lv_label_set_text(_lb_names[i], "");
            if (i < _lb_dollar_signs.size()) lv_label_set_text(_lb_dollar_signs[i], "");
            if (i < _lb_prices.size()) lv_label_set_text(_lb_prices[i], "");
            if (i < _lb_changes.size()) lv_label_set_text(_lb_changes[i], "");
            continue;
        }

        const String& sym = it->first;
        const CoinPrice& cp = it->second;
        ++it;

        String name = sym.endsWith("USDT") ? sym.substring(0, sym.length() - 4) : sym;
        String price_str = _format_price(cp.price);
        uint32_t color = (cp.change_pct >= 0.0f) ? 0x00FF7F : 0xFF4444;
        String change_str = (cp.change_pct >= 0.0f ? "+" : "") + String(cp.change_pct, 1) + "%";

        if (i < _lb_names.size()) {
            lv_label_set_text(_lb_names[i], name.c_str());
        }
        if (i < _lb_dollar_signs.size()) {
            lv_obj_set_style_text_color(_lb_dollar_signs[i], lv_color_hex(color), 0);
            lv_label_set_text(_lb_dollar_signs[i], "$");
        }
        if (i < _lb_prices.size()) {
            lv_obj_set_style_text_color(_lb_prices[i], lv_color_hex(color), 0);
            lv_label_set_text(_lb_prices[i], price_str.c_str());
        }
        if (i < _lb_changes.size()) {
            lv_obj_set_style_text_color(_lb_changes[i], lv_color_hex(color), 0);
            lv_label_set_text(_lb_changes[i], change_str.c_str());
        }
    }

    if (_total_pages > 1 && millis() - _last_switch_ms > 30000) {
        _cur_page = (uint16_t)((_cur_page + 1) % _total_pages);
        _last_switch_ms = millis();
    }
}

void PageMarketBase::_on_update() {
    if (!_parent) {
        return;
    }

    uint32_t now = millis();
    if (now - _last_update_ms < 1000) {
        return;
    }
    _last_update_ms = now;

    _init_labels();
    _sync_footer();
    _sync_watchlist_page();
}
