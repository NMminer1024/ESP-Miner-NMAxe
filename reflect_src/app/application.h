#pragma once
#include <Arduino.h>
#include "app_state.h"
#include "task_config.h"
#include "../mining/mining_types.h"
#include <vector>
#include <map>
#include <set>

// forward declarations for original driver types
class AxePowerHal;
class BMxxx;
class AsicMinerClass;
class StratumClass;
class FT6206Class;
class MarketClass;
struct fan_status_t;
struct fan_config_t;
struct BoardSpecConfig;

// ============================================================================
// MinerApp — application singleton
//
//   Owns all thread contexts, init/begin lifecycle, task creation registry.
//   All threads receive their own specific context struct (not a global blob).
// ============================================================================
class MinerApp {
public:
    static MinerApp& instance();

    bool init();
    void begin();

    void print_stack_hwm() const;

private:
    MinerApp()  = default;
    MinerApp(const MinerApp&)           = delete;
    MinerApp& operator=(const MinerApp&)= delete;

    // ── NVS cache: values loaded once at boot ───────────────────────────
    struct NvsCache {
        String   hostname;
        String   wifi_ssid;
        String   wifi_pswd;
        String   pool_url_pri;
        String   pool_url_fb;
        uint8_t  screen_brightness  = 100;
        uint8_t  screen_orient      = 0;   // 0=normal, 1=flip
        uint8_t  last_page          = 0;
        uint8_t  time_format        = 24;
        String   date_format        = "YYYY/MM/DD";
        String   timezone           = "8.0";
        bool     need_cfg           = false;
        bool     led_enable         = false;
        bool     screen_saver_enable = false;
        uint32_t screen_saver_timeout = 60;
        uint8_t  screen_saver_mode   = 0;
        String   coin_price_display = "BTC";
        String   coin_watchlist     = "BTC,ETH,LTC,BNB,DOGE,XRP,TRX,SOL";
    };
    NvsCache _cache;

    // ── Board spec config (read-only after init) ────────────────────────
    BoardSpecConfig* _board_spec = nullptr;  // points to static storage

    // ── Driver instances (owned by MinerApp) ────────────────────────────
    BMxxx*              _asic    = nullptr;
    AsicMinerClass*     _miner   = nullptr;
    AxePowerHal*        _power   = nullptr;
    StratumClass*       _stratum = nullptr;
    FT6206Class*        _touch   = nullptr;
    MarketClass*        _market_inst = nullptr;

    // ── Mining shared context (Stratum ↔ Miner IPC) ─────────────────────
    MiningSharedCtx*    _miningShared = nullptr;

    // ── System sync (init events, system events, semaphores) ────────────
    struct SystemSync {
        EventGroupHandle_t  init_evt;
        EventGroupHandle_t  sys_evt;
        SemaphoreHandle_t   reboot_xsem;
        SemaphoreHandle_t   nvs_save_xsem;
        SemaphoreHandle_t   recover_factory_xsem;
        SemaphoreHandle_t   force_config_xsem;
        SemaphoreHandle_t   brightness_update_xsem;
    };
    SystemSync* _sys = nullptr;

    // ── Power status context ────────────────────────────────────────────
    struct PowerStatus {
        volatile uint16_t   vbus;       // mV
        volatile uint16_t   ibus;       // mA
        volatile uint16_t   vcore;      // mV
        volatile float      vcore_temp; // °C
        volatile float      asic_temp;  // °C
        AxePowerHal*        power;      // points to _power
    };
    PowerStatus* _pwr = nullptr;

    // ── Fan status context ──────────────────────────────────────────────
    struct FanCtx {
        std::vector<fan_status_t>   list;
        std::vector<fan_config_t>   config;
    };
    FanCtx* _fan = nullptr;

    // ── WiFi status context ─────────────────────────────────────────────
    struct WifiCtx {
        volatile wl_status_t    status;
        char                    ip[16];
        char                    gateway[16];
        char                    subnet[16];
        char                    dns[16];
        volatile int            rssi;
        SemaphoreHandle_t       reconnect_xsem;
        volatile bool           client_connected;
        volatile uint32_t       config_timeout_s;
        volatile bool           force_config_required;
    };
    WifiCtx* _wifi = nullptr;

    // ── Neighbor scan context ───────────────────────────────────────────
    struct ScanCtx {
        std::vector<String>     alive_ips;
        SemaphoreHandle_t       mutex;
        SemaphoreHandle_t       scan_required;
        volatile uint32_t       last_scan_ms;
        volatile uint32_t       scan_generation;
        volatile int            scan_progress;
        volatile bool           is_scanning;

        // on-demand scheduling
        SemaphoreHandle_t       wake_sem;
        volatile bool           should_sleep;
        volatile bool           awake;
    };
    ScanCtx* _scan = nullptr;

    // ── Swarm probe context ─────────────────────────────────────────────
    struct SwarmCtx {
        SemaphoreHandle_t       mutex;
        volatile uint32_t       total_workers;
        volatile float          total_hr;
        volatile float          best_session_bd;
        volatile float          best_ever_bd;
        std::set<String>        confirmed_ips;
        std::set<String>        probe_blacklist;
        std::set<String>        gossip_union;
        std::map<String, uint8_t> probe_fail_cnt;
        volatile uint32_t       last_scan_gen;
        ScanCtx*                scan;           // borrows _scan

        // on-demand scheduling
        SemaphoreHandle_t       wake_sem;
        volatile bool           should_sleep;
        volatile bool           awake;
        SemaphoreHandle_t*      net_mutex;
    };
    SwarmCtx* _swarm = nullptr;

    // ── UI preferences ──────────────────────────────────────────────────
    struct UIPrefs {
        struct {
            bool    flip;
            bool    auto_rolling;
            bool    saver_enable;
            uint32_t saver_timeout;
            uint8_t  saver_mode;
            uint16_t brightness;
        } screen;
        struct {
            bool    enable;
            bool    sleep;
        } led;
        volatile uint8_t    current_page;
        volatile uint32_t   last_active_ms;
        SemaphoreHandle_t   lvgl_mutex;
    };
    UIPrefs* _ui_prefs = nullptr;

    // ── OTA manager ─────────────────────────────────────────────────────
    struct OTACtx {
        volatile bool       running;
        volatile int        progress;
        volatile uint32_t   last_progress_ms;
        char                filename[64];
        char                error[128];
    };
    OTACtx* _ota = nullptr;

    // ── Market context (on-demand) ──────────────────────────────────────
    struct MarketCtx {
        MarketClass*        market;         // points to _market_inst
        volatile bool       connected;
        SemaphoreHandle_t   wake_sem;
        volatile bool       should_sleep;
        volatile bool       awake;
    };
    MarketCtx* _market = nullptr;

    // ── Scheduled network mutex (shared by market/swarm) ────────────────
    SemaphoreHandle_t _sched_net_mutex = nullptr;

    // ── Permanent-task registry ─────────────────────────────────────────
    struct TaskEntry {
        TaskHandle_t handle;
        const char*  name;
        uint32_t     stack_bytes;
        UBaseType_t  priority;
    };
    std::vector<TaskEntry> _tasks;

    BaseType_t _create_task(TaskFunction_t fn, const char* name,
                            uint32_t stack_bytes, void* param,
                            UBaseType_t prio, BaseType_t core);

    // ── Boot progress helper ────────────────────────────────────────────
    struct BootProgress {
        int step     = 0;
        int step_inc;
        explicit BootProgress(int total_stages);
        void post(const char* msg, uint32_t c = 0xFFFFFF);
        void next(const char* msg, uint32_t c = 0xFFFFFF);
        void finish(const char* msg, uint32_t c = 0x00FF00);
    };

    // ── begin() sub-stages ──────────────────────────────────────────────
    void _begin_board_init  (BootProgress& boot);
    void _begin_wifi_connect(BootProgress& boot);
    void _begin_display     (BootProgress& boot);
    void _begin_infra       (BootProgress& boot);
    void _begin_miners      (BootProgress& boot);
    void _begin_market      (BootProgress& boot);

    // ── Scheduler: wake/sleep threads based on current page ─────────────
    uint8_t _sched_last_page = 0xFF;
    void _on_page_changed(uint8_t new_page);

    // ── Background task entry points ────────────────────────────────────
    static void _tick_thread_entry(void* args);
    static void _stratum_thread_entry(void* args);
    static void _miner_thread_entry(void* args);
    static void _power_loop_thread_entry(void* args);
    static void _fan_thread_entry(void* args);
    static void _led_thread_entry(void* args);
    static void _wifi_connect_thread_entry(void* args);
    static void _webserver_thread_entry(void* args);
    static void _scan_thread_entry(void* args);
    static void _swarm_thread_entry(void* args);
    static void _market_thread_entry(void* args);
    static void _lvgl_thread_entry(void* args);
    static void _ui_thread_entry(void* args);

    // LVGL init helpers
    bool _ui_init();

    friend void wifi_event_handler();
};
