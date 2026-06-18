#pragma once

#include <math.h>

#include "app/application.h"
#include "board/board.h"
#include "lvgl.h"
#include "ui/assets/fonts.h"
#include "ui/hal.h"
#include "utils/helper.h"

class PageHr_healthBase : public UIPage {
public:
    void destroy() override;
    void _on_update() override;

protected:
    struct PieSectorConfig {
        uint16_t angle = 0;
        lv_color_t color{};
        const char* label = nullptr;

        PieSectorConfig() = default;
        PieSectorConfig(uint16_t angle_, lv_color_t color_, const char* label_)
            : angle(angle_), color(color_), label(label_) {}
    };

    struct PieChart {
        lv_obj_t* arcs[4] = {nullptr, nullptr, nullptr, nullptr};
        lv_obj_t* labels[4] = {nullptr, nullptr, nullptr, nullptr};
        uint8_t sector_count = 0;
        lv_coord_t center_x = 0;
        lv_coord_t center_y = 0;
        lv_coord_t radius = 0;
    };

    lv_obj_t* _parent = nullptr;
    lv_obj_t* _lb_hr = nullptr;
    lv_obj_t* _lb_hr_unit = nullptr;
    lv_obj_t* _lb_scale = nullptr;
    lv_obj_t* _chart = nullptr;
    lv_chart_series_t* _chart_series = nullptr;

    lv_coord_t _W = 0;
    lv_coord_t _H = 0;
    lv_coord_t _lb_hr_x = 0;
    lv_coord_t _lb_hr_y = 0;
    lv_coord_t _lb_hr_unit_x = 0;
    lv_coord_t _lb_hr_unit_y = 0;
    lv_coord_t _lb_scale_x = 0;
    lv_coord_t _lb_scale_y = 0;
    lv_coord_t _chart_x = 0;
    lv_coord_t _chart_y = 0;
    lv_coord_t _chart_w = 0;
    lv_coord_t _chart_h = 0;
    const lv_font_t* _hr_font = nullptr;
    const lv_font_t* _hr_unit_font = nullptr;
    const lv_font_t* _scale_font = nullptr;
    const lv_font_t* _chart_tick_font = nullptr;
    bool _show_asic_pie = false;
    PieChart _pie{};
    PieSectorConfig _pie_cfg[4]{};

    virtual void _create_dynamic(lv_obj_t* parent) = 0;

    void _create_chart();
    void _create_pie_chart(uint8_t sector_count);
    void _update_pie_chart(const uint16_t* angles);

private:
    uint32_t _last_update_ms = 0;
    double _last_hashrate = -1.0;
};
