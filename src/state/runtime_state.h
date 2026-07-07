// What: Shared runtime telemetry and boot-status model for the new framework.
// Why: Services need one neutral place to publish power, thermal, cooling, and
// input state so UI and future web/mining layers do not query hardware directly.
// Role: Owns the cross-service runtime snapshot updated during polling.
// Benefit: Makes the main flow state-driven and keeps board drivers hidden
// behind abstract interfaces.
#pragma once

#include <array>
#include <stdint.h>

#include "state/mining_state.h"

namespace nm::state {

constexpr size_t kMaxFans = 4;
constexpr size_t kMaxButtons = 2;

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
    const char* message = "cold boot";
    bool board_ready = false;
    bool ui_ready = false;
    bool ready = false;
};

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

struct RuntimeState {
    BootState boot;
    PowerTelemetry power;
    ThermalTelemetry thermal;
    MiningState mining;
    std::array<FanTelemetry, kMaxFans> fans{};
    uint8_t fan_count = 0;
    std::array<ButtonTelemetry, kMaxButtons> buttons{};
    uint8_t button_count = 0;
    uint32_t last_sample_ms = 0;
};

}  // namespace nm::state
