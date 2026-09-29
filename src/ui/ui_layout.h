#pragma once

// ============================================================================
//  UI layout includes — resolution selected at COMPILE TIME by BOARD_* macro.
//
//  Each PlatformIO env defines exactly one BOARD_* macro, so only the
//  matching resolution's pages are compiled into the firmware:
//    - 240x135 : BOARD_NMAXE / BOARD_NMAXE_GAMMA
//    - 320x240 : BOARD_NMQAXE_PP / _REV61 / _REV81 / _NEXUS
// ============================================================================

// ©¤©¤ 135x240 (NMAXE / NMAXE_GAMMA) ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤
#if defined(BOARD_NMAXE) || defined(BOARD_NMAXE_GAMMA)
#include "layouts/layout_240x135/page_loading.h"
#include "layouts/layout_240x135/page_config.h"
#include "layouts/layout_240x135/page_miner.h"
#include "layouts/layout_240x135/page_dashboard.h"
#include "layouts/layout_240x135/page_hr_health.h"
#include "layouts/layout_240x135/page_clock.h"
#include "layouts/layout_240x135/page_market.h"
#include "layouts/layout_240x135/page_setting.h"
#endif

// ©¤©¤ 240x320 (NMQAXE_PLUS_PLUS / Rev6.1 / Rev8.1) ©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤
#if defined(BOARD_NMQAXE_PP) || defined(BOARD_NMQAXE_PP_REV61) || defined(BOARD_NMQAXE_PP_REV81) || defined(BOARD_NMQAXE_PP_NEXUS)
#include "layouts/layout_320x240/page_loading.h"
#include "layouts/layout_320x240/page_config.h"
#include "layouts/layout_320x240/page_miner.h"
#include "layouts/layout_320x240/page_dashboard.h"
#include "layouts/layout_320x240/page_hr_health.h"
#include "layouts/layout_320x240/page_clock.h"
#include "layouts/layout_320x240/page_market.h"
#include "layouts/layout_320x240/page_setting.h"
#endif
