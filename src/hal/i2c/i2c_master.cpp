// What: ESP32 I2C master HAL implementation shared by chip drivers.
// Why: Chip drivers like TMP102 should depend on a transport helper instead of
// embedding direct ESP-IDF bus transactions inside every device driver.
// Role: Configures the selected port and performs register-level transactions.
// Benefit: Gives the project one place to evolve I2C behavior while keeping
// device drivers and BSP assembly code small and consistent.
#include "hal/i2c/i2c_master.h"

#include <freertos/FreeRTOS.h>

namespace nm::hal::i2c {

namespace {

constexpr TickType_t kI2cTimeoutTicks = pdMS_TO_TICKS(1000);

}  // namespace

bool I2cMaster::init() {
    if (_initialized) {
        return true;
    }

    if (_sda_pin < 0 || _scl_pin < 0) {
        return false;
    }

    i2c_config_t config = {};
    config.mode = I2C_MODE_MASTER;
    config.sda_io_num = static_cast<gpio_num_t>(_sda_pin);
    config.scl_io_num = static_cast<gpio_num_t>(_scl_pin);
    config.sda_pullup_en = GPIO_PULLUP_ENABLE;
    config.scl_pullup_en = GPIO_PULLUP_ENABLE;
    config.master.clk_speed = _frequency_hz;

    if (i2c_param_config(_port, &config) != ESP_OK) {
        return false;
    }

    if (i2c_driver_install(_port, config.mode, 0, 0, 0) != ESP_OK) {
        return false;
    }

    _initialized = true;
    return true;
}

bool I2cMaster::read_register(uint8_t device_address, uint8_t reg_addr, uint8_t* data, size_t len) const {
    if (!_initialized || data == nullptr || len == 0) {
        return false;
    }

    return i2c_master_write_read_device(_port, device_address, &reg_addr, 1, data, len, kI2cTimeoutTicks) == ESP_OK;
}

bool I2cMaster::write_register_byte(uint8_t device_address, uint8_t reg_addr, uint8_t data) const {
    if (!_initialized) {
        return false;
    }

    const uint8_t write_buf[2] = {reg_addr, data};
    return i2c_master_write_to_device(_port, device_address, write_buf, sizeof(write_buf), kI2cTimeoutTicks) == ESP_OK;
}

}  // namespace nm::hal::i2c
