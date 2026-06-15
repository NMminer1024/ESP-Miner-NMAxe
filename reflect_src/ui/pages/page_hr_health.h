#pragma once
#include "ui/hal.h"
#include "app/app_state.h"
#include "lvgl.h"

class Pagehr_healthBase : public UIPage {
public:
    void _on_update() override {}
    void destroy() override { _bar_progress = _lb_details = nullptr; }
protected:
    lv_obj_t* _bar_progress = nullptr;
    lv_obj_t* _lb_details   = nullptr;
    lv_coord_t _W = 0, _H = 0;
    virtual void _create_dynamic(lv_obj_t* parent) = 0;
};
