#pragma once

#include <stdint.h>

#include "bsp/board_types.h"

namespace nm::product {

enum class UiLayoutId : uint8_t {
    Unknown = 0,
    Layout240x135 = 1,
    Layout320x240 = 2,
    Layout480x320 = 3,
};

enum class UiVariantId : uint8_t {
    Default = 0,
    TouchSingleAsic = 1,
    RichDashboard = 2,
};

struct UiProfile {
    UiLayoutId layout_id = UiLayoutId::Unknown;
    UiVariantId variant_id = UiVariantId::Default;
    bsp::UiInputMode input_mode = bsp::UiInputMode::ButtonOnly;
    const char* profile_name = "default";
};

}  // namespace nm::product
