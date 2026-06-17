#pragma once
#include "ui/hal.h"
#include "app/app_state.h"
#include "lvgl.h"

// ============================================================================
// PageClockBase — large local-time clock + date line (resolution-independent)
//
//   Data is Observable-driven: AppState::clock.{time_str,date_str} assignments
//   dispatch to the LVGL thread and update the labels. Subclasses create the
//   widgets (with fonts/positions) and call _finish_create() to subscribe.
// ============================================================================
class PageClockBase : public UIPage {
public:
    void _on_update() override {}
    void destroy() override;

protected:
    lv_obj_t* _lb_time = nullptr;
    lv_obj_t* _lb_date = nullptr;
    lv_coord_t _W = 0, _H = 0;

    virtual void _create_dynamic(lv_obj_t* parent) = 0;
    void _finish_create();

private:
    static void _on_text(const String& v, void* ctx);
};
