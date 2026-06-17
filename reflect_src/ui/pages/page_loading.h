#pragma once

#include "ui/hal.h"
#include "app/app_state.h"
#include "lvgl.h"

// ============================================================================
// PageLoadingBase - boot loading screen abstract base class
// ============================================================================
class PageLoadingBase : public UIPage {
public:
    void _on_update() override;
    void destroy() override;

protected:
    lv_obj_t* _bar_progress = nullptr;
    lv_obj_t* _lb_progress  = nullptr;
    lv_obj_t* _lb_details   = nullptr;
    lv_obj_t* _lb_ip        = nullptr;
    lv_obj_t* _lb_pool      = nullptr;

    lv_coord_t _W = 0;
    lv_coord_t _H = 0;
    float      _display_progress = 0.0f;
    int32_t    _target_progress  = 0;
    const lv_font_t* _ip_font    = nullptr;
    const lv_font_t* _pool_font  = nullptr;
    lv_coord_t _ip_max_width     = 0;
    lv_coord_t _pool_max_width   = 0;

    virtual void _create_dynamic(lv_obj_t* parent) = 0;
    void _finish_create();
    void _set_scrolling_label_text(lv_obj_t* lbl, const String& v,
                                   const lv_font_t* font, lv_coord_t max_width);

private:
    static void _on_progress(const int32_t& v, void* ctx);
    static void _on_text(const String& v, void* ctx);
    static void _on_color(const uint32_t& v, void* ctx);
    static void _on_ip_text(const String& v, void* ctx);
    static void _on_pool_text(const String& v, void* ctx);
};
