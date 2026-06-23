#include "application.h"

#include "lvgl.h"
#include "system_events.h"
#include "../thread/thread_entry.h"
#include "../nvs/nvs_config.h"
#include "../drivers/temp/temp_hal.h"
#include "../drivers/display/display_hal.h"
#include "../ui/ui_manager.h"
#include "../ui/overlay_manager.h"
#include "../ui/assets/images.h"
#include "../utils/helper.h"
#include "../utils/logger/logger.h"
#include "../utils/reboot_log/reboot_log.h"
#include "../version.h"
#include "../market/market_ctx.h"

MinerApp& MinerApp::instance() {
    static MinerApp app;
    return app;
}

MinerApp::BootProgress::BootProgress(int total_stages)
    : step_inc(100 / total_stages) {}

void MinerApp::BootProgress::post(const char* msg, uint32_t c) {
    AppState::instance().loading.details.text = String(msg);
    AppState::instance().loading.details.color = c;
}

void MinerApp::BootProgress::next(const char* msg, uint32_t c) {
    step += step_inc;
    if (step > 100) {
        step = 100;
    }
    post(msg, c);
}

void MinerApp::BootProgress::finish(const char* msg, uint32_t c) {
    step = 100;
    post(msg, c);
}

BaseType_t MinerApp::_create_task(TaskFunction_t fn, const char* name,
                                  uint32_t stack_bytes, void* param,
                                  UBaseType_t prio, BaseType_t core) {
    TaskHandle_t h = nullptr;
    BaseType_t ret = xTaskCreatePinnedToCore(fn, name, stack_bytes, param, prio, &h, core);
    if (ret == pdPASS) {
        _tasks.push_back({h, name, stack_bytes, prio});
    }
    return ret;
}

bool MinerApp::init() {
    // ── Stage 0: earliest boot bookkeeping + serial console ────────────────
    // 1) Recover and persist the previous boot's reboot trace from RTC memory.
    // 2) Bring up Serial first so every later init stage can log failures.
    // This stage must stay at the very top because later code may already set
    // reboot intents or emit logs we want to capture.
    reboot_log_init();

    Serial.setTimeout(20);
    Serial.begin(115200);
    delay(100);

    // ── Stage 1: bind singleton-owned shared state storage ──────────────────
    static SystemSync sys_storage;
    static WifiState wifi_storage;
    static SwarmState swarm_storage;
    static NeighborState neighbor_storage;

    _sys = &sys_storage;
    _wifi = &wifi_storage;
    _swarm = &swarm_storage;
    _neighbor = &neighbor_storage;

    // ── Stage 2: create core sync primitives / zero-cost shared runtime state ──
    // Build the process-wide synchronization objects first:
    // - init_evt/sys_evt coordinate boot milestones and runtime UI/events
    // - reboot_xsem centralizes deferred reboot requests from worker threads
    // Then reset the lightweight shared state blocks that threads will later
    // read and write without needing dynamic allocation.
    _sys->init_evt = xEventGroupCreate();
    _sys->sys_evt  = xEventGroupCreate();
    _sys->reboot_xsem = xSemaphoreCreateCounting(1, 0);

    // WiFi state starts disconnected. force_config_required is restored from
    // NVS so a previous long-press boot request can divert this boot straight
    // into AP setup mode before any STA or pool workflow proceeds.
    _wifi->status                = WL_DISCONNECTED;
    _wifi->rssi                  = 0;
    _wifi->reconnect_xsem        = xSemaphoreCreateCounting(1, 0);
    _wifi->client_connected      = false;
    _wifi->force_config_required = nvs_config_get_u8(NVS_CONFIG_FORCE_CONFIG, false);

    // Swarm/neighbor state is initialized to a safe empty topology so pages
    // and web APIs can render deterministic defaults before scans begin.
    _swarm->mutex         = xSemaphoreCreateMutex();
    _swarm->total_workers = 1;
    _swarm->total_hr      = 0.0f;
    _swarm->best_ever_bd  = 0.0f;

    _neighbor->mutex         = xSemaphoreCreateMutex();
    _neighbor->scan_required = xSemaphoreCreateCounting(1, 0);

    // ── Stage 3: detect board model and materialize the runtime board spec ──
    // get_board_model() internally debounces the selection pins (up to ~3 s).
    // After this point, _spec becomes the single source of truth for board-
    // specific pins, power rails, display geometry, ASIC type, and defaults.
    _model = get_board_model();
    if (_model == BOARD_UNKNOWN) {
        while (true) {
            LOG_E("Expected raw model pins: NMAXE=110, Gamma=010, QAxe++=101, QAxe++ Rev6.1=111.");
            delay(1000);
        }
    }
    _spec = get_board_config(_model);
    hardware_pre_init(_spec);
    LOG_I("board model detected: %s", _spec.display_name.c_str());

    // If benchmark mode was persisted, override the nominal ASIC operating
    // point here before any downstream object is created from _spec. This keeps
    // power/miner construction aligned with the benchmark resume state.
    if (nvs_config_get_u8(NVS_CONFIG_BM_MODE, 0) == 1) {
        uint16_t bm_freq  = nvs_config_get_u16(NVS_CONFIG_BM_CUR_FREQ, _spec.asic.req_frq);
        uint16_t bm_vcore = nvs_config_get_u16(NVS_CONFIG_BM_CUR_VCORE, _spec.asic.req_vcore);
        if (bm_vcore < _spec.asic.min_vcore) bm_vcore = _spec.asic.min_vcore;
        if (bm_vcore > _spec.asic.max_vcore) bm_vcore = _spec.asic.max_vcore;
        _spec.asic.req_frq   = bm_freq;
        _spec.asic.req_vcore = bm_vcore;
        LOG_W("[BM] Benchmark mode: freq=%dMHz vcore=%dmV", bm_freq, bm_vcore);
    }

    // ── Stage 4: load persistent user / network / UI configuration ─────────
    // Pull in all NVS-backed configuration that does not require hardware
    // objects yet:
    // - WiFi STA/AP identity
    // - time/date/UI preferences
    // - user-facing coins/watchlists
    // - page restore and control semaphores used by later threads

    // WiFi connection config (replaces g_board.info.connection.wifi)
    {
        String dev = gen_device_code();
        String ap_default = _spec.name + "_" + dev.substring(0, 5);
        _wifi_cfg.ap_ip      = IPAddress(192, 168, 4, 1);
        _wifi_cfg.ap_ssid    = nvs_config_get_string_value(NVS_CONFIG_AP_SSID,  ap_default.c_str());
        _wifi_cfg.sta_ssid   = nvs_config_get_string_value(NVS_CONFIG_WIFI_SSID, "NMTech-2.4G");
        _wifi_cfg.sta_pwd    = nvs_config_get_string_value(NVS_CONFIG_WIFI_PASS, "NMMiner2048");
        _wifi_cfg.hostname   = nvs_config_get_string_value(NVS_CONFIG_HOSTNAME, _wifi_cfg.ap_ssid.c_str());
        _wifi_cfg.board_name = _spec.name;
    }

    // These semaphores are control-plane signals consumed later by monitor,
    // daemon, web, and UI code. They exist before thread creation so every
    // context can safely capture them during begin().
    _nvs_save_xsem = xSemaphoreCreateCounting(1, 0);
    _recover_factory_xsem = xSemaphoreCreateCounting(1, 0);
    _force_config_xsem    = xSemaphoreCreateCounting(1, 0);
    _brightness_update_xsem = xSemaphoreCreateCounting(1, 0);

    // Time/UI restore:
    // - timezone + 12/24 h + date format feed clock/dashboard rendering
    // - last page restore is sanitized so LOADING/CONFIG never become boot
    //   landing pages after restart
    _tz      = nvs_config_get_string_value(NVS_CONFIG_TIMEZONE, "8.0");
    _bm_mode = nvs_config_get_u8(NVS_CONFIG_BM_MODE, 0);
    {
        uint8_t tf = nvs_config_get_u8(NVS_CONFIG_TIME_FORMAT, 24);
        _time.format.time = (tf == 12) ? 12 : 24;
    }
    _time.format.date = nvs_config_get_string_value(NVS_CONFIG_DATE_FORMAT, "YYYY/MM/DD");
    _last_ui_page = nvs_config_get_u8(NVS_CONFIG_UI_LAST_PAGE, (uint8_t)UIPageId::MINER);
    if (_last_ui_page >= (uint8_t)UIPageId::COUNT || _last_ui_page == (uint8_t)UIPageId::LOADING) {
        _last_ui_page = (uint8_t)UIPageId::MINER;
    }

    // Live user preferences start from board defaults in _spec and are then
    // overridden by NVS. Keeping that merge here makes display/LED behavior
    // deterministic before any related thread starts.
    _pref.screen.flip          = nvs_config_get_u8(NVS_CONFIG_FLIP_SCREEN,         _spec.preference.screen.flip);
    _pref.screen.auto_rolling  = nvs_config_get_u8(NVS_CONFIG_AUTO_SCREEN,         _spec.preference.screen.auto_rolling);
    _pref.screen.brightness    = nvs_config_get_u8(NVS_CONFIG_SCREEN_BRIGHTNESS,   _spec.preference.screen.brightness);
    _pref.screen.saver_enable  = nvs_config_get_u8(NVS_CONFIG_SCREEN_SAVER_ENABLE, _spec.preference.screen.saver_enable);
    _pref.screen.saver_timeout = nvs_config_get_u32(NVS_CONFIG_SCREEN_SAVER_TIMEOUT, _spec.preference.screen.saver_timeout);
    _pref.screen.saver_mode    = nvs_config_get_u8(NVS_CONFIG_SCREEN_SAVER_MODE,   0);
    _pref.led.enable           = nvs_config_get_u8(NVS_CONFIG_LED_INDICATOR,       _spec.preference.led.enable);
    _pref.led.sleep            = false;
    _pref.led.sleep_last       = _pref.led.sleep;

    _coin_price     = nvs_config_get_string_value(NVS_CONFIG_PRICE_DISPLAY_COIN, "BTC");
    _coin_watchlist = nvs_config_get_string_value(NVS_CONFIG_COIN_WATCHLIST, "BTC,ETH,LTC,BNB,DOGE,XRP,TRX,SOL");

    if (_bm_mode) {
        LOG_W("[BM] *** Benchmark mode active (bm_mode=%d) ***", _bm_mode);
    }

    // ── Stage 5: create shared runtime domains backed by the loaded config ──
    // Instantiate the shared long-lived runtime objects that threads will use:
    // - miner status / counters / pause state
    // - market client
    // - power HAL + temperature probe wiring
    // These depend on the board spec and loaded preferences above, but still
    // happen before worker thread creation.

    // Shared mining runtime state (replaces g_board.status.miner). Created here
    // so the power loop's controlled-idle check can reference it immediately.
    static MinerStatus miner_status;
    miner_status.init();
    _minerStatus = &miner_status;

    // Restore persisted miner counters so UI/web/logging start from the saved
    // lifetime values instead of resetting to zero on every reboot.
    {
        String best_ever = nvs_config_get_string_value(NVS_CONFIG_BEST_EVER, "0");
        _minerStatus->diff.best_ever = strtoull(best_ever.c_str(), nullptr, 10);
        _minerStatus->hits           = nvs_config_get_u16(NVS_CONFIG_BLOCK_HITS, 0);
        _minerStatus->uptime_ever    = nvs_config_get_u64(NVS_CONFIG_UPTIME, 0);
    }

    _market = new MarketClass();
    if (_market == nullptr) {
        LOG_E("MarketClass instance creation failed");
        return false;
    }

    // Build the board-specific power HAL from the resolved spec. This is the
    // one concrete hardware object many later threads share.
    _power = _spec.create_power_instance(
        _spec.pwr.en_pins, _spec.pwr.adc_pins,
        _spec.pwr.vcore_regulator_pin, _spec.pwr.pgood_pin, _spec.pwr.dc_plug_pin);
    if (_power == nullptr) {
        LOG_E("AxePower instance creation failed");
        return false;
    }
    // Some boards register temperature readers through the power domain.
    // Hook them here immediately after power HAL creation.
    if (_spec.setup_temp_hal) {
        _spec.setup_temp_hal(_power);
    }

    // ── Stage 6: build miners / pool connectivity objects from the spec + NVS ──
    // Final stage: parse persisted pool credentials/URLs, create the ASIC
    // driver, miner, and stratum client, and wire their dependencies together.
    // begin() can only launch worker threads after this succeeds.
    if (!_init_mining_instances()) {
        return false;
    }

    LOG_I("MinerApp::init ready");
    return true;
}

bool MinerApp::_init_mining_instances() {
    static ConnInfo conn;
    _conn = &conn;

    // Pool URLs (parse "stratum+tcp://host:port" / ssl|tls detection).
    String stratum_pri = nvs_config_get_string_value(NVS_CONFIG_STRATUM_URL_PRIMARY,  PRIMARY_POOL_URL);
    String stratum_fb  = nvs_config_get_string_value(NVS_CONFIG_STRATUM_URL_FALLBACK, FALLBACK_POOL_URL);

    conn.pool.primary.ssl  = (stratum_pri.indexOf("ssl") != -1) || (stratum_pri.indexOf("tls") != -1);
    conn.pool.primary.url  = stratum_pri.substring(stratum_pri.indexOf(":") + 3, stratum_pri.lastIndexOf(":"));
    conn.pool.primary.port = stratum_pri.substring(stratum_pri.lastIndexOf(":") + 1).toInt();
    conn.pool.fallback.ssl  = (stratum_fb.indexOf("ssl") != -1) || (stratum_fb.indexOf("tls") != -1);
    conn.pool.fallback.url  = stratum_fb.substring(stratum_fb.indexOf(":") + 3, stratum_fb.lastIndexOf(":"));
    conn.pool.fallback.port = stratum_fb.substring(stratum_fb.lastIndexOf(":") + 1).toInt();
    conn.pool.use           = conn.pool.primary;

    // Stratum users/passwords (default worker = "<USER>.<spec.name>_<dev5>").
    String device_code = gen_device_code();
    String default_user_pri = String(PRIMARY_USER)  + "." + _spec.name + "_" + device_code.substring(0, 5);
    String default_user_fb  = String(FALLBACK_USER) + "." + _spec.name + "_" + device_code.substring(0, 5);
    conn.stratum.primary.user  = nvs_config_get_string_value(NVS_CONFIG_STRATUM_USER_PRIMARY,  default_user_pri.c_str());
    conn.stratum.primary.pwd   = nvs_config_get_string_value(NVS_CONFIG_STRATUM_PASS_PRIMARY,  PRIMARY_POOL_PWD);
    conn.stratum.fallback.user = nvs_config_get_string_value(NVS_CONFIG_STRATUM_USER_FALLBACK, default_user_fb.c_str());
    conn.stratum.fallback.pwd  = nvs_config_get_string_value(NVS_CONFIG_STRATUM_PASS_FALLBACK, FALLBACK_POOL_PWD);
    conn.stratum.use           = conn.stratum.primary;

    // ASIC driver instance from the board spec factory.
    BMxxx* asic = _spec.create_asic_instance(
        *_spec.asic.com_port, _spec.asic.com_baud_init,
        _spec.asic.rx_pin, _spec.asic.tx_pin, _spec.asic.rst_pin);
    if (asic == nullptr) {
        LOG_E("BMxxx instance creation failed");
        return false;
    }

    _miner = new AsicMinerClass(asic);
    if (_miner == nullptr) { LOG_E("AsicMinerClass instance creation failed"); return false; }

    _stratum = new StratumClass(conn.pool.use, conn.stratum.use, 10);
    if (_stratum == nullptr) { LOG_E("StratumClass instance creation failed"); return false; }

    // Dependency injection (replaces former g_board cross-references).
    _miner->set_stratum(_stratum);
    _miner->set_asic_name(_spec.asic.name);
    _stratum->set_client_id(_spec.display_name + "/" + BOARD_CURRENT_FW_VERSION);

    LOG_I("Mining instances ready: asic=%s pool=%s:%d",
          _spec.asic.name.c_str(), conn.pool.use.url.c_str(), conn.pool.use.port);
    return true;
}

void MinerApp::_begin_board_init(BootProgress& boot) {
    String stage_msg = String("Board: ") + _spec.display_name;
    boot.next(stage_msg.c_str());
}

void MinerApp::_begin_fan(BootProgress& boot) {
    boot.next("Fan self-test...");

    // Initialize per-fan runtime status from spec + NVS (replaces g_board.status.fan.list)
    _fan_status.clear();
    uint16_t default_speed = nvs_config_get_u16(NVS_CONFIG_ASIC_FAN_SPEED, 100);
    for (uint8_t i = 0; i < _spec.fans.size(); i++) {
        fan_status_t state;
        state.id        = i;
        state.self_test = false;
        state.speed     = default_speed;
        state.rpm       = 0;
        _fan_status.push_back(state);
    }

    static FanCtx ctx;
    ctx.spec        = &_spec;
    ctx.init_evt    = _sys->init_evt;
    ctx.sys_evt     = _sys->sys_evt;
    ctx.status_list = &_fan_status;
    ctx.temp        = &_temp;
    _fan_ctx = &ctx;

    _create_task(fan_thread_entry, "(fan)", 1024 * 4, _fan_ctx, TASK_PRIORITY_FAN, 0);
}

void MinerApp::_begin_power(BootProgress& boot) {
    boot.next("Power up...");

    static PowerCtx ctx;
    ctx.power       = _power;
    ctx.spec        = &_spec;
    ctx.init_evt    = _sys->init_evt;
    ctx.sys_evt     = _sys->sys_evt;
    ctx.ota_running = &_ota.running;
    ctx.mining      = _minerStatus;    // controlled-idle check (nullptr until miners launch)
    _power_ctx = &ctx;

    _create_task(power_init_thread_entry, "(pwr_init)", 1024 * 7, _power_ctx, TASK_PRIORITY_PWR, 1);
    _create_task(power_loop_thread_entry, "(pwr_loop)", 1024 * 3, _power_ctx, TASK_PRIORITY_PWR, 1);
}

void MinerApp::_begin_wifi_connect(BootProgress& boot) {
    boot.next("WiFi connect...");

    static WifiCtx ctx;
    ctx.state    = _wifi;
    ctx.cfg      = &_wifi_cfg;
    ctx.init_evt = _sys->init_evt;
    _wifi_ctx = &ctx;

    _create_task(wifi_connect_thread_entry, "(wifi)", 1024 * 6, _wifi_ctx, TASK_PRIORITY_WIFI, 1);
}

void MinerApp::_begin_display(BootProgress& boot) {
    boot.next("Display init...");
    lv_init();
    _ui_init();
    xEventGroupSetBits(_sys->init_evt, INIT_EVENT_SCREEN_READY);
    _create_task(_lvgl_thread_entry, "(lvgl)", 1024 * 5, nullptr, TASK_PRIORITY_LVGL_DRV, 1);
}

void MinerApp::_begin_infra(BootProgress& boot) {
    boot.next("Daemon start...");

    static DaemonCtx ctx;
    ctx.reboot_xsem          = _sys->reboot_xsem;
    ctx.recover_factory_xsem = _recover_factory_xsem;
    ctx.wifi_reconnect_xsem  = _wifi->reconnect_xsem;
    ctx.init_evt             = _sys->init_evt;
    ctx.ota_running          = &_ota.running;
    ctx.wifi_status          = &_wifi->status;
    ctx.bm_mode              = &_bm_mode;
    ctx.status               = _minerStatus;
    ctx.wifi_cfg             = &_wifi_cfg;
    _daemon_ctx = &ctx;

    _create_task(daemon_thread_entry, "(daemon)", 1024 * 3, _daemon_ctx, TASK_PRIORITY_DAEMON, 0);

    static SwarmCtx swarm_ctx;
    swarm_ctx.swarm       = _swarm;
    swarm_ctx.neighbor    = _neighbor;
    swarm_ctx.status      = _minerStatus;
    swarm_ctx.ota_running = &_ota.running;
    swarm_ctx.sys_evt     = _sys->sys_evt;
    swarm_ctx.init_evt    = _sys->init_evt;
    _swarm_ctx = &swarm_ctx;

    _create_task(swarm_thread_entry, "(swarm)",    1024 * 4, _swarm_ctx, TASK_PRIORITY_SWARM, 0);
    _create_task(scan_thread_entry,  "(neighbor)", 1024 * 3, _swarm_ctx, TASK_PRIORITY_SCAN,  0);

    static ButtonCtx button_ctx;
    button_ctx.spec                 = &_spec;
    button_ctx.init_evt             = _sys->init_evt;
    button_ctx.sys_evt              = _sys->sys_evt;
    button_ctx.force_config_xsem    = _force_config_xsem;
    button_ctx.recover_factory_xsem = _recover_factory_xsem;
    button_ctx.ota_running          = &_ota.running;
    // UI navigation hooks (non-capturing lambdas -> function pointers). Thread-safe:
    // these set pending flags consumed by the LVGL thread in render_update().
    button_ctx.on_next_page = [](){ UIManager::instance().request_next_page(); };
    button_ctx.on_prev_page = [](){ UIManager::instance().request_prev_page(); };
    button_ctx.on_activity  = [](){ UIManager::instance().wake_activity(); };
    _button_ctx = &button_ctx;

    _create_task(button_thread_entry, "(button)", 1024 * 2, _button_ctx, TASK_PRIORITY_BTN, 1);

    static LedCtx led_ctx;
    led_ctx.spec         = &_spec;
    led_ctx.pref         = &_pref;
    led_ctx.stratum      = _stratum;
    led_ctx.status       = _minerStatus;
    led_ctx.wifi_status  = &_wifi->status;
    led_ctx.ota_running  = &_ota.running;
    led_ctx.ota_progress = &_ota.progress;
    _led_ctx = &led_ctx;

    _create_task(led_thread_entry, "(led)", 1024 * 2 + 512, _led_ctx, TASK_PRIORITY_LED, 1);

    static WebCtx web_ctx;
    web_ctx.miner          = _miner;
    web_ctx.stratum        = _stratum;
    web_ctx.market         = _market;
    web_ctx.power          = _power;
    web_ctx.spec           = &_spec;
    web_ctx.status         = _minerStatus;
    web_ctx.conn           = _conn;
    web_ctx.wifi           = _wifi;
    web_ctx.wifi_cfg       = &_wifi_cfg;
    web_ctx.pwr            = &_pwr_tele;
    web_ctx.temp           = &_temp;
    web_ctx.time           = &_time;
    web_ctx.ota            = &_ota;
    web_ctx.pref           = &_pref;
    web_ctx.neighbor       = _neighbor;
    web_ctx.fan_status     = &_fan_status;
    web_ctx.bm_mode        = &_bm_mode;
    web_ctx.utc            = &_utc;
    web_ctx.tz             = &_tz;
    web_ctx.coin_price     = &_coin_price;
    web_ctx.coin_watchlist = &_coin_watchlist;
    web_ctx.fw_version     = BOARD_CURRENT_FW_VERSION;
    web_ctx.sys_evt        = _sys->sys_evt;
    web_ctx.init_evt       = _sys->init_evt;
    web_ctx.reboot_xsem    = _sys->reboot_xsem;
    web_ctx.brightness_update_xsem = _brightness_update_xsem;
    _web_ctx = &web_ctx;

    _create_task(webserver_thread_entry, "(webserver)", 1024 * 4, _web_ctx, TASK_PRIORITY_WS, 0);
}

void MinerApp::_begin_market(BootProgress& boot) {
    boot.next("Market start...");

    static MarketCtx ctx;
    ctx.market         = _market;
    ctx.wifi_status    = &_wifi->status;
    ctx.ota_running    = &_ota.running;
    ctx.sys_evt        = _sys->sys_evt;
    ctx.coin_price     = _coin_price;
    ctx.coin_watchlist = _coin_watchlist;
    _market_ctx = &ctx;

    _create_task(market_thread_entry, "(market)", 1024 * 4, _market_ctx, TASK_PRIORITY_MARKET, 0);

    // static AphorismCtx aph_ctx;
    // _aphorism.mutex     = xSemaphoreCreateMutex();
    // aph_ctx.state       = &_aphorism;
    // aph_ctx.wifi_status = &_wifi->status;
    // aph_ctx.ota_running = &_ota.running;
    // _aphorism_ctx = &aph_ctx;

    // _create_task(aphorism_thread_entry, "(aphorism)", 1024 * 6, _aphorism_ctx, TASK_PRIORITY_APHORISM, 0);
}

void MinerApp::_begin_miners(BootProgress& boot) {
    boot.next("ASIC bring-up...");

    static MinerCtx ctx;
    ctx.miner         = _miner;
    ctx.stratum       = _stratum;
    ctx.power         = _power;
    ctx.spec          = &_spec;
    ctx.status        = _minerStatus;
    ctx.conn          = _conn;
    ctx.init_evt      = _sys->init_evt;
    ctx.sys_evt       = _sys->sys_evt;
    ctx.nvs_save_xsem = _nvs_save_xsem;
    ctx.ota_running   = &_ota.running;
    ctx.utc           = &_utc;
    ctx.wifi_status   = &_wifi->status;
    ctx.wifi_reconnect_xsem = _wifi->reconnect_xsem;
    ctx.fw_version    = BOARD_CURRENT_FW_VERSION;
    _miner_ctx = &ctx;

    _create_task(miner_count_thread_entry, "(asic_cnt)",  1024 * 5,  _miner_ctx, TASK_PRIORITY_ASIC_CNT,  1);
    _create_task(miner_init_thread_entry,  "(asic_init)", 1024 * 6,  _miner_ctx, TASK_PRIORITY_ASIC_INIT, 1);
    _create_task(stratum_thread_entry,     "(stratum)",   1024 * 5,  _miner_ctx, TASK_PRIORITY_STRATUM,   1);
    _create_task(miner_tx_thread_entry,    "(asic_tx)",   1024 * 4,  _miner_ctx, TASK_PRIORITY_MINER_TX,  1);
    _create_task(miner_rx_thread_entry,    "(asic_rx)",   1024 * 4,  _miner_ctx, TASK_PRIORITY_MINER_RX,  0);

    static MonitorCtx mctx;
    mctx.power             = _power;
    mctx.spec             = &_spec;
    mctx.miner            = _miner;
    mctx.status           = _minerStatus;
    mctx.pwr              = &_pwr_tele;
    mctx.temp             = &_temp;
    mctx.fan_status       = &_fan_status;
    mctx.wifi_status      = &_wifi->status;
    mctx.wifi_rssi        = &_wifi->rssi;
    mctx.utc              = &_utc;
    mctx.tz               = &_tz;
    mctx.ota_running      = &_ota.running;
    mctx.bm_mode          = &_bm_mode;
    mctx.reboot_xsem      = _sys->reboot_xsem;
    mctx.nvs_save_xsem    = _nvs_save_xsem;
    mctx.force_config_xsem = _force_config_xsem;
    mctx.init_evt         = _sys->init_evt;
    mctx.sys_evt          = _sys->sys_evt;
    _monitor_ctx = &mctx;

    _create_task(monitor_thread_entry, "(monitor)", 1024 * 4, _monitor_ctx, TASK_PRIORITY_MONITOR, 1);

    static BenchmarkCtx bctx;
    bctx.bm_mode     = &_bm_mode;
    bctx.bm          = &_bm;
    bctx.miner       = _miner;
    bctx.status      = _minerStatus;
    bctx.pwr         = &_pwr_tele;
    bctx.temp        = &_temp;
    bctx.reboot_xsem = _sys->reboot_xsem;
    bctx.init_evt    = _sys->init_evt;
    _benchmark_ctx = &bctx;

    _create_task(benchmark_thread_entry, "(benchmark)", 1024 * 6, _benchmark_ctx, TASK_PRIORITY_MONITOR, 0);
}

void MinerApp::begin() {
    constexpr int BOOT_STAGE_TOTAL = 8;
    BootProgress boot(BOOT_STAGE_TOTAL);
    
    _begin_board_init(boot);
    _begin_fan(boot);
    _begin_power(boot);
    _begin_wifi_connect(boot);
    _begin_display(boot);
    _begin_infra(boot);
    _begin_market(boot);
    _begin_miners(boot);
    _create_task(_tick_thread_entry, "(tick)", 1024 * 4, nullptr, TASK_PRIORITY_APP_TICK, 1);
    boot.post("Booting...");
}

void MinerApp::print_stack_hwm() const {
    static uint32_t last_print_ms = 0;
    if (millis() - last_print_ms < 3000) return;
    last_print_ms = millis();

    LOG_W("=========== Stack High Water Mark (in bytes) ===========");
    LOG_I("+-----------------+----------+------------+------------+");
    LOG_I("| Task Name       | HWM      | Total Stack| Optimizable|");
    LOG_I("+-----------------+----------+------------+------------+");
    for (const auto& t : _tasks) {
        if (t.name == nullptr || t.name[0] == '\0') continue;

        TaskHandle_t live_handle = xTaskGetHandle(t.name);
        if (live_handle == nullptr) continue;

        UBaseType_t hwm = uxTaskGetStackHighWaterMark(live_handle);
        uint32_t total = t.stack_bytes;
        uint32_t optimizable = (hwm > 512) ? (hwm - 512) : 0;
        LOG_I("| %-15s | %8u | %10u | %10u |",
              t.name, (unsigned)hwm, (unsigned)total, (unsigned)optimizable);
    }
    LOG_I("+-----------------+----------+------------+------------+");
    LOG_W("Note: Optimizable = HWM - 512 (keeping 512 bytes safety buffer)");
}

void MinerApp::_tick_thread_entry(void* args) {
    (void)args;
    auto& app = MinerApp::instance();
    static const char* const VBUS_CHK_STR[] = {
        "Vbus check   ", "Vbus check.  ", "Vbus check.. ", "Vbus check..."
    };
    static const char* const WIFI_CON_STR[] = {
        "Wifi connect   ", "Wifi connect.  ", "Wifi connect.. ", "Wifi connect..."
    };
    static const char* const ASIC_INIT_STR[] = {
        "ASIC init  ", "ASIC init.  ", "ASIC init.. ", "ASIC init..."
    };
    static const char* const TMP_CHK_STR[] = {
        "temp sensor check   ", "temp sensor check.  ",
        "temp sensor check.. ", "temp sensor check..."
    };
    static const char* const FAN_POLARITY_STR[] = {
        "Fan polarity check   ", "Fan polarity check.  ",
        "Fan polarity check.. ", "Fan polarity check..."
    };
    static const char* const FAN_SELF_TEST_STR[] = {
        "Fan test   ", "Fan test.  ", "Fan test.. ", "Fan test..."
    };
    static const char* const VCORE_CHK_STR[] = {
        "Vcore check   ", "Vcore check.  ", "Vcore check.. ", "Vcore check..."
    };
    static const char* const POOL_CON_STR[] = {
        "Pool connect   ", "Pool connect.  ", "Pool connect.. ", "Pool connect..."
    };
    static const char* const POOL_AUTH_STR[] = {
        "Pool auth   ", "Pool auth.  ", "Pool auth.. ", "Pool auth..."
    };
    static const char* const WAIT_JOB_STR[] = {
        "Waiting pool job   ", "Waiting pool job.  ",
        "Waiting pool job.. ", "Waiting pool job..."
    };

    enum class LoadingStage : uint8_t {
        WAIT_ADC,
        WAIT_VBUS,
        WAIT_WIFI,
        WAIT_ASIC,
        WAIT_ASIC_CONFIRM,
        WAIT_TMP,
        WAIT_TMP_CONFIRM,
        WAIT_FAN_POLARITY,
        WAIT_FAN_POLARITY_CONFIRM,
        WAIT_FAN_READY,
        WAIT_FAN_READY_CONFIRM,
        WAIT_VCORE,
        WAIT_VCORE_CONFIRM,
        WAIT_POOL_CONNECT,
        WAIT_POOL_CONNECT_CONFIRM,
        WAIT_POOL_AUTH,
        WAIT_POOL_AUTH_CONFIRM,
        WAIT_POOL_JOB,
        READY_CONFIRM,
    };

    bool     tmp_ready = false;
    uint32_t last_temp_ms = 0;
    uint32_t last_ui_ms   = 0;
    uint32_t last_roll_ms = 0;       // auto page-rolling cadence
    uint32_t last_cfg_timeout_ms = 0; // NMQAxe++ AP config timeout cadence
    bool     ss_active    = false;   // screensaver state (this thread owns backlight)
    bool     ui_switched  = false;   // one-shot LOADING -> MINER after boot
    float    bl_wave      = 0.0f;    // legacy-style event backlight pulse
    uint32_t last_bl_ms   = millis();
    LoadingStage loading_stage = LoadingStage::WAIT_ADC;
    uint32_t loading_stage_ms = millis();
    uint32_t loading_detail_ms = 0;

    xEventGroupWaitBits(app._sys->init_evt, INIT_EVENT_SCREEN_READY, pdFALSE, pdTRUE, portMAX_DELAY);
    uint8_t boot_brightness = app._pref.screen.brightness ? app._pref.screen.brightness : 80;
    for (uint8_t i = 0; i < boot_brightness; ++i) {
        tft_bl_ctrl(i, &app._spec);
        delay(10);
    }
    tft_bl_ctrl(boot_brightness, &app._spec);

    auto set_loading = [&](int32_t progress, const String& text, uint32_t color) {
        AppState::instance().loading.progress = progress;
        AppState::instance().loading.details.text = text;
        AppState::instance().loading.details.color = color;
    };
    auto advance_loading = [&](LoadingStage next, uint32_t now) {
        loading_stage = next;
        loading_stage_ms = now;
        loading_detail_ms = 0;
    };

    while (true) {
        delay(10);
        uint32_t now = millis();
        uint32_t delta_bl_ms = now - last_bl_ms;
        last_bl_ms = now;
        EventBits_t ib = app._sys ? xEventGroupGetBits(app._sys->init_evt) : 0;

        if (!ui_switched) {
            uint32_t stage_elapsed = now - loading_stage_ms;
            uint8_t anim_idx = (uint8_t)((stage_elapsed / 300) % 4);
            bool blink_500ms = (((stage_elapsed / 500) & 1U) == 0U);

            switch (loading_stage) {
                case LoadingStage::WAIT_ADC:
                    set_loading(10, VBUS_CHK_STR[anim_idx], 0xFFFFFF);
                    if (app._power && app._power->is_adc_ready()) {
                        advance_loading(LoadingStage::WAIT_VBUS, now);
                    }
                    break;

                case LoadingStage::WAIT_VBUS:
                    if (stage_elapsed < 500) {
                        set_loading(20,
                            app._power && app._power->is_dc_pluged() ? "DC pluged." : "USB pluged.",
                            0x00FF00);
                    } else {
                        bool vbus_ready = app._power &&
                                          app._power->get_vbus() >= app._spec.pwr.vbus_min_required;
                        if (vbus_ready) {
                            if (loading_detail_ms == 0 || now - loading_detail_ms >= 300) {
                                String vbus = "Vbus " + String(app._power->get_vbus() / 1000.0, 3) + "V.";
                                set_loading(20, vbus, 0x00FF00);
                                loading_detail_ms = now;
                            }
                            if (stage_elapsed >= 1000) {
                                advance_loading(LoadingStage::WAIT_WIFI, now);
                            }
                        } else if (app._power) {
                            if (loading_detail_ms == 0 || now - loading_detail_ms >= 300) {
                                String vbus = "Vbus " + String(app._power->get_vbus() / 1000.0, 1) +
                                              "v(at least" +
                                              String(app._spec.pwr.vbus_min_required / 1000.0, 1) + "v)";
                                uint32_t color = blink_500ms ? 0xFF0000 : 0xFFFFFF;
                                set_loading(20, vbus, color);
                                loading_detail_ms = now;
                            }
                            if (stage_elapsed >= 1500) {
                                advance_loading(LoadingStage::WAIT_WIFI, now);
                            }
                        }
                    }
                    break;

                case LoadingStage::WAIT_WIFI:
                    set_loading(30,
                        String(WIFI_CON_STR[anim_idx]) + "[" + app._wifi_cfg.sta_ssid + "]",
                        0xFFFFFF);
                    if ((ib & INIT_EVENT_WIFI_STA_CONNECTED) != 0) {
                        set_loading(30, "Wifi Connected!", 0x00FF00);
                        advance_loading(LoadingStage::WAIT_ASIC, now);
                    }
                    break;

                case LoadingStage::WAIT_ASIC:
                    if (loading_detail_ms == 0 || now - loading_detail_ms >= 100) {
                        uint8_t asic_anim_idx = (uint8_t)((stage_elapsed / 100) % 4);
                        set_loading(40, ASIC_INIT_STR[asic_anim_idx], 0xFFFFFF);
                        loading_detail_ms = now;
                    }
                    if (stage_elapsed >= 300 && app._miner && app._miner->get_asic_count() > 0) {
                        uint8_t asic_cnt = app._miner->get_asic_count();
                        String asic_cnt_str = (asic_cnt > 1)
                            ? (String(asic_cnt) + "/" + String(app._spec.asic.num_req) + " chips")
                            : "1 chip";
                        uint32_t color = (asic_cnt != app._spec.asic.num_req) ? 0xFF0000 : 0x00FF00;
                        set_loading(40, "Found " + asic_cnt_str, color);
                        advance_loading(LoadingStage::WAIT_ASIC_CONFIRM, now);
                    }
                    break;

                case LoadingStage::WAIT_ASIC_CONFIRM:
                    if (now - loading_stage_ms >= 3000 && (ib & INIT_EVENT_ASIC_COUNTED) != 0) {
                        advance_loading(LoadingStage::WAIT_TMP, now);
                    }
                    break;

                case LoadingStage::WAIT_TMP: {
                    float t_vrm  = temp_hal_get_vcore();
                    float t_asic = temp_hal_get_asic();
                    String vrm_str  = isnan(t_vrm)  ? "NAN" : (String(t_vrm, 1) + "C");
                    String asic_str = isnan(t_asic) ? "NAN" : (String(t_asic, 1) + "C");
                    set_loading(50, String(TMP_CHK_STR[anim_idx]) + " " + vrm_str + "/" + asic_str, 0xFFFFFF);
                    if ((ib & INIT_EVENT_TMP_READY) != 0) {
                        set_loading(50, String("Temp Pass  ") + vrm_str + "/" + asic_str, 0x00FF00);
                        advance_loading(LoadingStage::WAIT_TMP_CONFIRM, now);
                    }
                    break;
                }

                case LoadingStage::WAIT_TMP_CONFIRM:
                    if (now - loading_stage_ms >= 700) {
                        advance_loading(LoadingStage::WAIT_FAN_POLARITY, now);
                    }
                    break;

                case LoadingStage::WAIT_FAN_POLARITY:
                    set_loading(50, FAN_POLARITY_STR[anim_idx], 0xFFFFFF);
                    if ((ib & INIT_EVENT_FAN_POLARITY_DETECT) != 0) {
                        set_loading(50, "Fan polarity pass!", 0x00FF00);
                        advance_loading(LoadingStage::WAIT_FAN_POLARITY_CONFIRM, now);
                    }
                    break;

                case LoadingStage::WAIT_FAN_POLARITY_CONFIRM:
                    if (now - loading_stage_ms >= 1000) {
                        advance_loading(LoadingStage::WAIT_FAN_READY, now);
                    }
                    break;

                case LoadingStage::WAIT_FAN_READY: {
                    String fan_msg = FAN_SELF_TEST_STR[anim_idx];
                    if (!app._fan_status.empty()) {
                        fan_msg += String(app._fan_status[0].rpm) + "/ " +
                                   String(app._spec.fans[0].init.self_test_rpm_thr) + "rpm";
                    }
                    set_loading(50, fan_msg, 0xFFFFFF);
                    if ((ib & INIT_EVENT_FAN_READY) != 0) {
                        String pass_msg = "Fan Pass!";
                        if (!app._fan_status.empty()) {
                            pass_msg = "Fan Pass! [" + String(app._fan_status[0].rpm) + "/ " +
                                       String(app._spec.fans[0].init.self_test_rpm_thr) + " rpm]";
                        }
                        set_loading(50, pass_msg, 0x00FF00);
                        advance_loading(LoadingStage::WAIT_FAN_READY_CONFIRM, now);
                    }
                    break;
                }

                case LoadingStage::WAIT_FAN_READY_CONFIRM:
                    if (now - loading_stage_ms >= 1000) {
                        advance_loading(LoadingStage::WAIT_VCORE, now);
                    }
                    break;

                case LoadingStage::WAIT_VCORE:
                    set_loading(60, VCORE_CHK_STR[anim_idx], 0xFFFFFF);
                    if ((ib & INIT_EVENT_VCORE_READY) != 0) {
                        String vcore = "Vcore " + String(app._power->get_vcore() / 1000.0, 3) + "v.";
                        set_loading(60, vcore, 0x00FF00);
                        advance_loading(LoadingStage::WAIT_VCORE_CONFIRM, now);
                    }
                    break;

                case LoadingStage::WAIT_VCORE_CONFIRM:
                    if (now - loading_stage_ms >= 1000) {
                        advance_loading(LoadingStage::WAIT_POOL_CONNECT, now);
                    }
                    break;

                case LoadingStage::WAIT_POOL_CONNECT:
                    if (app._stratum && app._stratum->is_subscribed()) {
                        set_loading(75, "Pool connected!", 0x00FF00);
                        advance_loading(LoadingStage::WAIT_POOL_CONNECT_CONFIRM, now);
                    } else if (app._stratum && app._stratum->pool->get_last_errormsg().length() > 0) {
                        uint32_t color = blink_500ms ? 0xFFFFFF : 0xFF0000;
                        set_loading(75, app._stratum->pool->get_last_errormsg(), color);
                    } else {
                        String con_type = app._conn && app._conn->pool.use.ssl ? "[ssl]" : "[tcp]";
                        set_loading(75, String(POOL_CON_STR[anim_idx]) + con_type, 0xFFFFFF);
                    }
                    break;

                case LoadingStage::WAIT_POOL_CONNECT_CONFIRM:
                    if (now - loading_stage_ms >= 100) {
                        advance_loading(LoadingStage::WAIT_POOL_AUTH, now);
                    }
                    break;

                case LoadingStage::WAIT_POOL_AUTH:
                    if (app._stratum && app._stratum->is_authorized()) {
                        set_loading(85, "Pool authorized!", 0x00FF00);
                        advance_loading(LoadingStage::WAIT_POOL_AUTH_CONFIRM, now);
                    } else if (now - loading_stage_ms >= 6000) {
                        uint32_t color = blink_500ms ? 0xFFFFFF : 0xFF0000;
                        set_loading(85, "Wrong stratum user!", color);
                    } else {
                        set_loading(85, POOL_AUTH_STR[anim_idx], 0xFFFFFF);
                    }
                    break;

                case LoadingStage::WAIT_POOL_AUTH_CONFIRM:
                    if (now - loading_stage_ms >= 100) {
                        advance_loading(LoadingStage::WAIT_POOL_JOB, now);
                    }
                    break;

                case LoadingStage::WAIT_POOL_JOB:
                    if (app._stratum && app._stratum->get_job_counter() > 0) {
                        set_loading(100, "Miner ready!", 0x00FF00);
                        advance_loading(LoadingStage::READY_CONFIRM, now);
                    } else if (now - loading_stage_ms >= 60000) {
                        uint32_t color = blink_500ms ? 0xFFFFFF : 0xFF0000;
                        set_loading(100, "Pool job timeout!", color);
                    } else {
                        set_loading(100, WAIT_JOB_STR[anim_idx], 0xFFFFFF);
                    }
                    break;

                case LoadingStage::READY_CONFIRM:
                    set_loading(100, "Miner ready!", 0x00FF00);
                    if ((ib & INIT_EVENT_MINER_READY) == 0 && now - loading_stage_ms >= 500) {
                        xEventGroupSetBits(app._sys->init_evt, INIT_EVENT_MINER_READY);
                        LOG_I("INIT_EVENT_MINER_READY set");
                    }
                    break;
            }
        }

        // ── Boot UX: drive the LOADING page off to CONFIG (AP mode) or MINER. ──
        if (!ui_switched) {
            if (ib & INIT_EVENT_WIFI_AP_READY) {
                AppState::instance().loading.details.color = 0xFF3B30;
                AppState::instance().loading.details.text  = "AP config mode";
                UIManager::instance().request_goto_page(UIPageId::CONFIG);
                ui_switched = true;
            } else if (ib & INIT_EVENT_MINER_READY) {
                UIManager::instance().request_goto_page((UIPageId)app._last_ui_page);
                ui_switched = true;
            }
        }

        // ── temperature sampling (single writer) — gated on TMP102 readiness ──
        if (!tmp_ready) {
            tmp_ready = (xEventGroupGetBits(app._sys->init_evt) & INIT_EVENT_TMP_READY) != 0;
        }
        if (tmp_ready && now - last_temp_ms >= 125) {
            app._temp.asic  = roundf(temp_hal_get_asic()  * 100) / 100.0f;
            app._temp.vcore = roundf(temp_hal_get_vcore() * 10)  / 10.0f;
            // trigger fan PID + backlight temp effects
            xEventGroupSetBits(app._sys->sys_evt,
                SYS_EVENT_MINER_VCORE_TEMP_UPDATE | SYS_EVENT_MINER_ASIC_TEMP_UPDATE);
            last_temp_ms = now;
        }

        app._update_backlight_by_events(ss_active, bl_wave, delta_bl_ms);

        // ── Auto page-rolling: advance one page every 10 s when enabled and not
        //     in screensaver (mirrors legacy monitor behaviour). ──
        if (app._pref.screen.auto_rolling && now - last_roll_ms >= 10000) {
            last_roll_ms = now;
            if (!(xEventGroupGetBits(app._sys->sys_evt) & SYS_EVENT_SCREEN_SAVER_TRIGGERED)) {
                UIManager::instance().request_next_page();
            }
        }

        // ── Loading page live fields: keep these on the fast UI path so they
        //     react as soon as WiFi/Vcore becomes ready, matching legacy boot UX.
        if (app._wifi && app._wifi->status == WL_CONNECTED) {
            AppState::instance().loading.ip.text = app._wifi->ip.toString();
            AppState::instance().loading.ip.color = 0x00FF00;
        }
        if (app._conn && ((xEventGroupGetBits(app._sys->init_evt) & INIT_EVENT_VCORE_READY) != 0)) {
            AppState::instance().loading.pool.text =
                app._conn->pool.use.url + ":" + String(app._conn->pool.use.port);
        }

        // ── UI state refresh at 1 Hz ──
        if (now - last_ui_ms < 1000) {
            continue;
        }
        last_ui_ms = now;
        // app.print_stack_hwm();

        if (app._minerStatus) {
            auto& m = AppState::instance().miner;
            String hr = formatNumber(app._minerStatus->hashrate._3m, 3);
            if (app._minerStatus->hashrate._3m > 0 && hr.length() > 0) {
                m.hashrate.text = hr.substring(0, hr.length() - 1);
                m.hashrate_unit.text = String(hr.charAt(hr.length() - 1)) + "H/s";
            } else {
                m.hashrate.text = hr;
                m.hashrate_unit.text = "";
            }
            m.blk_hit.text  = String((int)app._minerStatus->hits);
            m.shares.text   = String((unsigned)app._minerStatus->share_rejected) + "/" +
                              String((unsigned)app._minerStatus->share_accepted);
            m.diff.text     = formatNumber(app._minerStatus->diff.best_session, 3) + "/" +
                              formatNumber(app._minerStatus->diff.network, 3);
            m.ver.text      = String(BOARD_CURRENT_FW_VERSION).substring(1);
            {
                String uptime = convert_uptime_to_string(app._minerStatus->uptime_session);
                m.uptime_day.text = uptime.substring(0, 3);
                m.uptime_hms.text = uptime.substring(5);
            }
            {
                float vbus_v = app._pwr_tele.vbus / 1000.0f;
                float ibus_a = app._pwr_tele.ibus / 1000.0f;
                m.power.text = formatNumber(vbus_v, 3) + "V/" +
                               formatNumber(vbus_v * ibus_a, 2) + "W";
                m.temp.text  = formatNumber(app._temp.vcore, 2) + "'C/" +
                               formatNumber(app._temp.asic, 2) + "'C";
            }

            // ── Hashrate-health page ──
            auto& hh = AppState::instance().hr_health;
            double ghs = (double)app._minerStatus->hashrate._3m;
            char hb[24];
            if (ghs >= 1000.0) snprintf(hb, sizeof(hb), "%.2f TH/s", ghs / 1000.0);
            else               snprintf(hb, sizeof(hb), "%.1f GH/s", ghs);
            hh.hashrate.text   = String(hb);
            hh.efficiency.text = String("Eff: ") + String(app._minerStatus->efficiency, 1) + " J/TH";
            hh.shares.text     = String("Shares: ") +
                                 String((unsigned)app._minerStatus->share_rejected) + "/" +
                                 String((unsigned)app._minerStatus->share_accepted);
            hh.best_diff.text  = String("Best: ") + String(app._minerStatus->diff.best_ever, 0);
        }

        // ── Dashboard page: power / thermal / performance ──
        {
            auto& d = AppState::instance().dashboard;
            float vbus_v = app._pwr_tele.vbus / 1000.0f;
            float ibus_a = app._pwr_tele.ibus / 1000.0f;
            d.power.text      = String("Power: ") + String(vbus_v * ibus_a, 1) + " W";
            d.vbus.text       = String("Vbus:  ") + String(vbus_v, 2) + " V";
            d.ibus.text       = String("Ibus:  ") + String(ibus_a, 2) + " A";
            d.asic_temp.text  = String("ASIC:  ") + String((float)app._temp.asic, 1) + " C";
            d.vcore_temp.text = String("VRM:   ") + String((float)app._temp.vcore, 1) + " C";
            d.freq.text       = String("Freq:  ") + String((unsigned)app._spec.asic.req_frq) + " MHz";
            d.vcore.text      = String("Vcore: ") + String((unsigned)app._pwr_tele.vcore) + " mV";
        }

        if (app._wifi) {
            AppState::instance().miner.ip.text = app._wifi->ip.toString();
            int rssi = app._wifi->rssi;
            uint32_t wc = (rssi >= -60) ? 0x00FF00 : (rssi >= -70) ? 0xFFA500 : 0xFF0000;
            AppState::instance().miner.wifi_color = wc;
        }

        if (!app._fan_status.empty()) {
            if (app._spec.name == BOARD_NMQAXE_PLUS_PLUS_NAME && app._fan_status.size() > 1) {
                AppState::instance().miner.fan.text = String((unsigned)app._fan_status[1].rpm) + "/" +
                                                      String((unsigned)app._fan_status[0].rpm);
            } else {
                AppState::instance().miner.fan.text = String((unsigned)app._fan_status[0].rpm) + " rpm";
            }
        }

        // ── Clock page: local time/date (TZ set by monitor via setenv/tzset) ──
        if (app._utc > 1600000000ULL) {   // only once NTP-synced
            time_t t = (time_t)app._utc;
            struct tm lt;
            localtime_r(&t, &lt);
            char tbuf[16];
            char miner_tbuf[8];
            if (app._time.format.time == 12) {  // 12h
                strftime(tbuf, sizeof(tbuf), "%I:%M %p", &lt);
                strftime(miner_tbuf, sizeof(miner_tbuf), "%I:%M", &lt);
            } else {                            // 24h
                strftime(tbuf, sizeof(tbuf), "%H:%M", &lt);
                strftime(miner_tbuf, sizeof(miner_tbuf), "%H:%M", &lt);
            }
            AppState::instance().clock.time_str.text = String(tbuf);
            AppState::instance().miner.utc_time.text = String(miner_tbuf);

            const String& df = app._time.format.date;
            char dbuf[16];
            const char* fmt = "%Y/%m/%d";
            if      (df == "MM-DD-YYYY") fmt = "%m-%d-%Y";
            else if (df == "DD-MM-YYYY") fmt = "%d-%m-%Y";
            else if (df == "YYYY-MM-DD") fmt = "%Y-%m-%d";
            else if (df == "DD/MM/YYYY") fmt = "%d/%m/%Y";
            else if (df == "MM/DD/YYYY") fmt = "%m/%d/%Y";
            else if (df == "YYYY/MM/DD") fmt = "%Y/%m/%d";
            strftime(dbuf, sizeof(dbuf), fmt, &lt);
            AppState::instance().clock.date_str.text = String(dbuf);
        }

        // ── Config page: AP setup info + config-window countdown ──
        if (app._wifi) {
            EventBits_t ib = app._sys ? xEventGroupGetBits(app._sys->init_evt) : 0;
            bool ap_config_active = ((ib & INIT_EVENT_WIFI_AP_READY) != 0) &&
                                    (app._wifi->status != WL_CONNECTED);
            auto& cfg = AppState::instance().config;
            cfg.ssid.text = String("SSID: ") + app._wifi_cfg.ap_ssid;
            cfg.ip.text   = String("IP: ") + app._wifi_cfg.ap_ip.toString();
            if (ap_config_active) {
                cfg.timeout.text = String("Timeout: ") + String((unsigned)app._wifi->config_timeout) + "s";
            } else {
                cfg.timeout.text = "";
            }
        }

        // ── Market page: main coin symbol / price / 24h change ──
        if (app._market && app._market->get_last_update() != 0) {
            const CoinPrice& mp = app._market->get_main_pair();
            auto& mk = AppState::instance().market;
            String sym = app._coin_price; sym.toUpperCase();
            mk.symbol.text = sym;
            char pb[24];
            if (mp.price >= 1000.0f)      snprintf(pb, sizeof(pb), "$%.0f", mp.price);
            else if (mp.price >= 1.0f)    snprintf(pb, sizeof(pb), "$%.2f", mp.price);
            else                          snprintf(pb, sizeof(pb), "$%.4f", mp.price);
            mk.price.text = String(pb);
            char cb[16];
            snprintf(cb, sizeof(cb), "%+.2f%%", mp.change_pct);
            mk.change.text  = String(cb);
            mk.change.color = (mp.change_pct >= 0.0f) ? (uint32_t)0x00C853 : (uint32_t)0xFF5252;
            if (now - app._market->get_last_update() <= (MINER_MARKET_UPDATE_INTERVAL * 3)) {
                String miner_price = formatNumber(mp.price, 6);
                AppState::instance().miner.price.text = String("$") + miner_price;
            } else {
                AppState::instance().miner.price.text = "";
            }
            String clock_price = (mp.price > 1.0f) ? String(mp.price, 1) : String(mp.price, 6);
            AppState::instance().clock.price.text = String("$") + clock_price;
        }

        if (app._swarm && xSemaphoreTake(app._swarm->mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            auto& m = AppState::instance().miner;
            uint32_t workers = app._swarm->total_workers;
            float    thr     = app._swarm->total_hr;
            float    sbd     = app._swarm->best_session_bd;
            m.swarm_workers.text = String(workers);
            m.swarm_hr.text = formatNumber(thr, 2) + "H/s";
            m.swarm_bd.text = formatNumber(sbd, 4);
            xSemaphoreGive(app._swarm->mutex);

            // ── Setting / Swarm page ──
            auto& ss = AppState::instance().setting_swarm;
            ss.workers.text   = String("Workers: ") + String(workers);
            ss.total_hr.text  = String("HR: ") + formatNumber(thr, 3);
            ss.best_diff.text = String("Best: ") + formatNumber(app._swarm->best_ever_bd, 4);
        }

        if (app._neighbor && app._neighbor->mutex &&
            xSemaphoreTake(app._neighbor->mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            size_t n = app._neighbor->alive_ips.size();
            xSemaphoreGive(app._neighbor->mutex);
            AppState::instance().setting_swarm.neighbors.text = String("Neighbors: ") + String((unsigned)n);
        }
        if (app._wifi) {
            AppState::instance().setting_swarm.ip.text = String("IP: ") + app._wifi->ip.toString();
        }

        size_t current_page = UIManager::instance().current();
        if (current_page >= (size_t)UIPageId::MINER &&
            current_page <= (size_t)UIPageId::SETTING_SWARM &&
            current_page != app._last_ui_page) {
            app._last_ui_page = (uint8_t)current_page;
            nvs_config_set_u8(NVS_CONFIG_UI_LAST_PAGE, app._last_ui_page);
            LOG_D("Saved last page to NVS: %u", (unsigned)app._last_ui_page);
        }
    }
}

void MinerApp::_update_backlight_by_events(bool& ss_active, float& bl_wave, uint32_t delta_ms) {
    EventBits_t evt = _sys ? xEventGroupGetBits(_sys->sys_evt) : 0;
    uint8_t base_bl = _pref.screen.brightness ? _pref.screen.brightness : 80;
    float step_scale = (float)delta_ms / 10.0f;
    if (step_scale < 0.1f) step_scale = 0.1f;
    if (step_scale > 10.0f) step_scale = 10.0f;
    const EventBits_t pulse_bits =
        SYS_EVENT_MINER_BLOCK_HIT |
        SYS_EVENT_MINER_HIGH_DIFF_ACHIEVED |
        SYS_EVENT_FIND_NEIGHBOR_TRIGGERED;

    // Web/UI brightness writes are still routed through this single hardware entry.
    if (_brightness_update_xsem && xSemaphoreTake(_brightness_update_xsem, 0) == pdTRUE) {
        ss_active = false;
        tft_bl_ctrl(_pref.screen.brightness, &_spec);
        lv_disp_trig_activity(nullptr);
        xEventGroupClearBits(_sys->sys_evt, SYS_EVENT_SCREEN_SAVER_TRIGGERED);
        LOG_I("Backlight updated -> %u%%", _pref.screen.brightness);
    }

    if ((evt & SYS_EVENT_MINER_BLOCK_HIT) != 0) {
        uint8_t pulse = (uint8_t)(100.0f * (1.0f + sinf(bl_wave)) / 2.0f);
        tft_bl_ctrl(pulse, &_spec);
        bl_wave += 0.10f * step_scale;
    } else if ((evt & SYS_EVENT_MINER_HIGH_DIFF_ACHIEVED) != 0) {
        uint8_t pulse = (uint8_t)(100.0f * (1.0f + sinf(bl_wave)) / 2.0f);
        tft_bl_ctrl(pulse, &_spec);
        bl_wave += 0.06f * step_scale;
    } else if ((evt & SYS_EVENT_FIND_NEIGHBOR_TRIGGERED) != 0) {
        uint8_t pulse = (uint8_t)(100.0f * (1.0f + sinf(bl_wave)) / 2.0f);
        tft_bl_ctrl(pulse, &_spec);
        bl_wave += 0.50f * step_scale;
    } else {
        bl_wave = 0.0f;
        if (_ota.running) {
            tft_bl_ctrl(base_bl, &_spec);
        } else if ((evt & SYS_EVENT_SCREEN_SAVER_TRIGGERED) != 0 &&
                   _pref.screen.saver_mode == 1) {
            tft_bl_ctrl(0, &_spec);
        } else {
            tft_bl_ctrl(base_bl, &_spec);
        }
    }

    if (_ota.running) {
        if (ss_active && (evt & pulse_bits) == 0) {
            tft_bl_ctrl(base_bl, &_spec);
            xEventGroupClearBits(_sys->sys_evt, SYS_EVENT_SCREEN_SAVER_TRIGGERED);
            ss_active = false;
        }
        lv_disp_trig_activity(nullptr);
        return;
    }

    if (_pref.screen.saver_enable && _pref.screen.saver_timeout > 0) {
        uint32_t idle_ms = lv_disp_get_inactive_time(nullptr);
        bool bit_set = (evt & SYS_EVENT_SCREEN_SAVER_TRIGGERED) != 0;
        bool event_active = (evt & pulse_bits) != 0;
        if (!ss_active) {
            if (idle_ms > _pref.screen.saver_timeout * 1000UL) {
                xEventGroupSetBits(_sys->sys_evt, SYS_EVENT_SCREEN_SAVER_TRIGGERED);
                if (!event_active) {
                    if (_pref.screen.saver_mode == 1) tft_bl_ctrl(0, &_spec);
                    else tft_bl_ctrl(base_bl, &_spec);
                }
                ss_active = true;
            }
        } else if (idle_ms < 1000 || !bit_set) {
            xEventGroupClearBits(_sys->sys_evt, SYS_EVENT_SCREEN_SAVER_TRIGGERED);
            if (!event_active) {
                tft_bl_ctrl(base_bl, &_spec);
            }
            lv_disp_trig_activity(nullptr);
            ss_active = false;
        }
        return;
    }

    if (ss_active) {
        if ((evt & pulse_bits) == 0) {
            tft_bl_ctrl(base_bl, &_spec);
        }
        xEventGroupClearBits(_sys->sys_evt, SYS_EVENT_SCREEN_SAVER_TRIGGERED);
        ss_active = false;
    }
}

void MinerApp::_lvgl_thread_entry(void* args) {
    (void)args;
    while (true) {
        // Update top-layer overlays (benchmark/pause/fault) then drive pages + LVGL.
        OverlayManager::instance().update();
        UIManager::instance().render_update();
        delay(5);
    }
}

bool MinerApp::_ui_init() {
    // Bring up the real TFT panel + LVGL display driver, then build the page tree.
    tft_init(&_spec, &_pref);
    uint16_t w = tft_screen_width();
    uint16_t h = tft_screen_height();
    ui_drv_register(w, h);
    touch_drv_register(&_pref, 50);   // enables tileview swipe nav (no-op if absent)
    lvgl_fs_spiffs_register();        // 'S' drive for lv_gif screensaver
    lvgl_fs_mem_register();           // 'M' drive for lv_gif screensaver cached in PSRAM
    images_init(w, h);                // set bg/image descriptor dimensions
    UIManager::instance().init(w, h);
    UIManager::instance().set_sys_evt(_sys->sys_evt);
    UIManager::instance().set_ota_running(&_ota.running);
    UIManager::instance().set_recover_factory_xsem(_recover_factory_xsem);  // touch long-press factory reset
    UIManager::instance().set_force_config_xsem(_force_config_xsem);        // boot long-press setup mode

    OverlayCtx octx;
    octx.bm_mode = &_bm_mode;
    octx.bm      = &_bm;
    octx.status  = _minerStatus;
    octx.sys_evt = _sys->sys_evt;
    octx.reboot_xsem = _sys->reboot_xsem;
    octx.ota     = &_ota;
    octx.spec    = &_spec;
    octx.aphorism   = &_aphorism;
    octx.saver_mode = &_pref.screen.saver_mode;
    // Screensaver GIF path (uploaded via web). Filename matches http upload handler.
    octx.gif_path = (_spec.name == BOARD_NMAXE_NAME || _spec.name == BOARD_NMAXE_GAMMA_NAME)
                  ? "/screen_saver_240x135.gif" : "/screen_saver_320x240.gif";
    OverlayManager::instance().init(octx);
    uint8_t br = _pref.screen.brightness ? _pref.screen.brightness : 80;
    LOG_I("UI init done: %ux%u, target brightness=%u%%", w, h, br);
    return true;
}
