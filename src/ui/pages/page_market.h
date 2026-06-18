#pragma once
#include "ui/hal.h"
#include "app/app_state.h"
#include "lvgl.h"

// ============================================================================
// PageMarketBase — main coin symbol + price + 24h change (resolution-independent)
// ============================================================================
class PageMarketBase : public UIPage {
public:
    void _on_update() override {}
    void destroy() override;

protected:
    lv_obj_t* _lb_symbol = nullptr;
    lv_obj_t* _lb_price  = nullptr;
    lv_obj_t* _lb_change = nullptr;
    lv_coord_t _W = 0, _H = 0;

    virtual void _create_dynamic(lv_obj_t* parent) = 0;
    void _finish_create();

private:
    static void _on_text (const String&   v, void* ctx);
    static void _on_color(const uint32_t& v, void* ctx);
};
