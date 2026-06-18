#pragma once

#include <vector>

#include "ui/hal.h"
#include "lvgl.h"

class PageMarketBase : public UIPage {
public:
    void _on_update() override;
    void destroy() override;

protected:
    lv_obj_t* _parent = nullptr;
    lv_obj_t* _bg = nullptr;
    lv_obj_t* _lb_hr = nullptr;
    lv_obj_t* _lb_hr_unit = nullptr;
    lv_obj_t* _lb_version = nullptr;
    lv_coord_t _W = 0;
    lv_coord_t _H = 0;
    const lv_font_t* _coin_font = nullptr;
    const lv_font_t* _hr_font = nullptr;
    const lv_font_t* _hr_unit_font = nullptr;
    const lv_font_t* _version_font = nullptr;
    lv_coord_t _col_dollar_x = 0;
    lv_coord_t _col_price_x = 0;
    lv_coord_t _col_change_x = 0;
    lv_coord_t _version_x = 0;
    lv_coord_t _version_y = 0;
    uint32_t _last_update_ms = 0;
    uint32_t _last_switch_ms = 0;
    uint16_t _max_per_page = 0;
    uint16_t _total_pages = 1;
    uint16_t _cur_page = 0;
    bool _inited = false;
    std::vector<lv_obj_t*> _lb_names;
    std::vector<lv_obj_t*> _lb_dollar_signs;
    std::vector<lv_obj_t*> _lb_prices;
    std::vector<lv_obj_t*> _lb_changes;

    virtual void _create_dynamic(lv_obj_t* parent) = 0;
    void _init_labels();
    void _sync_footer();
    void _sync_watchlist_page();
    static String _format_price(float price);
};
