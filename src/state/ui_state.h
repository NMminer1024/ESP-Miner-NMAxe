// What: UI-facing lightweight state owned above the rendering layer.
// Why: UI navigation and activity state should be represented as plain data so
// service logic can evolve independently from LVGL widgets.
// Role: Holds the currently selected placeholder page and last-activity stamp.
// Benefit: Keeps UI flow decisions testable and decoupled from view objects.
#pragma once

#include <stdint.h>

namespace nm::state {

enum class UiPageId : uint8_t {
    Summary = 0,
    Detail = 1,
};

struct UiState {
    UiPageId current_page = UiPageId::Summary;
    uint32_t last_activity_ms = 0;
    bool dirty = true;
};

}  // namespace nm::state
