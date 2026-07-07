// #include "Wire.h"
#include "drivers/iic/i2c_master.h"
#include "utils/logger/logger.h"
#include "tca9554.h"
#include <cmath>

#define TCA9554_IIC_ADDR                (uint8_t)(0x20)

#define TCA9554_REG_INPUT_PORT_ADDR     (uint8_t)(0x00)
#define TCA9554_REG_OUTPUT_PORT_ADDR    (uint8_t)(0x01)
#define TCA9554_REG_POLARITY_ADDR       (uint8_t)(0x02)
#define TCA9554_REG_CONFIG_ADDR         (uint8_t)(0x03)

static uint8_t tca9554_readRegister(uint8_t regaddr, uint8_t *data, uint8_t length) {
    esp_err_t ret = i2c_master_register_read(TCA9554_IIC_ADDR, regaddr, data, length);
    if (ret != ESP_OK) {
        LOG_E("TCA9554 read register 0x%02X failed: %d", regaddr, ret);
        return 1;
    }
    return 0;
}

static void tca9554_writeRegister(uint8_t regaddr, uint8_t data) {
    esp_err_t ret = i2c_master_register_write_byte(TCA9554_IIC_ADDR, regaddr, data);
    if (ret != ESP_OK) {
        LOG_E("TCA9554 write register 0x%02X failed: %d", regaddr, ret);
    }
}

void tca9554_set_io_level(uint8_t iobit, uint8_t level) {
    uint8_t current_state = 0;
    tca9554_readRegister(TCA9554_REG_OUTPUT_PORT_ADDR, &current_state, 1); // read current output state

    if (level) {
        current_state |= iobit;  // set the bit high
    } else {
        current_state &= ~iobit; // clear the bit low
    }
    tca9554_writeRegister(TCA9554_REG_OUTPUT_PORT_ADDR, current_state);
}

void tca9554_init() {
    tca9554_writeRegister(TCA9554_REG_CONFIG_ADDR, 0b11111101);// extio1 set to OUTPUT
}