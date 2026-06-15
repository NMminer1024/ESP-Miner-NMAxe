#pragma once

#include <Arduino.h>

enum class ReflectBoardModel : uint8_t {
    NMAxe = 0b110,
    NMAxeGamma = 0b010,
    NMQAxePP = 0b101,
    NMQAxePPRev61 = 0b111,
    Unknown = 0b000,
};

ReflectBoardModel detect_reflect_board_model();
const char* reflect_board_model_name(ReflectBoardModel model);
