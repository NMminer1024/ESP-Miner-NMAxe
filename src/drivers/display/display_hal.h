#pragma once
#include <Arduino.h>
#include "lvgl.h"
#include "../../board/board.h"        // BoardSpecConfig, BOARD_* name macros
#include "../../app/runtime_state.h"  // PreferenceState (screen.flip)

// ============================================================================
//  Display HAL — TFT_eSPI panel bring-up + LVGL display driver registration
//
//  Ported from the legacy src/drivers/displays/display.cpp panel layer, but
//  dependency-injected: no g_board. The owning thread passes the board spec
//  (pins / resolution) and live preference (screen flip).
// ============================================================================

// Power-on the panel, configure backlight PWM, construct TFT_eSPI with the
// board SPI pins and set rotation. Must be called from the LVGL/UI thread.
void tft_init(BoardSpecConfig* spec, PreferenceState* pref);

// Set backlight brightness 0..100 (%). Board-specific PWM mapping.
void tft_bl_ctrl(int8_t percent, BoardSpecConfig* spec);

// Register the LVGL display driver (PSRAM draw buffer + flush callback).
// hor_res/ver_res are the post-rotation logical resolution.
void ui_drv_register(uint16_t hor_res, uint16_t ver_res);

// Initialize the FT6206 capacitive touch controller and, if present, register
// an LVGL pointer input device (enables tileview swipe navigation). I2C must
// already be initialized (board init). Returns true if touch is available.
// `pref` provides the live screen-flip flag for coordinate mapping.
bool touch_drv_register(PreferenceState* pref, uint8_t threshold = 50);

// Register the SPIFFS-backed LVGL filesystem driver (drive letter 'S'), so
// widgets such as lv_gif can open "S:<name>" from SPIFFS (screensaver GIF).
void lvgl_fs_spiffs_register();

// Post-rotation logical resolution (valid after tft_init()).
uint16_t tft_screen_width();
uint16_t tft_screen_height();
