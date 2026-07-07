// What: Concrete Gamma BSP implementation.
// Why: This file is where compile-time board selection becomes one concrete
// hardware description plus one set of shared driver instances.
// Role: Describes Gamma's pins, panel quirks, policies, and driver assembly.
// Benefit: Gamma stays readable because board-specific facts remain here while
// reusable controller logic lives in shared drivers.
#include "bsp/nmaxe_gamma/board.h"

#include <Arduino.h>
#include <SPI.h>

#include "drivers/asic/bm1370/bm1370.h"
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

namespace nm::bsp::nmaxe_gamma {
namespace {  // namespace nm::bsp::nmaxe_gamma::(file-local board profiles)

constexpr size_t kGammaDisplayInitSequenceCount = 20;

const drivers::St7789PanelConfig& gamma_display_config();

uint16_t gamma_default_rotation() {
    return gamma_display_config().default_flip ? 1u : 3u;
}

bool gamma_backlight_active_high() {
    const auto& backlight = gamma_display_config().backlight;
    return backlight.on_duty > backlight.off_duty;
}

hal::spi::SpiMaster& gamma_display_bus() {
    static const hal::spi::SpiBusConfig config(
        &SPI,
        38,  // sclk_pin
        -1,  // miso_pin
        48); // mosi_pin
    static hal::spi::SpiMaster bus(config);
    return bus;
}

hal::uart::UartPort& gamma_asic_uart() {
    static const hal::uart::UartPortConfig config(
        &Serial1,
        44,  // rx_pin
        43); // tx_pin
    static hal::uart::UartPort port(config);
    return port;
}

hal::adc::AdcSampler& gamma_power_adc() {
    static hal::adc::AdcSampler sampler;
    return sampler;
}

const drivers::Bm1370UartConfig& gamma_asic_config() {
    static const drivers::Bm1370UartConfig config(
        &gamma_asic_uart(),
        115200,   // init_baud
        1000000,  // work_baud
        45);      // reset_pin
    return config;
}

const drivers::Tps53355PinConfig& gamma_power_config() {
    static const drivers::Tps53355PinConfig config(
        13,  // pll_enable_pin
        14,  // vdd_enable_pin
        10,  // vcore_enable_pin
        16,  // vcore_pwm_pin
        21,  // vcore_pgood_pin
        11,  // dc_plug_pin
        2,   // vbus_adc_pin
        3,   // ibus_adc_pin
        1);  // vcore_adc_pin
    return config;
}

const drivers::PwmTachFanConfig& gamma_fan0_config() {
    static const drivers::PwmTachFanConfig config(
        41,              // pwm_pin
        2,               // pwm_channel
        1000 * 100,      // pwm_frequency_hz
        8,               // pwm_resolution_bits
        42,              // tach_pin
        PCNT_UNIT_0,     // pcnt_unit
        PCNT_CHANNEL_0,  // pcnt_channel
        4000,            // self_test_rpm_threshold
        500);            // danger_rpm_threshold
    return config;
}

const drivers::St7789InitCommand (&gamma_display_init_sequence())[kGammaDisplayInitSequenceCount] {
    static const drivers::St7789InitCommand sequence[kGammaDisplayInitSequenceCount] = {
        // 01h SWRESET:
        // Software reset. The legacy Gamma BSP sends this even though a
        // hardware reset line exists.
        drivers::St7789InitCommand(0x01, {}, 150),

        // 11h SLPOUT:
        // Exit sleep mode and wait for the panel power blocks to settle.
        drivers::St7789InitCommand(0x11, {}, 120),

        // 13h NORON:
        // Enter normal display mode.
        drivers::St7789InitCommand(0x13, {}),

        // 36h MADCTL:
        // Set a temporary default memory access mode during panel init.
        // Final orientation comes later from the BSP-selected rotation config.
        drivers::St7789InitCommand(0x36, {0x08}),

        // B6h Display Function Control:
        // Gamma's working 240x135 glass needs this gate/source behavior.
        drivers::St7789InitCommand(0xB6, {0x0A, 0x82}),

        // B0h RAMCTRL:
        // Keep RGB565 packing aligned with the old Gamma implementation.
        drivers::St7789InitCommand(0xB0, {0x00, 0xE0}),

        // 3Ah COLMOD:
        // RGB565, one pixel = 16 bits.
        drivers::St7789InitCommand(0x3A, {0x55}, 10),

        // B2h PORCTRL:
        // Porch timing copied from the known-good legacy panel setup.
        drivers::St7789InitCommand(0xB2, {0x0C, 0x0C, 0x00, 0x33, 0x33}),

        // B7h GCTRL:
        // Gate control tuning.
        drivers::St7789InitCommand(0xB7, {0x35}),

        // BBh VCOMS:
        // Common electrode voltage.
        drivers::St7789InitCommand(0xBB, {0x28}),

        // C0h LCMCTRL:
        // LCD module internal control tuning.
        drivers::St7789InitCommand(0xC0, {0x0C}),

        // C2h VDVVRHEN:
        // Enable VRH/VDV register writes.
        drivers::St7789InitCommand(0xC2, {0x01, 0xFF}),

        // C3h VRHS:
        // Reference voltage setting.
        drivers::St7789InitCommand(0xC3, {0x10}),

        // C4h VDVSET:
        // Voltage delta setting.
        drivers::St7789InitCommand(0xC4, {0x20}),

        // C6h FRCTRL2:
        // Frame rate control.
        drivers::St7789InitCommand(0xC6, {0x0F}),

        // D0h PWCTRL1:
        // Source/gate drive power control.
        drivers::St7789InitCommand(0xD0, {0xA4, 0xA1}),

        // E0h PVGAMCTRL:
        // Positive gamma curve.
        drivers::St7789InitCommand(0xE0, {0xD0, 0x00, 0x02, 0x07, 0x0A, 0x28, 0x32, 0x44, 0x42, 0x06, 0x0E, 0x12, 0x14, 0x17}),

        // E1h NVGAMCTRL:
        // Negative gamma curve.
        drivers::St7789InitCommand(0xE1, {0xD0, 0x00, 0x02, 0x07, 0x0A, 0x28, 0x31, 0x54, 0x47, 0x0E, 0x1C, 0x17, 0x1B, 0x1E}),

        // 21h INVON:
        // This Gamma panel expects inversion enabled.
        drivers::St7789InitCommand(0x21, {}),

        // 29h DISPON:
        // Start visible output after the panel is fully configured.
        drivers::St7789InitCommand(0x29, {}, 120),
    };
    return sequence;
}

const drivers::St7789PanelConfig& gamma_display_config() {
    static const drivers::St7789PanelConfig config = [] {
        drivers::St7789PanelConfig panel;

        panel.display_id = "gamma-st7789";
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

        panel.spi.bus = &gamma_display_bus();
        panel.spi.dc_pin = 47;
        panel.spi.reset_pin = 40;
        panel.spi.cs_pin = 39;
        panel.spi.frequency_hz = 80000000;

        panel.rotation_normal = drivers::St7789RotationConfig(
            0x20 | 0x80 | 0x08,  // MADCTL = MV | MY | BGR
            40,                  // colstart
            52);                 // rowstart
        panel.rotation_flipped = drivers::St7789RotationConfig(
            0x40 | 0x20 | 0x08,  // MADCTL = MX | MV | BGR
            40,                  // colstart
            53);                 // rowstart

        panel.init_sequence = gamma_display_init_sequence();
        panel.init_sequence_count = kGammaDisplayInitSequenceCount;
        return panel;
    }();
    return config;
}

const BoardTraits& board_traits() {
    static BoardTraits traits;
    static bool initialized = false;

    if (!initialized) {
        traits.board_name = "NMAxeGamma";
        traits.display_name = "NMAxeGamma";
        traits.asic_family = AsicFamily::BM1370;
        traits.asic_count = 1;
        traits.fan_count = 1;
        traits.button_count = 2;
        traits.has_touch = false;
        traits.has_button = true;
        traits.has_led = false;
        traits.screen_width = gamma_display_config().width;
        traits.screen_height = gamma_display_config().height;
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
        policy.default_freq_mhz = 600;
        policy.default_vcore_mv = 1125;
        policy.min_vcore_mv = 1000;
        policy.max_vcore_mv = 1250;
        policy.vbus_min_required_mv = 8000;
        policy.default_rotation = gamma_default_rotation();
        policy.default_brightness_pct = 100;
        policy.default_flip = gamma_display_config().default_flip;
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
        defaults.benchmark.freq_max_mhz = 700;
        defaults.benchmark.freq_step_mhz = 25;
        defaults.benchmark.vcore_min_mv = 1000;
        defaults.benchmark.vcore_max_mv = 1250;
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
        profile.width = gamma_display_config().width;
        profile.height = gamma_display_config().height;
        profile.controller_id = DisplayControllerId::ST7789;
        profile.bus_type = DisplayBusType::Spi;
        profile.color_invert = gamma_display_config().color_invert;
        profile.rgb_order = true;
        profile.swap_bytes = false;
        profile.default_rotation = gamma_default_rotation();
        profile.backlight_active_high = gamma_backlight_active_high();
        profile.shared_bus_with_touch = false;
        profile.display_name = gamma_display_config().display_id;
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
        profile.asic_family = AsicFamily::BM1370;
        profile.asic_count = 1;
        profile.chain_topology = ChainTopology::Single;
        profile.default_freq_mhz = 600;
        profile.default_vcore_mv = 1125;
        profile.initial_difficulty = 64;
        profile.job_interval_ms = 500;
        profile.expected_hashrate_ghs = 1200;
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
    // TODO(agent): this phase-1 BM1370 driver currently owns only transport and
    // reset bring-up. Extend it with migrated mining protocol logic later.
    static drivers::Bm1370Asic asic("gamma-bm1370", gamma_asic_config());
    static drivers::Tps53355Power power("gamma-tps53355", gamma_power_config(), gamma_power_adc());
    static hal::i2c::I2cMaster temp_bus(9, 8, 400000);
    static drivers::Tmp102Sensor temp("gamma-tmp102", temp_bus);
    static drivers::St7789Display display(gamma_display_config());
    static drivers::PwmTachFan fan0("gamma-fan0", gamma_fan0_config());
    static drivers::GpioButton boot_button("gamma-boot-button", 0, true);
    static drivers::GpioButton user_button("gamma-user-button", 12, true);
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

}  // namespace nm::bsp::nmaxe_gamma::(file-local board profiles)

// -----------------------------------------------------------------------------
// NMAxeGammaBoard: board context assembly and runtime bring-up entry
// -----------------------------------------------------------------------------

NMAxeGammaBoard::NMAxeGammaBoard() {
    _context.traits = &board_traits();
    _context.policies = &board_policies();
    _context.config_defaults = &board_config_defaults();
    _context.display = &board_display_profile();
    _context.thermal = &board_thermal_profile();
    _context.mining = &board_mining_profile();
    _context.input = &board_input_profile();
    _context.drivers = &board_drivers();
}

void NMAxeGammaBoard::init() {
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

const char* NMAxeGammaBoard::key() const {
    return "nmaxe_gamma";
}

const BoardContext& NMAxeGammaBoard::context() const {
    return _context;
}

}  // namespace nm::bsp::nmaxe_gamma
