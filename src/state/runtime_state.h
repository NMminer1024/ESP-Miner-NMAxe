// What: Shared runtime telemetry and boot-status model for the new framework.
// Why: Services need one neutral place to publish power, thermal, cooling, and
// input state so UI and future web/mining layers do not query hardware directly.
// Role: Owns the cross-service runtime snapshot updated during polling.
// Benefit: Makes the main flow state-driven and keeps board drivers hidden
// behind abstract interfaces.
#pragma once

#include <array>
#include <stdint.h>
#include <string.h>

#include "state/mining_state.h"

namespace nm::state {

constexpr size_t kMaxFans = 4;
constexpr size_t kMaxButtons = 2;
constexpr size_t kBootMessageMaxLen = 96;
constexpr uint32_t kBootMessageMinVisibleMs = 500;

enum class BootPhase : uint8_t {
    ColdBoot = 0,
    LoadConfig = 1,
    InitBoard = 2,
    InitPower = 3,
    InitCooling = 4,
    InitUi = 5,
    Ready = 6,
    Fault = 7,
};

struct BootState {
    BootPhase phase = BootPhase::ColdBoot;
    char message[kBootMessageMaxLen] = "cold boot";
    uint32_t message_color = 0xFFFFFF;
    uint8_t progress_percent = 0;
    bool board_ready = false;
    bool ui_ready = false;
    bool ready = false;
    char pending_message[kBootMessageMaxLen] = {};
    uint32_t pending_message_color = 0xFFFFFF;
    bool pending_message_valid = false;
    uint32_t message_changed_ms = 0;
};

inline void copy_boot_message(char* dest, size_t dest_size, const char* message) {
    if (dest == nullptr || dest_size == 0) {
        return;
    }

    const char* resolved = message != nullptr ? message : "";
    strncpy(dest, resolved, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

inline bool boot_message_equals(const char* left, const char* right) {
    return strcmp(left != nullptr ? left : "", right != nullptr ? right : "") == 0;
}

inline void flush_pending_boot_message(BootState& boot, uint32_t now_ms) {
    if (!boot.pending_message_valid) {
        return;
    }

    if (boot.message_changed_ms != 0 &&
        (now_ms - boot.message_changed_ms) < kBootMessageMinVisibleMs) {
        return;
    }

    copy_boot_message(boot.message, sizeof(boot.message), boot.pending_message);
    boot.message_color = boot.pending_message_color;
    boot.message_changed_ms = now_ms;
    boot.pending_message_valid = false;
}

inline void publish_boot_state(
    BootState& boot,
    BootPhase phase,
    const char* message,
    uint8_t progress_percent,
    uint32_t message_color,
    uint32_t now_ms) {
    boot.phase = phase;
    if (progress_percent >= boot.progress_percent) {
        boot.progress_percent = progress_percent;
    }

    flush_pending_boot_message(boot, now_ms);

    const bool same_visible =
        boot_message_equals(boot.message, message) && boot.message_color == message_color;
    if (same_visible) {
        boot.pending_message_valid = false;
        return;
    }

    if (boot.message_changed_ms == 0 ||
        (now_ms - boot.message_changed_ms) >= kBootMessageMinVisibleMs) {
        copy_boot_message(boot.message, sizeof(boot.message), message);
        boot.message_color = message_color;
        boot.message_changed_ms = now_ms;
        boot.pending_message_valid = false;
        return;
    }

    copy_boot_message(boot.pending_message, sizeof(boot.pending_message), message);
    boot.pending_message_color = message_color;
    boot.pending_message_valid = true;
}

struct PowerTelemetry {
    bool adc_ready = false;
    bool dc_plugged = false;
    bool vcore_ready = false;
    uint32_t vbus_mv = 0;
    uint32_t ibus_ma = 0;
    uint32_t vcore_mv = 0;
    uint32_t power_mw = 0;
};

struct ThermalTelemetry {
    bool ready = false;
    float vcore_c = 0.0f;
    float asic_c = 0.0f;
};

struct FanTelemetry {
    bool present = false;
    bool self_test_passed = false;
    uint8_t speed_percent = 0;
    uint16_t rpm = 0;
};

struct ButtonTelemetry {
    bool present = false;
    bool pressed = false;
    uint32_t click_count = 0;
    uint32_t double_click_count = 0;
    uint32_t long_press_count = 0;
};

struct NetworkTelemetry {
    bool connecting = false;
    bool sta_connected = false;
    bool ap_ready = false;
    bool client_connected = false;
    bool force_config = false;
    int32_t rssi_dbm = 0;
    uint8_t channel = 0;
    uint8_t status = 0;
    char ssid[33] = {};
    char hostname[64] = {};
    char ap_ssid[64] = {};
    char ip[16] = {};
    char gateway[16] = {};
    char subnet[16] = {};
    char dns[16] = {};
};

struct StratumTelemetry {
    bool task_running = false;
    bool connecting = false;
    bool connected = false;
    bool subscribed = false;
    bool authorized = false;
    bool job_received = false;
    bool ssl = false;
    uint16_t port = 0;
    uint32_t job_counter = 0;
    uint32_t last_update_ms = 0;
    char host[96] = {};
    char user[128] = {};
    char last_error[kBootMessageMaxLen] = {};
};

struct RuntimeState {
    BootState boot;
    PowerTelemetry power;
    NetworkTelemetry network;
    StratumTelemetry stratum;
    ThermalTelemetry thermal;
    MiningState mining;
    std::array<FanTelemetry, kMaxFans> fans{};
    uint8_t fan_count = 0;
    std::array<ButtonTelemetry, kMaxButtons> buttons{};
    uint8_t button_count = 0;
    uint32_t last_sample_ms = 0;
};

}  // namespace nm::state
