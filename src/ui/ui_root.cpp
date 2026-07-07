// What: UI boot coordinator and tileview host for the new framework.
// Why: The firmware needs one place to bind LVGL ports, select the active
// layout, and host all pages in an old-style sliding tile container.
// Role: Bridges board/product context into a resolution-specific tileview tree.
// Benefit: Page classes stay layout-local while top-level UI flow retains the
// same sliding-page model the legacy product used.
#include "ui/ui_root.h"

#include <lvgl.h>

#include "product/ui_profile.h"
#include "ui/layouts/layout_resolver.h"
#include "ui/page.h"
#include "ui/port/ports.h"

namespace nm::ui {

namespace {

struct RootPages {
    const PageCatalog* catalog = nullptr;
    lv_obj_t* tileview = nullptr;
    std::array<lv_obj_t*, state::kUiPageCount> tiles{};
    state::UiPageId active_page = state::UiPageId::Count;
    bool ready = false;
};

RootPages g_root;

size_t page_slot(state::UiPageId page_id) {
    const uint8_t index = state::ui_page_index(page_id);
    return index < state::kUiPageCount ? static_cast<size_t>(index) : state::kUiPageCount;
}

const PageCatalogEntry* entry_for(state::UiPageId page_id) {
    if (g_root.catalog == nullptr) {
        return nullptr;
    }

    const size_t slot = page_slot(page_id);
    if (slot >= g_root.catalog->entries.size()) {
        return nullptr;
    }

    return &g_root.catalog->entries[slot];
}

UIPage* page_for(state::UiPageId page_id) {
    const auto* entry = entry_for(page_id);
    return entry != nullptr ? entry->page : nullptr;
}

void destroy_catalog_pages() {
    if (g_root.catalog == nullptr) {
        return;
    }

    for (const auto& entry : g_root.catalog->entries) {
        if (entry.page != nullptr) {
            entry.page->destroy();
        }
    }
}

bool build_page_tree(const bsp::Board& board, const PageCatalog& catalog, lv_obj_t* screen) {
    if (screen == nullptr) {
        return false;
    }

    destroy_catalog_pages();
    lv_obj_clean(screen);

    g_root.tiles.fill(nullptr);
    g_root.catalog = &catalog;
    g_root.active_page = state::UiPageId::Count;
    g_root.tileview = lv_tileview_create(screen);
    if (g_root.tileview == nullptr) {
        return false;
    }

    lv_obj_set_size(
        g_root.tileview,
        static_cast<lv_coord_t>(board.display_profile().width),
        static_cast<lv_coord_t>(board.display_profile().height));
    lv_obj_set_style_bg_color(g_root.tileview, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_root.tileview, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(g_root.tileview, LV_SCROLLBAR_MODE_OFF);
    lv_obj_align(g_root.tileview, LV_ALIGN_CENTER, 0, 0);

    bool has_page = false;
    for (size_t i = 0; i < catalog.entries.size(); ++i) {
        const auto& entry = catalog.entries[i];
        if (entry.page == nullptr) {
            continue;
        }

        lv_obj_t* tile = lv_tileview_add_tile(g_root.tileview, entry.col, entry.row, entry.nav_dir);
        if (tile == nullptr) {
            continue;
        }

        lv_obj_set_style_pad_all(tile, 0, LV_PART_MAIN);
        lv_obj_set_scrollbar_mode(tile, LV_SCROLLBAR_MODE_OFF);
        entry.page->create(tile);
        g_root.tiles[i] = tile;
        has_page = true;
    }

    return has_page;
}

void sync_active_page(state::UiPageId page_id, lv_anim_enable_t animate) {
    if (g_root.tileview == nullptr) {
        return;
    }

    const auto* entry = entry_for(page_id);
    if (entry == nullptr || entry->page == nullptr) {
        return;
    }

    lv_obj_set_tile_id(g_root.tileview, entry->col, entry->row, animate);
    g_root.active_page = page_id;
}

}  // namespace

bool boot(
    const bsp::Board& board,
    const config::AppConfig& config,
    const state::RuntimeState& runtime,
    const state::UiState& ui_state) {
    const auto& profile = product::active_ui_profile(board);

    bool display_ready = false;
    if (board.drivers().display != nullptr) {
        display_ready = port::bind_display(*board.drivers().display);
    }
    port::bind_input(board.drivers().touch);

    Serial.printf("[ui] profile=%s layout=%u variant=%u input=%u\n",
                  profile.profile_name,
                  static_cast<unsigned>(profile.layout_id),
                  static_cast<unsigned>(profile.variant_id),
                  static_cast<unsigned>(profile.input_mode));

    if (!display_ready) {
        return false;
    }

    lv_obj_t* screen = lv_scr_act();
    if (screen == nullptr) {
        return false;
    }

    const PageCatalog& catalog = resolve_page_catalog(profile.layout_id);
    if (!build_page_tree(board, catalog, screen)) {
        return false;
    }

    g_root.ready = true;
    sync_active_page(ui_state.current_page, LV_ANIM_OFF);
    render(board, config, runtime, ui_state);
    if (lv_disp_get_default() != nullptr) {
        lv_refr_now(lv_disp_get_default());
    }
    return true;
}

void render(
    const bsp::Board& board,
    const config::AppConfig& config,
    const state::RuntimeState& runtime,
    const state::UiState& ui_state) {
    if (!g_root.ready) {
        return;
    }

    UIPage* active_page = page_for(ui_state.current_page);
    if (active_page == nullptr) {
        return;
    }

    if (g_root.active_page != ui_state.current_page) {
        sync_active_page(ui_state.current_page, g_root.active_page == state::UiPageId::Count ? LV_ANIM_OFF : LV_ANIM_ON);
    }

    const PageContext context = {
        board,
        config,
        runtime,
        ui_state,
    };
    active_page->render(context);
}

void poll() {
    port::poll();
}

}  // namespace nm::ui
