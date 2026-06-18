#include "ui/pages/page_hr_health.h"

void PageHr_healthBase::destroy() {
    _parent = nullptr;
    _lb_hr = nullptr;
    _lb_hr_unit = nullptr;
    _lb_scale = nullptr;
    _chart = nullptr;
    _chart_series = nullptr;
    _pie = {};
    _last_update_ms = 0;
    _last_hashrate = -1.0;
}

void PageHr_healthBase::_create_chart() {
    if (!_parent || _chart != nullptr) {
        return;
    }

    const BoardSpecConfig& spec = MinerApp::instance().spec();
    const uint16_t max_bars = (uint16_t)spec.ui.hashrate_dist_page.max_x_bars;

    _chart = lv_chart_create(_parent);
    lv_obj_add_flag(_chart, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(_chart, _chart_w, _chart_h);
    lv_obj_align(_chart, LV_ALIGN_CENTER, _chart_x, _chart_y);
    lv_chart_set_type(_chart, LV_CHART_TYPE_BAR);
    lv_chart_set_range(_chart, LV_CHART_AXIS_PRIMARY_X, 0, max_bars - 1);
    lv_chart_set_range(_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_div_line_count(_chart, 5, 4);
    _chart_series = lv_chart_add_series(_chart, lv_palette_main(LV_PALETTE_RED), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_point_count(_chart, max_bars);
    lv_chart_set_all_value(_chart, _chart_series, 0);
    lv_chart_set_axis_tick(_chart, LV_CHART_AXIS_PRIMARY_X, 1, 1, max_bars, 1, true, 25);
    lv_chart_set_axis_tick(_chart, LV_CHART_AXIS_PRIMARY_Y, 1, 2, 5, 1, true, 25);
    lv_obj_set_style_bg_opa(_chart, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_opa(_chart, LV_OPA_TRANSP, LV_PART_MAIN);

    static lv_style_t style_grid;
    static bool style_inited = false;
    if (!style_inited) {
        lv_style_init(&style_grid);
        lv_style_set_line_dash_width(&style_grid, 2);
        lv_style_set_line_dash_gap(&style_grid, 4);
        lv_style_set_line_opa(&style_grid, LV_OPA_50);
        style_inited = true;
    }
    lv_obj_add_style(_chart, &style_grid, LV_PART_MAIN | LV_STATE_DEFAULT);
    if (_chart_tick_font) {
        lv_obj_set_style_text_font(_chart, _chart_tick_font, LV_PART_TICKS);
    }
}

void PageHr_healthBase::_create_pie_chart(uint8_t sector_count) {
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

void PageHr_healthBase::_update_pie_chart(const uint16_t* angles) {
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

void PageHr_healthBase::_on_update() {
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

    if (_last_hashrate == st->hashrate._3m) {
        _last_update_ms = now;
        return;
    }
    _last_hashrate = st->hashrate._3m;

    const BoardSpecConfig& spec = MinerApp::instance().spec();

    if (_chart && _chart_series) {
        for (uint16_t i = 0; i < spec.ui.hashrate_dist_page.max_x_bars; ++i) {
            uint8_t y = 0;
            auto it = spec.ui.hashrate_dist_page.dist_map.find(i);
            if (it != spec.ui.hashrate_dist_page.dist_map.end()) {
                y = it->second;
            }
            lv_chart_set_value_by_id(_chart, _chart_series, i, y);
        }
        lv_chart_refresh(_chart);
    }

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

    if (_show_asic_pie && spec.name == BOARD_NMQAXE_PLUS_PLUS_NAME) {
        uint8_t asic_count = MinerApp::instance().asic_count();
        if (asic_count > 0 && asic_count <= 4 && _pie.arcs[0] == nullptr) {
            _create_pie_chart(asic_count);
        }
        if (_pie.arcs[0] != nullptr && st->asic_rsp_counter.size() == asic_count) {
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

    _last_update_ms = now;
}
