// #include "Wire.h"
#include "i2c_master.h"
#include "logger.h"
#include "tca9554.h"
#include "global.h"
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






void tca9554_set_io_low(uint8_t iobit) {
    // todo 
    // tca9554_writeRegister(TCA9554_REG_OUTPUT_PORT_ADDR, iobit); // set extio2 to low
}
void tca9554_set_io_high(uint8_t iobit) {
    // todo
    // tca9554_writeRegister(TCA9554_REG_OUTPUT_PORT_ADDR, iobit); // set extio2 to high
}



void tca9554_init() {
    tca9554_writeRegister(TCA9554_REG_CONFIG_ADDR, 0b11111101);// extio1 set to OUTPUT
    // lcd reset
    tca9554_writeRegister(TCA9554_REG_OUTPUT_PORT_ADDR, 0b00000000); // set extio2 to low
    delay(50);
    tca9554_writeRegister(TCA9554_REG_OUTPUT_PORT_ADDR, 0b00000010); // set extio2 to high 
}