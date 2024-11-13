#include "Wire.h"
#include "logger.h"
#include "tmp102.h"
#include "global.h"
#include <cmath>

static uint8_t tmp102_readRegister(uint8_t chipaddr, uint8_t registerAddress, uint8_t *data, uint8_t length) {
    Wire.beginTransmission(chipaddr); 
    Wire.write(registerAddress); 
    if (Wire.endTransmission() != 0) { 
        return 1; 
    }

    Wire.requestFrom(chipaddr, length); 
    uint8_t index = 0;
    while (Wire.available() && index < length) {
        data[index++] = Wire.read(); 
    }

    if (index != length) {
        return 2; 
    }
    return 0; 
}

static void tmp102_writeRegister(uint8_t chipaddr, uint8_t registerAddress, uint8_t data) {
    Wire.beginTransmission(chipaddr); 
    Wire.write(registerAddress); 
    Wire.write(data); 
    Wire.endTransmission(); 
}

static bool get_temperature(uint8_t chipaddr, float *temp){
    uint8_t data[2] = {0,};
    uint8_t sta = tmp102_readRegister(chipaddr, TMP102_DEV_TMP_ADDR, data, 2);
    if(sta == 0){
        uint16_t tmp = data[0];
        tmp = tmp<<8;
        tmp |= data[1];
        tmp = tmp>>4;
        *temp = tmp * TMP102_12BIT_RESOLUTION;
        return true;
    }
    return false;
}


float get_vcore_temperature(){
    float temp;
    if(get_temperature(TMP102_IIC_VCORE_ADDR, &temp)){
        return temp;
    }
    return NAN;
}

float get_asic_temperature(){
    float temp;
    if(get_temperature(TMP102_IIC_ASIC_ADDR, &temp)){
        return temp;
    }
    return NAN;
}

float get_mcu_temperature(){
    return temperatureRead();
}