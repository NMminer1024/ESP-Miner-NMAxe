#pragma once

// ============================================================================
//  Compile-time resolution selection for embedded UI assets.
//
//  Each PlatformIO env defines exactly one BOARD_* macro (same convention as
//  ui_layout.h), so only the matching resolution's full-screen background
//  bitmaps are compiled into the firmware. The other resolution's arrays are
//  internal-linkage const in images.cpp's TU and become unreferenced, so the
//  compiler discards them (~1 MB flash saved per env).
// ============================================================================

#if defined(BOARD_NMAXE) || defined(BOARD_NMAXE_GAMMA)
#  define UI_RES_135_240 1
#else
#  define UI_RES_135_240 0
#endif

#if defined(BOARD_NMQAXE_PP) || defined(BOARD_NMQAXE_PP_REV61) || \
    defined(BOARD_NMQAXE_PP_REV81) || defined(BOARD_NMQAXE_PP_NEXUS)
#  define UI_RES_240_320 1
#else
#  define UI_RES_240_320 0
#endif

#if !UI_RES_135_240 && !UI_RES_240_320
#error "No BOARD_* macro defined: exactly one of BOARD_NMAXE / BOARD_NMAXE_GAMMA / BOARD_NMQAXE_PP / BOARD_NMQAXE_PP_REV61 / BOARD_NMQAXE_PP_REV81 / BOARD_NMQAXE_PP_NEXUS must be set"
#endif
