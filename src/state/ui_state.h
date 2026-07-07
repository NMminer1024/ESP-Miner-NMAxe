// What: UI-facing navigation state owned above the rendering layer.
// Why: Services should switch pages by stable page ids instead of touching LVGL
// widget trees or layout-specific classes directly.
// Role: Holds the active page, activity timestamp, and render invalidation bit.
// Benefit: The app flow can evolve toward the legacy multi-page UI without
// coupling service code to a specific resolution implementation.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace nm::state {

enum class UiPageId : uint8_t {
    Loading = 0,
    Config = 1,
    Miner = 2,
    Dashboard = 3,
    HrHealth = 4,
    Clock = 5,
    Market = 6,
    SettingSwarm = 7,
    Count = 8,
};

constexpr size_t kUiPageCount = static_cast<size_t>(UiPageId::Count);

constexpr uint8_t ui_page_index(UiPageId page_id) {
    return static_cast<uint8_t>(page_id);
}

constexpr UiPageId ui_page_from_index(uint8_t index) {
    return index < ui_page_index(UiPageId::Count) ? static_cast<UiPageId>(index) : UiPageId::Loading;
}

inline UiPageId next_ui_page(UiPageId page_id) {
    const uint8_t next_index = (ui_page_index(page_id) + 1u) % ui_page_index(UiPageId::Count);
    return ui_page_from_index(next_index);
}

inline UiPageId prev_ui_page(UiPageId page_id) {
    const uint8_t count = ui_page_index(UiPageId::Count);
    const uint8_t current_index = ui_page_index(page_id);
    const uint8_t prev_index = current_index == 0u ? static_cast<uint8_t>(count - 1u) : static_cast<uint8_t>(current_index - 1u);
    return ui_page_from_index(prev_index);
}

constexpr bool is_runtime_ui_page(UiPageId page_id) {
    return page_id >= UiPageId::Miner && page_id <= UiPageId::SettingSwarm;
}

inline UiPageId next_runtime_ui_page(UiPageId page_id) {
    if (!is_runtime_ui_page(page_id)) {
        return UiPageId::Miner;
    }

    return page_id == UiPageId::SettingSwarm ? UiPageId::Miner : ui_page_from_index(ui_page_index(page_id) + 1u);
}

inline UiPageId prev_runtime_ui_page(UiPageId page_id) {
    if (!is_runtime_ui_page(page_id)) {
        return UiPageId::SettingSwarm;
    }

    return page_id == UiPageId::Miner ? UiPageId::SettingSwarm : ui_page_from_index(ui_page_index(page_id) - 1u);
}

struct UiState {
    UiPageId current_page = UiPageId::Loading;
    uint32_t last_activity_ms = 0;
    bool dirty = true;
};

}  // namespace nm::state
