// What: Shared ST7789 SPI direct-drive display implementation.
// Why: Multiple BSPs can reuse the same controller driver while still keeping
// panel-specific pins, offsets, init commands, and bus selection in board code.
// Role: Implements the `Display` abstraction for ST7789-class panels using an
// injected BSP-owned configuration block.
// Benefit: New boards only need to describe their panel wiring and quirks
// instead of cloning another full register-level display driver.
#pragma once

#include <initializer_list>
#include <stddef.h>
#include <stdint.h>

#include <SPI.h>

#include "drivers/display/display.h"

namespace nm::drivers {

struct St7789InitCommand {
    uint8_t command = 0;
    uint8_t data[14] = {};
    uint8_t size = 0;
    uint16_t delay_ms = 0;

    St7789InitCommand() = default;
    St7789InitCommand(uint8_t command_value, std::initializer_list<uint8_t> data_value, uint16_t delay_ms_value = 0);
};

struct St7789RotationConfig {
    uint8_t madctl = 0;
    uint16_t colstart = 0;
    uint16_t rowstart = 0;

    St7789RotationConfig() = default;
    St7789RotationConfig(uint8_t madctl_value, uint16_t colstart_value, uint16_t rowstart_value)
        : madctl(madctl_value), colstart(colstart_value), rowstart(rowstart_value) {}
};

struct St7789BacklightConfig {
    int8_t pin = -1;
    uint8_t pwm_channel = 0;
    uint32_t pwm_frequency_hz = 1000 * 100;
    uint8_t pwm_resolution_bits = 8;
    uint8_t off_duty = 255;
    uint8_t on_duty = 0;
};

struct St7789SpiConfig {
    SPIClass* bus = nullptr;
    int8_t dc_pin = -1;
    int8_t reset_pin = -1;
    int8_t cs_pin = -1;
    int8_t mosi_pin = -1;
    int8_t miso_pin = -1;
    int8_t sclk_pin = -1;
    uint32_t frequency_hz = 80000000;
};

struct St7789PanelConfig {
    const char* display_id = "st7789";
    uint16_t width = 240;
    uint16_t height = 320;
    int8_t power_pin = -1;
    bool color_invert = false;
    bool default_flip = false;
    St7789BacklightConfig backlight{};
    St7789SpiConfig spi{};
    St7789RotationConfig rotation_normal{};
    St7789RotationConfig rotation_flipped{};
    const St7789InitCommand* init_sequence = nullptr;
    size_t init_sequence_count = 0;
};

class St7789Display final : public Display {
public:
    explicit St7789Display(const St7789PanelConfig& config)
        : _config(config), _flip(config.default_flip) {}

    bool init() override;
    const char* name() const override;
    DisplaySize size() const override;
    bool write_rect(const DisplayRect& rect, const uint16_t* pixels) override;
    bool set_flip(bool flip) override;
    bool flip() const override;
    bool set_brightness_percent(uint8_t percent) override;
    uint8_t brightness_percent() const override;

private:
    static constexpr uint16_t kTransferChunkPixels = 128;

    void enable_panel_power();
    void setup_backlight_pwm();
    void set_boot_backlight_off();
    void set_backlight_on();
    void init_bus();
    void hardware_reset();
    void begin_transaction();
    void end_transaction();
    void select_panel();
    void release_panel();
    SPIClass& spi_bus() const;
    int8_t resolved_spi_miso_pin() const;
    void set_command_mode();
    void set_data_mode();
    void write_command_with_data(uint8_t command, const uint8_t* data, uint8_t size);
    void run_init_sequence();
    void apply_rotation(bool flip);
    void begin_memory_write(uint16_t x, uint16_t y, uint16_t width, uint16_t height);
    void end_memory_write();
    bool contains_rect(const DisplayRect& rect) const;

    const St7789PanelConfig& _config;
    bool _initialized = false;
    bool _flip = false;
    uint8_t _brightness_percent = 0;
    St7789RotationConfig _rotation{};
    uint8_t _transfer_buffer[kTransferChunkPixels * 2] = {};
};

}  // namespace nm::drivers
