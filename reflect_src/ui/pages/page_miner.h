#pragma once

#include "ui/hal.h"
#include "app/app_state.h"
#include "lvgl.h"

// ============================================================================
// PageMinerBase — miner dashboard, exact legacy element set. Subclasses create
// the widgets (with bg image, fonts, coords, symbols) and call _finish_create()
// to bind the AppState::miner Observables.
// ============================================================================
class PageMinerBase : public UIPage {
public:
    void _on_update() override {}
    void destroy() override;

protected:
    // value labels (subset created per resolution; nullptr = not on this layout)
    lv_obj_t* _lb_hashrate   = nullptr;
    lv_obj_t* _lb_hr_unit    = nullptr;
    lv_obj_t* _lb_blk_hit    = nullptr;
    lv_obj_t* _lb_price      = nullptr;
    lv_obj_t* _lb_ver        = nullptr;
    lv_obj_t* _lb_power      = nullptr;
    lv_obj_t* _lb_ip         = nullptr;
    lv_obj_t* _lb_uptime_hms = nullptr;
    lv_obj_t* _lb_uptime_day = nullptr;
    lv_obj_t* _lb_diff       = nullptr;
    lv_obj_t* _lb_share      = nullptr;
    lv_obj_t* _lb_temp       = nullptr;
    lv_obj_t* _lb_fan        = nullptr;
    lv_obj_t* _lb_utc_time   = nullptr;
    lv_obj_t* _lb_wifi_symb  = nullptr;
    lv_obj_t* _lb_swarm_bd   = nullptr;
    lv_obj_t* _lb_swarm_hr   = nullptr;
    lv_obj_t* _lb_swarm_wk   = nullptr;

    lv_coord_t _W = 0, _H = 0;

    virtual void _create_dynamic(lv_obj_t* parent) = 0;
    void _finish_create();

private:
    static void _on_text (const String&   v, void* ctx);
    static void _on_color(const uint32_t& v, void* ctx);
};
