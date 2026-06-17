#pragma once
#include "ui/hal.h"
#include "app/app_state.h"
#include "lvgl.h"

// ============================================================================
// PageClockBase — large clock + hashrate/hits/price (exact legacy clock page).
// Binds AppState::clock (time/date) and AppState::miner (hr/unit/hits/price).
// ============================================================================
class PageClockBase : public UIPage {
public:
    void _on_update() override {}
    void destroy() override;

protected:
    lv_obj_t* _lb_time    = nullptr;
    lv_obj_t* _lb_date    = nullptr;
    lv_obj_t* _lb_hr      = nullptr;
    lv_obj_t* _lb_hr_unit = nullptr;
    lv_obj_t* _lb_hits    = nullptr;
    lv_obj_t* _lb_price   = nullptr;
    lv_coord_t _W = 0, _H = 0;

    virtual void _create_dynamic(lv_obj_t* parent) = 0;
    void _finish_create();

private:
    static void _on_text(const String& v, void* ctx);
};
