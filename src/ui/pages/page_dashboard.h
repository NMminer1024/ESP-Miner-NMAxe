#pragma once
#include "ui/hal.h"
#include "app/app_state.h"
#include "lvgl.h"

// ============================================================================
// PageDashboardBase — power / thermal / performance readouts (text rows).
// ============================================================================
class PageDashboardBase : public UIPage {
public:
    void _on_update() override {}
    void destroy() override;

protected:
    lv_obj_t* _lb_power      = nullptr;
    lv_obj_t* _lb_vbus       = nullptr;
    lv_obj_t* _lb_ibus       = nullptr;
    lv_obj_t* _lb_asic_temp  = nullptr;
    lv_obj_t* _lb_vcore_temp = nullptr;
    lv_obj_t* _lb_freq       = nullptr;
    lv_obj_t* _lb_vcore      = nullptr;
    lv_coord_t _W = 0, _H = 0;

    virtual void _create_dynamic(lv_obj_t* parent) = 0;
    void _finish_create();

private:
    static void _on_text(const String& v, void* ctx);
};
