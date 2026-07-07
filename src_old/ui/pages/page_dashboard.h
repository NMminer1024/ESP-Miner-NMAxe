#ifndef _PAGE_DASHBOARD_H
#define _PAGE_DASHBOARD_H

#include "ui/hal.h"
#include "board/board.h"
#include "ui/assets/fonts.h"
#include "ui/assets/images.h"
#include "utils/helper.h"
#include "lvgl.h"

class PageDashboardBase : public UIPage {
public:
    void destroy() override;
    void _on_update() override;

protected:
    struct RingConfig {
        lv_coord_t x = 0;
        lv_coord_t y = 0;
        lv_coord_t radius = 0;
        lv_coord_t line_width = 0;
        uint16_t angle_start = 0;
        uint16_t angle_end = 0;
        uint16_t angle_full = 0;
        lv_color_t bg_color{};
        lv_color_t indicator_color{};
        const char* center_text = nullptr;
        const lv_font_t* center_font = nullptr;
        lv_color_t center_text_color{};
        const char* title_text = nullptr;
        const lv_font_t* title_font = nullptr;
        lv_color_t title_text_color{};

        RingConfig() = default;
        RingConfig(lv_coord_t x_, lv_coord_t y_, lv_coord_t radius_, lv_coord_t line_width_,
                   uint16_t angle_start_, uint16_t angle_end_, uint16_t angle_full_,
                   lv_color_t bg_color_, lv_color_t indicator_color_,
                   const char* center_text_, const lv_font_t* center_font_, lv_color_t center_text_color_,
                   const char* title_text_, const lv_font_t* title_font_, lv_color_t title_text_color_)
            : x(x_), y(y_), radius(radius_), line_width(line_width_),
              angle_start(angle_start_), angle_end(angle_end_), angle_full(angle_full_),
              bg_color(bg_color_), indicator_color(indicator_color_),
              center_text(center_text_), center_font(center_font_), center_text_color(center_text_color_),
              title_text(title_text_), title_font(title_font_), title_text_color(title_text_color_) {}
    };

    struct RingObj {
        lv_obj_t* arc = nullptr;
        lv_obj_t* label_center = nullptr;
        lv_obj_t* label_title = nullptr;
    };

    struct RingBundle {
        RingObj obj;
        RingConfig cfg;
    };

    lv_obj_t* _parent = nullptr;
    lv_obj_t* _lb_hr = nullptr;
    lv_obj_t* _lb_hr_unit = nullptr;
    lv_obj_t* _img_miner = nullptr;
    lv_obj_t* _lb_diff = nullptr;

    const lv_img_dsc_t* _miner_img_dsc = nullptr;
    lv_coord_t _img_miner_x = 0;
    lv_coord_t _img_miner_y = 0;
    lv_coord_t _lb_hr_x = 0;
    lv_coord_t _lb_hr_y = 0;
    lv_coord_t _lb_hr_unit_x = 0;
    lv_coord_t _lb_hr_unit_y = 0;
    lv_coord_t _lb_diff_x = 0;
    lv_coord_t _lb_diff_y = 0;
    bool _show_miner_diff = false;
    bool _show_vcore_temp_ring = false;
    uint16_t _ring_angle_full = 230;
    uint32_t _last_ring_update_ms = 0;
    uint32_t _last_miner_anim_ms = 0;
    float _miner_anim_step = 0.0f;
    lv_coord_t _W = 0;
    lv_coord_t _H = 0;

    RingBundle _ring_oc;
    RingBundle _ring_pwr;
    RingBundle _ring_vc_req;
    RingBundle _ring_vc_real;
    RingBundle _ring_asic_temp;
    RingBundle _ring_vcore_temp;

    virtual void _create_dynamic(lv_obj_t* parent) = 0;

    RingObj _draw_ring(lv_obj_t* parent, const RingConfig& config);
    void _update_ring(RingObj& ring, uint16_t angle, const String& center_text);
    void _sync_hashrate();
    void _sync_miner_diff_animation();
    void _sync_rings();

    static uint16_t _calc_angle(float value, const limited_data_f& range, uint16_t angle_full);
};

#endif
