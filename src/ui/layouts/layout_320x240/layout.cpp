// What: 320x240 layout registry implementation for QAxe++.
// Why: QAxe++ needs a real catalog so loading and page navigation exercise the
// correct product path during BSP bring-up.
// Role: Owns shared 320x240 metrics and static page instances.
// Benefit: The first QAxe++ target can boot without pretending to be a 240x135
// board while exact legacy 320x240 pages remain isolated follow-up work.
#include "ui/layouts/layout_320x240/layout.h"

#include "ui/assets/loading/background_320x240.h"
#include "ui/layouts/layout_320x240/product_pages.h"

namespace nm::ui {

const PageScaffoldMetrics& page_metrics_320x240() {
    static const PageScaffoldMetrics metrics = [] {
        PageScaffoldMetrics value;
        value.width = 320;
        value.height = 240;
        value.title_y = 12;
        value.subtitle_y = 42;
        value.line_y = {{72, 96, 120, 144, 168}};
        value.footer_y = -8;
        value.horizontal_padding = 14;
        return value;
    }();
    return metrics;
}

void PageLoading320x240::create(lv_obj_t* parent) {
    create_loading_page(parent, 320, 240);

    if (_background != nullptr) {
        lv_img_set_src(_background, &assets::loading_background_320x240());
        lv_obj_set_pos(_background, 0, 0);
        lv_obj_move_background(_background);
    }

    if (_lb_details != nullptr) {
        lv_obj_set_style_text_font(_lb_details, &lv_font_montserrat_16, LV_PART_MAIN);
    }
    if (_bar_progress != nullptr) {
        lv_obj_set_size(_bar_progress, 288, 6);
        lv_obj_align(_bar_progress, LV_ALIGN_CENTER, 0, -20);
    }
    if (_lb_ip != nullptr) {
        _ip_font = &lv_font_montserrat_20;
        _ip_max_width = 320;
        lv_obj_set_width(_lb_ip, 320);
        lv_obj_set_style_text_font(_lb_ip, _ip_font, LV_PART_MAIN);
        lv_obj_align(_lb_ip, LV_ALIGN_CENTER, 0, 25);
    }
    if (_lb_pool != nullptr) {
        _pool_font = &lv_font_montserrat_20;
        _pool_max_width = 320;
        lv_obj_set_width(_lb_pool, 320);
        lv_obj_set_style_text_font(_lb_pool, _pool_font, LV_PART_MAIN);
        lv_obj_align(_lb_pool, LV_ALIGN_CENTER, 0, 65);
    }
}

void PageConfig320x240::create(lv_obj_t* parent) {
    create_config_page(parent, page_metrics_320x240());
}

void PageMiner320x240::create(lv_obj_t* parent) {
    create_miner_page(parent, page_metrics_320x240());
}

void PageDashboard320x240::create(lv_obj_t* parent) {
    create_dashboard_page(parent, page_metrics_320x240());
}

void PageHrHealth320x240::create(lv_obj_t* parent) {
    create_hr_health_page(parent, page_metrics_320x240());
}

void PageClock320x240::create(lv_obj_t* parent) {
    create_clock_page(parent, page_metrics_320x240());
}

void PageMarket320x240::create(lv_obj_t* parent) {
    create_market_page(parent, page_metrics_320x240());
}

void PageSetting320x240::create(lv_obj_t* parent) {
    create_setting_page(parent, page_metrics_320x240());
}

namespace {

PageCatalog make_catalog(UIPage& page_loading,
                         UIPage& page_config,
                         UIPage& page_miner,
                         UIPage& page_dashboard,
                         UIPage& page_hr_health,
                         UIPage& page_clock,
                         UIPage& page_market,
                         UIPage& page_setting) {
    PageCatalog value;
    value.entries[0].page = &page_loading;
    value.entries[0].col = 0;
    value.entries[0].row = 0;
    value.entries[0].nav_dir = LV_DIR_NONE;

    value.entries[1].page = &page_config;
    value.entries[1].col = 0;
    value.entries[1].row = 1;
    value.entries[1].nav_dir = LV_DIR_NONE;

    value.entries[2].page = &page_miner;
    value.entries[2].col = 1;
    value.entries[2].row = 0;
    value.entries[2].nav_dir = static_cast<lv_dir_t>(LV_DIR_RIGHT | LV_DIR_BOTTOM);

    value.entries[3].page = &page_dashboard;
    value.entries[3].col = 1;
    value.entries[3].row = 1;
    value.entries[3].nav_dir = static_cast<lv_dir_t>(LV_DIR_RIGHT | LV_DIR_TOP | LV_DIR_BOTTOM);

    value.entries[4].page = &page_hr_health;
    value.entries[4].col = 1;
    value.entries[4].row = 2;
    value.entries[4].nav_dir = static_cast<lv_dir_t>(LV_DIR_RIGHT | LV_DIR_TOP);

    value.entries[5].page = &page_clock;
    value.entries[5].col = 2;
    value.entries[5].row = 2;
    value.entries[5].nav_dir = static_cast<lv_dir_t>(LV_DIR_LEFT | LV_DIR_TOP);

    value.entries[6].page = &page_market;
    value.entries[6].col = 2;
    value.entries[6].row = 1;
    value.entries[6].nav_dir = static_cast<lv_dir_t>(LV_DIR_LEFT | LV_DIR_TOP | LV_DIR_BOTTOM);

    value.entries[7].page = &page_setting;
    value.entries[7].col = 2;
    value.entries[7].row = 0;
    value.entries[7].nav_dir = static_cast<lv_dir_t>(LV_DIR_LEFT | LV_DIR_BOTTOM);
    return value;
}

}  // namespace

const PageCatalog& page_catalog_320x240() {
    using ProductPages = layout_320x240::ActiveProductPages;

    static ProductPages::LoadingPage page_loading;
    static ProductPages::ConfigPage page_config;
    static ProductPages::MinerPage page_miner;
    static ProductPages::DashboardPage page_dashboard;
    static ProductPages::HrHealthPage page_hr_health;
    static ProductPages::ClockPage page_clock;
    static ProductPages::MarketPage page_market;
    static ProductPages::SettingPage page_setting;
    static const PageCatalog catalog = make_catalog(
        page_loading,
        page_config,
        page_miner,
        page_dashboard,
        page_hr_health,
        page_clock,
        page_market,
        page_setting);
    return catalog;
}

}  // namespace nm::ui
