#pragma once
#include "ui/hal.h"
#include "app/app_state.h"
#include "lvgl.h"

// ============================================================================
// PageHr_healthBase — hashrate health summary (rate / efficiency / shares / best).
// ============================================================================
class PageHr_healthBase : public UIPage {
public:
    void _on_update() override {}
    void destroy() override;

protected:
    lv_obj_t* _lb_hashrate   = nullptr;
    lv_obj_t* _lb_efficiency = nullptr;
    lv_obj_t* _lb_shares     = nullptr;
    lv_obj_t* _lb_best_diff  = nullptr;
    lv_coord_t _W = 0, _H = 0;

    virtual void _create_dynamic(lv_obj_t* parent) = 0;
    void _finish_create();

private:
    static void _on_text(const String& v, void* ctx);
};
