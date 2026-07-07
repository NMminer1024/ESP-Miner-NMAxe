// What: Concrete Gamma BSP implementation and direct-drive display bring-up.
// Why: This file is where compile-time board description becomes real runtime
// hardware initialization, driver ownership, and exported board metadata.
// Role: Builds Gamma's board context, owns the ST7789 register-level driver, and
// performs ordered bring-up for the board's subsystems.
// Benefit: All Gamma-specific hardware logic stays in one place, which keeps the
// upper layers clean and makes future board additions follow the same BSP pattern.
#include "bsp/nmaxe_gamma/board.h"

#include <Arduino.h>
#include <SPI.h>

#include "drivers/asic/asic.h"
#include "drivers/display/display.h"
#include "drivers/fan/fan.h"
#include "drivers/power/power.h"
#include "drivers/temp/temp.h"
#include "drivers/touch/touch.h"

namespace nm::bsp::nmaxe_gamma {
namespace {  // namespace nm::bsp::nmaxe_gamma::(file-local board profiles)

const BoardTraits& board_traits() {
    static BoardTraits traits;
    static bool initialized = false;

    if (!initialized) {
        traits.board_name = "NMAxeGamma";
        traits.display_name = "NMAxeGamma";
        traits.asic_family = AsicFamily::BM1370;
        traits.asic_count = 1;
        traits.fan_count = 1;
        traits.has_touch = false;
        traits.has_button = true;
        traits.has_led = false;
        traits.screen_width = DisplayDevice::screen_width();
        traits.screen_height = DisplayDevice::screen_height();
        traits.capabilities =   BoardCapability::Display |
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
        policy.default_rotation = DisplayDevice::default_rotation();
        policy.default_brightness_pct = 100;
        policy.default_flip = DisplayDevice::default_flip();
        initialized = true;
    }

    return policy;
}

const DisplayProfile& board_display_profile() {
    static DisplayProfile profile;
    static bool initialized = false;

    if (!initialized) {
        profile.width = DisplayDevice::screen_width();
        profile.height = DisplayDevice::screen_height();
        profile.controller_id = DisplayControllerId::ST7789;
        profile.bus_type = DisplayBusType::Spi;
        profile.color_invert = DisplayDevice::color_invert();
        profile.rgb_order = true;
        profile.swap_bytes = false;
        profile.default_rotation = DisplayDevice::default_rotation();
        profile.backlight_active_high = false;
        profile.shared_bus_with_touch = false;
        profile.display_name = DisplayDevice::display_id();
        initialized = true;
    }

    return profile;
}

const ThermalProfile& board_thermal_profile() {
    static ThermalProfile profile;
    static bool initialized = false;

    if (!initialized) {
        // TODO(agent): replace with the real gamma Vcore sensor id once the BSP thermal path is wired.
        profile.vcore_sensor_id = TemperatureSensorId::Placeholder;
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
    // TODO(agent): replace the remaining Null* instances below with real gamma BSP
    // implementations as each subsystem is brought up.
    static drivers::NullAsic asic("bm1370-placeholder");
    static drivers::NullPower power("gamma-power-placeholder");
    static drivers::NullTempSensor temp("gamma-temp-placeholder", 42.0f, 55.0f);
    static DisplayDevice display;
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

}  // namespace nm::bsp::nmaxe_gamma::(file-local board profiles)

// -----------------------------------------------------------------------------
// DisplayDevice: board-private ST7789 direct-drive implementation
// -----------------------------------------------------------------------------

void DisplayDevice::enable_panel_power() {
    const auto& panel = panel_config();
    if (panel.power_pin < 0) {
        return;
    }

    pinMode(panel.power_pin, OUTPUT);
    digitalWrite(panel.power_pin, LOW);
    delay(20);
}

void DisplayDevice::setup_backlight_pwm() {
    const auto& panel = panel_config();
    if (panel.backlight_pin < 0) {
        return;
    }

    pinMode(panel.backlight_pin, OUTPUT);
    ledcSetup(
        panel.bl_pwm_channel,
        panel.bl_pwm_frequency,
        panel.bl_pwm_resolution);
    ledcAttachPin(panel.backlight_pin, panel.bl_pwm_channel);
}

void DisplayDevice::set_boot_backlight_off() {
    const auto& panel = panel_config();
    if (panel.backlight_pin < 0) {
        return;
    }

    // Gamma/NMAxe legacy hardware uses inverted backlight PWM.
    ledcWrite(panel.bl_pwm_channel, kBacklightOffDuty);
}

void DisplayDevice::set_backlight_on() {
    const auto& panel = panel_config();
    if (panel.backlight_pin < 0) {
        return;
    }

    ledcWrite(panel.bl_pwm_channel, kBacklightOnDuty);
}

bool DisplayDevice::init() {
    if (_initialized) {
        return true;
    }

    enable_panel_power();
    setup_backlight_pwm();
    set_boot_backlight_off();
    init_bus();
    hardware_reset();
    run_init_sequence();
    apply_rotation(default_flip());
    set_backlight_on();

    const auto display_size = size();
    Serial.printf(
        "[bsp.display] gamma direct init panel=%s size=%ux%u dc=%d rst=%d cs=%d mosi=%d miso=%d sclk=%d pwr=%d bl=%d rotation=%u offset=(%u,%u)\n",
        name(),
        static_cast<unsigned>(display_size.width),
        static_cast<unsigned>(display_size.height),
        static_cast<int>(panel_config().dc_pin),
        static_cast<int>(panel_config().reset_pin),
        static_cast<int>(panel_config().spi_cs_pin),
        static_cast<int>(panel_config().spi_mosi_pin),
        static_cast<int>(resolved_spi_miso_pin()),
        static_cast<int>(panel_config().spi_sclk_pin),
        static_cast<int>(panel_config().power_pin),
        static_cast<int>(panel_config().backlight_pin),
        static_cast<unsigned>(default_rotation()),
        static_cast<unsigned>(_rotation.colstart),
        static_cast<unsigned>(_rotation.rowstart));

    _initialized = true;
    return true;
}

const char* DisplayDevice::name() const {
    return display_id();
}

drivers::DisplaySize DisplayDevice::size() const {
    return {screen_width(), screen_height()};
}

bool DisplayDevice::write_rect(const drivers::DisplayRect& rect, const uint16_t* pixels) {
    if (!_initialized || pixels == nullptr || rect.width == 0 || rect.height == 0 || !contains_rect(rect)) {
        return false;
    }

    uint32_t remaining = static_cast<uint32_t>(rect.width) * rect.height;
    const uint16_t* cursor = pixels;

    begin_memory_write(rect.x, rect.y, rect.width, rect.height);
    while (remaining > 0) {
        const uint16_t chunk_pixels =
            remaining > kTransferChunkPixels ? kTransferChunkPixels : static_cast<uint16_t>(remaining);

        for (uint16_t i = 0; i < chunk_pixels; ++i) {
            const uint16_t color = cursor[i];
            _transfer_buffer[i * 2] = static_cast<uint8_t>(color >> 8);
            _transfer_buffer[i * 2 + 1] = static_cast<uint8_t>(color & 0xFF);
        }

        SPI.writeBytes(_transfer_buffer, chunk_pixels * 2);
        cursor += chunk_pixels;
        remaining -= chunk_pixels;
    }
    end_memory_write();

    return true;
}

void DisplayDevice::init_bus() {
    const auto& panel = panel_config();
    pinMode(panel.dc_pin, OUTPUT);
    pinMode(panel.spi_cs_pin, OUTPUT);
    digitalWrite(panel.dc_pin, HIGH);
    digitalWrite(panel.spi_cs_pin, HIGH);

    if (panel.reset_pin >= 0) {
        pinMode(panel.reset_pin, OUTPUT);
        digitalWrite(panel.reset_pin, HIGH);
    }

    SPI.begin(
        panel.spi_sclk_pin,
        resolved_spi_miso_pin(),
        panel.spi_mosi_pin,
        panel.spi_cs_pin);
}

void DisplayDevice::hardware_reset() {
    const auto& panel = panel_config();
    if (panel.reset_pin < 0) {
        return;
    }

    digitalWrite(panel.reset_pin, HIGH);
    delay(5);
    digitalWrite(panel.reset_pin, LOW);
    delay(20);
    digitalWrite(panel.reset_pin, HIGH);
    delay(150);
}

void DisplayDevice::begin_transaction() {
    SPI.beginTransaction(SPISettings(panel_config().spi_frequency_hz, MSBFIRST, SPI_MODE3));
}

void DisplayDevice::end_transaction() {
    SPI.endTransaction();
}

void DisplayDevice::select_panel() {
    digitalWrite(panel_config().spi_cs_pin, LOW);
}

void DisplayDevice::release_panel() {
    digitalWrite(panel_config().spi_cs_pin, HIGH);
}

int8_t DisplayDevice::resolved_spi_miso_pin() const {
    const auto& panel = panel_config();

    // Match the working TFT_eSPI/ESP32-S3 behavior used by the legacy code:
    // when the panel has no real MISO line, bind MISO to MOSI so the FSPI bus
    // still gets a fully-defined pin map.
    return panel.spi_miso_pin >= 0 ? panel.spi_miso_pin : panel.spi_mosi_pin;
}

void DisplayDevice::set_command_mode() {
    digitalWrite(panel_config().dc_pin, LOW);
}

void DisplayDevice::set_data_mode() {
    digitalWrite(panel_config().dc_pin, HIGH);
}

void DisplayDevice::write_command_with_data(uint8_t command, const uint8_t* data, uint8_t size) {
    select_panel();
    set_command_mode();
    SPI.transfer(command);
    if (data != nullptr && size > 0) {
        set_data_mode();
        SPI.writeBytes(data, size);
    }
    release_panel();
}

void DisplayDevice::run_init_sequence() {
    begin_transaction();
    for (const auto& step : init_sequence()) {
        write_command_with_data(step.command, step.data, step.size);
        if (step.delay_ms > 0) {
            end_transaction();
            delay(step.delay_ms);
            begin_transaction();
        }
    }
    end_transaction();
}

void DisplayDevice::apply_rotation(bool flip) {
    _rotation = rotation_config(flip);
    begin_transaction();
    write_command_with_data(0x36, &_rotation.madctl, 1);
    end_transaction();
}

void DisplayDevice::begin_memory_write(uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
    const uint16_t x0 = x + _rotation.colstart;
    const uint16_t x1 = x0 + width - 1;
    const uint16_t y0 = y + _rotation.rowstart;
    const uint16_t y1 = y0 + height - 1;
    const uint8_t column_data[] = {
        static_cast<uint8_t>(x0 >> 8),
        static_cast<uint8_t>(x0 & 0xFF),
        static_cast<uint8_t>(x1 >> 8),
        static_cast<uint8_t>(x1 & 0xFF),
    };
    const uint8_t row_data[] = {
        static_cast<uint8_t>(y0 >> 8),
        static_cast<uint8_t>(y0 & 0xFF),
        static_cast<uint8_t>(y1 >> 8),
        static_cast<uint8_t>(y1 & 0xFF),
    };

    begin_transaction();
    write_command_with_data(0x2A, column_data, sizeof(column_data));
    write_command_with_data(0x2B, row_data, sizeof(row_data));

    // Keep CS asserted after RAMWR so the pixel payload can stream directly.
    select_panel();
    set_command_mode();
    SPI.transfer(0x2C);
    set_data_mode();
}

void DisplayDevice::end_memory_write() {
    release_panel();
    end_transaction();
}

bool DisplayDevice::contains_rect(const drivers::DisplayRect& rect) const {
    const auto display_size = size();
    return rect.x < display_size.width &&
           rect.y < display_size.height &&
           rect.width <= display_size.width - rect.x &&
           rect.height <= display_size.height - rect.y;
}

// -----------------------------------------------------------------------------
// NMAxeGammaBoard: board context assembly and runtime bring-up entry
// -----------------------------------------------------------------------------

NMAxeGammaBoard::NMAxeGammaBoard() {
    _context.traits = &board_traits();
    _context.policies = &board_policies();
    _context.display = &board_display_profile();
    _context.thermal = &board_thermal_profile();
    _context.mining = &board_mining_profile();
    _context.input = &board_input_profile();
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
