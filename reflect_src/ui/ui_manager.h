#pragma once

#include <stdint.h>
#include "hal.h"
#include <vector>
#include <functional>

#if defined(LVGL_ENABLE)
#include "lvgl.h"
#endif

// ============================================================================
// UIPageId — page enum (order must match _register_page calls in init())
// ============================================================================
enum class UIPageId : uint8_t {
    LOADING  = 0,
    CONFIG   = 1,
    MINER    = 2,
    SETTING  = 3,

    COUNT    // must be last
};

// ============================================================================
// UIManager — UI page manager (singleton)
//
//   Architecture: lv_tileview as container, each page occupies one tile.
//   LVGL handles touch-swipe navigation natively.
//   render_update() drives current active tile's page update + lv_timer_handler().
// ============================================================================
class UIManager {
public:
    static UIManager& instance();

    void init();

    // Called periodically from LVGL task: sync active tile, refresh page, drive LVGL
    void render_update();

    // Page navigation
    void next_page();
    void prev_page();
    void goto_page(UIPageId id);

    size_t current() const;

    // ── Cross-thread-safe page requests (set pending flags, consumed in render_update) ──
    void request_next_page();
    void request_prev_page();
    void request_goto_page(UIPageId id);

    // ── Wake activity: reset screensaver idle timer ─────────────────────
    void wake_activity();

private:
    UIManager() = default;
    UIManager(const UIManager&) = delete;
    UIManager& operator=(const UIManager&) = delete;

    void _register_page(UIPage* page, UIPageId id,
                        uint8_t col, uint8_t row, lv_dir_t dir);

    std::vector<UIPage*>   _pages;
    size_t                 _current = 0;
    int8_t                 _next_dir = 1;

    // Cross-thread pending flags
    volatile bool          _next_page_pending = false;
    volatile bool          _prev_page_pending = false;
    volatile bool          _goto_page_pending = false;
    volatile uint8_t       _goto_page_id = 0;

#if defined(LVGL_ENABLE)
    struct TileEntry { lv_obj_t* obj; uint8_t col; uint8_t row; };
    lv_obj_t*               _tileview      = nullptr;
    std::vector<TileEntry>  _tile_entries;

    lv_obj_t*  _s_start_tile = nullptr;
    lv_coord_t _s_rel_x = 0;
    lv_coord_t _s_rel_y = 0;

    static void _pressed_cb(lv_event_t* e);
    static void _released_cb(lv_event_t* e);
    static void _scroll_end_cb(lv_event_t* e);
    static void _scroll_begin_cb(lv_event_t* e);
#endif
};
