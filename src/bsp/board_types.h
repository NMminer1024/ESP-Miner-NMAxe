// What: Shared BSP metadata and driver bundle definitions.
// Why: The framework needs a common language for board capabilities, display
// facts, mining policy, thermal policy, and exported driver interfaces.
// Role: Declares the typed data structures that concrete BSPs fill and upper
// layers consume through `BoardContext`.
// Benefit: Separates "what this board is" from "how this board is implemented",
// which makes BSPs easier to compare, extend, and keep compile-time deterministic.
#pragma once

#include <stdint.h>
#include <vector>

#include "drivers/asic/asic.h"
#include "drivers/display/display.h"
#include "drivers/fan/fan.h"
#include "drivers/power/power.h"
#include "drivers/temp/temp.h"
#include "drivers/touch/touch.h"

namespace nm::bsp {

enum class BoardCapability : uint32_t {
    None = 0,
    Display = 1u << 0,
    Touch = 1u << 1,
    Button = 1u << 2,
    Led = 1u << 3,
    Fan = 1u << 4,
    Power = 1u << 5,
    Temp = 1u << 6,
    Mining = 1u << 7,
};

constexpr uint32_t operator|(BoardCapability lhs, BoardCapability rhs) {
    return static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs);
}

constexpr uint32_t operator|(uint32_t lhs, BoardCapability rhs) {
    return lhs | static_cast<uint32_t>(rhs);
}

enum class AsicFamily : uint8_t {
    Unknown = 0,
    BM1366 = 1,
    BM1370 = 2,
    BM1373 = 3,
};

enum class ChainTopology : uint8_t {
    Single = 0,
    DaisyChain = 1,
};

enum class DisplayControllerId : uint8_t {
    None = 0,
    // Temporary metadata-only tag used while some BSPs are still skeletons.
    // TODO(agent): remove once every active board is wired to a real panel controller id.
    Placeholder = 1,
    ST7789 = 2,
    ILI9341 = 3,
};

enum class DisplayBusType : uint8_t {
    None = 0,
    Spi = 1,
    I2c = 2,
};

enum class TemperatureSensorId : uint8_t {
    None = 0,
    // Temporary metadata-only tag used while the BSP still carries stub thermal wiring.
    // TODO(agent): remove once every active board reports real sensor identities.
    Placeholder = 1,
    PowerInternal = 2,
    TMP102 = 3,
};

enum class SensorBusType : uint8_t {
    None = 0,
    Internal = 1,
    I2c = 2,
    OneWire = 3,
};

enum class UiInputMode : uint8_t {
    ButtonOnly = 0,
    TouchOnly = 1,
    Hybrid = 2,
};

struct DisplayProfile {
    uint16_t width = 0;
    uint16_t height = 0;
    DisplayControllerId controller_id = DisplayControllerId::None;
    DisplayBusType bus_type = DisplayBusType::None;
    bool color_invert = false;
    bool rgb_order = true;
    bool swap_bytes = false;
    uint16_t default_rotation = 0;
    bool backlight_active_high = true;
    bool shared_bus_with_touch = false;
    const char* display_name = "none";
};

struct InputProfile {
    UiInputMode mode = UiInputMode::ButtonOnly;
    bool has_touch = false;
    bool has_button = true;
};

struct MiningProfile {
    AsicFamily asic_family = AsicFamily::Unknown;
    uint8_t asic_count = 0;
    ChainTopology chain_topology = ChainTopology::Single;
    uint16_t default_freq_mhz = 0;
    uint16_t default_vcore_mv = 0;
    uint32_t job_interval_ms = 0;
    uint32_t expected_hashrate_ghs = 0;
};

struct ThermalProfile {
    TemperatureSensorId vcore_sensor_id = TemperatureSensorId::None;
    TemperatureSensorId asic_sensor_id = TemperatureSensorId::None;
    SensorBusType sensor_bus_type = SensorBusType::None;
    const char* sample_policy = "none";
    const char* aggregation_policy = "none";
    const char* fault_value_policy = "none";
};

struct BoardTraits {
    const char* board_name = "unknown";
    const char* display_name = "unknown";
    AsicFamily asic_family = AsicFamily::Unknown;
    uint8_t asic_count = 0;
    uint8_t fan_count = 0;
    bool has_touch = false;
    bool has_button = false;
    bool has_led = false;
    uint16_t screen_width = 0;
    uint16_t screen_height = 0;
    uint32_t capabilities = 0;
    const char* board_revision = "unknown";
};

struct BoardPolicies {
    uint16_t default_freq_mhz = 0;
    uint16_t default_vcore_mv = 0;
    uint16_t min_vcore_mv = 0;
    uint16_t max_vcore_mv = 0;
    uint16_t default_rotation = 0;
    uint8_t default_brightness_pct = 0;
    bool default_flip = false;
};

struct BoardDrivers {
    drivers::Asic* asic = nullptr;
    drivers::Power* power = nullptr;
    drivers::TempSensor* temp = nullptr;
    drivers::Display* display = nullptr;
    drivers::Touch* touch = nullptr;
    std::vector<drivers::Fan*> fans;
};

struct BoardContext {
    const BoardTraits* traits = nullptr;
    const BoardPolicies* policies = nullptr;
    const DisplayProfile* display = nullptr;
    const ThermalProfile* thermal = nullptr;
    const MiningProfile* mining = nullptr;
    const InputProfile* input = nullptr;
    const BoardDrivers* drivers = nullptr;
};

}  // namespace nm::bsp
