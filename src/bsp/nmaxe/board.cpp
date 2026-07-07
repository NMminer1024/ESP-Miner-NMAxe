// What: Concrete NMAxe BSP implementation.
// Why: Even though NMAxe shares the 240x135 panel class with Gamma, it differs
// in ASIC family, default tuning, and product identity, so it needs its own
// board description and driver bundle.
// Role: Describes NMAxe pins, display quirks, policies, and shared drivers.
// Benefit: Product-level divergence now starts at the BSP instead of leaking
// through compile-time UI macros.
#include "bsp/nmaxe/board.h"

#include <Arduino.h>
#include <SPI.h>

#include "drivers/asic/bm1366/bm1366.h"
#include "drivers/button/button.h"
#include "drivers/button/gpio/gpio_button.h"
#include "drivers/display/st7789/st7789.h"
#include "drivers/fan/fan.h"
#include "drivers/fan/pwm_tach/pwm_tach_fan.h"
#include "drivers/power/power.h"
#include "drivers/power/tps53355/tps53355.h"
#include "drivers/temp/temp.h"
#include "drivers/temp/tmp102/tmp102.h"
#include "drivers/touch/touch.h"
#include "hal/adc/adc_sampler.h"
#include "hal/i2c/i2c_master.h"
#include "hal/spi/spi_master.h"
#include "hal/uart/uart_port.h"
#include "utils/logger/logger.h"

namespace nm::bsp::nmaxe {
namespace {

constexpr size_t kAxeDisplayInitSequenceCount = 20;

const drivers::St7789PanelConfig& axe_display_config();

uint16_t axe_default_rotation() {
    // NMAxe keeps the persisted legacy `flipscreen` bit, but this BSP now
    // maps that bit to the opposite landscape orientation so existing NVS
    // values render 180 degrees from the previous firmware behavior.
    return axe_display_config().default_flip ? 3u : 1u;
}

bool axe_backlight_active_high() {
    const auto& backlight = axe_display_config().backlight;
    return backlight.on_duty > backlight.off_duty;
}

hal::spi::SpiMaster& axe_display_bus() {
    static const hal::spi::SpiBusConfig config(
        &SPI,
        38,
        -1,
        48);
    static hal::spi::SpiMaster bus(config);
    return bus;
}

hal::uart::UartPort& axe_asic_uart() {
    static const hal::uart::UartPortConfig config(
        &Serial1,
        44,
        43);
    static hal::uart::UartPort port(config);
    return port;
}

hal::adc::AdcSampler& axe_power_adc() {
    static hal::adc::AdcSampler sampler;
    return sampler;
}

const drivers::Bm1366UartConfig& axe_asic_config() {
    static const drivers::Bm1366UartConfig config(
        &axe_asic_uart(),
        115200,
        1000000,
        45);
    return config;
}

const drivers::Tps53355PinConfig& axe_power_config() {
    static const drivers::Tps53355PinConfig config(
        13,
        14,
        10,
        16,
        21,
        11,
        2,
        3,
        1);
    return config;
}

const drivers::PwmTachFanConfig& axe_fan0_config() {
    static const drivers::PwmTachFanConfig config(
        41,
        2,
        1000 * 100,
        8,
        42,
        PCNT_UNIT_0,
        PCNT_CHANNEL_0,
        4000,
        500);
    return config;
}

const drivers::St7789InitCommand (&axe_display_init_sequence())[kAxeDisplayInitSequenceCount] {
    // Legacy NMAxe uses the same 240x135 ST7789 panel path as Gamma.
    static const drivers::St7789InitCommand sequence[kAxeDisplayInitSequenceCount] = {
        drivers::St7789InitCommand(0x01, {}, 150),
        drivers::St7789InitCommand(0x11, {}, 120),
        drivers::St7789InitCommand(0x13, {}),
        drivers::St7789InitCommand(0x36, {0x08}),
        drivers::St7789InitCommand(0xB6, {0x0A, 0x82}),
        drivers::St7789InitCommand(0xB0, {0x00, 0xE0}),
        drivers::St7789InitCommand(0x3A, {0x55}, 10),
        drivers::St7789InitCommand(0xB2, {0x0C, 0x0C, 0x00, 0x33, 0x33}),
        drivers::St7789InitCommand(0xB7, {0x35}),
        drivers::St7789InitCommand(0xBB, {0x28}),
        drivers::St7789InitCommand(0xC0, {0x0C}),
        drivers::St7789InitCommand(0xC2, {0x01, 0xFF}),
        drivers::St7789InitCommand(0xC3, {0x10}),
        drivers::St7789InitCommand(0xC4, {0x20}),
        drivers::St7789InitCommand(0xC6, {0x0F}),
        drivers::St7789InitCommand(0xD0, {0xA4, 0xA1}),
        drivers::St7789InitCommand(0xE0, {0xD0, 0x00, 0x02, 0x07, 0x0A, 0x28, 0x32, 0x44, 0x42, 0x06, 0x0E, 0x12, 0x14, 0x17}),
        drivers::St7789InitCommand(0xE1, {0xD0, 0x00, 0x02, 0x07, 0x0A, 0x28, 0x31, 0x54, 0x47, 0x0E, 0x1C, 0x17, 0x1B, 0x1E}),
        drivers::St7789InitCommand(0x21, {}),
        drivers::St7789InitCommand(0x29, {}, 120),
    };
    return sequence;
}

const drivers::St7789PanelConfig& axe_display_config() {
    static const drivers::St7789PanelConfig config = [] {
        drivers::St7789PanelConfig panel;

        panel.display_id = "axe-st7789";
        panel.width = 240;
        panel.height = 135;
        panel.power_pin = 18;
        panel.color_invert = true;
        panel.default_flip = false;

        panel.backlight.pin = 17;
        panel.backlight.pwm_channel = 0;
        panel.backlight.pwm_frequency_hz = 1000 * 100;
        panel.backlight.pwm_resolution_bits = 8;
        panel.backlight.off_duty = 255;
        panel.backlight.on_duty = 0;

        panel.spi.bus = &axe_display_bus();
        panel.spi.dc_pin = 47;
        panel.spi.reset_pin = 40;
        panel.spi.cs_pin = 39;
        panel.spi.frequency_hz = 80000000;

        // Invert the legacy `flipscreen` meaning only for NMAxe:
        // - persisted `flip = 0` used to select landscape rotation 3
        // - persisted `flip = 1` used to select landscape rotation 1
        // Now each saved value resolves to the opposite landscape orientation
        // so the visible result is "current rotation + 180 degrees".
        panel.rotation_normal = drivers::St7789RotationConfig(
            0x20 | 0x80 | 0x08,
            40,
            52);
        panel.rotation_flipped = drivers::St7789RotationConfig(
            0x40 | 0x20 | 0x08,
            40,
            53);

        panel.init_sequence = axe_display_init_sequence();
        panel.init_sequence_count = kAxeDisplayInitSequenceCount;
        return panel;
    }();
    return config;
}

const BoardTraits& board_traits() {
    static BoardTraits traits;
    static bool initialized = false;

    if (!initialized) {
        traits.board_name = "NMAxe";
        traits.display_name = "NMAxe";
        traits.asic_family = AsicFamily::BM1366;
        traits.asic_count = 1;
        traits.fan_count = 1;
        traits.button_count = 2;
        traits.has_touch = false;
        traits.has_button = true;
        traits.has_led = false;
        traits.screen_width = axe_display_config().width;
        traits.screen_height = axe_display_config().height;
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
        policy.default_freq_mhz = 575;
        policy.default_vcore_mv = 1250;
        policy.min_vcore_mv = 1100;
        policy.max_vcore_mv = 1300;
        policy.vbus_min_required_mv = 8000;
        policy.default_rotation = axe_default_rotation();
        policy.default_brightness_pct = 100;
        policy.default_flip = axe_display_config().default_flip;
        initialized = true;
    }

    return policy;
}

const BoardConfigDefaults& board_config_defaults() {
    static BoardConfigDefaults defaults;
    static bool initialized = false;

    if (!initialized) {
        defaults.auto_cycle_pages = false;
        defaults.screensaver_enabled = false;
        defaults.screensaver_timeout_s = 15u * 60u;
        defaults.led_indicator_enabled = true;
        defaults.asic_fan.auto_control = true;
        defaults.asic_fan.target_temp_c = 30.0f;
        defaults.vcore_fan.auto_control = true;
        defaults.vcore_fan.target_temp_c = 70.0f;
        defaults.benchmark.freq_min_mhz = 400;
        defaults.benchmark.freq_max_mhz = 575;
        defaults.benchmark.freq_step_mhz = 25;
        defaults.benchmark.vcore_min_mv = 1100;
        defaults.benchmark.vcore_max_mv = 1300;
        defaults.benchmark.vcore_step_mv = 25;
        defaults.benchmark.sample_interval_s = 5;
        defaults.benchmark.benchmark_time_s = 1000;
        defaults.benchmark.stabilize_time_s = 200;
        initialized = true;
    }

    return defaults;
}

const DisplayProfile& board_display_profile() {
    static DisplayProfile profile;
    static bool initialized = false;

    if (!initialized) {
        profile.width = axe_display_config().width;
        profile.height = axe_display_config().height;
        profile.controller_id = DisplayControllerId::ST7789;
        profile.bus_type = DisplayBusType::Spi;
        profile.color_invert = axe_display_config().color_invert;
        profile.rgb_order = true;
        profile.swap_bytes = false;
        profile.default_rotation = axe_default_rotation();
        profile.backlight_active_high = axe_backlight_active_high();
        profile.shared_bus_with_touch = false;
        profile.display_name = axe_display_config().display_id;
        initialized = true;
    }

    return profile;
}

const ThermalProfile& board_thermal_profile() {
    static ThermalProfile profile;
    static bool initialized = false;

    if (!initialized) {
        profile.vcore_sensor_id = TemperatureSensorId::TMP102;
        profile.asic_sensor_id = TemperatureSensorId::TMP102;
        profile.sensor_bus_type = SensorBusType::I2c;
        profile.sample_policy = "board-sensor-poll";
        profile.aggregation_policy = "single-sensor";
        profile.fault_value_policy = "nan-on-fault";
        initialized = true;
    }

    return profile;
}

const MiningProfile& board_mining_profile() {
    static MiningProfile profile;
    static bool initialized = false;

    if (!initialized) {
        profile.asic_family = AsicFamily::BM1366;
        profile.asic_count = 1;
        profile.chain_topology = ChainTopology::Single;
        profile.default_freq_mhz = 575;
        profile.default_vcore_mv = 1250;
        profile.initial_difficulty = 32;
        profile.job_interval_ms = 2000;
        profile.expected_hashrate_ghs = 500;
        initialized = true;
    }

    return profile;
}

const InputProfile& board_input_profile() {
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
    // TODO(agent): this phase-1 BM1366 driver currently owns only transport and
    // reset bring-up. Extend it with the migrated BM1366 mining protocol later.
    static drivers::Bm1366Asic asic("axe-bm1366", axe_asic_config());
    static drivers::Tps53355Power power("axe-tps53355", axe_power_config(), axe_power_adc());
    static hal::i2c::I2cMaster temp_bus(9, 8, 400000);
    static drivers::Tmp102Sensor temp("axe-tmp102", temp_bus);
    static drivers::St7789Display display(axe_display_config());
    static drivers::PwmTachFan fan0("axe-fan0", axe_fan0_config());
    static drivers::GpioButton boot_button("axe-boot-button", 0, true);
    static drivers::GpioButton user_button("axe-user-button", 12, true);
    static BoardDrivers drivers;
    static bool initialized = false;

    if (!initialized) {
        drivers.asic = &asic;
        drivers.power = &power;
        drivers.temp = &temp;
        drivers.display = &display;
        drivers.touch = nullptr;
        drivers.fans.push_back(&fan0);
        drivers.buttons.push_back(&boot_button);
        drivers.buttons.push_back(&user_button);
        initialized = true;
    }

    return drivers;
}

}  // namespace

NMAxeBoard::NMAxeBoard() {
    _context.traits = &board_traits();
    _context.policies = &board_policies();
    _context.config_defaults = &board_config_defaults();
    _context.display = &board_display_profile();
    _context.thermal = &board_thermal_profile();
    _context.mining = &board_mining_profile();
    _context.input = &board_input_profile();
    _context.drivers = &board_drivers();
}

void NMAxeBoard::init() {
    LOG_I("[bsp] init %s (%s)", key(), traits().board_revision);

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
    for (auto* button : drivers().buttons) {
        if (button != nullptr) {
            button->init();
        }
    }
}

const char* NMAxeBoard::key() const {
    return "nmaxe";
}

const BoardContext& NMAxeBoard::context() const {
    return _context;
}

}  // namespace nm::bsp::nmaxe
