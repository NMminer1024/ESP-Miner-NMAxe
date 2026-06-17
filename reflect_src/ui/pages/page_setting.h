#pragma once
#include "ui/hal.h"
#include "app/app_state.h"
#include "lvgl.h"

// ============================================================================
// PageSettingBase — swarm aggregate stats + this device's IP (text rows).
// ============================================================================
class PageSettingBase : public UIPage {
public:
    void _on_update() override {}
    void destroy() override;

protected:
    lv_obj_t* _lb_workers   = nullptr;
    lv_obj_t* _lb_total_hr  = nullptr;
    lv_obj_t* _lb_best_diff = nullptr;
    lv_obj_t* _lb_neighbors = nullptr;
    lv_obj_t* _lb_ip        = nullptr;
    lv_coord_t _W = 0, _H = 0;

    virtual void _create_dynamic(lv_obj_t* parent) = 0;
    void _finish_create();

private:
    static void _on_text(const String& v, void* ctx);
};
