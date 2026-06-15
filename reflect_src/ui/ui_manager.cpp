#include "ui_manager.h"
#include "ui_layout.h"
#include "utils/logger/logger.h"
#include "app/app_state.h"


// ============================================================================
//  Singleton
// ============================================================================
UIManager& UIManager::instance() {
    static UIManager mgr;
    return mgr;
}

// ============================================================================
//  init() — create tileview + register all pages
// ============================================================================
void UIManager::init() {
    // ── Get screen dimensions from active display ───────────────────────
    lv_disp_t* disp = lv_disp_get_default();
    lv_coord_t w = lv_disp_get_hor_res(disp);
    lv_coord_t h = lv_disp_get_ver_res(disp);

    // ── Create tileview ─────────────────────────────────────────────────
    _tileview = lv_tileview_create(lv_scr_act());
    lv_obj_set_size(_tileview, w, h);
    lv_obj_set_style_bg_color(_tileview, lv_color_hex(0x000000), 0);
    lv_obj_set_scrollbar_mode(_tileview, LV_SCROLLBAR_MODE_OFF);

    // Snap acceleration
    lv_obj_add_event_cb(_tileview, _scroll_begin_cb, LV_EVENT_SCROLL_BEGIN, nullptr);
    lv_obj_add_event_cb(_tileview, _pressed_cb,      LV_EVENT_PRESSED,      nullptr);
    lv_obj_add_event_cb(_tileview, _released_cb,     LV_EVENT_RELEASED,     nullptr);
    lv_obj_add_event_cb(_tileview, _scroll_end_cb,   LV_EVENT_SCROLL_END,   nullptr);

    // ── Register pages ──────────────────────────────────────────────────
    static PageLoading320x240 page_loading;
    static PageMiner320x240   page_miner;

    _register_page(&page_loading, UIPageId::LOADING, 0, 0, LV_DIR_RIGHT | LV_DIR_BOTTOM);
    _register_page(&page_miner,   UIPageId::MINER,   1, 0, LV_DIR_LEFT  | LV_DIR_BOTTOM);

    LOG_I("UIManager: %u pages registered, tileview %dx%d",
          (unsigned)_pages.size(), w, h);
}

// ============================================================================
//  _register_page — create tile, call page->create(tile), store entry
// ============================================================================
void UIManager::_register_page(UIPage* page, UIPageId id,
                                uint8_t col, uint8_t row, lv_dir_t dir) {
    lv_obj_t* tile = lv_tileview_add_tile(_tileview, col, row, dir);
    lv_obj_set_style_pad_all(tile, 0, 0);
    page->create(tile);

    _tile_entries.push_back({tile, col, row});
    if ((size_t)id >= _pages.size()) {
        _pages.resize((size_t)id + 1, nullptr);
    }
    _pages[(size_t)id] = page;
    LOG_D("UIManager: page[%d] '%s' registered at (%d,%d)",
          (int)id, page->name(), col, row);
}

// ============================================================================
//  render_update() — sync active tile, refresh current page, drive LVGL
// ============================================================================
void UIManager::render_update() {
    // ── Consume cross-thread page requests ──────────────────────────────
    if (_goto_page_pending) {
        _goto_page_pending = false;
        goto_page((UIPageId)_goto_page_id);
    }
    if (_next_page_pending) {
        _next_page_pending = false;
        next_page();
    }
    if (_prev_page_pending) {
        _prev_page_pending = false;
        prev_page();
    }

    // ── Refresh current page ────────────────────────────────────────────
    if (_current < _pages.size() && _pages[_current]) {
        _pages[_current]->update();
    }

    // ── Drive LVGL ──────────────────────────────────────────────────────
    lv_timer_handler();
}

// ============================================================================
//  Page navigation
// ============================================================================
size_t UIManager::current() const {
    return _current;
}

void UIManager::next_page() {
    size_t n = _pages.size();
    if (n < 2) return;

    size_t next = _current;
    uint8_t tries = 0;
    do {
        next = (next + 1) % n;
        tries++;
    } while (_pages[next] == nullptr && tries < n);

    if (_pages[next]) {
        // Animate tileview
        int8_t dx = (_tile_entries[next].col > _tile_entries[_current].col) ? 1
                   : ((_tile_entries[next].col < _tile_entries[_current].col) ? -1 : 0);
        int8_t dy = (_tile_entries[next].row > _tile_entries[_current].row) ? 1
                   : ((_tile_entries[next].row < _tile_entries[_current].row) ? -1 : 0);

        lv_obj_set_tile(_tileview, _tile_entries[next].obj,
                        dx > 0 ? LV_ANIM_ON : (dx < 0 ? LV_ANIM_ON : LV_ANIM_OFF));
        _current = next;
        LOG_D("UIManager: page -> %u '%s'", (unsigned)_current, _pages[_current]->name());
    }
}

void UIManager::prev_page() {
    size_t n = _pages.size();
    if (n < 2) return;

    size_t prev = _current;
    uint8_t tries = 0;
    do {
        prev = (prev == 0) ? (n - 1) : (prev - 1);
        tries++;
    } while (_pages[prev] == nullptr && tries < n);

    if (_pages[prev]) {
        int8_t dx = (_tile_entries[prev].col > _tile_entries[_current].col) ? 1
                   : ((_tile_entries[prev].col < _tile_entries[_current].col) ? -1 : 0);
        int8_t dy = (_tile_entries[prev].row > _tile_entries[_current].row) ? 1
                   : ((_tile_entries[prev].row < _tile_entries[_current].row) ? -1 : 0);

        lv_obj_set_tile(_tileview, _tile_entries[prev].obj,
                        dx < 0 ? LV_ANIM_ON : (dx > 0 ? LV_ANIM_ON : LV_ANIM_OFF));
        _current = prev;
        LOG_D("UIManager: page <- %u '%s'", (unsigned)_current, _pages[_current]->name());
    }
}

void UIManager::goto_page(UIPageId id) {
    size_t idx = (size_t)id;
    if (idx >= _pages.size() || _pages[idx] == nullptr) return;
    if (idx == _current) return;

    lv_obj_set_tile_id(_tileview,
                       _tile_entries[idx].col, _tile_entries[idx].row,
                       LV_ANIM_ON);
    _current = idx;
    LOG_D("UIManager: page -> %u '%s'", (unsigned)_current, _pages[_current]->name());
}

// ============================================================================
//  Cross-thread-safe requests
// ============================================================================
void UIManager::request_next_page()           { _next_page_pending = true; }
void UIManager::request_prev_page()           { _prev_page_pending = true; }
void UIManager::request_goto_page(UIPageId id){ _goto_page_id = (uint8_t)id; _goto_page_pending = true; }

// ============================================================================
//  wake_activity
// ============================================================================
void UIManager::wake_activity() {
    AppState::instance().miner.ip.text = AppState::instance().miner.ip.text;
}

// ============================================================================
//  LVGL event callbacks (scroll snap logic)
// ============================================================================
void UIManager::_scroll_begin_cb(lv_event_t* e) {
    lv_obj_t* tv = lv_event_get_target(e);
    // Accelerate snap animation
    lv_obj_set_style_anim_time(tv, 80, LV_STATE_DEFAULT);
}

void UIManager::_pressed_cb(lv_event_t* e) {
    UIManager& self = instance();
    self._s_start_tile = lv_tileview_get_tile_act(lv_event_get_target(e));
}

void UIManager::_released_cb(lv_event_t* e) {
    UIManager& self = instance();
    lv_obj_t* tv = lv_event_get_target(e);
    // Read scroll offset BEFORE scroll_end (where it resets to 0)
    lv_coord_t x = lv_obj_get_scroll_x(tv);
    lv_coord_t y = lv_obj_get_scroll_y(tv);
    // Normalize: tile size = screen size
    lv_coord_t w = lv_obj_get_width(tv);
    lv_coord_t h = lv_obj_get_height(tv);
    self._s_rel_x = x % w;
    self._s_rel_y = y % h;
}

void UIManager::_scroll_end_cb(lv_event_t* e) {
    UIManager& self = instance();
    lv_obj_t* tv = lv_event_get_target(e);
    lv_obj_t* active = lv_tileview_get_tile_act(tv);

    // Find current index
    for (size_t i = 0; i < self._tile_entries.size(); i++) {
        if (self._tile_entries[i].obj == active) {
            self._current = i;
            break;
        }
    }
    LOG_D("UIManager: scroll_end -> tile %u", (unsigned)self._current);
}

