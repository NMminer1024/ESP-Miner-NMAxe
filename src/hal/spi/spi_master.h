// What: Reusable HAL-level SPI master helper for shared chip drivers.
// Why: SPI bus ownership is transport infrastructure, so it belongs in `hal`
// rather than being embedded inside a specific display or touch driver.
// Role: Wraps one Arduino SPI controller instance plus board-selected bus pins.
// Benefit: BSP code can assemble shared SPI devices cleanly while chip drivers
// stay focused on controller registers and panel-specific behavior.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <SPI.h>

namespace nm::hal::spi {

struct SpiBusConfig {
    SPIClass* controller = nullptr;
    int8_t sclk_pin = -1;
    int8_t miso_pin = -1;
    int8_t mosi_pin = -1;

    SpiBusConfig() = default;
    SpiBusConfig(
        SPIClass* controller_value,
        int8_t sclk_pin_value,
        int8_t miso_pin_value,
        int8_t mosi_pin_value)
        : controller(controller_value),
          sclk_pin(sclk_pin_value),
          miso_pin(miso_pin_value),
          mosi_pin(mosi_pin_value) {}
};

class SpiMaster {
public:
    explicit SpiMaster(const SpiBusConfig& config)
        : _config(config) {}

    bool init();
    bool begin_transaction(uint32_t frequency_hz, uint8_t data_mode = SPI_MODE0, uint8_t bit_order = SPI_MSBFIRST);
    void end_transaction();
    uint8_t transfer(uint8_t value);
    void write_bytes(const uint8_t* data, size_t size);

    int8_t sclk_pin() const { return _config.sclk_pin; }
    int8_t miso_pin() const { return _config.miso_pin; }
    int8_t mosi_pin() const { return _config.mosi_pin; }
    int8_t effective_miso_pin() const;

private:
    SPIClass& _controller() const;

    SpiBusConfig _config{};
    bool _initialized = false;
    bool _transaction_open = false;
};

}  // namespace nm::hal::spi
