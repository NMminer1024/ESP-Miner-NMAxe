#pragma once

#include <vector>

#include "ui/hal.h"
#include "app/app_state.h"
#include "board/board.h"
#include "lvgl.h"

class PageSwarmBase : public UIPage {
public:
    void _on_update() override;
    void destroy() override;

protected:
    lv_obj_t* _lb_workers = nullptr;
    lv_obj_t* _lb_total_hr = nullptr;
    lv_obj_t* _lb_best_session = nullptr;
    lv_obj_t* _lb_best_ever = nullptr;
    lv_coord_t _W = 0;
    lv_coord_t _H = 0;

    virtual void _create_dynamic(lv_obj_t* parent) = 0;
};

class PageSettingBase : public UIPage {
public:
    void _on_update() override;
    void destroy() override;

protected:
    lv_obj_t* _parent = nullptr;
    lv_obj_t* _slider_brightness = nullptr;
    lv_obj_t* _dropdown_vcore = nullptr;
    lv_obj_t* _dropdown_freq = nullptr;
    lv_obj_t* _dropdown_saver = nullptr;
    lv_obj_t* _checkbox_auto_roll = nullptr;
    lv_obj_t* _checkbox_flip = nullptr;
    lv_obj_t* _btn_save = nullptr;
    lv_obj_t* _btn_restart = nullptr;
    lv_obj_t* _keyboard = nullptr;
    lv_coord_t _W = 0;
    lv_coord_t _H = 0;
    uint32_t _last_enter_serial = 0;

    virtual void _create_dynamic(lv_obj_t* parent) = 0;
    void _reload_from_runtime();
    void _save_settings();
    static uint16_t _build_dropdown_with_custom(lv_obj_t* dropdown,
                                                const std::vector<work_option_t>& spec_opts,
                                                uint16_t cur_val,
                                                const char* custom_unit);
    static void _enable_label_recolor_recursive(lv_obj_t* obj);
    static void _show_toast(const char* msg, uint32_t duration_ms = 2000);
    static void _keyboard_event_cb(lv_event_t* e);
    static void _slider_event_cb(lv_event_t* e);
    static void _checkbox_event_cb(lv_event_t* e);
    static void _button_event_cb(lv_event_t* e);
    static void _msgbox_save_cb(lv_event_t* e);
    static void _msgbox_restart_cb(lv_event_t* e);
    static void _toast_timer_cb(lv_timer_t* t);
};
