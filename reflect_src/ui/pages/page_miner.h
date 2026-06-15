#pragma once

#include "ui/hal.h"
#include "app/app_state.h"
#include "lvgl.h"

// ============================================================================
// PageMinerBase ¡ª miner dashboard abstract base class (resolution-independent)
//
//   Data update is purely Observable-driven (no polling):
//     AppState::instance().miner field assignment ¡ú Observable lv_async_call
//     ¡ú LVGL thread callback updates corresponding widget.
//
//   Subclasses only implement create() (with coordinates) and _create_dynamic().
//   create() calls _finish_create() at end to subscribe all Observables.
// ============================================================================
class PageMinerBase : public UIPage {
public:
    void _on_update() override {}
    void destroy() override;

protected:
    // ©¤©¤ Top bar ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤
    lv_obj_t* _lb_time_str      = nullptr;
    lv_obj_t* _lb_uptime_day    = nullptr;
    lv_obj_t* _lb_uptime_hms    = nullptr;
    lv_obj_t* _lb_hasrate       = nullptr;
    lv_obj_t* _lb_hasrate_unit  = nullptr;
    lv_obj_t* _lb_power         = nullptr;
    lv_obj_t* _lb_vcore_temp    = nullptr;
    lv_obj_t* _lb_asic_temp     = nullptr;

    // ©¤©¤ Middle section ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤
    lv_obj_t* _lb_net_diff      = nullptr;
    lv_obj_t* _lb_best_diff     = nullptr;
    lv_obj_t* _lb_shares        = nullptr;
    lv_obj_t* _lb_blk_hit       = nullptr;
    lv_obj_t* _lb_job_count     = nullptr;

    // ©¤©¤ Bottom info ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤
    lv_obj_t* _lb_ver           = nullptr;
    lv_obj_t* _lb_ip            = nullptr;
    lv_obj_t* _lb_rssi          = nullptr;

    // ©¤©¤ Swarm bar ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤
    lv_obj_t* _lb_swarm_bd      = nullptr;
    lv_obj_t* _lb_swarm_hr      = nullptr;
    lv_obj_t* _lb_swarm_workers = nullptr;

    // ©¤©¤ Screen dimensions (set by subclass in create()) ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤
    lv_coord_t _W = 0;
    lv_coord_t _H = 0;

    // Subclass implements actual widget layout at its resolution
    virtual void _create_dynamic(lv_obj_t* parent) = 0;

    // Called at end of subclass create(): subscribes to AppState::miner Observables
    void _finish_create();

private:
    static void _on_text (const String&   v, void* ctx);
    static void _on_color(const uint32_t& v, void* ctx);
};
