#include "ui/pages/page_setting.h"

#include "app/application.h"
#include "ui/ui_manager.h"
#include "nvs/nvs_config.h"
#include "utils/reboot_log/reboot_log.h"

void PageSwarmBase::destroy() {
    _lb_workers = nullptr;
    _lb_total_hr = nullptr;
    _lb_best_session = nullptr;
    _lb_best_ever = nullptr;
}

void PageSwarmBase::_on_update() {
    SwarmState* swarm = MinerApp::instance().swarm();
    if (!swarm || !swarm->mutex) {
        return;
    }
    if (xSemaphoreTake(swarm->mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
        return;
    }

    if (_lb_workers) {
        lv_label_set_text_fmt(_lb_workers, "%lu", (unsigned long)swarm->total_workers);
    }
    if (_lb_total_hr) {
        lv_label_set_text_fmt(_lb_total_hr, "%s", formatNumber(swarm->total_hr, 3).c_str());
    }
    if (_lb_best_session) {
        lv_label_set_text_fmt(_lb_best_session, "%s", formatNumber(swarm->best_session_bd, 4).c_str());
    }
    if (_lb_best_ever) {
        lv_label_set_text_fmt(_lb_best_ever, "%s", formatNumber(swarm->best_ever_bd, 4).c_str());
    }

    xSemaphoreGive(swarm->mutex);
}

void PageSettingBase::destroy() {
    _parent = nullptr;
    _slider_brightness = nullptr;
    _dropdown_vcore = nullptr;
    _dropdown_freq = nullptr;
    _dropdown_saver = nullptr;
    _checkbox_auto_roll = nullptr;
    _checkbox_flip = nullptr;
    _btn_save = nullptr;
    _btn_restart = nullptr;
    _keyboard = nullptr;
    _last_enter_serial = 0;
}

uint16_t PageSettingBase::_build_dropdown_with_custom(lv_obj_t* dropdown,
                                                      const std::vector<work_option_t>& spec_opts,
                                                      uint16_t cur_val,
                                                      const char* custom_unit) {
    bool found = false;
    for (const auto& item : spec_opts) {
        if (item.value == cur_val) {
            found = true;
            break;
        }
    }

    String s;
    uint16_t sel_idx = 0;
    uint16_t idx = 0;
    bool inserted = false;
    for (size_t i = 0; i < spec_opts.size(); ++i) {
        if (!found && !inserted && cur_val < spec_opts[i].value) {
            if (s.length() > 0) s += "\n";
            s += String(cur_val) + " " + custom_unit + "*";
            sel_idx = idx++;
            inserted = true;
        }
        if (s.length() > 0) s += "\n";
        s += spec_opts[i].name;
        if (found && spec_opts[i].value == cur_val) {
            sel_idx = idx;
        }
        idx++;
    }
    if (!found && !inserted) {
        if (s.length() > 0) s += "\n";
        s += String(cur_val) + " " + custom_unit + "*";
        sel_idx = idx;
    }
    lv_dropdown_set_options(dropdown, s.c_str());
    lv_dropdown_set_selected(dropdown, sel_idx);
    return sel_idx;
}

void PageSettingBase::_enable_label_recolor_recursive(lv_obj_t* obj) {
    if (!obj) return;
    if (lv_obj_check_type(obj, &lv_label_class)) {
        lv_label_set_recolor(obj, true);
    }
    uint32_t child_cnt = lv_obj_get_child_cnt(obj);
    for (uint32_t i = 0; i < child_cnt; ++i) {
        _enable_label_recolor_recursive(lv_obj_get_child(obj, i));
    }
}

void PageSettingBase::_toast_timer_cb(lv_timer_t* t) {
    lv_obj_t* toast = static_cast<lv_obj_t*>(t->user_data);
    if (toast) {
        lv_obj_del(toast);
    }
    lv_timer_del(t);
}

void PageSettingBase::_show_toast(const char* msg, uint32_t duration_ms) {
    lv_obj_t* toast = lv_obj_create(lv_scr_act());
    lv_coord_t max_w = lv_obj_get_width(lv_scr_act()) - 24;
    if (max_w < 120) {
        max_w = 120;
    }
    lv_obj_set_width(toast, max_w);
    lv_obj_set_height(toast, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(toast, lv_color_hex(0x009900), 0);
    lv_obj_set_style_bg_opa(toast, LV_OPA_80, 0);
    lv_obj_set_style_radius(toast, 8, 0);
    lv_obj_set_style_pad_all(toast, 6, 0);
    lv_obj_set_style_border_width(toast, 0, 0);
    lv_obj_clear_flag(toast, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* lbl = lv_label_create(toast);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl, max_w - 12);
    lv_label_set_text(lbl, msg);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_align(toast, LV_ALIGN_CENTER, 0, -10);
    lv_timer_create(_toast_timer_cb, duration_ms, toast);
}

void PageSettingBase::_keyboard_event_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* keyboard = lv_event_get_target(e);
    if (code == LV_EVENT_CANCEL || code == LV_EVENT_READY) {
        lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

void PageSettingBase::_slider_event_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }
    auto& app = MinerApp::instance();
    app.pref().screen.brightness = (uint8_t)lv_slider_get_value(lv_event_get_target(e));
    if (app.brightness_update_xsem()) {
        xSemaphoreGive(app.brightness_update_xsem());
    }
}

void PageSettingBase::_checkbox_event_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }
}

void PageSettingBase::_msgbox_restart_cb(lv_event_t* e) {
    lv_obj_t* mbox = lv_event_get_current_target(e);
    uint16_t btn = lv_msgbox_get_active_btn(mbox);
    if (btn == 0) {
        reboot_intent_set(REBOOT_INTENT_LCD_USER_RESTART,
                          "user confirmed restart on LCD setting page");
        xSemaphoreGive(MinerApp::instance().reboot_xsem());
    }
    lv_msgbox_close(mbox);
}

void PageSettingBase::_save_settings() {
    auto& app = MinerApp::instance();
    uint8_t brightness = (uint8_t)lv_slider_get_value(_slider_brightness);
    nvs_config_set_u8(NVS_CONFIG_SCREEN_BRIGHTNESS, brightness);
    app.pref().screen.brightness = brightness;
    if (app.brightness_update_xsem()) {
        xSemaphoreGive(app.brightness_update_xsem());
    }

    bool auto_roll = lv_obj_has_state(_checkbox_auto_roll, LV_STATE_CHECKED);
    app.pref().screen.auto_rolling = auto_roll;
    nvs_config_set_u8(NVS_CONFIG_AUTO_SCREEN, auto_roll ? 1 : 0);

    bool flip = lv_obj_has_state(_checkbox_flip, LV_STATE_CHECKED);
    bool flip_changed = (app.pref().screen.flip != flip);
    nvs_config_set_u8(NVS_CONFIG_FLIP_SCREEN, flip ? 1 : 0);

    if (_dropdown_vcore) {
        char vcore_buf[32] = "";
        lv_dropdown_get_selected_str(_dropdown_vcore, vcore_buf, sizeof(vcore_buf));
        uint16_t vcore = (uint16_t)atoi(vcore_buf);
        app.spec_mut().asic.req_vcore = vcore;
        if (app.power()) app.power()->set_vcore_voltage(vcore);
        nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, vcore);
    }

    if (_dropdown_freq) {
        char freq_buf[32] = "";
        lv_dropdown_get_selected_str(_dropdown_freq, freq_buf, sizeof(freq_buf));
        uint16_t freq = (uint16_t)atoi(freq_buf);
        app.spec_mut().asic.req_frq = freq;
        nvs_config_set_u16(NVS_CONFIG_ASIC_FREQ, freq);
        if (app.miner() && !app.miner()->request_asic_frequency(freq)) {
            // no-op: legacy just warned
        }
    }

    if (_dropdown_saver) {
        static const uint32_t SAVER_VALS[] = {0, 30, 60, 300, 900, 1800, 3600, 7200, 21600};
        uint16_t idx = lv_dropdown_get_selected(_dropdown_saver);
        uint32_t tmo = (idx < 9) ? SAVER_VALS[idx] : 0;
        uint8_t en = (tmo > 0) ? 1 : 0;
        nvs_config_set_u8(NVS_CONFIG_SCREEN_SAVER_ENABLE, en);
        nvs_config_set_u32(NVS_CONFIG_SCREEN_SAVER_TIMEOUT, tmo);
        app.pref().screen.saver_enable = en;
        app.pref().screen.saver_timeout = tmo;
    }

    _show_toast(flip_changed ? "Saved! Reboot to apply Flip Screen."
                             : "Settings Saved!",
                2500);
}

void PageSettingBase::_msgbox_save_cb(lv_event_t* e) {
    lv_obj_t* mbox = lv_event_get_current_target(e);
    uint16_t btn = lv_msgbox_get_active_btn(mbox);
    if (btn == 0) {
        auto* self = static_cast<PageSettingBase*>(lv_event_get_user_data(e));
        if (self) {
            self->_save_settings();
        }
    }
    lv_msgbox_close(mbox);
}

void PageSettingBase::_button_event_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED) {
        return;
    }

    auto* self = static_cast<PageSettingBase*>(lv_event_get_user_data(e));
    lv_obj_t* btn = lv_event_get_target(e);
    if (!self) {
        return;
    }

    if (btn == self->_btn_save) {
        uint8_t brightness = (uint8_t)lv_slider_get_value(self->_slider_brightness);
        bool auto_roll = lv_obj_has_state(self->_checkbox_auto_roll, LV_STATE_CHECKED);
        bool flip = lv_obj_has_state(self->_checkbox_flip, LV_STATE_CHECKED);
        char freq_buf[32] = "N/A";
        char vcore_buf[32] = "N/A";
        char saver_buf[16] = "never";

        if (self->_dropdown_freq) lv_dropdown_get_selected_str(self->_dropdown_freq, freq_buf, sizeof(freq_buf));
        if (self->_dropdown_vcore) lv_dropdown_get_selected_str(self->_dropdown_vcore, vcore_buf, sizeof(vcore_buf));
        if (self->_dropdown_saver) lv_dropdown_get_selected_str(self->_dropdown_saver, saver_buf, sizeof(saver_buf));

        static char confirm_msg[512];
        snprintf(confirm_msg, sizeof(confirm_msg),
                 "#7F8FA6 Brightness:# #00E5FF %d%%#\n"
                 "#7F8FA6 Frequency:# #00E5FF %s#\n"
                 "#7F8FA6 Vcore:# #00E5FF %s#\n"
                 "#7F8FA6 Screen Roll:# #00E5FF %s#\n"
                 "#7F8FA6 Screen Flip:# #00E5FF %s# #7F8FA6(restart)#\n"
                 "#7F8FA6 Screen Saver:# #00E5FF %s#",
                 brightness, freq_buf, vcore_buf,
                 auto_roll ? "ON" : "OFF",
                 flip ? "ON" : "OFF",
                 saver_buf);

        static const char* btns[] = {"OK", "Cancel", ""};
        lv_obj_t* mbox = lv_msgbox_create(nullptr, "Save Settings?", confirm_msg, btns, false);
        _enable_label_recolor_recursive(mbox);
        lv_obj_add_event_cb(mbox, _msgbox_save_cb, LV_EVENT_VALUE_CHANGED, self);
        lv_obj_center(mbox);
    } else if (btn == self->_btn_restart) {
        static const char* btns[] = {"Yes", "No", ""};
        lv_obj_t* mbox = lv_msgbox_create(nullptr, "", "Restart Miner?", btns, false);
        lv_obj_add_event_cb(mbox, _msgbox_restart_cb, LV_EVENT_VALUE_CHANGED, self);
        lv_obj_center(mbox);
    }
}

void PageSettingBase::_reload_from_runtime() {
    auto& app = MinerApp::instance();

    if (_slider_brightness) {
        lv_slider_set_value(_slider_brightness,
                            nvs_config_get_u8(NVS_CONFIG_SCREEN_BRIGHTNESS, app.spec().preference.screen.brightness),
                            LV_ANIM_OFF);
    }

    if (_dropdown_vcore) {
        const std::vector<work_option_t>& vc_opts = app.spec().ui.setting_page.vc;
        uint16_t cur = nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE, app.spec().asic.req_vcore);
        _build_dropdown_with_custom(_dropdown_vcore, vc_opts, cur, "mV");
    }

    if (_dropdown_freq) {
        const std::vector<work_option_t>& oc_opts = app.spec().ui.setting_page.oc;
        uint16_t cur = nvs_config_get_u16(NVS_CONFIG_ASIC_FREQ, app.spec().asic.req_frq);
        _build_dropdown_with_custom(_dropdown_freq, oc_opts, cur, "MHz");
    }

    if (_dropdown_saver) {
        static const uint32_t SAVER_VALS[] = {0, 30, 60, 300, 900, 1800, 3600, 7200, 21600};
        uint8_t en = nvs_config_get_u8(NVS_CONFIG_SCREEN_SAVER_ENABLE, app.spec().preference.screen.saver_enable);
        uint32_t tmo = nvs_config_get_u32(NVS_CONFIG_SCREEN_SAVER_TIMEOUT, app.spec().preference.screen.saver_timeout);
        uint16_t idx = 0;
        if (en) {
            for (int i = 1; i < 9; ++i) {
                if (SAVER_VALS[i] == tmo) {
                    idx = (uint16_t)i;
                    break;
                }
            }
        }
        lv_dropdown_set_selected(_dropdown_saver, idx);
    }

    if (_checkbox_auto_roll) {
        if (nvs_config_get_u8(NVS_CONFIG_AUTO_SCREEN, app.spec().preference.screen.auto_rolling)) {
            lv_obj_add_state(_checkbox_auto_roll, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(_checkbox_auto_roll, LV_STATE_CHECKED);
        }
    }

    if (_checkbox_flip) {
        if (nvs_config_get_u8(NVS_CONFIG_FLIP_SCREEN, app.spec().preference.screen.flip)) {
            lv_obj_add_state(_checkbox_flip, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(_checkbox_flip, LV_STATE_CHECKED);
        }
    }
}

void PageSettingBase::_on_update() {
    uint32_t enter_serial = UIManager::instance().page_enter_serial();
    if (enter_serial != _last_enter_serial) {
        _last_enter_serial = enter_serial;
        _reload_from_runtime();
    }
}
