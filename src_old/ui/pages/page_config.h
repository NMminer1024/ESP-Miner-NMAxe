#pragma once

#include <WiFi.h>

#include "app/app_state.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "lvgl.h"
#include "ui/hal.h"

class PageConfigBase : public UIPage {
public:
    void destroy() override;

protected:
    lv_obj_t* _parent = nullptr;
    lv_obj_t* _lb_version = nullptr;
    lv_obj_t* _lb_timeout = nullptr;
    lv_obj_t* _lb_logo = nullptr;
    lv_coord_t _W = 0;
    lv_coord_t _H = 0;

    virtual void _create_dynamic(lv_obj_t* parent) = 0;
};

class PageConfigQrBase : public PageConfigBase {
public:
    void destroy() override;

protected:
    lv_obj_t* _lb_config = nullptr;
    lv_obj_t* _qr = nullptr;
    uint32_t _last_timeout_ms = 0;
    uint8_t _timeout_anim = 0;

    void _on_update() override;
};

class PageConfigWifiListBase : public PageConfigBase {
public:
    void destroy() override;

protected:
    enum class Phase : uint8_t { LIST = 0, PWD_INPUT, CONFIRM, SAVING };

    lv_obj_t* _ws_cont = nullptr;
    lv_obj_t* _ws_ssid_list = nullptr;
    lv_obj_t* _ws_scan_lbl = nullptr;
    lv_obj_t* _ws_pwd_ta = nullptr;
    lv_obj_t* _ws_kbd = nullptr;
    lv_obj_t* _ws_key_popup[4] = {nullptr, nullptr, nullptr, nullptr};
    Phase _phase = Phase::LIST;
    Phase _last_phase = (Phase)0xFF;
    int16_t _scan_status = -2;
    String _pending_ssid;
    String _pending_pwd;
    uint32_t _last_timeout_ms = 0;

    void _on_update() override;
    void _destroy_phase_widgets();
    void _build_phase();
    void _build_list_phase();
    void _build_pwd_input_phase();
    void _build_confirm_phase();
    void _build_saving_phase();
    void _poll_scan_results();
    void _show_key_popups(const char* txt, lv_coord_t touch_x, lv_coord_t touch_y);
    void _clear_key_popups();
};
