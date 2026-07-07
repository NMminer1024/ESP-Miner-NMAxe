// What: Shared ST7789 direct-drive implementation used by BSP-owned panel configs.
// Why: Boards can share the same controller logic even when panel dimensions,
// offsets, buses, or init tables differ.
// Role: Owns SPI transactions, reset/init sequencing, backlight control, and
// rectangle writes for ST7789-based displays.
// Benefit: Controller logic stays in one reusable place while BSPs keep full
// control of board-level panel parameters and quirks.
#include "drivers/display/st7789/st7789.h"

#include <Arduino.h>

namespace nm::drivers {

St7789InitCommand::St7789InitCommand(
    uint8_t command_value,
    std::initializer_list<uint8_t> data_value,
    uint16_t delay_ms_value)
    : command(command_value),
      size(static_cast<uint8_t>(data_value.size())),
      delay_ms(delay_ms_value) {
    uint8_t index = 0;
    for (uint8_t value : data_value) {
        if (index >= sizeof(data)) {
            break;
        }
        data[index++] = value;
    }
}

bool St7789Display::init() {
    if (_initialized) {
        return true;
    }

    if (_config.spi.bus == nullptr) {
        return false;
    }

    _enable_panel_power();
    _setup_backlight_pwm();
    _set_boot_backlight_off();
    if (!_init_bus()) {
        return false;
    }
    _hardware_reset();
    _run_init_sequence();
    _apply_rotation(_flip);

    const auto display_size = size();
    Serial.printf(
        "[display.st7789] init panel=%s size=%ux%u dc=%d rst=%d cs=%d mosi=%d miso=%d sclk=%d pwr=%d bl=%d rotation=%u offset=(%u,%u)\n",
        name(),
        static_cast<unsigned>(display_size.width),
        static_cast<unsigned>(display_size.height),
        static_cast<int>(_config.spi.dc_pin),
        static_cast<int>(_config.spi.reset_pin),
        static_cast<int>(_config.spi.cs_pin),
        static_cast<int>(_spi_bus().mosi_pin()),
        static_cast<int>(_spi_bus().effective_miso_pin()),
        static_cast<int>(_spi_bus().sclk_pin()),
        static_cast<int>(_config.power_pin),
        static_cast<int>(_config.backlight.pin),
        static_cast<unsigned>(_flip ? 1u : 3u),
        static_cast<unsigned>(_rotation.colstart),
        static_cast<unsigned>(_rotation.rowstart));

    _initialized = true;
    return true;
}

const char* St7789Display::name() const {
    return _config.display_id;
}

DisplaySize St7789Display::size() const {
    return DisplaySize(_config.width, _config.height);
}

bool St7789Display::write_rect(const DisplayRect& rect, const uint16_t* pixels) {
    if (!_initialized || pixels == nullptr || rect.width == 0 || rect.height == 0 || !_contains_rect(rect)) {
        return false;
    }

    uint32_t remaining = static_cast<uint32_t>(rect.width) * rect.height;
    const uint16_t* cursor = pixels;

    _begin_memory_write(rect.x, rect.y, rect.width, rect.height);
    while (remaining > 0) {
        const uint16_t chunk_pixels =
            remaining > kTransferChunkPixels ? kTransferChunkPixels : static_cast<uint16_t>(remaining);

        for (uint16_t i = 0; i < chunk_pixels; ++i) {
            const uint16_t color = cursor[i];
            _transfer_buffer[i * 2] = static_cast<uint8_t>(color >> 8);
            _transfer_buffer[i * 2 + 1] = static_cast<uint8_t>(color & 0xFF);
        }

        _spi_bus().write_bytes(_transfer_buffer, chunk_pixels * 2);
        cursor += chunk_pixels;
        remaining -= chunk_pixels;
    }
    _end_memory_write();

    return true;
}

bool St7789Display::set_flip(bool flip_value) {
    _flip = flip_value;
    if (_initialized) {
        _apply_rotation(_flip);
    }
    return true;
}

bool St7789Display::flip() const {
    return _flip;
}

bool St7789Display::set_brightness_percent(uint8_t percent) {
    if (_config.backlight.pin < 0) {
        return false;
    }

    if (percent > 100) {
        percent = 100;
    }

    const uint16_t off_duty = _config.backlight.off_duty;
    const uint16_t on_duty = _config.backlight.on_duty;
    const bool descending = off_duty >= on_duty;
    const uint16_t duty_span = descending ? static_cast<uint16_t>(off_duty - on_duty)
                                          : static_cast<uint16_t>(on_duty - off_duty);
    const uint16_t duty = descending
        ? static_cast<uint16_t>(off_duty - ((static_cast<uint16_t>(percent) * duty_span) / 100u))
        : static_cast<uint16_t>(off_duty + ((static_cast<uint16_t>(percent) * duty_span) / 100u));

    ledcWrite(_config.backlight.pwm_channel, static_cast<uint32_t>(duty));
    _brightness_percent = percent;
    return true;
}

uint8_t St7789Display::brightness_percent() const {
    return _brightness_percent;
}

void St7789Display::_enable_panel_power() {
    if (_config.power_pin < 0) {
        return;
    }

    pinMode(_config.power_pin, OUTPUT);
    digitalWrite(_config.power_pin, LOW);
    delay(20);
}

void St7789Display::_setup_backlight_pwm() {
    if (_config.backlight.pin < 0) {
        return;
    }

    pinMode(_config.backlight.pin, OUTPUT);
    ledcSetup(
        _config.backlight.pwm_channel,
        _config.backlight.pwm_frequency_hz,
        _config.backlight.pwm_resolution_bits);
    ledcAttachPin(_config.backlight.pin, _config.backlight.pwm_channel);
}

void St7789Display::_set_boot_backlight_off() {
    if (_config.backlight.pin < 0) {
        return;
    }

    ledcWrite(_config.backlight.pwm_channel, _config.backlight.off_duty);
    _brightness_percent = 0;
}

bool St7789Display::_init_bus() {
    pinMode(_config.spi.dc_pin, OUTPUT);
    pinMode(_config.spi.cs_pin, OUTPUT);
    digitalWrite(_config.spi.dc_pin, HIGH);
    digitalWrite(_config.spi.cs_pin, HIGH);

    if (_config.spi.reset_pin >= 0) {
        pinMode(_config.spi.reset_pin, OUTPUT);
        digitalWrite(_config.spi.reset_pin, HIGH);
    }

    return _spi_bus().init();
}

void St7789Display::_hardware_reset() {
    if (_config.spi.reset_pin < 0) {
        return;
    }

    digitalWrite(_config.spi.reset_pin, HIGH);
    delay(5);
    digitalWrite(_config.spi.reset_pin, LOW);
    delay(20);
    digitalWrite(_config.spi.reset_pin, HIGH);
    delay(150);
}

void St7789Display::_begin_transaction() {
    _spi_bus().begin_transaction(_config.spi.frequency_hz, _config.spi.data_mode, _config.spi.bit_order);
}

void St7789Display::_end_transaction() {
    _spi_bus().end_transaction();
}

void St7789Display::_select_panel() {
    digitalWrite(_config.spi.cs_pin, LOW);
}

void St7789Display::_release_panel() {
    digitalWrite(_config.spi.cs_pin, HIGH);
}

hal::spi::SpiMaster& St7789Display::_spi_bus() const {
    return *_config.spi.bus;
}

void St7789Display::_set_command_mode() {
    digitalWrite(_config.spi.dc_pin, LOW);
}

void St7789Display::_set_data_mode() {
    digitalWrite(_config.spi.dc_pin, HIGH);
}

void St7789Display::_write_command_with_data(uint8_t command, const uint8_t* data, uint8_t size) {
    _select_panel();
    _set_command_mode();
    _spi_bus().transfer(command);
    if (data != nullptr && size > 0) {
        _set_data_mode();
        _spi_bus().write_bytes(data, size);
    }
    _release_panel();
}

void St7789Display::_run_init_sequence() {
    _begin_transaction();
    for (size_t i = 0; i < _config.init_sequence_count; ++i) {
        const auto& step = _config.init_sequence[i];
        _write_command_with_data(step.command, step.data, step.size);
        if (step.delay_ms > 0) {
            _end_transaction();
            delay(step.delay_ms);
            _begin_transaction();
        }
    }
    _end_transaction();
}

void St7789Display::_apply_rotation(bool flip_value) {
    _rotation = flip_value ? _config.rotation_flipped : _config.rotation_normal;
    _begin_transaction();
    _write_command_with_data(0x36, &_rotation.madctl, 1);
    _end_transaction();
}

void St7789Display::_begin_memory_write(uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
    const uint16_t x0 = static_cast<uint16_t>(x + _rotation.colstart);
    const uint16_t x1 = static_cast<uint16_t>(x0 + width - 1);
    const uint16_t y0 = static_cast<uint16_t>(y + _rotation.rowstart);
    const uint16_t y1 = static_cast<uint16_t>(y0 + height - 1);
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

    _begin_transaction();
    _write_command_with_data(0x2A, column_data, sizeof(column_data));
    _write_command_with_data(0x2B, row_data, sizeof(row_data));

    _select_panel();
    _set_command_mode();
    _spi_bus().transfer(0x2C);
    _set_data_mode();
}

void St7789Display::_end_memory_write() {
    _release_panel();
    _end_transaction();
}

bool St7789Display::_contains_rect(const DisplayRect& rect) const {
    const auto display_size = size();
    return rect.x < display_size.width &&
           rect.y < display_size.height &&
           rect.width <= display_size.width - rect.x &&
           rect.height <= display_size.height - rect.y;
}

}  // namespace nm::drivers
