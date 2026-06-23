#pragma once

#include <stdint.h>
#include "hal.h"
#include "lvgl.h"
#include <vector>
#include <functional>

// ============================================================================
// UIPageId — page enum (order must match _register_page calls in init())
// Matches original display.cpp: LOADING/CONFIG/MINER/DASHBOARD/HR_HEALTH/CLOCK/MARKET/SETTING_SWARM
// ============================================================================
enum class UIPageId : uint8_t {
    LOADING        = 0,
    CONFIG         = 1,
    MINER          = 2,
    DASHBOARD      = 3,
    HR_HEALTH      = 4,
    CLOCK          = 5,
    MARKET         = 6,
    SETTING_SWARM  = 7,

    COUNT
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

    // Initialize with screen dimensions. Resolution-specific pages are selected
    // based on the closest match to the given width/height.
    void init(uint16_t width, uint16_t height);
    void set_sys_evt(EventGroupHandle_t sys_evt) { _sys_evt = sys_evt; }
    void set_ota_running(volatile bool* ota_running) { _ota_running = ota_running; }

    // Called periodically from LVGL task: sync active tile, refresh page, drive LVGL
    void render_update();

    // Page navigation
    void next_page();
    void prev_page();
    void goto_page(UIPageId id);

    size_t current() const;
    uint32_t page_enter_serial() const { return _page_enter_serial; }

    // ── Cross-thread-safe page requests (set pending flags, consumed in render_update) ──
    void request_next_page();
    void request_prev_page();
    void request_goto_page(UIPageId id);

    // ── Wake activity: reset screensaver idle timer ─────────────────────
    void wake_activity();
    void process_touch_sample(bool pressed, const lv_point_t* point);

    // ── Touch long-press factory reset (primary path on boards w/o user btn) ──
    void set_recover_factory_xsem(SemaphoreHandle_t s) { _recover_factory_xsem = s; }
    void set_force_config_xsem(SemaphoreHandle_t s) { _force_config_xsem = s; }
    int  factory_countdown() const { return _factory_cd; }   // <0 = inactive
    int  setup_countdown() const { return _setup_cd; }       // <0 = inactive
    bool factory_rebooting() const { return _factory_rebooting; }
    bool setup_rebooting() const { return _setup_rebooting; }
    void start_factory_countdown();
    void cancel_factory_countdown();
    void tick_factory_countdown();
    void start_setup_countdown();
    void cancel_setup_countdown();
    void tick_setup_countdown();

private:
    UIManager() = default;
    UIManager(const UIManager&) = delete;
    UIManager& operator=(const UIManager&) = delete;

    void _register_page(UIPage* page, UIPageId id,
                        uint8_t col, uint8_t row, lv_dir_t dir);

    std::vector<UIPage*>   _pages;
    size_t                 _current = 0;
    uint32_t               _page_enter_serial = 0;
    int8_t                 _next_dir = 1;
    uint32_t               _last_active_ms = 0;

    // Cross-thread pending flags
    volatile bool          _next_page_pending = false;
    volatile bool          _prev_page_pending = false;
    volatile bool          _goto_page_pending = false;
    volatile uint8_t       _goto_page_id = 0;
    volatile bool          _wake_pending = false;

    struct TileEntry { lv_obj_t* obj; uint8_t col; uint8_t row; };
    lv_obj_t*               _tileview      = nullptr;
    std::vector<TileEntry>  _tile_entries;

    lv_obj_t*  _s_start_tile = nullptr;
    lv_coord_t _s_release_scroll_x = 0;
    lv_coord_t _s_release_scroll_y = 0;
    bool       _s_was_special_state = false;
    EventGroupHandle_t _sys_evt = nullptr;
    volatile bool* _ota_running = nullptr;

    static void _pressed_cb(lv_event_t* e);
    static void _released_cb(lv_event_t* e);
    static void _scroll_end_cb(lv_event_t* e);
    static void _scroll_begin_cb(lv_event_t* e);

    int _tile_index_from_obj(lv_obj_t* obj) const;
    lv_obj_t* _resolve_target_tile(size_t departure_idx, lv_coord_t drift_x, lv_coord_t drift_y) const;

    // Touch long-press → factory-reset countdown
    static void _long_pressed_cb(lv_event_t* e);
    static void _long_press_repeat_cb(lv_event_t* e);
    static void _long_press_release_cb(lv_event_t* e);

    SemaphoreHandle_t _recover_factory_xsem = nullptr;
    SemaphoreHandle_t _force_config_xsem = nullptr;
    volatile int      _factory_cd = -1;     // seconds remaining (<0 = inactive)
    volatile int      _setup_cd = -1;       // seconds remaining (<0 = inactive)
    uint32_t          _lp_last_tick = 0;
    uint32_t          _setup_last_tick = 0;
    bool              _touch_pressed = false;
    bool              _factory_hold_consumed = false;
    bool              _factory_hold_cancelled = false;
    bool              _factory_rebooting = false;
    bool              _setup_hold_active = false;
    bool              _setup_hold_consumed = false;
    bool              _setup_rebooting = false;
    uint32_t          _touch_press_start_ms = 0;
    lv_point_t        _touch_press_point = {0, 0};
};
