#include "bsp/nmaxe_gamma/board.h"

#include <Arduino.h>

#include "drivers/asic/asic.h"
#include "drivers/display/display.h"
#include "drivers/fan/fan.h"
#include "drivers/power/power.h"
#include "drivers/temp/temp.h"
#include "drivers/touch/touch.h"

namespace nm::bsp::nmaxe_gamma {

namespace {

const BoardTraits& board_traits() {
    static BoardTraits traits;
    static bool initialized = false;

    if (!initialized) {
        traits.board_name = "NMAxeGamma";
        traits.display_name = "NMAxeGamma Placeholder";
        traits.asic_family = AsicFamily::BM1370;
        traits.asic_count = 1;
        traits.fan_count = 1;
        traits.has_touch = false;
        traits.has_button = true;
        traits.has_led = false;
        traits.screen_width = 320;
        traits.screen_height = 240;
        traits.capabilities = BoardCapability::Display |
            BoardCapability::Button |
            BoardCapability::Fan |
            BoardCapability::Power |
            BoardCapability::Temp |
            BoardCapability::Mining;
        traits.board_revision = "skeleton-v1";
        initialized = true;
    }

    return traits;
}

const BoardPolicies& board_policies() {
    static BoardPolicies policy;
    static bool initialized = false;

    if (!initialized) {
        policy.default_freq_mhz = 525;
        policy.default_vcore_mv = 1200;
        policy.min_vcore_mv = 1100;
        policy.max_vcore_mv = 1350;
        policy.default_rotation = 0;
        policy.default_brightness_pct = 80;
        policy.default_flip = false;
        initialized = true;
    }

    return policy;
}

const DisplayProfile& display_profile() {
    static DisplayProfile profile;
    static bool initialized = false;

    if (!initialized) {
        profile.width = 320;
        profile.height = 240;
        profile.controller_id = DisplayControllerId::Placeholder;
        profile.bus_type = DisplayBusType::Spi;
        profile.color_invert = false;
        profile.rgb_order = true;
        profile.swap_bytes = false;
        profile.default_rotation = 0;
        profile.backlight_active_high = true;
        profile.shared_bus_with_touch = false;
        profile.display_name = "gamma-placeholder-display";
        initialized = true;
    }

    return profile;
}

const ThermalProfile& thermal_profile() {
    static ThermalProfile profile;
    static bool initialized = false;

    if (!initialized) {
        profile.vcore_sensor_id = TemperatureSensorId::Placeholder;
        profile.asic_sensor_id = TemperatureSensorId::Placeholder;
        profile.sensor_bus_type = SensorBusType::Internal;
        profile.sample_policy = "placeholder-sample";
        profile.aggregation_policy = "placeholder-aggregate";
        profile.fault_value_policy = "nan-on-fault";
        initialized = true;
    }

    return profile;
}

const MiningProfile& mining_profile() {
    static MiningProfile profile;
    static bool initialized = false;

    if (!initialized) {
        profile.asic_family = AsicFamily::BM1370;
        profile.asic_count = 1;
        profile.chain_topology = ChainTopology::Single;
        profile.default_freq_mhz = 525;
        profile.default_vcore_mv = 1200;
        profile.job_interval_ms = 500;
        profile.expected_hashrate_ghs = 1200;
        initialized = true;
    }

    return profile;
}

const InputProfile& input_profile() {
    static InputProfile profile;
    static bool initialized = false;

    if (!initialized) {
        profile.mode = UiInputMode::ButtonOnly;
        profile.has_touch = false;
        profile.has_button = true;
        initialized = true;
    }

    return profile;
}

const BoardDrivers& board_drivers() {
    static drivers::NullAsic asic("bm1370-placeholder");
    static drivers::NullPower power("gamma-power-placeholder");
    static drivers::NullTempSensor temp("gamma-temp-placeholder", 42.0f, 55.0f);
    static drivers::NullDisplay display("gamma-display-placeholder", 320, 240);
    static drivers::NullFan fan0("gamma-fan-placeholder");
    static BoardDrivers drivers;
    static bool initialized = false;

    if (!initialized) {
        drivers.asic = &asic;
        drivers.power = &power;
        drivers.temp = &temp;
        drivers.display = &display;
        drivers.touch = nullptr;
        drivers.fans.push_back(&fan0);
        initialized = true;
    }

    return drivers;
}

}  // namespace

NMAxeGammaBoard::NMAxeGammaBoard() {
    _context.traits = &board_traits();
    _context.policies = &board_policies();
    _context.display = &display_profile();
    _context.thermal = &thermal_profile();
    _context.mining = &mining_profile();
    _context.input = &input_profile();
    _context.drivers = &board_drivers();
}

void NMAxeGammaBoard::init() {
    Serial.printf("[bsp] init %s (%s)\n", key(), traits().board_revision);

    if (drivers().display != nullptr) {
        drivers().display->init();
    }
    if (drivers().touch != nullptr) {
        drivers().touch->init();
    }
    if (drivers().power != nullptr) {
        drivers().power->init();
    }
    if (drivers().temp != nullptr) {
        drivers().temp->init();
    }
    if (drivers().asic != nullptr) {
        drivers().asic->init();
    }
    for (auto* fan : drivers().fans) {
        if (fan != nullptr) {
            fan->init();
        }
    }
}

const char* NMAxeGammaBoard::key() const {
    return "nmaxe_gamma";
}

const BoardContext& NMAxeGammaBoard::context() const {
    return _context;
}

}  // namespace nm::bsp::nmaxe_gamma
