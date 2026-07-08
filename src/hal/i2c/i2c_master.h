// What: Reusable HAL-level I2C master helper for board-composed device drivers.
// Why: Bus setup is transport infrastructure, not a chip driver, so it should
// live below `drivers` and be shared by any I2C-attached device implementation.
// Role: Owns one configured ESP32 I2C master port and exposes register helpers.
// Benefit: Keeps transport concerns in `hal`, device logic in `drivers`, and
// BSP code focused on selecting pins, addresses, and chip instances.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <driver/i2c.h>

namespace nm::hal::i2c {

class I2cMaster {
public:
    I2cMaster(int8_t sda_pin, int8_t scl_pin, uint32_t frequency_hz, i2c_port_t port = I2C_NUM_0)
        : _sda_pin(sda_pin), _scl_pin(scl_pin), _frequency_hz(frequency_hz), _port(port) {}

    bool init();
    bool write(uint8_t device_address, const uint8_t* data, size_t len) const;
    bool read_register(uint8_t device_address, uint8_t reg_addr, uint8_t* data, size_t len) const;
    bool write_command(uint8_t device_address, uint8_t cmd) const;
    bool write_register_byte(uint8_t device_address, uint8_t reg_addr, uint8_t data) const;
    bool write_register_word_le(uint8_t device_address, uint8_t reg_addr, uint16_t data) const;

private:
    int8_t _sda_pin = -1;
    int8_t _scl_pin = -1;
    uint32_t _frequency_hz = 400000;
    i2c_port_t _port = I2C_NUM_0;
    bool _initialized = false;
};

}  // namespace nm::hal::i2c
