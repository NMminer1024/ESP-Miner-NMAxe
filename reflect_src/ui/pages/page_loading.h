#pragma once

#include "ui/hal.h"
#include "app/app_state.h"
#include "lvgl.h"

// ============================================================================
// PageLoadingBase ¡ª boot loading screen abstract base class
// ============================================================================
class PageLoadingBase : public UIPage {
public:
    void _on_update() override {}
    void destroy() override;

protected:
    lv_obj_t* _bar_progress   = nullptr;
    lv_obj_t* _lb_details     = nullptr;
    lv_obj_t* _lb_ip          = nullptr;

    lv_coord_t _W = 0;
    lv_coord_t _H = 0;

    virtual void _create_dynamic(lv_obj_t* parent) = 0;
    void _finish_create();

private:
    static void _on_progress(const int32_t&  v, void* ctx);
    static void _on_text   (const String&   v, void* ctx);
    static void _on_color  (const uint32_t& v, void* ctx);
};
