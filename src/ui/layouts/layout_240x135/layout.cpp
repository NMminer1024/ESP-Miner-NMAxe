// What: 240x135 layout registry implementation for the new page framework.
// Why: The current product profile resolves to 240x135, so the UI layer needs
// a concrete catalog of page objects instead of a placeholder stub.
// Role: Owns the shared 240x135 metrics and static page instances.
// Benefit: The active layout can now be selected cleanly without hard-coding
// page construction into `ui_root`.
#include "ui/layouts/layout_240x135/layout.h"

#include "ui/layouts/layout_240x135/page_clock.h"
#include "ui/layouts/layout_240x135/page_config.h"
#include "ui/layouts/layout_240x135/page_dashboard.h"
#include "ui/layouts/layout_240x135/page_hr_health.h"
#include "ui/layouts/layout_240x135/page_loading.h"
#include "ui/layouts/layout_240x135/page_market.h"
#include "ui/layouts/layout_240x135/page_miner.h"
#include "ui/layouts/layout_240x135/page_setting.h"

namespace nm::ui {

const PageScaffoldMetrics& page_metrics_240x135() {
    static const PageScaffoldMetrics metrics = [] {
        PageScaffoldMetrics value;
        value.width = 240;
        value.height = 135;
        value.title_y = 6;
        value.subtitle_y = 26;
        value.line_y = {{46, 60, 74, 88, 102}};
        value.footer_y = -4;
        value.horizontal_padding = 8;
        return value;
    }();
    return metrics;
}

const PageCatalog& page_catalog_240x135() {
    static PageLoading240x135 page_loading;
    static PageConfig240x135 page_config;
    static PageMiner240x135 page_miner;
    static PageDashboard240x135 page_dashboard;
    static PageHrHealth240x135 page_hr_health;
    static PageClock240x135 page_clock;
    static PageMarket240x135 page_market;
    static PageSetting240x135 page_setting;
    static const PageCatalog catalog = [] {
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
    }();

    return catalog;
}

}  // namespace nm::ui
