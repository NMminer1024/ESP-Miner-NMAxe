// What: ESP32 Arduino SPI HAL implementation shared by chip drivers.
// Why: Shared devices such as ST7789 panels should not each open-code SPI bus
// bring-up and transaction control.
// Role: Owns one SPI controller binding and exposes a tiny transaction API.
// Benefit: Keeps SPI transport policy centralized while BSPs still choose bus
// pins and shared drivers remain reusable.
#include "hal/spi/spi_master.h"

namespace nm::hal::spi {

bool SpiMaster::init() {
    if (_initialized) {
        return true;
    }

    if (_config.sclk_pin < 0 || _config.mosi_pin < 0) {
        return false;
    }

    _controller().begin(_config.sclk_pin, effective_miso_pin(), _config.mosi_pin, -1);
    _initialized = true;
    return true;
}

bool SpiMaster::begin_transaction(uint32_t frequency_hz, uint8_t data_mode, uint8_t bit_order) {
    if (!_initialized && !init()) {
        return false;
    }

    _controller().beginTransaction(SPISettings(frequency_hz, bit_order, data_mode));
    _transaction_open = true;
    return true;
}

void SpiMaster::end_transaction() {
    if (!_transaction_open) {
        return;
    }

    _controller().endTransaction();
    _transaction_open = false;
}

uint8_t SpiMaster::transfer(uint8_t value) {
    return _controller().transfer(value);
}

void SpiMaster::write_bytes(const uint8_t* data, size_t size) {
    if (data == nullptr || size == 0) {
        return;
    }

    _controller().writeBytes(data, size);
}

int8_t SpiMaster::effective_miso_pin() const {
    return _config.miso_pin >= 0 ? _config.miso_pin : _config.mosi_pin;
}

SPIClass& SpiMaster::_controller() const {
    if (_config.controller != nullptr) {
        return *_config.controller;
    }
    return SPI;
}

}  // namespace nm::hal::spi
