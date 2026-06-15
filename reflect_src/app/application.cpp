#include "application.h"
// Original project headers (read-only reuse, no modification)
#include "board/board.h"
#include "drivers/asic/bm_hal.h"
#include "drivers/power/power_hal.h"
#include "drivers/fan/fan.h"
#include "drivers/displays/display.h"
#include "drivers/touch/ft6206.h"
#include "stratum/stratum.h"
#include "mining/miner.h"
#include "market/market.h"
#include "nvs/nvs_config.h"
#include "utils/logger/logger.h"

// LVGL + UI
#include "lvgl.h"
#include "ui/ui_manager.h"

// ============================================================================
//  Singleton
// ============================================================================
MinerApp& MinerApp::instance() {
    static MinerApp app;
    return app;
}

// ============================================================================
//  BootProgress helper
// ============================================================================
MinerApp::BootProgress::BootProgress(int total_stages)
    : step_inc(100 / total_stages) {}

void MinerApp::BootProgress::post(const char* msg, uint32_t c) {
    AppState::instance().loading.details.text  = String(msg);
    AppState::instance().loading.details.color = c;
    delay(300);
}

void MinerApp::BootProgress::next(const char* msg, uint32_t c) {
    step += step_inc;
    if (step > 100) step = 100;
    AppState::instance().loading.progress      = step;
    AppState::instance().loading.details.text  = String(msg);
    AppState::instance().loading.details.color = c;
    delay(1000);
}

void MinerApp::BootProgress::finish(const char* msg, uint32_t c) {
    step = 100;
    AppState::instance().loading.progress = 100;
    post(msg, c);
}

// ============================================================================
//  _create_task — task registry helper
// ============================================================================
BaseType_t MinerApp::_create_task(TaskFunction_t fn, const char* name,
                                   uint32_t stack_bytes, void* param,
                                   UBaseType_t prio, BaseType_t core) {
    TaskHandle_t h = NULL;
    BaseType_t ret = xTaskCreatePinnedToCore(fn, name, stack_bytes, param, prio, &h, core);
    if (ret == pdPASS) {
        _tasks.push_back({h, name, stack_bytes, prio});
    }
    return ret;
}

// ============================================================================
//  init() — hardware pre-init + board init + NVS load
// ============================================================================
bool MinerApp::init() {
    // ── Board detection and configuration ───────────────────────────────
    BoardModelType model = get_board_model();
    if (model == BOARD_UNKNOWN) {
        LOG_E("Unknown board model, halting.");
        while (true) { delay(1000); }
    }
    static BoardSpecConfig spec_storage = get_board_config(model);
    _board_spec = &spec_storage;
    hardware_pre_init(spec_storage);

    // ── NVS cache: load all persistent settings into RAM ─────────────────
    {
        _cache.hostname       = nvs_config_get_string(NVS_CONFIG_HOSTNAME, spec_storage.name.c_str());
        _cache.wifi_ssid      = nvs_config_get_string(NVS_CONFIG_WIFI_SSID, "NMTech-2.4G");
        _cache.wifi_pswd      = nvs_config_get_string(NVS_CONFIG_WIFI_PASS, "NMMiner2048");
        _cache.pool_url_pri   = nvs_config_get_string(NVS_CONFIG_STRATUM_URL_PRIMARY,
                                                      "stratum+tcp://solo.ckpool.org:3333");
        _cache.pool_url_fb    = nvs_config_get_string(NVS_CONFIG_STRATUM_URL_FALLBACK,
                                                      "stratum+tcp://xec.nmminer.com:3333");
        _cache.screen_brightness = nvs_config_get_u8(NVS_CONFIG_SCREEN_BRIGHTNESS,
                                                      spec_storage.preference.screen.brightness);
        _cache.screen_orient     = nvs_config_get_u8(NVS_CONFIG_FLIP_SCREEN,
                                                      spec_storage.preference.screen.flip ? 1 : 0);
        _cache.last_page         = nvs_config_get_u8(NVS_CONFIG_UI_LAST_PAGE, 0);
        _cache.time_format       = nvs_config_get_u8(NVS_CONFIG_TIME_FORMAT, 24);
        _cache.date_format       = nvs_config_get_string(NVS_CONFIG_DATE_FORMAT, "YYYY/MM/DD");
        _cache.timezone          = nvs_config_get_string(NVS_CONFIG_TIMEZONE, "8.0");
        _cache.need_cfg          = nvs_config_get_u8(NVS_CONFIG_FORCE_CONFIG, false);
        _cache.led_enable        = nvs_config_get_u8(NVS_CONFIG_LED_INDICATOR,
                                                      spec_storage.preference.led.enable);
        _cache.screen_saver_enable = nvs_config_get_u8(NVS_CONFIG_SCREEN_SAVER_ENABLE,
                                                        spec_storage.preference.screen.saver_enable);
        _cache.screen_saver_timeout = nvs_config_get_u32(NVS_CONFIG_SCREEN_SAVER_TIMEOUT,
                                                          spec_storage.preference.screen.saver_timeout);
        _cache.screen_saver_mode    = nvs_config_get_u8(NVS_CONFIG_SCREEN_SAVER_MODE, 0);
        _cache.coin_price_display   = nvs_config_get_string(NVS_CONFIG_PRICE_DISPLAY_COIN, "BTC");
        _cache.coin_watchlist       = nvs_config_get_string(NVS_CONFIG_COIN_WATCHLIST,
                                                              "BTC,ETH,LTC,BNB,DOGE,XRP,TRX,SOL");
    }

    // ── Allocate context structs ────────────────────────────────────────
    static SystemSync   sys_storage;
    static PowerStatus  pwr_storage;
    static FanCtx       fan_storage;
    static WifiCtx      wifi_storage;
    static ScanCtx      scan_storage;
    static SwarmCtx     swarm_storage;
    static UIPrefs      ui_prefs_storage;
    static OTACtx       ota_storage;
    static MarketCtx    market_storage;

    _sys      = &sys_storage;
    _pwr      = &pwr_storage;
    _fan      = &fan_storage;
    _wifi     = &wifi_storage;
    _scan     = &scan_storage;
    _swarm    = &swarm_storage;
    _ui_prefs = &ui_prefs_storage;
    _ota      = &ota_storage;
    _market   = &market_storage;

    // ── Initialize sync primitives ──────────────────────────────────────
    _sys->init_evt              = xEventGroupCreate();
    _sys->sys_evt               = xEventGroupCreate();
    _sys->reboot_xsem           = xSemaphoreCreateCounting(1, 0);
    _sys->nvs_save_xsem         = xSemaphoreCreateCounting(1, 0);
    _sys->recover_factory_xsem  = xSemaphoreCreateCounting(1, 0);
    _sys->force_config_xsem     = xSemaphoreCreateCounting(1, 0);
    _sys->brightness_update_xsem= xSemaphoreCreateCounting(1, 0);

    // ── Initialize WiFi context ─────────────────────────────────────────
    _wifi->status                = WL_DISCONNECTED;
    _wifi->rssi                  = 0;
    _wifi->reconnect_xsem        = xSemaphoreCreateCounting(1, 0);
    _wifi->client_connected      = false;
    _wifi->config_timeout_s      = 300;
    _wifi->force_config_required = _cache.need_cfg;
    memset(_wifi->ip,      0, sizeof(_wifi->ip));
    memset(_wifi->gateway, 0, sizeof(_wifi->gateway));
    memset(_wifi->subnet,  0, sizeof(_wifi->subnet));
    memset(_wifi->dns,     0, sizeof(_wifi->dns));

    // ── Initialize scan context ─────────────────────────────────────────
    _scan->mutex          = xSemaphoreCreateMutex();
    _scan->scan_required  = xSemaphoreCreateCounting(1, 0);
    _scan->last_scan_ms   = 0;
    _scan->scan_generation = 0;
    _scan->scan_progress  = 0;
    _scan->is_scanning    = false;
    _scan->wake_sem       = xSemaphoreCreateBinary();
    _scan->should_sleep   = false;
    _scan->awake          = false;

    // ── Initialize swarm context ────────────────────────────────────────
    _swarm->mutex            = xSemaphoreCreateMutex();
    _swarm->total_workers    = 1;
    _swarm->total_hr         = 0.0f;
    _swarm->best_session_bd  = 0.0f;
    _swarm->best_ever_bd     = 0.0f;
    _swarm->last_scan_gen    = UINT32_MAX;
    _swarm->scan             = _scan;
    _swarm->wake_sem         = xSemaphoreCreateBinary();
    _swarm->should_sleep     = false;
    _swarm->awake            = false;
    _swarm->net_mutex        = nullptr;  // set after _sched_net_mutex is created

    // ── Initialize UI prefs ─────────────────────────────────────────────
    _ui_prefs->screen.flip          = (_cache.screen_orient != 0);
    _ui_prefs->screen.auto_rolling  = spec_storage.preference.screen.auto_rolling;
    _ui_prefs->screen.saver_enable  = _cache.screen_saver_enable;
    _ui_prefs->screen.saver_timeout = _cache.screen_saver_timeout;
    _ui_prefs->screen.saver_mode    = _cache.screen_saver_mode;
    _ui_prefs->screen.brightness    = _cache.screen_brightness;
    _ui_prefs->led.enable           = _cache.led_enable;
    _ui_prefs->led.sleep            = false;
    _ui_prefs->current_page         = 0;
    _ui_prefs->last_active_ms       = 0;
    _ui_prefs->lvgl_mutex           = xSemaphoreCreateMutex();

    // ── Fan status init ─────────────────────────────────────────────────
    for (uint8_t i = 0; i < spec_storage.fans.size(); i++) {
        fan_status_t state;
        state.id        = i;
        state.self_test = false;
        state.speed     = nvs_config_get_u16(NVS_CONFIG_ASIC_FAN_SPEED, 100);
        state.rpm       = 0;
        _fan->list.push_back(state);
    }
    _fan->config = spec_storage.fans;

    // ── Touch driver ────────────────────────────────────────────────────
    _touch = new FT6206Class();
    if (_touch == nullptr) {
        LOG_E("FT6206Class creation failed");
        return false;
    }

    // ── ASIC driver ─────────────────────────────────────────────────────
    _asic = spec_storage.create_asic_instance(
        *spec_storage.asic.com_port, spec_storage.asic.com_baud_init,
        spec_storage.asic.rx_pin, spec_storage.asic.tx_pin, spec_storage.asic.rst_pin);
    if (_asic == nullptr) {
        LOG_E("ASIC driver creation failed");
        return false;
    }

    _miner = new AsicMinerClass(_asic);
    if (_miner == nullptr) {
        LOG_E("AsicMinerClass creation failed");
        return false;
    }

    // ── Power HAL ───────────────────────────────────────────────────────
    _power = spec_storage.create_power_instance(
        spec_storage.pwr.en_pins, spec_storage.pwr.adc_pins,
        spec_storage.pwr.vcore_regulator_pin,
        spec_storage.pwr.pgood_pin, spec_storage.pwr.dc_plug_pin);
    if (_power == nullptr) {
        LOG_E("Power HAL creation failed");
        return false;
    }
    _pwr->power = _power;

    if (spec_storage.setup_temp_hal) {
        spec_storage.setup_temp_hal(_power);
    }

    // ── Market instance ─────────────────────────────────────────────────
    _market_inst = new MarketClass();
    if (_market_inst == nullptr) {
        LOG_E("MarketClass creation failed");
        return false;
    }
    _market->market    = _market_inst;
    _market->connected = false;
    _market->wake_sem  = xSemaphoreCreateBinary();
    _market->should_sleep = false;
    _market->awake        = false;

    // ── Stratum (created here, pool config from NVS cache) ──────────────
    {
        String pri_url = _cache.pool_url_pri;
        pool_info_t pool_use;
        pool_use.ssl  = (pri_url.indexOf("ssl") != -1) || (pri_url.indexOf("tls") != -1);
        pool_use.url  = pri_url.substring(pri_url.indexOf(":") + 3, pri_url.lastIndexOf(":"));
        pool_use.port = pri_url.substring(pri_url.lastIndexOf(":") + 1).toInt();

        String default_user = String("user") + "." + spec_storage.name;
        stratum_info_t stratum_use;
        stratum_use.user = nvs_config_get_string(NVS_CONFIG_STRATUM_USER_PRIMARY,
                                                  default_user.c_str());
        stratum_use.pwd  = nvs_config_get_string(NVS_CONFIG_STRATUM_PASS_PRIMARY, "x");

        _stratum = new StratumClass(pool_use, stratum_use, 10);
        if (_stratum == nullptr) {
            LOG_E("StratumClass creation failed");
            return false;
        }
    }

    // ── OTA init ────────────────────────────────────────────────────────
    _ota->running          = false;
    _ota->progress         = 0;
    _ota->last_progress_ms = 0;
    memset(_ota->filename, 0, sizeof(_ota->filename));
    memset(_ota->error,    0, sizeof(_ota->error));

    LOG_I("MinerApp::init() done.");
    return true;
}

// ============================================================================
//  _begin_board_init — power init sequence, ASIC count, fan self-test
// ============================================================================
void MinerApp::_begin_board_init(BootProgress& boot) {
    boot.next("Power init...");

    _power->set_vcore_range(_board_spec->asic.min_vcore, _board_spec->asic.max_vcore);

    if (_power->is_dc_pluged()) LOG_I("DC power detected");
    else                        LOG_I("USB power detected");

    _power->init();
    _power->set_pll_0v8(PWR_ON);
    _power->set_vdd_1v8(PWR_ON);

    // Wait for VBUS stable
    while (_power->get_vbus() < _board_spec->pwr.vbus_min_required) {
        LOG_W("VBUS %.2fV, need %.2fV...",
              _power->get_vbus() / 1000.0f,
              _board_spec->pwr.vbus_min_required / 1000.0f);
        delay(1000);
    }
    xEventGroupSetBits(_sys->init_evt, (1 << 5));  // INIT_EVENT_VBUS_READY

    // Wait for fan ready + wifi connected before enabling Vcore
    xEventGroupWaitBits(_sys->init_evt,
        (1 << 1) | (1 << 2),   // FAN_READY | WIFI_STA_CONNECTED
        pdFALSE, pdTRUE, portMAX_DELAY);

    _power->set_vcore_voltage(_board_spec->asic.req_vcore);
    _power->set_vcore_status(PWR_ON);

    while (!_power->is_vcore_ready()) {
        delay(500);
        LOG_W("Waiting for Vcore...");
    }
    xEventGroupSetBits(_sys->init_evt, (1 << 7));  // INIT_EVENT_VCORE_READY

    LOG_I("Vcore ready: %d mV", _power->get_vcore());
    boot.next("Power ready.", 0x00FF00);
}

// ============================================================================
//  _begin_wifi_connect
// ============================================================================
void MinerApp::_begin_wifi_connect(BootProgress& boot) {
    if (_wifi->force_config_required) {
        boot.next("Entering WiFi config portal...");
        // TODO: start AP + config monitor
        LOG_W("Force config mode — TODO");
        while (true) { delay(1000); }  // placeholder
    }

    boot.next("WiFi connecting...");

    // TODO: actual WiFi STA connect logic
    // For now, just mark as connected so the rest of begin() can proceed
    _wifi->status = WL_CONNECTED;
    snprintf(_wifi->ip, sizeof(_wifi->ip), "192.168.1.100");
    xEventGroupSetBits(_sys->init_evt, (1 << 2));  // INIT_EVENT_WIFI_STA_CONNECTED

    boot.next("WiFi connected.", 0x00FF00);
}

// ============================================================================
//  _begin_display — LVGL + display driver init
// ============================================================================
void MinerApp::_begin_display(BootProgress& boot) {
    boot.next("Init display...");

    // ── LVGL core init ──────────────────────────────────────────────────
    lv_init();

    // ── Frame buffer + display driver registration ──────────────────────
    if (!_ui_init()) {
        LOG_E("UI init failed");
        return;
    }

    // ── UIManager: create tileview + register all pages ─────────────────
    // Pass board-specific screen dimensions for runtime resolution selection
    UIManager::instance().init(
        (uint16_t)_board_spec->tft.width,
        (uint16_t)_board_spec->tft.height);

    // ── Start LVGL rendering task ───────────────────────────────────────
    _create_task(_lvgl_thread_entry, "(lvgl)", 1024 * 5, nullptr,
                 TASK_PRIORITY_LVGL_DRV, 1);

    // ── Backlight ramp-up ───────────────────────────────────────────────
    uint16_t brightness = _cache.screen_brightness;
    for (int i = 0; i <= brightness; i++) {
        tft_bl_ctrl(i);
        delay(10);
    }

    boot.next("Display ready.", 0x00FF00);
}

// ============================================================================
//  _begin_infra — scan, swarm, webserver
// ============================================================================
void MinerApp::_begin_infra(BootProgress& boot) {
    boot.next("Starting infrastructure...");

    // Scan task
    // _create_task(_scan_thread_entry, "(scan)", 1024 * 3, _scan, 4, 0);

    // Swarm task
    _swarm->net_mutex = &_sched_net_mutex;
    // _create_task(_swarm_thread_entry, "(swarm)", 1024 * 4, _swarm, 5, 0);

    // Webserver task
    // _create_task(_webserver_thread_entry, "(web)", 1024 * 6, nullptr, 8, 0);

    boot.next("Infra ready.", 0x00FF00);
}

// ============================================================================
//  _begin_market — market data fetch task (on-demand)
// ============================================================================
void MinerApp::_begin_market(BootProgress& boot) {
    boot.next("Starting market...");

    // _create_task(_market_thread_entry, "(market)", 1024 * 3, _market, 7, 0);

    boot.next("Market ready.", 0x00FF00);
}

// ============================================================================
//  _begin_miners — stratum + miner threads
// ============================================================================
void MinerApp::_begin_miners(BootProgress& boot) {
    boot.next("Starting miners...");

    // ── MiningSharedCtx ─────────────────────────────────────────────────
    static MiningSharedCtx mining_shared;
    mining_shared.init();
    // Load persisted values from NVS
    {
        mining_shared.uptime_ever_base = nvs_config_get_u64(NVS_CONFIG_UPTIME, 0);
        String best_ever = nvs_config_get_string(NVS_CONFIG_BEST_EVER, "0");
        mining_shared.best_ever_diff  = strtoull(best_ever.c_str(), NULL, 10);
        mining_shared.block_hits      = nvs_config_get_u16(NVS_CONFIG_BLOCK_HITS, 0);
        LOG_D("Mining NVS: best_ever=%.0f, hits=%d, uptime_base=%u",
              mining_shared.best_ever_diff, mining_shared.block_hits,
              mining_shared.uptime_ever_base);
    }
    _miningShared = &mining_shared;

    // ── Stratum thread ──────────────────────────────────────────────────
    static StratumCtx stratum_ctx;
    stratum_ctx.shared = &mining_shared;
    // _create_task(_stratum_thread_entry, "(stratum)", 1024 * 12,
    //              &stratum_ctx, 21, 1);

    // ── Miner TX thread (job dispatch) ──────────────────────────────────
    static MinerCtx miner_tx_ctx;
    miner_tx_ctx.shared = &mining_shared;
    miner_tx_ctx.name   = "(asic_tx)";
    // _create_task(_miner_thread_entry, miner_tx_ctx.name, 1024 * 8,
    //              &miner_tx_ctx, 22, 1);

    // ── Miner RX thread (asic response) ─────────────────────────────────
    static MinerCtx miner_rx_ctx;
    miner_rx_ctx.shared = &mining_shared;
    miner_rx_ctx.name   = "(asic_rx)";
    // _create_task(_miner_thread_entry, miner_rx_ctx.name, 1024 * 7,
    //              &miner_rx_ctx, 23, 0);

    xEventGroupSetBits(_sys->init_evt, (1 << 11));  // INIT_EVENT_MINER_READY
    boot.next("Miners started.", 0x00FF00);
}

// ============================================================================
//  _on_page_changed — on-demand thread scheduler
// ============================================================================
void MinerApp::_on_page_changed(uint8_t new_page) {
    _sched_last_page = new_page;

    // TODO: based on current UI page, wake/sleep market, scan, swarm threads
    LOG_D("Page changed to %d", new_page);
}

// ============================================================================
//  print_stack_hwm
// ============================================================================
void MinerApp::print_stack_hwm() const {
    for (const auto& t : _tasks) {
        if (t.handle == NULL) continue;
        UBaseType_t hwm = uxTaskGetStackHighWaterMark(t.handle);
        LOG_I("%-15s  hwm=%u / %u", t.name, (unsigned)hwm, (unsigned)t.stack_bytes);
    }
}

// ============================================================================
//  begin() — main orchestration
// ============================================================================
void MinerApp::begin() {
    constexpr int BOOT_STAGE_TOTAL = 7;
    BootProgress boot(BOOT_STAGE_TOTAL);

    boot.post("Initializing...");

    _begin_board_init(boot);        // ① power / asic / fan
    _begin_wifi_connect(boot);      // ② WiFi
    _begin_display(boot);           // ③ LVGL + display
    _begin_infra(boot);             // ④ scan / swarm / web
    _sched_net_mutex = xSemaphoreCreateMutex();
    _begin_market(boot);            // ⑤ market
    _begin_miners(boot);            // ⑥ stratum + miners

    // ── Post-init: start permanent background threads ────────────────────
    _create_task(_tick_thread_entry, "(tick)", 1024 * 4, nullptr, 4, 1);

    boot.finish("Started!");
    LOG_I("MinerApp::begin() done.");
}

// ============================================================================
//  _tick_thread_entry — periodic tick (backlight, data bridge, NVS flush)
// ============================================================================
void MinerApp::_tick_thread_entry(void* args) {
    auto& app = MinerApp::instance();
    uint32_t uptime_s = 0;

    while (true) {
        delay(1000);
        uptime_s++;

        // ── Bridge mining data to AppState ──────────────────────────────
        if (app._miningShared) {
            auto* ms = app._miningShared;
            auto& m  = AppState::instance().miner;

            double hr = ms->hashrate_3m;
            m.hashrate.text = String(hr / 1e12, 1);  // TH/s

            m.blk_hit.text = String((int)ms->block_hits);
            m.shares.text  = String((int)ms->shares_rejected) + "/"
                           + String((int)ms->shares_accepted);

            String bd = String(ms->best_ever_diff, 0);
            m.local_diff.text = String(ms->best_session_diff, 0) + "/" + bd;
        }

        // ── Bridge WiFi data ────────────────────────────────────────────
        if (app._wifi) {
            AppState::instance().miner.ip.text   = String(app._wifi->ip);
            AppState::instance().miner.rssi.text = String(app._wifi->rssi);
        }

        // ── Bridge swarm data ───────────────────────────────────────────
        if (app._swarm && xSemaphoreTake(app._swarm->mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            auto& m = AppState::instance().miner;
            m.swarm_workers.text = String(app._swarm->total_workers);
            m.swarm_hr.text      = String(app._swarm->total_hr, 0);
            m.swarm_bd.text      = String(app._swarm->best_ever_bd, 0);
            xSemaphoreGive(app._swarm->mutex);
        }
    }
}

// ============================================================================
//  _lvgl_thread_entry — LVGL rendering loop
// ============================================================================
void MinerApp::_lvgl_thread_entry(void* args) {
    while (true) {
        UIManager::instance().render_update();
        delay(5);
    }
}

// ============================================================================
//  _ui_init() — allocate frame buffer, register display/touch drivers
// ============================================================================
bool MinerApp::_ui_init() {
    lv_coord_t w = (lv_coord_t)_board_spec->tft.width;
    lv_coord_t h = (lv_coord_t)_board_spec->tft.height;

    // ── Frame buffer ───────────────────────────────────────────────────
    static lv_color_t* buf = nullptr;
    static lv_disp_draw_buf_t draw_buf;

#ifdef BOARD_HAS_PSRAM
    const uint32_t buf_size = w * h;
    buf = (lv_color_t*)heap_caps_malloc(buf_size * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
#else
    const uint32_t buf_size = w * h / 10;
    buf = (lv_color_t*)malloc(buf_size * sizeof(lv_color_t));
#endif

    if (buf == nullptr) {
        LOG_E("Failed to allocate LVGL frame buffer");
        return false;
    }
    lv_disp_draw_buf_init(&draw_buf, buf, nullptr, buf_size);

    // ── Display driver ─────────────────────────────────────────────────
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = w;
    disp_drv.ver_res  = h;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.flush_cb = [](lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p) {
        uint16_t x1 = area->x1, y1 = area->y1;
        uint16_t x2 = area->x2, y2 = area->y2;
        tft_start_write();
        tft_set_addr_window(x1, y1, x2 - x1 + 1, y2 - y1 + 1);
        tft_push_colors((uint16_t*)color_p, (x2 - x1 + 1) * (y2 - y1 + 1), true);
        tft_end_write();
        lv_disp_flush_ready(drv);
    };
    lv_disp_drv_register(&disp_drv);

    LOG_I("LVGL display driver registered: %dx%d", w, h);
    return true;
}
