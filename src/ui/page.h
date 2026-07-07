// What: Common UI page contract and render context for the new page framework.
// Why: Service code should choose pages by id and pass state snapshots, while
// layout code owns every LVGL widget and resolution-specific detail.
// Role: Defines the abstract page API plus the data bundle each page consumes.
// Benefit: New boards can reuse the same page framework by swapping only BSP
// drivers and layout classes instead of rewriting top-level UI flow.
#pragma once

#include <array>

#include <lvgl.h>

#include "bsp/board.h"
#include "config/app_config.h"
#include "state/runtime_state.h"
#include "state/ui_state.h"

namespace nm::ui {

struct PageContext {
    const bsp::Board& board;
    const config::AppConfig& config;
    const state::RuntimeState& runtime;
    const state::UiState& ui_state;
};

class UIPage {
public:
    virtual ~UIPage() = default;

    virtual state::UiPageId id() const = 0;
    virtual const char* name() const = 0;
    virtual void create(lv_obj_t* parent) = 0;
    virtual void destroy() = 0;
    virtual void render(const PageContext& context) = 0;
};

struct PageCatalogEntry {
    UIPage* page = nullptr;
    uint8_t col = 0;
    uint8_t row = 0;
    lv_dir_t nav_dir = LV_DIR_NONE;
};

struct PageCatalog {
    std::array<PageCatalogEntry, state::kUiPageCount> entries{};
};

}  // namespace nm::ui
