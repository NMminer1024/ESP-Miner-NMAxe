#pragma once

#include <math.h>

#include "app/application.h"
#include "board/board.h"
#include "lvgl.h"
#include "ui/assets/fonts.h"
#include "ui/hal.h"
#include "utils/helper.h"

class PageLuckyBase : public UIPage {
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

    // ── Lucky Statistics (SESSION data, mirrors the AxeOS card) ────────────
    //    Source: st.proximity_history — the accepted shares of this power-on
    //    session (RAM deque <=36, lost on reboot). The LCD draws only
    //    _lucky_points bars: the session top-3 shares (never dropped) plus
    //    the newest remaining samples, then re-sorted into strict arrival
    //    (time) order — same x-axis ordering as the AxeOS card. Gold/silver/
    //    bronze bars = session top-3, red line = network difficulty.
    static const int LUCKY_SAMPLES = 36;
    int         _lucky_points = 10;             // bars drawn on this layout
    int         _lucky_axis_ticks = 6;          // max decade tick labels on the Y axis
    lv_obj_t*   _axis_lbs[8]{};                 // log-decade tick labels (1K/1M/1G...)
    lv_obj_t*   _lb_title = nullptr;
    lv_obj_t*   _chart = nullptr;          // BAR chart (bottom): share-diff bars
    lv_obj_t*   _chart_line = nullptr;     // LINE chart (overlay): net-diff line
    lv_chart_series_t* _series_share = nullptr;
    lv_chart_series_t* _series_net = nullptr;
    int         _medal_slot[3] = {-1, -1, -1};   // deque slots of session top-3
    // _chart_x doubles as the left gutter reserved for Y-axis tick labels.
    lv_coord_t  _chart_x = 0, _chart_y = 0, _chart_w = 0, _chart_h = 0;
    lv_coord_t  _pad_side = 4;          // chart inner side padding (both charts)
    lv_coord_t  _bar_gap = 1;           // gap between share bars (MAIN pad_column)
    bool        _lucky_bar_values = false;   // show share value above top-3 bars
    lv_obj_t*   _val_lbs[3]{};               // share-diff labels above medal bars
    lv_obj_t*   _lb_legend = nullptr;
    // Snapshot of the deque for change detection + drawing (copied under mutex).
    float       _snap_share[LUCKY_SAMPLES]{};
    float       _snap_net[LUCKY_SAMPLES]{};
    uint64_t    _snap_epoch[LUCKY_SAMPLES]{};
    int         _snap_count = 0;
    uint64_t    _snap_last_epoch = 0;

    lv_coord_t _W = 0;
    lv_coord_t _H = 0;
    lv_coord_t _lb_hr_x = 0;
    lv_coord_t _lb_hr_y = 0;
    lv_coord_t _lb_hr_unit_x = 0;
    lv_coord_t _lb_hr_unit_y = 0;
    const lv_font_t* _hr_font = nullptr;
    const lv_font_t* _hr_unit_font = nullptr;
    bool _show_asic_pie = false;
    PieChart _pie{};
    PieSectorConfig _pie_cfg[4]{};

    virtual void _create_dynamic(lv_obj_t* parent) = 0;

    void _create_pie_chart(uint8_t sector_count);
    void _update_pie_chart(const uint16_t* angles);

    // Lucky Statistics helpers (chart created lazily in _on_update once the
    // layout has provided _chart_* geometry and _rank_font).
    void _create_lucky_chart();
    void _update_lucky(const MinerStatus* st);
    // Recolors bars per rank: #1 gold / #2 silver / #3 bronze, rest dim gold.
    static void _lucky_bar_cb(lv_event_t* e);

private:
    uint32_t _last_update_ms = 0;
    double _last_hashrate = -1.0;
};
