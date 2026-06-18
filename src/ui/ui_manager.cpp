#include "ui_manager.h"
#include "ui_layout.h"
#include "../utils/logger/logger.h"
#include "app/app_state.h"
#include "../app/system_events.h"
#include "../nvs/nvs_config.h"


// ============================================================================
//  Singleton
// ============================================================================
UIManager& UIManager::instance() {
    static UIManager mgr;
    return mgr;
}

static void s_save_last_page(size_t idx) {
    if (idx >= (size_t)UIPageId::COUNT || idx == (size_t)UIPageId::LOADING) {
        return;
    }
    nvs_config_set_u8(NVS_CONFIG_UI_LAST_PAGE, (uint8_t)idx);
}

// ============================================================================
//  All layout instances �� both resolutions compiled in, selected at runtime
// ============================================================================
static PageLoading240x135  s_load_135;
static PageConfig240x135   s_cfg_135;
static PageMiner240x135    s_miner_135;
static PageDashboard240x135 s_dash_135;
static PageHr_health240x135 s_health_135;
static PageClock240x135    s_clock_135;
static PageMarket240x135   s_market_135;
static PageSetting240x135  s_sett_135;

static PageLoading320x240  s_load_240;
static PageConfig320x240   s_cfg_240;
static PageMiner320x240    s_miner_240;
static PageDashboard320x240 s_dash_240;
static PageHr_health320x240 s_health_240;
static PageClock320x240    s_clock_240;
static PageMarket320x240   s_market_240;
static PageSetting320x240  s_sett_240;

// Helper: pick the correct layout array based on screen dimensions
static UIPage* s_pick_layouts(UIPageId id, uint16_t w, uint16_t h) {
    // Match by width �� NMAXE is 135 wide, QAxe++ is 240 wide
    bool is_135 = (h <= 160);
    switch (id) {
        case UIPageId::LOADING:        return is_135 ? (UIPage*)&s_load_135   : (UIPage*)&s_load_240;
        case UIPageId::CONFIG:         return is_135 ? (UIPage*)&s_cfg_135    : (UIPage*)&s_cfg_240;
        case UIPageId::MINER:          return is_135 ? (UIPage*)&s_miner_135  : (UIPage*)&s_miner_240;
        case UIPageId::DASHBOARD:      return is_135 ? (UIPage*)&s_dash_135   : (UIPage*)&s_dash_240;
        case UIPageId::HR_HEALTH:      return is_135 ? (UIPage*)&s_health_135 : (UIPage*)&s_health_240;
        case UIPageId::CLOCK:          return is_135 ? (UIPage*)&s_clock_135  : (UIPage*)&s_clock_240;
        case UIPageId::MARKET:         return is_135 ? (UIPage*)&s_market_135 : (UIPage*)&s_market_240;
        case UIPageId::SETTING_SWARM:  return is_135 ? (UIPage*)&s_sett_135   : (UIPage*)&s_sett_240;
        default: return nullptr;
    }
}

// ============================================================================
//  init() �� create tileview + register all pages
// ============================================================================
void UIManager::init(uint16_t w, uint16_t h) {
    // ���� Create tileview ��������������������������������������������������������������������������������������������������
    _tileview = lv_tileview_create(lv_scr_act());
    lv_obj_set_size(_tileview, w, h);
    lv_obj_set_style_bg_color(_tileview, lv_color_hex(0x000000), 0);
    lv_obj_set_scrollbar_mode(_tileview, LV_SCROLLBAR_MODE_OFF);

    lv_obj_add_event_cb(_tileview, _scroll_begin_cb, LV_EVENT_SCROLL_BEGIN, nullptr);
    lv_obj_add_event_cb(_tileview, _pressed_cb,      LV_EVENT_PRESSED,      nullptr);
    lv_obj_add_event_cb(_tileview, _released_cb,     LV_EVENT_RELEASED,     nullptr);
    lv_obj_add_event_cb(_tileview, _scroll_end_cb,   LV_EVENT_SCROLL_END,   nullptr);
    // Touch long-press → factory-reset countdown (primary path on user-button-less boards)
    lv_obj_add_event_cb(_tileview, _long_pressed_cb,        LV_EVENT_LONG_PRESSED,        nullptr);
    lv_obj_add_event_cb(_tileview, _long_press_repeat_cb,   LV_EVENT_LONG_PRESSED_REPEAT, nullptr);
    lv_obj_add_event_cb(_tileview, _long_press_release_cb,  LV_EVENT_RELEASED,            nullptr);
    lv_obj_add_event_cb(_tileview, _long_press_release_cb,  LV_EVENT_PRESS_LOST,          nullptr);

    // ���� Register all 8 pages (matching original display.cpp page order) ��
    // Tile layout: rows with horizontal swipe, columns for vertical grouping
    // 2D tile grid + per-page swipe directions — identical to legacy display.cpp:
    //   (col,row): LOADING(0,0) CONFIG(0,1) | MINER(1,0) DASHBOARD(1,1) HR_HEALTH(1,2)
    //              SETTING(2,0) MARKET(2,1) CLOCK(2,2)
    _register_page(s_pick_layouts(UIPageId::LOADING, w, h),       UIPageId::LOADING,       0, 0, LV_DIR_NONE);
    _register_page(s_pick_layouts(UIPageId::CONFIG, w, h),        UIPageId::CONFIG,        0, 1, LV_DIR_NONE);
    _register_page(s_pick_layouts(UIPageId::MINER, w, h),         UIPageId::MINER,         1, 0, (lv_dir_t)(LV_DIR_RIGHT | LV_DIR_BOTTOM));
    _register_page(s_pick_layouts(UIPageId::DASHBOARD, w, h),     UIPageId::DASHBOARD,     1, 1, (lv_dir_t)(LV_DIR_RIGHT | LV_DIR_TOP | LV_DIR_BOTTOM));
    _register_page(s_pick_layouts(UIPageId::HR_HEALTH, w, h),     UIPageId::HR_HEALTH,     1, 2, (lv_dir_t)(LV_DIR_RIGHT | LV_DIR_TOP));
    _register_page(s_pick_layouts(UIPageId::SETTING_SWARM, w, h), UIPageId::SETTING_SWARM, 2, 0, (lv_dir_t)(LV_DIR_LEFT  | LV_DIR_BOTTOM));
    _register_page(s_pick_layouts(UIPageId::MARKET, w, h),        UIPageId::MARKET,        2, 1, (lv_dir_t)(LV_DIR_LEFT  | LV_DIR_TOP | LV_DIR_BOTTOM));
    _register_page(s_pick_layouts(UIPageId::CLOCK, w, h),         UIPageId::CLOCK,         2, 2, (lv_dir_t)(LV_DIR_LEFT  | LV_DIR_TOP));

    LOG_I("UIManager: %u pages registered for %dx%d (%s)",
          (unsigned)_pages.size(), w, h, (h <= 160) ? "NMAXE" : "QAxe++");
}

// ============================================================================
//  _register_page �� create tile, call page->create(tile), store entry
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
//  render_update() �� sync active tile, refresh current page, drive LVGL
// ============================================================================
void UIManager::render_update() {
    // ���� Consume cross-thread page requests ������������������������������������������������������������
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
    if (_wake_pending) {
        _wake_pending = false;
        lv_disp_trig_activity(nullptr);   // reset LVGL inactivity timer (screensaver)
    }

    // ���� Refresh current page ����������������������������������������������������������������������������������������
    if (_current < _pages.size() && _pages[_current]) {
        _pages[_current]->update();
    }

    // ���� Drive LVGL ������������������������������������������������������������������������������������������������������������
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
        s_save_last_page(_current);
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
        s_save_last_page(_current);
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
    s_save_last_page(_current);
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
    // Called from non-LVGL threads (button). Defer the LVGL inactivity reset to
    // render_update() (LVGL thread) to keep all LVGL calls single-threaded.
    _wake_pending = true;
}

// ============================================================================
//  Touch long-press → factory-reset countdown (10 s hold). Runs on LVGL thread.
//  Mirrors legacy long_press_event_cb (BOARD_TOUCH_LONG_PRESS_TO_RECOVER).
// ============================================================================
static const int UI_FACTORY_HOLD_SEC = 10;

void UIManager::_long_pressed_cb(lv_event_t* e) {
    UIManager& self = instance();
    self._factory_cd   = UI_FACTORY_HOLD_SEC;
    self._lp_last_tick = millis();
    // Freeze page swiping during the countdown to avoid confusing mid-hold nav.
    lv_obj_clear_flag(self._tileview, LV_OBJ_FLAG_SCROLLABLE);
}

void UIManager::_long_press_repeat_cb(lv_event_t* e) {
    UIManager& self = instance();
    if (self._factory_cd < 0) return;
    if (millis() - self._lp_last_tick < 1000) return;
    self._lp_last_tick = millis();
    if (self._factory_cd > 0) self._factory_cd--;
    if (self._factory_cd <= 0) {
        self._factory_cd = -1;
        lv_obj_add_flag(self._tileview, LV_OBJ_FLAG_SCROLLABLE);
        if (self._recover_factory_xsem) xSemaphoreGive(self._recover_factory_xsem);
    }
}

void UIManager::_long_press_release_cb(lv_event_t* e) {
    UIManager& self = instance();
    if (self._factory_cd >= 0) {
        self._factory_cd = -1;   // cancelled — user released before 0
        lv_obj_add_flag(self._tileview, LV_OBJ_FLAG_SCROLLABLE);
    }
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
    self._s_was_special_state = false;
    if (self._sys_evt != nullptr) {
        const EventBits_t special_bits = SYS_EVENT_SCREEN_SAVER_TRIGGERED | SYS_EVENT_FIND_NEIGHBOR_TRIGGERED;
        self._s_was_special_state = (xEventGroupGetBits(self._sys_evt) & special_bits) != 0;
        xEventGroupClearBits(self._sys_evt,
            SYS_EVENT_MINER_BLOCK_HIT |
            SYS_EVENT_MINER_HIGH_DIFF_ACHIEVED |
            special_bits);
    }
    self.wake_activity();
}

void UIManager::_released_cb(lv_event_t* e) {
    UIManager& self = instance();
    lv_obj_t* tv = lv_event_get_target(e);
    self._s_release_scroll_x = lv_obj_get_scroll_x(tv);
    self._s_release_scroll_y = lv_obj_get_scroll_y(tv);
}

void UIManager::_scroll_end_cb(lv_event_t* e) {
    UIManager& self = instance();
    lv_obj_t* tv = lv_event_get_target(e);

    if (self._s_was_special_state && self._s_start_tile != nullptr) {
        lv_obj_set_tile(tv, self._s_start_tile, LV_ANIM_OFF);
        self._s_was_special_state = false;
    }
    lv_obj_t* release_tile = self._s_start_tile;
    lv_coord_t release_scroll_x = self._s_release_scroll_x;
    lv_coord_t release_scroll_y = self._s_release_scroll_y;
    self._s_start_tile = nullptr;

    if (release_tile != nullptr) {
        int departure_idx = self._tile_index_from_obj(release_tile);
        if (departure_idx >= 0) {
            lv_coord_t ref_x = lv_obj_get_x(release_tile);
            lv_coord_t ref_y = lv_obj_get_y(release_tile);
            lv_coord_t drift_x = release_scroll_x - ref_x;
            lv_coord_t drift_y = release_scroll_y - ref_y;
            if (lv_obj_t* target_tile = self._resolve_target_tile((size_t)departure_idx, drift_x, drift_y)) {
                lv_obj_t* snapped_tile = lv_tileview_get_tile_act(tv);
                if (target_tile != snapped_tile) {
                    lv_obj_set_tile(tv, target_tile, LV_ANIM_ON);
                }
            }
        }
    }

    lv_obj_t* active = lv_tileview_get_tile_act(tv);

    // Find current index
    for (size_t i = 0; i < self._tile_entries.size(); i++) {
        if (self._tile_entries[i].obj == active) {
            self._current = i;
            break;
        }
    }
    self._s_was_special_state = false;
    s_save_last_page(self._current);
    LOG_D("UIManager: scroll_end -> tile %u", (unsigned)self._current);
}

int UIManager::_tile_index_from_obj(lv_obj_t* obj) const {
    for (size_t i = 0; i < _tile_entries.size(); ++i) {
        if (_tile_entries[i].obj == obj) {
            return (int)i;
        }
    }
    return -1;
}

lv_obj_t* UIManager::_resolve_target_tile(size_t departure_idx, lv_coord_t drift_x, lv_coord_t drift_y) const {
    if (departure_idx >= _tile_entries.size()) {
        return nullptr;
    }

    const lv_coord_t threshold_x = lv_obj_get_width(_tileview) / 100;
    const lv_coord_t threshold_y = lv_obj_get_height(_tileview) / 100;
    size_t target_idx = departure_idx;
    bool has_target = false;

    if (LV_ABS(drift_x) > LV_ABS(drift_y)) {
        if (LV_ABS(drift_x) <= threshold_x) return nullptr;
        if (drift_x > 0) {
            switch ((UIPageId)departure_idx) {
                case UIPageId::MINER:         target_idx = (size_t)UIPageId::SETTING_SWARM; has_target = true; break;
                case UIPageId::DASHBOARD:     target_idx = (size_t)UIPageId::MARKET;        has_target = true; break;
                case UIPageId::HR_HEALTH:     target_idx = (size_t)UIPageId::CLOCK;         has_target = true; break;
                default: break;
            }
        } else {
            switch ((UIPageId)departure_idx) {
                case UIPageId::SETTING_SWARM: target_idx = (size_t)UIPageId::MINER;         has_target = true; break;
                case UIPageId::MARKET:        target_idx = (size_t)UIPageId::DASHBOARD;     has_target = true; break;
                case UIPageId::CLOCK:         target_idx = (size_t)UIPageId::HR_HEALTH;     has_target = true; break;
                default: break;
            }
        }
    } else {
        if (LV_ABS(drift_y) <= threshold_y) return nullptr;
        if (drift_y > 0) {
            switch ((UIPageId)departure_idx) {
                case UIPageId::MINER:         target_idx = (size_t)UIPageId::DASHBOARD;     has_target = true; break;
                case UIPageId::DASHBOARD:     target_idx = (size_t)UIPageId::HR_HEALTH;     has_target = true; break;
                case UIPageId::MARKET:        target_idx = (size_t)UIPageId::CLOCK;         has_target = true; break;
                case UIPageId::SETTING_SWARM: target_idx = (size_t)UIPageId::MARKET;        has_target = true; break;
                default: break;
            }
        } else {
            switch ((UIPageId)departure_idx) {
                case UIPageId::DASHBOARD:     target_idx = (size_t)UIPageId::MINER;         has_target = true; break;
                case UIPageId::HR_HEALTH:     target_idx = (size_t)UIPageId::DASHBOARD;     has_target = true; break;
                case UIPageId::CLOCK:         target_idx = (size_t)UIPageId::MARKET;        has_target = true; break;
                case UIPageId::MARKET:        target_idx = (size_t)UIPageId::SETTING_SWARM; has_target = true; break;
                default: break;
            }
        }
    }

    if (!has_target || target_idx >= _tile_entries.size() || _pages[target_idx] == nullptr) {
        return nullptr;
    }
    return _tile_entries[target_idx].obj;
}

