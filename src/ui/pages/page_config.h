#pragma once
#include "ui/hal.h"
#include "app/app_state.h"
#include "lvgl.h"

// ============================================================================
// PageConfigBase — WiFi AP setup page: SSID to join, captive-portal IP, and the
// AP config-window countdown (resolution-independent).
// ============================================================================
class PageConfigBase : public UIPage {
public:
    void _on_update() override {}
    void destroy() override;

protected:
    lv_obj_t* _lb_ssid    = nullptr;
    lv_obj_t* _lb_ip      = nullptr;
    lv_obj_t* _lb_timeout = nullptr;
    lv_coord_t _W = 0, _H = 0;

    virtual void _create_dynamic(lv_obj_t* parent) = 0;
    void _finish_create();

private:
    static void _on_text(const String& v, void* ctx);
};
