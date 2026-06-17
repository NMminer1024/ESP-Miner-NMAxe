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

class MinerApp {
public:
    static MinerApp& instance();

    bool init();
    void begin();

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

    SystemSync* _sys = nullptr;
    WifiState* _wifi = nullptr;          // live network state (replaces g_board.status.wifi)
    WifiConnConfig _wifi_cfg;            // connection params loaded from NVS
    WifiCtx*   _wifi_ctx = nullptr;      // DI context for wifi/config_monitor threads
    SwarmState*    _swarm = nullptr;         // aggregated neighbor stats (replaces g_board.status.swarm)
    NeighborState* _neighbor = nullptr;      // local ICMP scan results (replaces g_board.status.neighbor)
    SwarmCtx*      _swarm_ctx = nullptr;      // DI context for swarm/scan threads
    MinerStatus*     _minerStatus = nullptr;  // shared mining runtime state (replaces g_board.status.miner)
    AsicMinerClass*  _miner = nullptr;        // ASIC miner instance (replaces g_board.miner)
    StratumClass*    _stratum = nullptr;      // stratum client instance (replaces g_board.stratum)
    ConnInfo*        _conn = nullptr;         // pool/stratum connection set (replaces g_board.info.connection)
    MinerCtx*        _miner_ctx = nullptr;    // DI context for stratum/miner/monitor threads
    volatile uint64_t _utc = 0;              // shared UTC seconds (time domain)
    String           _tz;                    // timezone string, e.g. "8.0" (NVS)
    PowerTelemetry   _pwr_tele;              // measured vbus/ibus/vcore (replaces g_board.status.power)
    MonitorCtx*      _monitor_ctx = nullptr; // DI context for monitor thread
    SemaphoreHandle_t _nvs_save_xsem = nullptr; // request NVS persist of best-ever/hits/uptime
    BoardModelType   _model = BOARD_UNKNOWN;
    BoardSpecConfig  _spec;                 // runtime board spec (replaces g_board.info.spec)
    AxePowerHal*     _power = nullptr;       // power HAL instance (replaces g_board.power)
    PowerCtx*        _power_ctx = nullptr;   // DI context for power threads
    volatile bool    _ota_running = false;   // shared OTA flag (replaces g_board.status.ota.running)
    volatile int     _ota_progress = 0;      // OTA progress 0..100 (replaces g_board.status.ota.progress)
    PreferenceState  _pref;                  // live user preferences (replaces g_board.status.preference)
    LedCtx*          _led_ctx = nullptr;     // DI context for led thread
    BenchmarkState   _bm;                     // benchmark overlay progress (replaces g_board.status.bm)
    BenchmarkCtx*    _benchmark_ctx = nullptr;// DI context for benchmark thread
    TempState        _temp;                  // shared temp samples (replaces g_board.status.temp)
    std::vector<fan_status_t> _fan_status;   // runtime fan status (replaces g_board.status.fan.list)
    FanCtx*          _fan_ctx = nullptr;     // DI context for fan thread
    MarketClass*     _market = nullptr;      // crypto price client (replaces g_board.market)
    MarketCtx*       _market_ctx = nullptr;  // DI context for market thread
    String           _coin_price;            // main display coin (NVS)
    String           _coin_watchlist;        // watchlist coins (NVS)
    SemaphoreHandle_t _recover_factory_xsem = nullptr; // factory reset request (control)
    SemaphoreHandle_t _force_config_xsem = nullptr;    // force AP config request (control)
    volatile uint8_t _bm_mode = 0;           // benchmark mode flag (NVS cached at boot)
    DaemonCtx*       _daemon_ctx = nullptr;   // DI context for daemon thread
    ButtonCtx*       _button_ctx = nullptr;   // DI context for button thread
    std::vector<TaskEntry> _tasks;

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

    static void _tick_thread_entry(void* args);
    static void _lvgl_thread_entry(void* args);

    bool _ui_init();
};
