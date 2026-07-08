// What: Compile-time page binding for 240x135 products.
// Why: Boards like NMAxe and Gamma can share one resolution while still
// diverging on the content behind the same page id.
// Role: Maps the active BOARD_* target to the concrete page classes used by
// the 240x135 page catalog.
// Benefit: Product-specific page differences stay compile-time and local,
// without reintroducing runtime product switches into the layout registry.
#pragma once

#include "ui/layouts/layout_240x135/page_clock.h"
#include "ui/layouts/layout_240x135/page_config.h"
#include "ui/layouts/layout_240x135/page_dashboard.h"
#include "ui/layouts/layout_240x135/page_hr_health.h"
#include "ui/layouts/layout_240x135/page_loading.h"
#include "ui/layouts/layout_240x135/page_market.h"
#include "ui/layouts/layout_240x135/page_miner.h"
#include "ui/layouts/layout_240x135/page_setting.h"

namespace nm::ui::layout_240x135 {

#if defined(BOARD_NMAXE)

struct ActiveProductPages {
    using LoadingPage = PageLoading240x135;
    using ConfigPage = PageConfig240x135;
    using MinerPage = PageMiner240x135;
    using DashboardPage = PageDashboard240x135;
    using HrHealthPage = PageHrHealth240x135;
    using ClockPage = PageClock240x135;
    using MarketPage = PageMarket240x135;
    using SettingPage = PageSetting240x135;
};

#elif defined(BOARD_NMAXE_GAMMA)

struct ActiveProductPages {
    using LoadingPage = PageLoading240x135;
    using ConfigPage = PageConfig240x135;
    using MinerPage = PageMiner240x135;
    using DashboardPage = PageDashboard240x135;
    using HrHealthPage = PageHrHealth240x135;
    using ClockPage = PageClock240x135;
    using MarketPage = PageMarket240x135;
    using SettingPage = PageSetting240x135;
};

#else

// Layout sources are compiled for every firmware target by PlatformIO's broad
// src_filter. Keep this catalog buildable even when the active product resolves
// to another layout; layout_resolver will not select it for those targets.
struct ActiveProductPages {
    using LoadingPage = PageLoading240x135;
    using ConfigPage = PageConfig240x135;
    using MinerPage = PageMiner240x135;
    using DashboardPage = PageDashboard240x135;
    using HrHealthPage = PageHrHealth240x135;
    using ClockPage = PageClock240x135;
    using MarketPage = PageMarket240x135;
    using SettingPage = PageSetting240x135;
};

#endif

}  // namespace nm::ui::layout_240x135
