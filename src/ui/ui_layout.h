#pragma once

// ============================================================================
//  UI layout includes ¡ª all resolution layouts compiled into firmware.
//
//  Board model is detected at runtime via get_board_model(), so all
//  resolution-specific classes must be available. The UIManager selects
//  the correct layout based on BoardSpecConfig.tft.width/height.
// ============================================================================

// ©¤©¤ 135x240 (NMAXE / NMAXE_GAMMA) ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤
#include "layouts/layout_240x135/page_loading.h"
#include "layouts/layout_240x135/page_config.h"
#include "layouts/layout_240x135/page_miner.h"
#include "layouts/layout_240x135/page_dashboard.h"
#include "layouts/layout_240x135/page_hr_health.h"
#include "layouts/layout_240x135/page_clock.h"
#include "layouts/layout_240x135/page_market.h"
#include "layouts/layout_240x135/page_setting.h"

// ©¤©¤ 240x320 (NMQAXE_PLUS_PLUS / Rev6.1 / Rev8.1) ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤
#include "layouts/layout_320x240/page_loading.h"
#include "layouts/layout_320x240/page_config.h"
#include "layouts/layout_320x240/page_miner.h"
#include "layouts/layout_320x240/page_dashboard.h"
#include "layouts/layout_320x240/page_hr_health.h"
#include "layouts/layout_320x240/page_clock.h"
#include "layouts/layout_320x240/page_market.h"
#include "layouts/layout_320x240/page_setting.h"
