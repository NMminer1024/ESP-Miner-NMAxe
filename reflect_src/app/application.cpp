#include "application.h"

#include "lvgl.h"

namespace {
void app_log(const char* msg) {
    Serial.print("[reflect] ");
    Serial.println(msg);
}
}

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

    app_log("MinerApp::init placeholder ready");
    return true;
}

void MinerApp::_begin_board_init(BootProgress& boot) {
    boot.next("Board init placeholder");
    xEventGroupSetBits(_sys->init_evt, 1 << 0);
}

void MinerApp::_begin_wifi_connect(BootProgress& boot) {
    boot.next("WiFi placeholder");
    _wifi->status = WIFI_CONNECTED;
    snprintf(_wifi->ip, sizeof(_wifi->ip), "192.168.1.100");
    _wifi->rssi = -55;
    xEventGroupSetBits(_sys->init_evt, 1 << 1);
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
    constexpr int BOOT_STAGE_TOTAL = 6;
    BootProgress boot(BOOT_STAGE_TOTAL);

    boot.post("Reflect booting...");
    _begin_board_init(boot);
    _begin_wifi_connect(boot);
    _begin_display(boot);
    _begin_infra(boot);
    _begin_market(boot);
    _begin_miners(boot);

    _create_task(_tick_thread_entry, "(tick)", 1024 * 3, nullptr, TASK_PRIORITY_APP_TICK, 1);

    boot.finish("Reflect started");
    app_log("MinerApp::begin done");
}

void MinerApp::print_stack_hwm() const {
    for (const auto& t : _tasks) {
        if (t.handle == nullptr) {
            continue;
        }
        UBaseType_t hwm = uxTaskGetStackHighWaterMark(t.handle);
        Serial.printf("[reflect] %-12s hwm=%u/%u\n", t.name, (unsigned)hwm, (unsigned)t.stack_bytes);
    }
}

void MinerApp::_tick_thread_entry(void* args) {
    (void)args;
    auto& app = MinerApp::instance();

    while (true) {
        delay(1000);

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
