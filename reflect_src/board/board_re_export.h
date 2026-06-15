#pragma once
// ============================================================================
//  Board config re-export
//
//  This file re-exports the original board/board.h types and functions
//  so that reflect_src code can reference board configuration without
//  including the original board directory directly.
//
//  No modification to original src/board/ files.
// ============================================================================

// Include the original board config — read-only usage, unchanged.
#include "board/board.h"
// Also include the specific board model headers used by factory functions.
#include "board/nmaxe.h"
#include "board/nmaxegamma.h"
#include "board/nmqaxepp.h"

// ============================================================================
//  Convenience re-exports for the new codebase
// ============================================================================

// Pin definitions from board.h
// NM_MODEL_SELECT_PIN0, NM_MODEL_SELECT_PIN1, NM_MODEL_SELECT_PIN2
// BoardModelType enum
// limited_data_f, limited_data_u16
// screen_preference_info_t, led_preference_info_t
// work_option_t, BoardSpecConfig

// Factory functions
// get_board_model(), get_board_config(), hardware_pre_init()

// All of the above are already available via #include "board/board.h"
