#pragma once

#include <Arduino.h>
#include <vector>
#include <algorithm>
#include "lvgl.h"

// ============================================================================
// Observable<T> — thread-safe observable property
//
//   Assignment (operator=) notifies all subscribers via lv_async_call,
//   allowing any FreeRTOS task to safely write values.
//   Value unchanged → no notification (avoids redundant LVGL redraws).
//
//   subscribe / unsubscribe should be called from the LVGL task thread
//   during UIPage::create / destroy.
// ============================================================================
template<typename T>
class Observable {
public:
    using Observer = void(*)(const T&, void*);

    Observable() = default;
    explicit Observable(const T& v) : _val(v) {}

    Observable(const Observable&)            = delete;
    Observable& operator=(const Observable&) = delete;

    void operator=(const T& v) {
        if (_val == v) return;
        _val = v;
        _dispatch();
    }

    const T& get()      const { return _val; }
    operator const T&() const { return _val; }

    void subscribe(Observer obs, void* ctx = nullptr) {
        _subs.push_back({obs, ctx});
    }

    void unsubscribe(Observer obs) {
        _subs.erase(
            std::remove_if(_subs.begin(), _subs.end(),
                [obs](const Sub& s){ return s.obs == obs; }),
            _subs.end());
    }

private:
    struct Sub { Observer obs; void* ctx; };
    struct AsyncMsg { T val; std::vector<Sub> subs; };

    T                _val{};
    std::vector<Sub> _subs;

    void _dispatch() {
        if (_subs.empty()) return;
        vTaskSuspendAll();
        auto* msg = new (std::nothrow) AsyncMsg{ _val, _subs };
        if (msg) {
            lv_async_call([](void* arg) {
                auto* m = static_cast<AsyncMsg*>(arg);
                for (auto& s : m->subs) s.obs(m->val, s.ctx);
                delete m;
            }, msg);
        }
        xTaskResumeAll();
    }
};

// ============================================================================
// ObsLabel — text + color pair for a single UI label widget
// ============================================================================
struct ObsLabel {
    Observable<String>   text;
    Observable<uint32_t> color{(uint32_t)0xFFFFFF};

    // Convenience forwarders so pages can subscribe text/color directly on the
    // label. Overloads disambiguate by observer signature (String vs uint32_t).
    void subscribe(Observable<String>::Observer obs, void* ctx = nullptr)   { text.subscribe(obs, ctx); }
    void subscribe(Observable<uint32_t>::Observer obs, void* ctx = nullptr) { color.subscribe(obs, ctx); }
    void unsubscribe(Observable<String>::Observer obs)   { text.unsubscribe(obs); }
    void unsubscribe(Observable<uint32_t>::Observer obs) { color.unsubscribe(obs); }
};

// ============================================================================
// Loading page state
// ============================================================================
struct LoadingPageState {
    Observable<int32_t> progress{(int32_t)0};
    ObsLabel            details;
    ObsLabel            ip;
};

// ============================================================================
// Miner page state
// ============================================================================
struct MinerPageState {
    ObsLabel time_str;
    ObsLabel uptime_day;
    ObsLabel uptime_hms;
    ObsLabel rssi;
    Observable<uint32_t> rssi_icon_color{(uint32_t)0xEE7D30};
    ObsLabel price;

    ObsLabel job_count;
    Observable<uint32_t> job_icon_color{(uint32_t)0xA9A9A9};
    ObsLabel net_diff;
    ObsLabel local_diff;
    Observable<uint32_t> local_diff_icon_color{(uint32_t)0xA9A9A9};
    ObsLabel shares;
    Observable<uint32_t> shares_icon_color{(uint32_t)0xA9A9A9};

    ObsLabel blk_hit;
    ObsLabel hashrate;

    ObsLabel ver;
    ObsLabel ip;

    // Swarm bar
    ObsLabel swarm_bd;
    ObsLabel swarm_hr;
    ObsLabel swarm_workers;
};

// ============================================================================
// Config page state
// ============================================================================
struct ConfigPageState {
    ObsLabel ssid;       // AP SSID the user should join
    ObsLabel ip;         // AP IP (captive-portal address)
    ObsLabel timeout;    // AP config-window countdown (seconds)
};

// ============================================================================
// Clock page state
// ============================================================================
struct ClockPageState {
    ObsLabel time_str;   // "HH:MM" (24h) or "hh:MM AM/PM" (12h)
    ObsLabel date_str;   // formatted per user date-format preference
};

// ============================================================================
// Market page state
// ============================================================================
struct MarketPageState {
    ObsLabel symbol;     // main coin symbol, e.g. "BTC"
    ObsLabel price;      // formatted price, e.g. "$67,123"
    ObsLabel change;     // 24h change %, colored (green up / red down)
};

// ============================================================================
// Dashboard page state — power / thermal / performance readouts
// ============================================================================
struct DashboardPageState {
    ObsLabel power;      // bus power, "12.3 W"
    ObsLabel vbus;       // input voltage, "5.01 V"
    ObsLabel ibus;       // input current, "2.45 A"
    ObsLabel asic_temp;  // "58.2 C"
    ObsLabel vcore_temp; // "47.1 C"
    ObsLabel freq;       // requested ASIC frequency, "525 MHz"
    ObsLabel vcore;      // measured core voltage, "1180 mV"
};

// ============================================================================
// Hashrate-health page state
// ============================================================================
struct HrHealthPageState {
    ObsLabel hashrate;    // 3-min avg hashrate, "1.23 TH/s"
    ObsLabel efficiency;  // "21.4 J/TH"
    ObsLabel shares;      // "rej/acc"
    ObsLabel best_diff;   // best-ever difficulty
};

// ============================================================================
// Setting / Swarm page state — aggregated neighbor-miner statistics
// ============================================================================
struct SettingSwarmPageState {
    ObsLabel workers;     // total workers (incl. self)
    ObsLabel total_hr;    // aggregated swarm hashrate
    ObsLabel best_diff;   // swarm best-ever difficulty
    ObsLabel neighbors;   // alive-IP count on the LAN
    ObsLabel ip;          // this device's IP
};

// ============================================================================
// AppState — application global state singleton
//
//   Usage (from any FreeRTOS task):
//     AppState::instance().loading.progress       = 50;
//     AppState::instance().loading.details.text    = "WiFi connected!";
//     AppState::instance().loading.details.color   = 0x00FF00;
//
//   UI pages subscribe in create(), unsubscribe in destroy().
// ============================================================================
class AppState {
public:
    static AppState& instance() {
        static AppState inst;
        return inst;
    }

    LoadingPageState  loading;
    MinerPageState    miner;
    ConfigPageState     config;
    ClockPageState      clock;
    MarketPageState     market;
    DashboardPageState     dashboard;
    HrHealthPageState      hr_health;
    SettingSwarmPageState  setting_swarm;

private:
    AppState() = default;
    AppState(const AppState&) = delete;
    AppState& operator=(const AppState&) = delete;
};
