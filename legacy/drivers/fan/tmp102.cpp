// #include "Wire.h"
#include "drivers/iic/i2c_master.h"
#include "utils/logger/logger.h"
#include "tmp102.h"
#include "global.h"
#include "drivers/temp/temp_hal.h"
#include <cmath>

static uint8_t tmp102_readRegister(uint8_t chipaddr, uint8_t registerAddress, uint8_t *data, uint8_t length) {
    esp_err_t ret = i2c_master_register_read(chipaddr, registerAddress, data, length);
    if (ret != ESP_OK) {
        LOG_D("TMP102 read register 0x%02X failed: %d", registerAddress, ret);
        return 1;
    }
    return 0;
}

static void tmp102_writeRegister(uint8_t chipaddr, uint8_t registerAddress, uint8_t data) {
    esp_err_t ret = i2c_master_register_write_byte(chipaddr, registerAddress, data);
    if (ret != ESP_OK) {
        LOG_E("TMP102 write register 0x%02X failed: %d", registerAddress, ret);
    }
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

// ---------------------------------------------------------------------------
// Temperature HAL registration helpers
// ---------------------------------------------------------------------------
static float _tmp102_vcore_cb(void*) {
    float t;
    return get_temperature(TMP102_IIC_VCORE_ADDR, &t) ? t : NAN;
}

static float _tmp102_asic_cb(void*) {
    float t;
    return get_temperature(TMP102_IIC_ASIC_ADDR, &t) ? t : NAN;
}

void tmp102_register_vcore_temp_hal(void) { temp_hal_register_vcore(_tmp102_vcore_cb, nullptr); }
void tmp102_register_asic_temp_hal(void)  { temp_hal_register_asic (_tmp102_asic_cb,  nullptr); }
void tmp102_register_temp_hal(void)       { tmp102_register_vcore_temp_hal(); tmp102_register_asic_temp_hal(); }

