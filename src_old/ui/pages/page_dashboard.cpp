#include "ui/pages/page_dashboard.h"
#include "app/application.h"

void PageDashboardBase::destroy() {
    _parent = nullptr;
    _lb_hr = nullptr;
    _lb_hr_unit = nullptr;
    _img_miner = nullptr;
    _lb_diff = nullptr;
    _miner_img_dsc = nullptr;
    _last_ring_update_ms = 0;
    _last_miner_anim_ms = 0;
    _miner_anim_step = 0.0f;
    _ring_oc = {};
    _ring_pwr = {};
    _ring_vc_req = {};
    _ring_vc_real = {};
    _ring_asic_temp = {};
    _ring_vcore_temp = {};
}

PageDashboardBase::RingObj PageDashboardBase::_draw_ring(lv_obj_t* parent, const RingConfig& config) {
    RingObj ring;
    if (!parent) {
        return ring;
    }

    ring.arc = lv_arc_create(parent);
    lv_obj_set_size(ring.arc, 2 * config.radius, 2 * config.radius);
    lv_arc_set_bg_angles(ring.arc, 0, config.angle_full);
    lv_arc_set_angles(ring.arc, config.angle_start, config.angle_end);
    lv_obj_remove_style(ring.arc, nullptr, LV_PART_KNOB);
    lv_arc_set_rotation(ring.arc, 90 + (360 - config.angle_full) / 2);
    lv_obj_set_style_arc_width(ring.arc, config.line_width, LV_PART_MAIN);
    lv_obj_set_style_arc_width(ring.arc, config.line_width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(ring.arc, config.bg_color, LV_PART_MAIN);
    lv_obj_set_style_arc_color(ring.arc, config.indicator_color, LV_PART_INDICATOR);
    lv_obj_set_pos(ring.arc, config.x, config.y);
    lv_obj_clear_flag(ring.arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(ring.arc, LV_OBJ_FLAG_EVENT_BUBBLE);

    if (config.center_text && config.center_font) {
        ring.label_center = lv_label_create(ring.arc);
        lv_label_set_text(ring.label_center, config.center_text);
        lv_obj_set_style_text_font(ring.label_center, config.center_font, LV_PART_MAIN);
        lv_obj_set_style_text_color(ring.label_center, config.center_text_color, LV_PART_MAIN);
        lv_obj_set_style_text_align(ring.label_center, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_label_set_long_mode(ring.label_center, LV_LABEL_LONG_CLIP);
        lv_obj_center(ring.label_center);
    }

    if (config.title_text && config.title_font) {
        ring.label_title = lv_label_create(parent);
        lv_obj_set_width(ring.label_title, 2 * config.radius);
        lv_label_set_text(ring.label_title, config.title_text);
        lv_obj_set_style_text_font(ring.label_title, config.title_font, LV_PART_MAIN);
        lv_obj_set_style_text_color(ring.label_title, config.title_text_color, LV_PART_MAIN);
        lv_obj_set_style_text_align(ring.label_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_label_set_long_mode(ring.label_title, LV_LABEL_LONG_DOT);
        lv_coord_t font_height = lv_font_get_line_height(config.title_font);
        lv_obj_align(ring.label_title, LV_ALIGN_TOP_LEFT,
                     config.x,
                     config.y + config.radius + config.line_width + font_height / 2);
    }

    return ring;
}

void PageDashboardBase::_update_ring(RingObj& ring, uint16_t angle, const String& center_text) {
    if (!ring.arc) {
        return;
    }
    lv_arc_set_angles(ring.arc, 0, angle);
    if (ring.label_center) {
        lv_label_set_text(ring.label_center, center_text.c_str());
    }
}

uint16_t PageDashboardBase::_calc_angle(float value, const limited_data_f& range, uint16_t angle_full) {
    const float span = range.max - range.min;
    if (span <= 0.0f) {
        return 0;
    }
    float clamped = value;
    if (clamped < range.min) clamped = range.min;
    if (clamped > range.max) clamped = range.max;
    return (uint16_t)(angle_full * (clamped - range.min) / span);
}

void PageDashboardBase::_sync_hashrate() {
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

void PageDashboardBase::_sync_miner_diff_animation() {
    if (!_show_miner_diff || !_miner_img_dsc) {
        return;
    }

    const MinerStatus* st = MinerApp::instance().status();
    if (!st) {
        return;
    }

    uint32_t now = millis();
    if (now - _last_miner_anim_ms < 50) {
        return;
    }
    _last_miner_anim_ms = now;

    _miner_anim_step += 0.01f;
    lv_coord_t last_x = (lv_coord_t)(sinf(_miner_anim_step) * (_W / 2 - _miner_img_dsc->header.w / 2));

    if (_img_miner) {
        lv_obj_set_x(_img_miner, last_x);
    }
    if (_lb_diff) {
        String diff = formatNumber(st->diff.last, 4) + "\r" + formatNumber(st->diff.best_session, 4);
        lv_label_set_text(_lb_diff, diff.c_str());
        lv_obj_set_x(_lb_diff, last_x + 15);
    }
}

void PageDashboardBase::_sync_rings() {
    auto& app = MinerApp::instance();
    const BoardSpecConfig& spec = app.spec();
    const TempState& temp = app.temp();
    const MinerStatus* st = app.status();
    if (!st) {
        return;
    }

    const PowerTelemetry& pwr = app.pwr_tele();
    float power_w = pwr.vbus * pwr.ibus / 1000.0f / 1000.0f;
    float vcore_real = pwr.vcore / 1000.0f;
    float vcore_req = spec.asic.req_vcore / 1000.0f;

    uint16_t oc_angle = _calc_angle((float)spec.asic.req_frq, spec.ui.dashboard_page.performance.asic_freq_req, _ring_angle_full);
    uint16_t pwr_angle = _calc_angle(power_w, spec.ui.dashboard_page.power.power, _ring_angle_full);
    uint16_t vcore_req_angle = _calc_angle(vcore_req, spec.ui.dashboard_page.performance.vcore_req, _ring_angle_full);
    uint16_t vcore_real_angle = _calc_angle(vcore_real, spec.ui.dashboard_page.performance.vcore_measure, _ring_angle_full);
    uint16_t asic_temp_angle = _calc_angle(temp.asic, spec.ui.dashboard_page.heat.asic, _ring_angle_full);
    uint16_t vcore_temp_angle = _calc_angle(temp.vcore, spec.ui.dashboard_page.heat.vcore, _ring_angle_full);

    _update_ring(_ring_oc.obj, oc_angle, String(spec.asic.req_frq) + "M");
    _update_ring(_ring_pwr.obj, pwr_angle, formatNumber(power_w, 3) + "w");
    _update_ring(_ring_vc_req.obj, vcore_req_angle, formatNumber(vcore_req, 4) + "v");
    _update_ring(_ring_vc_real.obj, vcore_real_angle, formatNumber(vcore_real, 4) + "v");
    _update_ring(_ring_asic_temp.obj, asic_temp_angle, formatNumber(temp.asic, 2) + "°C");
    if (_show_vcore_temp_ring) {
        _update_ring(_ring_vcore_temp.obj, vcore_temp_angle, formatNumber(temp.vcore, 2) + "°C");
    }
}

void PageDashboardBase::_on_update() {
    if (!_parent) {
        return;
    }

    if (_ring_oc.obj.arc == nullptr) _ring_oc.obj = _draw_ring(_parent, _ring_oc.cfg);
    if (_ring_pwr.obj.arc == nullptr) _ring_pwr.obj = _draw_ring(_parent, _ring_pwr.cfg);
    if (_ring_vc_req.obj.arc == nullptr) _ring_vc_req.obj = _draw_ring(_parent, _ring_vc_req.cfg);
    if (_ring_vc_real.obj.arc == nullptr) _ring_vc_real.obj = _draw_ring(_parent, _ring_vc_real.cfg);
    if (_ring_asic_temp.obj.arc == nullptr) _ring_asic_temp.obj = _draw_ring(_parent, _ring_asic_temp.cfg);
    if (_show_vcore_temp_ring && _ring_vcore_temp.obj.arc == nullptr) {
        _ring_vcore_temp.obj = _draw_ring(_parent, _ring_vcore_temp.cfg);
    }

    _sync_miner_diff_animation();

    uint32_t now = millis();
    if (now - _last_ring_update_ms < 1000) {
        return;
    }
    _last_ring_update_ms = now;

    _sync_hashrate();
    _sync_rings();
}
