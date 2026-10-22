#include "ui/pages/page_lucky.h"

// Integer-mantissa KMGT label for the medal value labels ("166K", not
// "166.4K" / "93.2K"): round the mantissa, never print decimals.
static String fmtIntKMGT(float num) {
    static const char* units[] = {"", "K", "M", "G", "T", "P", "E"};
    int ui = 0;
    while (num >= 1000.0f && ui < 6) { num /= 1000.0f; ui++; }
    return String((long)lroundf(num)) + units[ui];
}

// Compact label for an exact power-of-ten axis tick: exponent d -> "1"/"10"/
// "100" scaled by the K/M/G/T/P/E unit (d/3). Matches the AxeOS log axis.
static String fmtDecade(int d) {
    static const char* units[] = {"", "K", "M", "G", "T", "P", "E"};
    int ui = d / 3;
    if (ui < 0) ui = 0;
    if (ui > 6) ui = 6;
    const int lead = d - ui * 3;   // 0, 1 or 2
    const char* mant = (lead == 0) ? "1" : (lead == 1) ? "10" : "100";
    return String(mant) + units[ui];
}

void PageLuckyBase::destroy() {
    _parent = nullptr;
    _lb_hr = nullptr;
    _lb_hr_unit = nullptr;
    _pie = {};
    _last_update_ms = 0;
    _last_hashrate = -1.0;
    _lb_title = nullptr;
    _chart = nullptr;
    _chart_line = nullptr;
    _series_share = nullptr;
    _series_net = nullptr;
    _lb_legend = nullptr;
    for (int i = 0; i < 3; i++) {
        _medal_slot[i] = -1;
        _val_lbs[i] = nullptr;
    }
    for (int i = 0; i < 8; i++) {
        _axis_lbs[i] = nullptr;
    }
    _snap_count = 0;
    _snap_last_epoch = 0;
}

void PageLuckyBase::_create_pie_chart(uint8_t sector_count) {
    if (!_parent || !_show_asic_pie || sector_count == 0 || sector_count > 4 || _pie.arcs[0] != nullptr) {
        return;
    }

    _pie.sector_count = sector_count;
    uint16_t current_angle = 0;
    for (uint8_t i = 0; i < sector_count; ++i) {
        uint16_t end_angle = current_angle + _pie_cfg[i].angle;
        lv_obj_t* arc = lv_arc_create(_parent);
        _pie.arcs[i] = arc;
        lv_obj_set_size(arc, _pie.radius * 2, _pie.radius * 2);
        lv_obj_set_pos(arc, _pie.center_x - _pie.radius, _pie.center_y - _pie.radius);
        lv_arc_set_bg_angles(arc, 0, 360);
        lv_arc_set_angles(arc, current_angle, end_angle);
        lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);
        lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
        lv_arc_set_rotation(arc, 270);
        lv_obj_set_style_arc_width(arc, 0, LV_PART_MAIN);
        lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_arc_width(arc, _pie.radius, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(arc, _pie_cfg[i].color, LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(arc, LV_OPA_50, LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);

        if (_pie_cfg[i].label && _pie_cfg[i].label[0] != '\0') {
            float mid_angle = current_angle + _pie_cfg[i].angle / 2.0f;
            float label_radius = _pie.radius * 0.6f;
            float angle_rad = (mid_angle - 90.0f) * (float)M_PI / 180.0f;
            lv_coord_t label_x = _pie.center_x + (lv_coord_t)(label_radius * cosf(angle_rad));
            lv_coord_t label_y = _pie.center_y + (lv_coord_t)(label_radius * sinf(angle_rad));
            _pie.labels[i] = lv_label_create(_parent);
            lv_label_set_text(_pie.labels[i], _pie_cfg[i].label);
            lv_obj_set_style_text_color(_pie.labels[i], lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(_pie.labels[i], &lv_font_montserrat_12, 0);
            lv_obj_align(_pie.labels[i], LV_ALIGN_TOP_LEFT, label_x - 10, label_y - 10);
        }

        current_angle = end_angle;
    }
}

void PageLuckyBase::_update_pie_chart(const uint16_t* angles) {
    if (!_show_asic_pie || !_pie.arcs[0] || !angles) {
        return;
    }

    uint16_t current_angle = 0;
    for (uint8_t i = 0; i < _pie.sector_count; ++i) {
        if (_pie.arcs[i] == nullptr) {
            continue;
        }
        uint16_t end_angle = current_angle + angles[i];
        lv_arc_set_angles(_pie.arcs[i], current_angle, end_angle);

        if (_pie.labels[i] != nullptr) {
            float mid_angle = current_angle + angles[i] / 2.0f;
            float label_radius = _pie.radius * 0.6f;
            float angle_rad = (mid_angle - 90.0f) * (float)M_PI / 180.0f;
            lv_coord_t label_x = _pie.center_x + (lv_coord_t)(label_radius * cosf(angle_rad));
            lv_coord_t label_y = _pie.center_y + (lv_coord_t)(label_radius * sinf(angle_rad));
            uint8_t percentage = (uint8_t)((angles[i] * 100U) / 360U);
            String label_text = "A" + String(i + 1) + "\r" + String(percentage) + "%";
            lv_label_set_text(_pie.labels[i], label_text.c_str());
            lv_obj_align(_pie.labels[i], LV_ALIGN_TOP_LEFT, label_x - 10, label_y - 10);
        }

        current_angle = end_angle;
    }
}

void PageLuckyBase::_on_update() {
    if (!_parent) {
        return;
    }

    uint32_t now = millis();
    if (now - _last_update_ms < 1000) {
        return;
    }

    const MinerStatus* st = MinerApp::instance().status();
    if (!st) {
        return;
    }

    // Hashrate label — refresh only when the value actually changed.
    if (_last_hashrate != st->hashrate._3m) {
        _last_hashrate = st->hashrate._3m;

        String hr_value = formatNumber(st->hashrate._3m, 3);
        String hr_unit = (st->hashrate._3m > 0.0 && hr_value.length() > 0)
            ? (String(hr_value.charAt(hr_value.length() - 1)) + "H/s")
            : "";
        String hr_num = (st->hashrate._3m > 0.0 && hr_value.length() > 0)
            ? hr_value.substring(0, hr_value.length() - 1)
            : hr_value;

        if (_lb_hr) {
            lv_label_set_text(_lb_hr, hr_num.c_str());
        }
        if (_lb_hr_unit) {
            lv_label_set_text(_lb_hr_unit, hr_unit.c_str());
        }
    }

#if defined(BOARD_NMQAXE_PP) || defined(BOARD_NMQAXE_PP_REV61) || defined(BOARD_NMQAXE_PP_REV81)  || defined(BOARD_NMQAXE_PP_NEXUS)
    // ASIC share-distribution pie is temporarily disabled (the page now shows
    // Lucky Statistics). The code path is kept intact — flip _show_asic_pie
    // back to true in the 320x240 layout to restore it.
    if (_show_asic_pie) {
        const BoardSpecConfig& spec = MinerApp::instance().spec();
        // Use board-design expected chip count (num_req), not runtime detected count.
        // This ensures all sectors always show — a dead chip simply stays at 0%.
        uint8_t asic_count = spec.asic.num_req;
        if (asic_count > 0 && asic_count <= 4 && _pie.arcs[0] == nullptr) {
            _create_pie_chart(asic_count);
        }
        if (_pie.arcs[0] != nullptr) {
            uint64_t total = 0;
            for (const auto& pair : st->asic_rsp_counter) {
                total += pair.second;
            }

            uint16_t new_angles[4] = {0, 0, 0, 0};
            for (const auto& pair : st->asic_rsp_counter) {
                if (pair.first >= asic_count) {
                    continue;
                }
                float per = (total == 0) ? (1.0f / (float)asic_count) : ((float)pair.second / (float)total);
                new_angles[pair.first] = (uint16_t)(per * 360.0f);
            }

            uint32_t total_angle = 0;
            for (uint8_t i = 0; i + 1 < asic_count; ++i) {
                total_angle += new_angles[i];
            }
            new_angles[asic_count - 1] = (total_angle < 360U) ? (uint16_t)(360U - total_angle) : 0U;
            _update_pie_chart(new_angles);
        }
    }
#elif defined(BOARD_NMAXE) || defined(BOARD_NMAXE_GAMMA)
#else
    #error "No board model defined. Add -D BOARD_<model> in platformio.ini"
#endif

    // Lucky Statistics: session share/net-difficulty chart fed from
    // proximity_history (RAM, lost on reboot). Redraws only when the sample
    // set actually changed.
    _update_lucky(st);

    _last_update_ms = now;
}

// ── Lucky Statistics (session data, mirrors the AxeOS card) ────────────────
// Source: st.proximity_history — the accepted shares of this power-on session
// (RAM deque ≤36; lost on reboot). The LCD only has room for _lucky_points
// bars, so the window is: the session top-3 shares (NEVER dropped) plus the
// newest remaining samples to fill the row — then everything is re-sorted by
// arrival time, so the x-axis order stays identical to the AxeOS card
// (oldest→newest). Bars = share difficulty (top-3 = gold/silver/bronze),
// red line = network difficulty at that moment.
void PageLuckyBase::_create_lucky_chart() {
    if (_chart || _chart_w <= 0 || _chart_h <= 0) {
        return;
    }
    const uint16_t pts = (uint16_t)(_lucky_points > 0 && _lucky_points <= LUCKY_SAMPLES
                                        ? _lucky_points : LUCKY_SAMPLES);

    // ── Bottom chart: share-difficulty BARS (like the AxeOS card) ──────────
    _chart = lv_chart_create(_parent);
    lv_obj_set_size(_chart, _chart_w, _chart_h);
    lv_obj_set_pos(_chart, _chart_x, _chart_y);
    lv_obj_clear_flag(_chart, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(_chart, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(_chart, 0, LV_PART_MAIN);
    // Grid: light-white dashed lines (matches the requested style).
    lv_obj_set_style_line_color(_chart, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_line_opa(_chart, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_line_width(_chart, 1, LV_PART_MAIN);
    lv_obj_set_style_line_dash_width(_chart, 3, LV_PART_MAIN);
    lv_obj_set_style_line_dash_gap(_chart, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_left(_chart, _pad_side, LV_PART_MAIN);
    lv_obj_set_style_pad_right(_chart, _pad_side, LV_PART_MAIN);
    lv_obj_set_style_pad_top(_chart, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(_chart, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(_chart, _bar_gap, LV_PART_MAIN);                 // gap between bars
    lv_obj_set_style_radius(_chart, 1, LV_PART_ITEMS);
    lv_chart_set_type(_chart, LV_CHART_TYPE_BAR);
    lv_chart_set_point_count(_chart, pts);
    lv_chart_set_update_mode(_chart, LV_CHART_UPDATE_MODE_CIRCULAR);
    lv_chart_set_div_line_count(_chart, _lucky_axis_ticks, 5);
    _series_share = lv_chart_add_series(_chart, lv_color_hex(0x29B6F6), LV_CHART_AXIS_PRIMARY_Y);
    // Per-sample bar colours: session top-3 = gold/silver/bronze, rest blue.
    lv_obj_add_event_cb(_chart, _lucky_bar_cb, LV_EVENT_DRAW_PART_BEGIN, this);

    // ── Y-axis decade tick labels (log scale, like the AxeOS card) ─────────
    // Created hidden; text + position set in _update_lucky once the log range
    // is known. They live in the left gutter (_axis_gutter px wide).
    for (int i = 0; i < _lucky_axis_ticks && i < 8; i++) {
        _axis_lbs[i] = lv_label_create(_parent);
        lv_label_set_text(_axis_lbs[i], "");
        lv_obj_set_style_text_font(_axis_lbs[i], &lv_font_montserrat_10, LV_PART_MAIN);
        lv_obj_set_style_text_color(_axis_lbs[i], lv_color_hex(0xB0B0B0), LV_PART_MAIN);
        lv_obj_set_width(_axis_lbs[i], _chart_x > 4 ? _chart_x - 3 : 0);
        lv_obj_set_style_text_align(_axis_lbs[i], LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
        lv_obj_add_flag(_axis_lbs[i], LV_OBJ_FLAG_HIDDEN);
    }

    // ── Overlay chart: red network-difficulty LINE, drawn on top ───────────
    // LVGL 8.4 cannot mix BAR + LINE in one chart, so a second transparent
    // chart is stacked on the same rect. Its side padding is widened by
    // half a bar block so line vertices land exactly on bar centres.
    const lv_coord_t content_w = _chart_w - 2 * _pad_side;
    const lv_coord_t block_w = (content_w - (pts - 1) * _bar_gap) / pts;
    const lv_coord_t line_pad = _pad_side + block_w / 2;

    _chart_line = lv_chart_create(_parent);
    lv_obj_set_size(_chart_line, _chart_w, _chart_h);
    lv_obj_set_pos(_chart_line, _chart_x, _chart_y);
    lv_obj_clear_flag(_chart_line, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(_chart_line, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(_chart_line, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(_chart_line, line_pad, LV_PART_MAIN);
    lv_obj_set_style_pad_right(_chart_line, line_pad, LV_PART_MAIN);
    lv_obj_set_style_pad_top(_chart_line, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(_chart_line, 0, LV_PART_MAIN);
    lv_obj_set_style_line_width(_chart_line, 2, LV_PART_ITEMS);                  // net line
    lv_obj_set_style_line_rounded(_chart_line, true, LV_PART_ITEMS);
    lv_obj_set_style_size(_chart_line, 2, LV_PART_INDICATOR);                    // small dots
    lv_chart_set_type(_chart_line, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(_chart_line, pts);
    lv_chart_set_update_mode(_chart_line, LV_CHART_UPDATE_MODE_CIRCULAR);
    lv_chart_set_div_line_count(_chart_line, 0, 0);                              // grid from bar chart
    _series_net = lv_chart_add_series(_chart_line, lv_color_hex(0xFF5252), LV_CHART_AXIS_PRIMARY_Y);

    // Share-difficulty value labels above the medal bars (positioned in
    // _update_lucky once bar heights are known).
    if (_lucky_bar_values) {
        for (int i = 0; i < 3; i++) {
            _val_lbs[i] = lv_label_create(_parent);
            lv_label_set_text(_val_lbs[i], "");
            lv_obj_set_style_text_font(_val_lbs[i], &lv_font_montserrat_10, LV_PART_MAIN);
            lv_obj_set_style_text_color(_val_lbs[i], lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        }
    }

    if (_lb_legend) {
        lv_label_set_text(_lb_legend, "");
    }
}

// LV_EVENT_DRAW_PART_BEGIN on the BAR chart: recolour the session top-3
// sample bars as gold / silver / bronze medals; all others stay web-blue.
void PageLuckyBase::_lucky_bar_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_DRAW_PART_BEGIN) return;
    PageLuckyBase* self = (PageLuckyBase*)lv_event_get_user_data(e);
    if (!self) return;
    lv_obj_draw_part_dsc_t* dsc = (lv_obj_draw_part_dsc_t*)lv_event_get_draw_part_dsc(e);
    if (!dsc || dsc->part != LV_PART_ITEMS || dsc->type != LV_CHART_DRAW_PART_BAR) return;
    if (!dsc->rect_dsc) return;
    const int slot = (int)dsc->id;   // sample index in the session window
    lv_color_t c = lv_color_hex(0x29B6F6);   // regular share bar (web blue)
    for (int m = 0; m < 3; m++) {
        if (self->_medal_slot[m] == slot) {
            c = lv_color_hex(m == 0 ? 0xFFC107 : (m == 1 ? 0xC0C0C0 : 0xCD7F32));
            break;
        }
    }
    dsc->rect_dsc->bg_color = c;
}

void PageLuckyBase::_update_lucky(const MinerStatus* st) {
    if (!_chart) {
        _create_lucky_chart();
    }
    if (!_chart || !_chart_line || !_series_share || !_series_net) {
        return;
    }

    // ── Snapshot the session deque under its mutex ─────────────────────────
    // Copying into fixed arrays keeps the rest of the update lock-free and
    // gives us a cheap change-detection baseline.
    int n = 0;
    if (st->proximity_history.mutex &&
        xSemaphoreTake(st->proximity_history.mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        int skip = 0;
        if ((int)st->proximity_history.deque.size() > LUCKY_SAMPLES) {
            skip = (int)st->proximity_history.deque.size() - LUCKY_SAMPLES;
        }
        int i = 0;
        for (const auto& node : st->proximity_history.deque) {
            if (i++ < skip) continue;
            if (n >= LUCKY_SAMPLES) break;
            _snap_share[n] = node.share_diff;
            _snap_net[n]   = node.net_diff;
            _snap_epoch[n] = node.epoch;
            n++;
        }
        xSemaphoreGive(st->proximity_history.mutex);
    }

    // ── Change detection: sample count + newest timestamp ─────────────────
    // The deque only mutates when a share lands (push or random eviction),
    // and every mutation changes either the size or the newest epoch. A
    // steady-state session therefore costs one mutex grab + two compares.
    const uint64_t newest = (n > 0) ? _snap_epoch[n - 1] : 0ULL;
    if (n == _snap_count && newest == _snap_last_epoch && newest != 0ULL) {
        return;
    }
    _snap_count = n;
    _snap_last_epoch = newest;

    // ── Build the display window (top-3 pinned + newest, chronological) ────
    // pts = bars this layout draws. Selection rule:
    //   1. The session top-3 shares by difficulty are ALWAYS kept.
    //   2. Remaining slots are filled with the NEWEST samples (back to front).
    //   3. The selection is re-sorted by arrival index, so the x axis keeps
    //      the exact AxeOS ordering (oldest→newest); gaps just mean older
    //      samples aged out of the window.
    const int pts = (_lucky_points > 0 && _lucky_points <= LUCKY_SAMPLES)
                        ? _lucky_points : LUCKY_SAMPLES;
    float disp_share[LUCKY_SAMPLES], disp_net[LUCKY_SAMPLES];
    int dn = 0;
    {
        bool sel[LUCKY_SAMPLES] = {false};
        // 1. Pin the session top-3 (by share_diff) — never dropped.
        for (int m = 0; m < 3 && m < n; m++) {
            int best = -1;
            float bestv = -1.0f;
            for (int i = 0; i < n; i++) {
                if (sel[i]) continue;
                if (_snap_share[i] > bestv) { bestv = _snap_share[i]; best = i; }
            }
            if (best >= 0) sel[best] = true;
        }
        // 2. Fill the remaining slots with the newest samples (newest first).
        int pinned = 0;
        for (int i = 0; i < n; i++) if (sel[i]) pinned++;
        for (int i = n - 1; i >= 0 && pinned < pts; i--) {
            if (!sel[i]) { sel[i] = true; pinned++; }
        }
        // 3. Emit in chronological (snapshot index) order.
        int d = 0;
        for (int i = 0; i < n && d < pts; i++) {
            if (!sel[i]) continue;
            disp_share[d] = _snap_share[i];
            disp_net[d]   = _snap_net[i];
            d++;
        }
        dn = d;
        // Medals = the pinned top-3, located inside the display window.
        for (int m = 0; m < 3; m++) _medal_slot[m] = -1;
        for (int m = 0; m < 3; m++) {
            int best = -1;
            float bestv = -1.0f;
            for (int i = 0; i < dn; i++) {
                bool taken = false;
                for (int k = 0; k < m; k++) if (_medal_slot[k] == i) { taken = true; break; }
                if (taken) continue;
                if (disp_share[i] > bestv) { bestv = disp_share[i]; best = i; }
            }
            _medal_slot[m] = best;
        }
    }

    // ── Log-scaled axis (like the AxeOS card's Chart.js log axis) ─────────
    // Range = floor(lo)..ceil(hi) of the data's log10 values — no extra
    // decade padding, so the plot area is fully used. Tick labels are placed
    // at the TRUE decade positions (1K/10K/100K/1M...) inside the range.
    double lo = 1e30, hi = -1e30;
    for (int i = 0; i < dn; i++) {
        if (disp_share[i] > 0.0f) { double v = log10(disp_share[i]); lo = fmin(lo, v); hi = fmax(hi, v); }
        if (disp_net[i]   > 0.0f) { double v = log10(disp_net[i]);   lo = fmin(lo, v); hi = fmax(hi, v); }
    }
    if (dn == 0) { lo = 3.0; hi = 5.0; }   // empty-state placeholder axis
    const int scale = 100;
    lv_coord_t y_min = (lv_coord_t)((int)floor(lo) * scale);
    lv_coord_t y_max = (lv_coord_t)((int)ceil(hi) * scale);
    if (y_max <= y_min) y_max = y_min + scale;
    // Both charts share the identical Y range so bars and line align.
    lv_chart_set_range(_chart, LV_CHART_AXIS_PRIMARY_Y, y_min, y_max);
    lv_chart_set_range(_chart_line, LV_CHART_AXIS_PRIMARY_Y, y_min, y_max);
    // One horizontal grid line per decade (range ends are whole decades, so
    // the evenly-spaced div lines land exactly on the tick labels).
    lv_chart_set_div_line_count(_chart, (y_max - y_min) / scale + 1, 5);

    // ── Place the Y-axis decade labels at their true log positions ───────
    int tick_idx = 0;
    {
        const lv_coord_t content_h = _chart_h - 2;   // pad_top=2, pad_bottom=0
        const lv_coord_t yspan = (y_max > y_min) ? (y_max - y_min) : 1;
        for (int d = (int)ceil(lo); d <= (int)floor(hi) && tick_idx < 8; d++) {
            if (!_axis_lbs[tick_idx]) { tick_idx++; continue; }
            lv_coord_t gy = _chart_y + 2
                + (lv_coord_t)((int64_t)(y_max - d * scale) * content_h / yspan);
            lv_coord_t lh = lv_obj_get_height(_axis_lbs[tick_idx]);
            if (lh <= 0) lh = 10;
            lv_label_set_text(_axis_lbs[tick_idx], fmtDecade(d).c_str());
            lv_obj_align(_axis_lbs[tick_idx], LV_ALIGN_TOP_LEFT, 0, gy - lh / 2);
            lv_obj_clear_flag(_axis_lbs[tick_idx], LV_OBJ_FLAG_HIDDEN);
            tick_idx++;
        }
        for (int i = tick_idx; i < 8; i++) {
            if (_axis_lbs[i]) {
                lv_label_set_text(_axis_lbs[i], "");
                lv_obj_add_flag(_axis_lbs[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    // ── Write point arrays; samples are left-aligned, tail slots empty ────
    for (int i = 0; i < pts; i++) {
        if (i < dn) {
            _series_share->y_points[i] = (disp_share[i] > 0.0f)
                ? (lv_coord_t)(log10(disp_share[i]) * scale) : LV_CHART_POINT_NONE;
            _series_net->y_points[i] = (disp_net[i] > 0.0f)
                ? (lv_coord_t)(log10(disp_net[i]) * scale) : LV_CHART_POINT_NONE;
        } else {
            _series_share->y_points[i] = LV_CHART_POINT_NONE;
            _series_net->y_points[i]   = LV_CHART_POINT_NONE;
        }
    }
    lv_chart_refresh(_chart);
    lv_chart_refresh(_chart_line);

    // ── Value labels above the medal bars (big screens only) ──────────────
    // Bar-top screen-Y mirrors LVGL's BAR geometry:
    //   y_tmp = (val - ymin) * content_h / (ymax - ymin)
    //   top   = chart_y + pad_top + (content_h - y_tmp)
    if (_lucky_bar_values) {
        const lv_coord_t content_w = _chart_w - 2 * _pad_side;
        const lv_coord_t content_h = _chart_h - 2;   // pad_top=2, pad_bottom=0
        const lv_coord_t block_w = (content_w - (pts - 1) * _bar_gap) / pts;
        const lv_coord_t yspan = (y_max > y_min) ? (y_max - y_min) : 1;
        for (int m = 0; m < 3; m++) {
            if (!_val_lbs[m]) continue;
            const int slot = _medal_slot[m];
            if (slot >= 0 && slot < dn && disp_share[slot] > 0.0f) {
                lv_coord_t val = _series_share->y_points[slot];
                lv_coord_t y_tmp = (lv_coord_t)((int64_t)(val - y_min) * content_h / yspan);
                lv_coord_t bar_top = _chart_y + 2 + (content_h - y_tmp);
                lv_coord_t cx = _chart_x + _pad_side
                    + (lv_coord_t)((int64_t)slot * (content_w - block_w) / (pts - 1))
                    + block_w / 2;
                lv_label_set_text(_val_lbs[m], fmtIntKMGT(disp_share[slot]).c_str());
                // Match the label colour to its medal bar (gold/silver/bronze).
                lv_obj_set_style_text_color(_val_lbs[m],
                    lv_color_hex(m == 0 ? 0xFFC107 : (m == 1 ? 0xC0C0C0 : 0xCD7F32)),
                    LV_PART_MAIN);
                lv_coord_t lw = lv_obj_get_width(_val_lbs[m]);
                if (lw <= 0) lw = 40;
                lv_coord_t ly = bar_top - (lv_coord_t)lv_obj_get_height(_val_lbs[m]) - 1;
                if (ly < _chart_y) ly = _chart_y;   // keep inside chart area
                lv_obj_set_pos(_val_lbs[m], cx - lw / 2, ly);
                lv_obj_clear_flag(_val_lbs[m], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_label_set_text(_val_lbs[m], "");
                lv_obj_add_flag(_val_lbs[m], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    // ── Legend: sample count + block-solve warning ────────────────────────
    // solved counts over the FULL session snapshot (not just the visible
    // window) so a solve outside the plotted bars is still reported.
    int solved = 0;
    for (int i = 0; i < n; i++) {
        if (_snap_net[i] > 0.0f && _snap_share[i] >= _snap_net[i]) solved++;
    }
    if (_lb_legend) {
        // Plain ASCII only: montserrat_10 has no glyph for U+00B7 etc.
        // (they rendered as tofu boxes on the LCD).
        if (solved > 0) {
            lv_label_set_text(_lb_legend, (String(LV_SYMBOL_WARNING) + " " +
                String(solved) + " block solve" + (solved > 1 ? "s" : "")).c_str());
            lv_obj_set_style_text_color(_lb_legend, lv_color_hex(0xFF5252), LV_PART_MAIN);
        } else if (dn > 0) {
            lv_label_set_text(_lb_legend, "bar share diff / line net diff");
            lv_obj_set_style_text_color(_lb_legend, lv_color_hex(0x808080), LV_PART_MAIN);
        } else {
            lv_label_set_text(_lb_legend, "no shares yet this session");
            lv_obj_set_style_text_color(_lb_legend, lv_color_hex(0x808080), LV_PART_MAIN);
        }
    }
}
