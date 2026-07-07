// What: Reusable I2C master helper for BSP-owned peripheral drivers.
// Why: Several board drivers need simple register reads and writes without each
// one re-implementing bus setup and transaction boilerplate.
// Role: Owns one configured ESP32 I2C master port and exposes register helpers.
// Benefit: Keeps bus management shared and lets board code focus on wiring and
// device composition rather than transaction plumbing.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <driver/i2c.h>

namespace nm::drivers::i2c {

class I2cMaster {
public:
    I2cMaster(int8_t sda_pin, int8_t scl_pin, uint32_t frequency_hz, i2c_port_t port = I2C_NUM_0)
        : _sda_pin(sda_pin), _scl_pin(scl_pin), _frequency_hz(frequency_hz), _port(port) {}

    bool init();
    bool read_register(uint8_t device_address, uint8_t reg_addr, uint8_t* data, size_t len) const;
    bool write_register_byte(uint8_t device_address, uint8_t reg_addr, uint8_t data) const;

private:
    int8_t _sda_pin = -1;
    int8_t _scl_pin = -1;
    uint32_t _frequency_hz = 400000;
    i2c_port_t _port = I2C_NUM_0;
    bool _initialized = false;
};

}  // namespace nm::drivers::i2c
