#pragma once

#include <Arduino.h>
#include <vector>

#include "app_state.h"
#include "task_config.h"
#include "../mining/mining_types.h"
#include "../board/board.h"
#include "../drivers/power/power_ctx.h"
#include "../drivers/fan/fan_ctx.h"
#include "../drivers/temp/temp_ctx.h"
#include "../net/wifi_ctx.h"
#include "../net/swarm_ctx.h"
#include "../market/market_ctx.h"
#include "daemon_ctx.h"
#include "monitor_ctx.h"
#include "button_ctx.h"
#include "runtime_state.h"
#include "led_ctx.h"
#include "benchmark_ctx.h"
#include "aphorism_ctx.h"
#include "../web/web_ctx.h"
#include "../market/market.h"

class MinerApp {
public:
    static MinerApp& instance();

    bool init();
    void begin();

    const MinerStatus* status() const { return _state_miner; }
    const BoardSpecConfig& spec() const { return _board_spec; }
    BoardSpecConfig& spec_mut() { return _board_spec; }
    const TempState& temp() const { return _state_temp; }
    const PowerTelemetry& pwr_tele() const { return _state_power_telemetry; }
    const std::vector<fan_status_t>& fan_status() const { return _state_fans; }
    uint8_t asic_count() const { return _service_miner ? _service_miner->get_asic_count() : 0; }
    MarketClass* market() const { return _service_market; }
    SwarmState* swarm() const { return _state_swarm; }
    PreferenceState& pref() { return _config_pref; }
    AxePowerHal* power() const { return _hal_power; }
    AsicMinerClass* miner() const { return _service_miner; }
    WifiState* wifi() const { return _state_wifi; }
    WifiConnConfig& wifi_cfg() { return _config_wifi; }
    const WifiConnConfig& wifi_cfg() const { return _config_wifi; }
    EventGroupHandle_t init_evt() const { return _sync_system ? _sync_system->init_evt : nullptr; }
    EventGroupHandle_t sys_evt() const { return _sync_system ? _sync_system->sys_evt : nullptr; }
    SemaphoreHandle_t brightness_update_xsem() const { return _xsem_brightness_update; }
    SemaphoreHandle_t reboot_xsem() const { return _sync_system ? _sync_system->reboot_xsem : nullptr; }

    void print_stack_hwm() const;

private:
    MinerApp() = default;
    MinerApp(const MinerApp&) = delete;
    MinerApp& operator=(const MinerApp&) = delete;

    struct SystemSync {
        EventGroupHandle_t init_evt = nullptr;
        EventGroupHandle_t sys_evt = nullptr;
        SemaphoreHandle_t reboot_xsem = nullptr;
    };

    struct TaskEntry {
        TaskHandle_t handle;
        const char* name;
        uint32_t stack_bytes;
        UBaseType_t priority;
    };

    struct BootProgress {
        int step = 0;
        int step_inc;
        explicit BootProgress(int total_stages);
        void post(const char* msg, uint32_t c = 0xFFFFFF);
        void next(const char* msg, uint32_t c = 0xFFFFFF);
        void finish(const char* msg, uint32_t c = 0x00FF00);
    };

    // ── Platform-wide synchronization / ownership roots ────────────────────
    SystemSync* _sync_system = nullptr;          // init/sys event groups + reboot gate
    BoardModelType _board_model = BOARD_UNKNOWN; // detected board selector result
    BoardSpecConfig _board_spec;                 // resolved runtime board spec

    // ── Persistent configuration restored from NVS ─────────────────────────
    WifiConnConfig _config_wifi;                 // WiFi STA/AP identity and host name
    PreferenceState _config_pref;                // live UI/LED preferences after NVS merge
    TimeState _config_time;                      // time/date format preferences
    String _config_timezone;                     // timezone string, e.g. "8.0"
    String _config_coin_price;                   // primary market symbol
    String _config_coin_watchlist;               // watchlist symbols
    uint8_t _config_last_ui_page = 0;            // sanitized runtime page restore target
    volatile uint8_t _config_benchmark_mode = 0; // benchmark mode flag restored at boot

    // ── Shared runtime state exposed across threads / UI / web ─────────────
    WifiState* _state_wifi = nullptr;                // live WiFi/AP status
    SwarmState* _state_swarm = nullptr;              // aggregated peer miner summary
    NeighborState* _state_neighbor = nullptr;        // LAN discovery / ICMP scan results
    MinerStatus* _state_miner = nullptr;             // mining runtime stats + control flags
    PowerTelemetry _state_power_telemetry;           // measured VBUS/IBUS/Vcore samples
    TempState _state_temp;                           // board/ASIC thermal samples
    std::vector<fan_status_t> _state_fans;           // per-fan runtime speed/rpm state
    OtaState _state_ota;                             // OTA progress / running state
    BenchmarkState _state_benchmark;                 // benchmark overlay / sweep progress
    AphorismState _state_aphorism;                   // screensaver quote pool
    volatile uint64_t _state_utc = 0;                // shared UTC seconds

    // ── Core service / hardware objects ────────────────────────────────────
    AxePowerHal* _hal_power = nullptr;           // board-specific power HAL
    MarketClass* _service_market = nullptr;      // market data client
    ConnInfo* _service_conn = nullptr;           // parsed pool + stratum connection set
    AsicMinerClass* _service_miner = nullptr;    // ASIC miner orchestrator
    StratumClass* _service_stratum = nullptr;    // pool protocol client

    // ── Cross-thread control semaphores ────────────────────────────────────
    SemaphoreHandle_t _xsem_nvs_save = nullptr;            // persist counters/status to NVS
    SemaphoreHandle_t _xsem_recover_factory = nullptr;     // user-triggered factory reset
    SemaphoreHandle_t _xsem_force_config = nullptr;        // boot long-press -> AP setup on reboot
    SemaphoreHandle_t _xsem_brightness_update = nullptr;   // async screen brightness apply

    // ── Dependency-injection contexts captured by worker threads ───────────
    WifiCtx* _ctx_wifi = nullptr;
    SwarmCtx* _ctx_swarm = nullptr;
    PowerCtx* _ctx_power = nullptr;
    FanCtx* _ctx_fan = nullptr;
    MarketCtx* _ctx_market = nullptr;
    MinerCtx* _ctx_miner = nullptr;
    MonitorCtx* _ctx_monitor = nullptr;
    BenchmarkCtx* _ctx_benchmark = nullptr;
    DaemonCtx* _ctx_daemon = nullptr;
    ButtonCtx* _ctx_button = nullptr;
    LedCtx* _ctx_led = nullptr;
    WebCtx* _ctx_web = nullptr;
    AphorismCtx* _ctx_aphorism = nullptr;

    // ── Task registry / diagnostics ────────────────────────────────────────
    std::vector<TaskEntry> _tasks;           // created RTOS tasks for HWM reporting

    BaseType_t _create_task(TaskFunction_t fn, const char* name,
                            uint32_t stack_bytes, void* param,
                            UBaseType_t prio, BaseType_t core);

    bool _init_mining_instances();   // parse NVS conn + build asic/miner/stratum with DI

    void _begin_board_init(BootProgress& boot);
    void _begin_fan(BootProgress& boot);
    void _begin_power(BootProgress& boot);
    void _begin_wifi_connect(BootProgress& boot);
    void _begin_display(BootProgress& boot);
    void _begin_infra(BootProgress& boot);
    void _begin_market(BootProgress& boot);
    void _begin_miners(BootProgress& boot);
    void _update_backlight_by_events(bool& ss_active, float& bl_wave, uint32_t delta_ms);

    static void _tick_thread_entry(void* args);
    static void _lvgl_thread_entry(void* args);

    bool _ui_init();
};
