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
    // Delegates to the shared money formatter so the market wall cannot drift
    // from the MINER / CLOCK page rendering. No "$" here: this layout draws the
    // currency symbol in its own label (_lb_dollar_signs).
    return format_money(price);
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

    // Snapshot under the market lock: the (market) task rebuilds the watchlist every
    // refresh cycle, so iterating its containers directly races a realloc and walks
    // freed PSRAM nodes. Rows are fixed-size POD, so the copy owns no allocations.
    size_t total_coins = market->watchlist_size();
    if (total_coins == 0) {
        return;
    }

    _total_pages = (uint16_t)((total_coins + _max_per_page - 1) / _max_per_page);
    if (_total_pages == 0) {
        _total_pages = 1;
    }
    if (_cur_page >= _total_pages) {
        _cur_page = 0;
    }

    const size_t offset = (size_t)_cur_page * _max_per_page;
    std::vector<WatchlistRow> rows(_max_per_page);
    size_t got = market->copy_watchlist_window(offset, _max_per_page, rows.data());
    if (got == 0) {
        return;   // lock busy — keep the previous frame
    }

    for (uint16_t i = 0; i < _max_per_page; ++i) {
        if (i >= got) {
            if (i < _lb_names.size()) lv_label_set_text(_lb_names[i], "");
            if (i < _lb_dollar_signs.size()) lv_label_set_text(_lb_dollar_signs[i], "");
            if (i < _lb_prices.size()) lv_label_set_text(_lb_prices[i], "");
            if (i < _lb_changes.size()) lv_label_set_text(_lb_changes[i], "");
            continue;
        }

        const WatchlistRow& row = rows[i];
        const char* sym = row.symbol;
        size_t sym_len = strlen(sym);
        String name = (sym_len > 4 && strcmp(sym + sym_len - 4, "USDT") == 0)
                          ? String(sym).substring(0, sym_len - 4)
                          : String(sym);
        String price_str = _format_price(row.price);
        // Same constants the MINER page price uses (see app_state.h).
        uint32_t color = (row.change_pct >= 0.0f) ? APP_COLOR_PRICE_UP : APP_COLOR_PRICE_DOWN;
        String change_str = (row.change_pct >= 0.0f ? "+" : "") + String(row.change_pct, 1) + "%";

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
