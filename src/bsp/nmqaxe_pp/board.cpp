// What: Concrete QAxe++ BSP implementation.
// Why: The QAxe++ family uses a 320x240 ST7789 panel with TCA9554 reset, no
// panel CS pin, two fans, and four ASICs, which is materially different from
// the Axe/Gamma BSPs.
// Role: Describes QAxe++ pins, panel quirks, policies, and driver assembly.
// Benefit: The firmware can bring up QAxe++ with real display, fan, PMBus
// power, and Vcore telemetry while BM1373 support remains an explicit gap.
#include "bsp/nmqaxe_pp/board.h"

#include <Arduino.h>
#include <SPI.h>

#include "drivers/asic/asic.h"
#include "drivers/asic/bm1370/bm1370.h"
#include "drivers/button/button.h"
#include "drivers/button/gpio/gpio_button.h"
#include "drivers/display/st7789/st7789.h"
#include "drivers/fan/fan.h"
#include "drivers/fan/pwm_tach/pwm_tach_fan.h"
#include "drivers/power/power.h"
#include "drivers/power/tps53647/tps53647.h"
#include "drivers/temp/temp.h"
#include "drivers/temp/tmp102/tmp102.h"
#include "drivers/touch/touch.h"
#include "hal/adc/adc_sampler.h"
#include "hal/i2c/i2c_master.h"
#include "hal/spi/spi_master.h"
#include "hal/uart/uart_port.h"
#include "utils/logger/logger.h"

namespace nm::bsp::nmqaxe_pp {
namespace {

constexpr uint8_t kTca9554Address   = 0x20;
constexpr uint8_t kTca9554RegOutput = 0x01;
constexpr uint8_t kTca9554RegConfig = 0x03;
constexpr uint8_t kTftResetIoBit = 1u << 1;
constexpr size_t kQaxeDisplayInitSequenceCount = 21;

const drivers::St7789PanelConfig& qaxe_display_config();

const char* qaxe_display_name() {
#if defined(BOARD_NMQAXE_PP_REV81)
    return "NMQAxe++Rev8.1";
#elif defined(BOARD_NMQAXE_PP_REV61)
    return "NMQAxe++Rev6.1";
#else
    return "NMQAxe++";
#endif
}

const char* qaxe_key() {
#if defined(BOARD_NMQAXE_PP_REV81)
    return "nmqaxe_pp_rev81";
#elif defined(BOARD_NMQAXE_PP_REV61)
    return "nmqaxe_pp_rev61";
#else
    return "nmqaxe_pp";
#endif
}

AsicFamily qaxe_asic_family() {
#if defined(BOARD_NMQAXE_PP_REV81)
    return AsicFamily::BM1373;
#else
    return AsicFamily::BM1370;
#endif
}

uint16_t qaxe_default_freq_mhz() {
#if defined(BOARD_NMQAXE_PP_REV81)
    return 375;
#elif defined(BOARD_NMQAXE_PP_REV61)
    return 750;
#else
    return 600;
#endif
}

uint16_t qaxe_default_vcore_mv() {
#if defined(BOARD_NMQAXE_PP_REV81)
    return 950;
#elif defined(BOARD_NMQAXE_PP_REV61)
    return 1250;
#else
    return 1150;
#endif
}

uint16_t qaxe_min_vcore_mv() {
#if defined(BOARD_NMQAXE_PP_REV81)
    return 900;
#elif defined(BOARD_NMQAXE_PP_REV61)
    return 1100;
#else
    return 1000;
#endif
}

uint16_t qaxe_max_vcore_mv() {
#if defined(BOARD_NMQAXE_PP_REV81)
    return 1200;
#elif defined(BOARD_NMQAXE_PP_REV61)
    return 1550;
#else
    return 1350;
#endif
}

uint16_t qaxe_benchmark_freq_min_mhz() {
#if defined(BOARD_NMQAXE_PP_REV81)
    return 300;
#elif defined(BOARD_NMQAXE_PP_REV61)
    return 650;
#else
    return 515;
#endif
}

uint16_t qaxe_benchmark_freq_max_mhz() {
#if defined(BOARD_NMQAXE_PP_REV81)
    return 475;
#elif defined(BOARD_NMQAXE_PP_REV61)
    return 1000;
#else
    return 750;
#endif
}

uint32_t qaxe_expected_hashrate_ghs() {
#if defined(BOARD_NMQAXE_PP_REV81)
    return 4500;
#elif defined(BOARD_NMQAXE_PP_REV61)
    return 9000;
#else
    return 6500;
#endif
}

uint16_t qaxe_default_rotation() {
    return qaxe_display_config().default_flip ? 1u : 3u;
}

bool qaxe_backlight_active_high() {
    const auto& backlight = qaxe_display_config().backlight;
    return backlight.on_duty > backlight.off_duty;
}

hal::i2c::I2cMaster& qaxe_i2c_bus() {
    static hal::i2c::I2cMaster bus(8, 7, 400000);
    return bus;
}

hal::adc::AdcSampler& qaxe_power_adc() {
    static hal::adc::AdcSampler sampler;
    return sampler;
}

hal::spi::SpiMaster& qaxe_display_bus() {
    static SPIClass controller(HSPI);
    static const hal::spi::SpiBusConfig config(
        &controller,
        5,
        2,
        1);
    static hal::spi::SpiMaster bus(config);
    return bus;
}

hal::uart::UartPort& qaxe_asic_uart() {
    static const hal::uart::UartPortConfig config(
        &Serial1,
        44,
        43);
    static hal::uart::UartPort port(config);
    return port;
}

const drivers::Bm1370UartConfig& qaxe_bm1370_config() {
    static const drivers::Bm1370UartConfig config(
        &qaxe_asic_uart(),
        115200,
        1000000,
        45);
    return config;
}

const drivers::Tps53647PinConfig& qaxe_power_pins() {
    static const drivers::Tps53647PinConfig pins(
        39,  // pll_enable_pin
        40,  // vdd_enable_pin
        38,  // vcore_enable_pin
        21,  // vcore_pgood_pin
        -1,  // dc_plug_pin, not used on QAxe++
        18,  // vbus_adc_pin
        11,  // ibus_adc_pin
        17); // vcore_adc_pin
    return pins;
}

const drivers::Tps53647ControllerConfig& qaxe_power_controller() {
    static const drivers::Tps53647ControllerConfig config = [] {
        drivers::Tps53647ControllerConfig controller;
#if defined(BOARD_NMQAXE_PP)
        controller.phases = 2;
        controller.imax_a = 60;
        controller.ifault_a = 73.0f;
        controller.ibus_shunt_ohm = 0.005f;
#else
        controller.phases = 3;
        controller.imax_a = 120;
        controller.ifault_a = 100.0f;
        controller.ibus_shunt_ohm = 0.003f;
#endif
        controller.tfault_c = 125.0f;
        controller.i2c_address = 0x71;
        return controller;
    }();
    return config;
}

class QaxeTempSensor final : public drivers::TempSensor {
public:
    QaxeTempSensor(
        const char* sensor_name,
        drivers::Tps53647Power& power,
        drivers::Tmp102Sensor& tmp102)
        : _name(sensor_name), _power(power), _tmp102(tmp102) {}

    bool init() override {
        _tmp102.init();
        return true;
    }

    const char* name() const override { return _name; }
    float read_vcore_c() const override { return _power.read_temperature_c(); }
    float read_asic_c() const override { return _tmp102.read_asic_c(); }

private:
    const char* _name = "qaxe-temp";
    drivers::Tps53647Power& _power;
    drivers::Tmp102Sensor& _tmp102;
};

bool tca_write_register(uint8_t reg, uint8_t value) {
    return qaxe_i2c_bus().write_register_byte(kTca9554Address, reg, value);
}

bool tca_read_register(uint8_t reg, uint8_t& value) {
    return qaxe_i2c_bus().read_register(kTca9554Address, reg, &value, 1);
}

bool tca_set_io_level(uint8_t io_bit, bool level) {
    uint8_t state = 0;
    if (!tca_read_register(kTca9554RegOutput, state)) {
        return false;
    }

    if (level) {
        state |= io_bit;
    } else {
        state &= static_cast<uint8_t>(~io_bit);
    }

    return tca_write_register(kTca9554RegOutput, state);
}

void qaxe_tft_reset_via_tca9554() {
    if (!qaxe_i2c_bus().init()) {
        LOG_E("[bsp.qaxe] TCA9554 I2C init failed");
        return;
    }
    if (!tca_write_register(kTca9554RegConfig, 0b11111101)) {
        LOG_E("[bsp.qaxe] TCA9554 config failed");
        return;
    }

    if (!tca_set_io_level(kTftResetIoBit, false)) {
        LOG_E("[bsp.qaxe] LCD reset low failed via TCA9554");
        return;
    }
    delay(10);

    if (!tca_set_io_level(kTftResetIoBit, true)) {
        LOG_E("[bsp.qaxe] LCD reset high failed via TCA9554");
        return;
    }
    delay(10);
}

const drivers::PwmTachFanConfig& qaxe_fan0_config() {
    static const drivers::PwmTachFanConfig config(
        41,
        1,
        1000 * 100,
        8,
        42,
        PCNT_UNIT_0,
        PCNT_CHANNEL_0,
        1500,
        100);
    return config;
}

const drivers::PwmTachFanConfig& qaxe_fan1_config() {
    static const drivers::PwmTachFanConfig config(
        10,
        2,
        1000 * 100,
        8,
        47,
        PCNT_UNIT_1,
        PCNT_CHANNEL_0,
        2000,
        100);
    return config;
}

const drivers::St7789InitCommand (&qaxe_display_init_sequence())[kQaxeDisplayInitSequenceCount] {
    static const drivers::St7789InitCommand sequence[kQaxeDisplayInitSequenceCount] = {
        drivers::St7789InitCommand(0x11, {}, 120),
        drivers::St7789InitCommand(0x13, {}),
        drivers::St7789InitCommand(0x36, {0x68}),
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
        drivers::St7789InitCommand(0x2A, {0x00, 0x00, 0x01, 0x3F}),
        drivers::St7789InitCommand(0x2B, {0x00, 0x00, 0x00, 0xEF}),
        drivers::St7789InitCommand(0x21, {}),
        drivers::St7789InitCommand(0x29, {}, 120),
    };
    return sequence;
}

const drivers::St7789PanelConfig& qaxe_display_config() {
    static const drivers::St7789PanelConfig config = [] {
        drivers::St7789PanelConfig panel;

        panel.display_id = "qaxe-st7789";
        panel.width = 320;
        panel.height = 240;
        panel.power_pin = -1;
        panel.color_invert = false;
        panel.default_flip = false;

        panel.backlight.pin = 6;
        panel.backlight.pwm_channel = 0;
        panel.backlight.pwm_frequency_hz = 1000 * 100;
        panel.backlight.pwm_resolution_bits = 8;
        panel.backlight.off_duty = 0;
        panel.backlight.on_duty = 255;

        panel.spi.bus = &qaxe_display_bus();
        panel.spi.dc_pin = 3;
        panel.spi.reset_pin = -1;
        panel.spi.cs_pin = -1;
        panel.spi.external_reset = qaxe_tft_reset_via_tca9554;
        panel.spi.frequency_hz = 55000000;
        panel.spi.data_mode = SPI_MODE3;

        panel.rotation_normal = drivers::St7789RotationConfig(0xA8, 0, 0);
        panel.rotation_flipped = drivers::St7789RotationConfig(0x68, 0, 0);

        panel.init_sequence = qaxe_display_init_sequence();
        panel.init_sequence_count = kQaxeDisplayInitSequenceCount;
        return panel;
    }();
    return config;
}

const BoardTraits& board_traits() {
    static BoardTraits traits;
    static bool initialized = false;

    if (!initialized) {
        traits.board_name = "NMQAxe++";
        traits.display_name = qaxe_display_name();
        traits.asic_family = qaxe_asic_family();
        traits.asic_count = 4;
        traits.fan_count = 2;
        traits.button_count = 1;
        traits.has_touch = false;
        traits.has_button = true;
        traits.has_led = false;
        traits.screen_width = qaxe_display_config().width;
        traits.screen_height = qaxe_display_config().height;
        traits.capabilities = BoardCapability::Display |
                              BoardCapability::Button |
                              BoardCapability::Fan |
                              BoardCapability::Power |
                              BoardCapability::Temp |
                              BoardCapability::Mining;
        traits.board_revision = qaxe_display_name();
        initialized = true;
    }

    return traits;
}

const BoardPolicies& board_policies() {
    static BoardPolicies policy;
    static bool initialized = false;

    if (!initialized) {
        policy.default_freq_mhz = qaxe_default_freq_mhz();
        policy.default_vcore_mv = qaxe_default_vcore_mv();
        policy.min_vcore_mv = qaxe_min_vcore_mv();
        policy.max_vcore_mv = qaxe_max_vcore_mv();
        policy.vbus_min_required_mv = 8000;
        policy.default_rotation = qaxe_default_rotation();
        policy.default_brightness_pct = 100;
        policy.default_flip = qaxe_display_config().default_flip;
        initialized = true;
    }

    return policy;
}

const BoardConfigDefaults& board_config_defaults() {
    static BoardConfigDefaults defaults;
    static bool initialized = false;

    if (!initialized) {
        defaults.auto_cycle_pages = false;
        defaults.screensaver_enabled = true;
        defaults.screensaver_timeout_s = 15u * 60u;
        defaults.led_indicator_enabled = true;
        defaults.asic_fan.auto_control = true;
        defaults.asic_fan.target_temp_c = 30.0f;
        defaults.vcore_fan.auto_control = true;
        defaults.vcore_fan.target_temp_c = 70.0f;
        defaults.benchmark.freq_min_mhz = qaxe_benchmark_freq_min_mhz();
        defaults.benchmark.freq_max_mhz = qaxe_benchmark_freq_max_mhz();
        defaults.benchmark.freq_step_mhz = 25;
        defaults.benchmark.vcore_min_mv = qaxe_min_vcore_mv();
        defaults.benchmark.vcore_max_mv = qaxe_max_vcore_mv();
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
        profile.width = qaxe_display_config().width;
        profile.height = qaxe_display_config().height;
        profile.controller_id = DisplayControllerId::ST7789;
        profile.bus_type = DisplayBusType::Spi;
        profile.color_invert = qaxe_display_config().color_invert;
        profile.rgb_order = true;
        profile.swap_bytes = true;
        profile.default_rotation = qaxe_default_rotation();
        profile.backlight_active_high = qaxe_backlight_active_high();
        profile.shared_bus_with_touch = false;
        profile.display_name = qaxe_display_config().display_id;
        initialized = true;
    }

    return profile;
}

const ThermalProfile& board_thermal_profile() {
    static ThermalProfile profile;
    static bool initialized = false;

    if (!initialized) {
        profile.vcore_sensor_id = TemperatureSensorId::PowerInternal;
        profile.asic_sensor_id = TemperatureSensorId::TMP102;
        profile.sensor_bus_type = SensorBusType::I2c;
        profile.sample_policy = "tps53647-vcore-and-tmp102-asic";
        profile.aggregation_policy = "direct";
        profile.fault_value_policy = "nan-on-read-failure";
        initialized = true;
    }

    return profile;
}

const MiningProfile& board_mining_profile() {
    static MiningProfile profile;
    static bool initialized = false;

    if (!initialized) {
        profile.asic_family = qaxe_asic_family();
        profile.asic_count = 4;
        profile.chain_topology = ChainTopology::DaisyChain;
        profile.default_freq_mhz = qaxe_default_freq_mhz();
        profile.default_vcore_mv = qaxe_default_vcore_mv();
        profile.initial_difficulty = 128;
        profile.job_interval_ms = 500;
        profile.expected_hashrate_ghs = qaxe_expected_hashrate_ghs();
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
#if defined(BOARD_NMQAXE_PP_REV81)
    static drivers::NullAsic asic("qaxe-bm1373-pending");
#else
    static drivers::Bm1370Asic asic("qaxe-bm1370", qaxe_bm1370_config());
#endif
    static drivers::Tps53647Power power("qaxe-tps53647", qaxe_power_pins(), qaxe_power_controller(), qaxe_i2c_bus(), qaxe_power_adc());
    static drivers::Tmp102Sensor asic_temp("qaxe-tmp102", qaxe_i2c_bus());
    static QaxeTempSensor temp("qaxe-temp", power, asic_temp);
    static drivers::St7789Display display(qaxe_display_config());
    static drivers::PwmTachFan fan0("qaxe-asic-fan", qaxe_fan0_config());
    static drivers::PwmTachFan fan1("qaxe-vcore-fan", qaxe_fan1_config());
    static drivers::GpioButton boot_button("qaxe-boot-button", 0, true);
    static BoardDrivers drivers;
    static bool initialized = false;

    if (!initialized) {
        drivers.asic = &asic;
        drivers.power = &power;
        drivers.temp = &temp;
        drivers.display = &display;
        drivers.touch = nullptr;
        drivers.fans.push_back(&fan0);
        drivers.fans.push_back(&fan1);
        drivers.buttons.push_back(&boot_button);
        initialized = true;
    }

    return drivers;
}

}  // namespace

NMQAxePPBoard::NMQAxePPBoard() {
    _context.traits = &board_traits();
    _context.policies = &board_policies();
    _context.config_defaults = &board_config_defaults();
    _context.display = &board_display_profile();
    _context.thermal = &board_thermal_profile();
    _context.mining = &board_mining_profile();
    _context.input = &board_input_profile();
    _context.drivers = &board_drivers();
}

void NMQAxePPBoard::init() {
    LOG_I("[bsp] init %s (%s)", key(), traits().board_revision);

    if (drivers().display != nullptr) {
        drivers().display->init();
    }
    if (drivers().touch != nullptr) {
        drivers().touch->init();
    }
    if (drivers().power != nullptr) {
        drivers().power->set_vcore_limits(policies().min_vcore_mv, policies().max_vcore_mv);
        drivers().power->set_vcore_mv(policies().default_vcore_mv);
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

const char* NMQAxePPBoard::key() const {
    return qaxe_key();
}

const BoardContext& NMQAxePPBoard::context() const {
    return _context;
}

}  // namespace nm::bsp::nmqaxe_pp
