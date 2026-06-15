#pragma once

// ============================================================================
//  UI layout selector — compile-time resolution dispatch
//
//  The board config.h (or build_flags) defines UI_LAYOUT_* to select
//  the correct resolution-specific page implementations.
//
//  Add new resolutions here as needed.
// ============================================================================

#if defined(LVGL_ENABLE)

#if defined(UI_LAYOUT_320x240)
    #include "layouts/layout_320x240/page_loading.h"
    #include "layouts/layout_320x240/page_miner.h"
#else
    // Default: 320x240 (NMAxe primary resolution)
    #include "layouts/layout_320x240/page_loading.h"
    #include "layouts/layout_320x240/page_miner.h"
#endif

#endif // LVGL_ENABLE
