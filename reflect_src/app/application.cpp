#include "application.h"

#include "lvgl.h"
#include "system_events.h"
#include "../thread/thread_entry.h"
#include "../nvs/nvs_config.h"
#include "../drivers/temp/temp_hal.h"
#include "../utils/logger/logger.h"

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
    AppState::instance().loading.progress = step;
    AppState::instance().loading.details.text = String(msg);
    AppState::instance().loading.details.color = c;
}

void MinerApp::BootProgress::finish(const char* msg, uint32_t c) {
    step = 100;
    AppState::instance().loading.progress = 100;
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
    Serial.setTimeout(20);
    Serial.begin(115200);
    delay(100);

    static SystemSync sys_storage;
    static WifiCtx wifi_storage;
    static SwarmCtx swarm_storage;

    _sys = &sys_storage;
    _wifi = &wifi_storage;
    _swarm = &swarm_storage;

    _sys->init_evt = xEventGroupCreate();
    _sys->sys_evt = xEventGroupCreate();
    _sys->reboot_xsem = xSemaphoreCreateCounting(1, 0);

    _wifi->status = WIFI_DISCONNECTED;
    _wifi->rssi = 0;
    _wifi->reconnect_xsem = xSemaphoreCreateCounting(1, 0);
    _wifi->client_connected = false;
    memset(_wifi->ip, 0, sizeof(_wifi->ip));

    _swarm->mutex = xSemaphoreCreateMutex();
    _swarm->total_workers = 1;
    _swarm->total_hr = 0.0f;
    _swarm->best_ever_bd = 0.0f;

    // ── Board detection + spec (real board layer, preserves original timing) ──
    // get_board_model() internally debounces the selection pins (up to ~3 s).
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

    // ── Power HAL instance (replaces g_board.power), built from the board spec ──
    _power = _spec.create_power_instance(
        _spec.pwr.en_pins, _spec.pwr.adc_pins,
        _spec.pwr.vcore_regulator_pin, _spec.pwr.pgood_pin, _spec.pwr.dc_plug_pin);
    if (_power == nullptr) {
        LOG_E("AxePower instance creation failed");
        return false;
    }
    // Register board-specific temperature readers into the temp HAL.
    if (_spec.setup_temp_hal) {
        _spec.setup_temp_hal(_power);
    }

    LOG_D("MinerApp::init ready");
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

    _create_task(fan_thread_entry, "(fan)", 1024 * 5, _fan_ctx, TASK_PRIORITY_FAN, 0);
}

void MinerApp::_begin_power(BootProgress& boot) {
    boot.next("Power up...");

    static PowerCtx ctx;
    ctx.power       = _power;
    ctx.spec        = &_spec;
    ctx.init_evt    = _sys->init_evt;
    ctx.sys_evt     = _sys->sys_evt;
    ctx.ota_running = &_ota_running;
    ctx.mining      = _miningShared;   // nullptr until the miner domain is migrated
    _power_ctx = &ctx;

    _create_task(power_init_thread_entry, "(pwr_init)", 1024 * 7, _power_ctx, TASK_PRIORITY_PWR, 1);
    _create_task(power_loop_thread_entry, "(pwr_loop)", 1024 * 5, _power_ctx, TASK_PRIORITY_PWR, 1);
}

void MinerApp::_begin_wifi_connect(BootProgress& boot) {
    boot.next("WiFi placeholder");
    _wifi->status = WIFI_CONNECTED;
    snprintf(_wifi->ip, sizeof(_wifi->ip), "192.168.1.100");
    _wifi->rssi = -55;
    xEventGroupSetBits(_sys->init_evt, INIT_EVENT_WIFI_STA_CONNECTED);
}

void MinerApp::_begin_display(BootProgress& boot) {
    boot.next("Display placeholder");
    lv_init();
    _ui_init();
    _create_task(_lvgl_thread_entry, "(lvgl)", 1024 * 4, nullptr, TASK_PRIORITY_LVGL_DRV, 1);
}

void MinerApp::_begin_infra(BootProgress& boot) {
    boot.next("Infra placeholder");
}

void MinerApp::_begin_market(BootProgress& boot) {
    boot.next("Market placeholder");
}

void MinerApp::_begin_miners(BootProgress& boot) {
    static MiningSharedCtx mining_shared;
    mining_shared.init();
    _miningShared = &mining_shared;
    boot.next("Miners placeholder");
}

void MinerApp::begin() {
    constexpr int BOOT_STAGE_TOTAL = 8;
    BootProgress boot(BOOT_STAGE_TOTAL);

    boot.post("Reflect booting...");
    _begin_board_init(boot);
    _begin_fan(boot);
    _begin_power(boot);
    _begin_wifi_connect(boot);
    _begin_display(boot);
    _begin_infra(boot);
    _begin_market(boot);
    _begin_miners(boot);

    _create_task(_tick_thread_entry, "(tick)", 1024 * 3, nullptr, TASK_PRIORITY_APP_TICK, 1);

    boot.finish("Reflect started");
    LOG_I("MinerApp::begin done");
}

void MinerApp::print_stack_hwm() const {
    for (const auto& t : _tasks) {
        if (t.handle == nullptr) {
            continue;
        }
        UBaseType_t hwm = uxTaskGetStackHighWaterMark(t.handle);
        LOG_D("%-12s hwm=%u/%u", t.name, (unsigned)hwm, (unsigned)t.stack_bytes);
    }
}

void MinerApp::_tick_thread_entry(void* args) {
    (void)args;
    auto& app = MinerApp::instance();

    bool     tmp_ready = false;
    uint32_t last_temp_ms = 0;
    uint32_t last_ui_ms   = 0;

    while (true) {
        delay(10);
        uint32_t now = millis();

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

        // ── UI state refresh at 1 Hz ──
        if (now - last_ui_ms < 1000) {
            continue;
        }
        last_ui_ms = now;

        if (app._miningShared) {
            auto& m = AppState::instance().miner;
            m.hashrate.text = String(app._miningShared->hashrate_3m / 1e12, 1);
            m.blk_hit.text = String((int)app._miningShared->block_hits);
            m.shares.text = String((int)app._miningShared->shares_rejected) + "/" +
                            String((int)app._miningShared->shares_accepted);
            m.local_diff.text = String(app._miningShared->best_session_diff, 0) + "/" +
                                String(app._miningShared->best_ever_diff, 0);
        }

        if (app._wifi) {
            AppState::instance().miner.ip.text = String(app._wifi->ip);
            AppState::instance().miner.rssi.text = String(app._wifi->rssi);
        }

        if (app._swarm && xSemaphoreTake(app._swarm->mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            auto& m = AppState::instance().miner;
            m.swarm_workers.text = String(app._swarm->total_workers);
            m.swarm_hr.text = String(app._swarm->total_hr, 1);
            m.swarm_bd.text = String(app._swarm->best_ever_bd, 0);
            xSemaphoreGive(app._swarm->mutex);
        }
    }
}

void MinerApp::_lvgl_thread_entry(void* args) {
    (void)args;
    while (true) {
        lv_timer_handler();
        delay(5);
    }
}

bool MinerApp::_ui_init() {
    static lv_color_t buf[32];
    static lv_disp_draw_buf_t draw_buf;
    static lv_disp_drv_t disp_drv;

    lv_disp_draw_buf_init(&draw_buf, buf, nullptr, 32);
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 8;
    disp_drv.ver_res = 4;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.flush_cb = [](lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p) {
        (void)area;
        (void)color_p;
        lv_disp_flush_ready(drv);
    };

    lv_disp_drv_register(&disp_drv);
    return true;
}
