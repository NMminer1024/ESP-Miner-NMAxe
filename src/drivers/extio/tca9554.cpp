#include "Wire.h"
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
    Wire.beginTransmission(TCA9554_IIC_ADDR); 
    Wire.write(regaddr); 
    if (Wire.endTransmission() != 0) { 
        return 1; 
    }

    Wire.requestFrom(TCA9554_IIC_ADDR, length); 
    uint8_t index = 0;
    while (Wire.available() && index < length) {
        data[index++] = Wire.read(); 
    }

    if (index != length) {
        return 2; 
    }
    return 0; 
}

static void tca9554_writeRegister(uint8_t regaddr, uint8_t data) {
    Wire.beginTransmission(TCA9554_IIC_ADDR); 
    Wire.write(regaddr); 
    Wire.write(data); 
    Wire.endTransmission(); 
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