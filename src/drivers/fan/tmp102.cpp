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

static bool get_config(uint8_t chipaddr, uint16_t *temp){
    uint8_t data[2] = {0,};
    uint8_t sta = tmp102_readRegister(chipaddr, TMP102_DEV_CFG_ADDR, data, 2);
    if(sta == 0){
        uint16_t tmp = data[0];
        tmp = tmp<<8;
        tmp |= data[1];
        tmp = tmp>>4;
        *temp = tmp;
        return true;
    }
    return false;
}

void tmp102_init() {
    uint16_t config = 0x00;
    if(get_config(TMP102_IIC_VCORE_ADDR, &config)){
        LOG_D("VCORE TMP102 config: 0x%04X", config);
        config |= (0b11 << 6); //CR0 and CR1 set, data update rate to 8Hz
        tmp102_writeRegister(TMP102_IIC_VCORE_ADDR, TMP102_DEV_CFG_ADDR, config);
        uint16_t ddd = 0;
        get_config(TMP102_IIC_VCORE_ADDR, &ddd);
        LOG_D("VCORE TMP102 config after write: 0x%04X", ddd);
    }
    if(get_config(TMP102_IIC_ASIC_ADDR, &config)){
        LOG_D("ASIC TMP102 config: 0x%04X", config);
        config |= (0b11 << 6); //CR0 and CR1 set, data update rate to 8Hz
        tmp102_writeRegister(TMP102_IIC_ASIC_ADDR, TMP102_DEV_CFG_ADDR, config);
        uint16_t ddd = 0;
        get_config(TMP102_IIC_ASIC_ADDR, &ddd);
        LOG_D("ASIC TMP102 config after write: 0x%04X", ddd);
    }
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