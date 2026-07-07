// What: Product-level UI identity types and profile selection contract.
// Why: BSPs should expose hardware facts, while the product layer decides which
// layout and interaction style those facts map to in the UI tree.
// Role: Defines the compact metadata passed from board selection into UI boot.
// Benefit: Lets multiple boards share one UI framework while still diverging by
// layout, page variant, or input mode without leaking board macros upward.
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

enum class UiProductId : uint8_t {
    Unknown = 0,
    NMAxe = 1,
    NMAxeGamma = 2,
    NMQAxePP = 3,
    NMQAxePPRev61 = 4,
    NMQAxePPRev81 = 5,
};

struct UiProfile {
    UiLayoutId layout_id = UiLayoutId::Unknown;
    UiVariantId variant_id = UiVariantId::Default;
    UiProductId product_id = UiProductId::Unknown;
    bsp::UiInputMode input_mode = bsp::UiInputMode::ButtonOnly;
    const char* profile_name = "default";
};

const UiProfile& active_ui_profile();

}  // namespace nm::product
