// What: Compile-time page binding for 320x240 QAxe++ products.
// Why: QAxe++ variants share the same screen class and can use one initial page
// catalog while variant-specific content is migrated later.
// Role: Maps active QAxe++ BOARD_* targets to concrete 320x240 page classes.
// Benefit: UI routing stays compile-time and layout-specific instead of falling
// back to 240x135 product bindings.
#pragma once

#include "ui/pages/page_clock.h"
#include "ui/pages/page_config.h"
#include "ui/pages/page_dashboard.h"
#include "ui/pages/page_hr_health.h"
#include "ui/pages/page_loading.h"
#include "ui/pages/page_market.h"
#include "ui/pages/page_miner.h"
#include "ui/pages/page_setting.h"

namespace nm::ui {

class PageLoading320x240 : public PageLoadingBase {
public:
    void create(lv_obj_t* parent) override;
    const char* name() const override { return "PageLoading320x240"; }
};

class PageConfig320x240 : public PageConfigBase {
public:
    void create(lv_obj_t* parent) override;
    const char* name() const override { return "PageConfig320x240"; }
};

class PageMiner320x240 : public PageMinerBase {
public:
    void create(lv_obj_t* parent) override;
    const char* name() const override { return "PageMiner320x240"; }
};

class PageDashboard320x240 : public PageDashboardBase {
public:
    void create(lv_obj_t* parent) override;
    const char* name() const override { return "PageDashboard320x240"; }
};

class PageHrHealth320x240 : public PageHrHealthBase {
public:
    void create(lv_obj_t* parent) override;
    const char* name() const override { return "PageHrHealth320x240"; }
};

class PageClock320x240 : public PageClockBase {
public:
    void create(lv_obj_t* parent) override;
    const char* name() const override { return "PageClock320x240"; }
};

class PageMarket320x240 : public PageMarketBase {
public:
    void create(lv_obj_t* parent) override;
    const char* name() const override { return "PageMarket320x240"; }
};

class PageSetting320x240 : public PageSettingBase {
public:
    void create(lv_obj_t* parent) override;
    const char* name() const override { return "PageSetting320x240"; }
};

namespace layout_320x240 {

#if defined(BOARD_NMQAXE_PP) || defined(BOARD_NMQAXE_PP_REV61) || defined(BOARD_NMQAXE_PP_REV81)

struct ActiveProductPages {
    using LoadingPage = PageLoading320x240;
    using ConfigPage = PageConfig320x240;
    using MinerPage = PageMiner320x240;
    using DashboardPage = PageDashboard320x240;
    using HrHealthPage = PageHrHealth320x240;
    using ClockPage = PageClock320x240;
    using MarketPage = PageMarket320x240;
    using SettingPage = PageSetting320x240;
};

#else

// Layout sources are compiled for every firmware target by PlatformIO's broad
// src_filter. Keep this catalog buildable even when the active product resolves
// to another layout; layout_resolver will not select it for those targets.
struct ActiveProductPages {
    using LoadingPage = PageLoading320x240;
    using ConfigPage = PageConfig320x240;
    using MinerPage = PageMiner320x240;
    using DashboardPage = PageDashboard320x240;
    using HrHealthPage = PageHrHealth320x240;
    using ClockPage = PageClock320x240;
    using MarketPage = PageMarket320x240;
    using SettingPage = PageSetting320x240;
};

#endif

}  // namespace layout_320x240
}  // namespace nm::ui
