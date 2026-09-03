// #include "Wire.h"
#include "drivers/iic/i2c_master.h"
#include "utils/logger/logger.h"
#include "tmp102.h"
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

void tmp102_debug_print(uint8_t chipaddr, const char* label){
    float temp = NAN;
    uint16_t config = 0;
    bool temp_ok = get_temperature(chipaddr, &temp);
    bool cfg_ok  = get_config(chipaddr, &config);
    if (!temp_ok) {
        LOG_W("TMP102 %s (0x%02X): temperature read failed", label, chipaddr);
        return;
    }
    // CR1:CR0 sit at bits [7:6] of the 12-bit config value returned by get_config()
    // (see the >>4 shift in get_config()); 00=0.25Hz 01=1Hz 10=4Hz 11=8Hz conversion rate.
    uint8_t conv_rate = (config >> 6) & 0x03;
    static const char* conv_rate_str[4] = {"0.25Hz", "1Hz", "4Hz", "8Hz"};
    LOG_I("TMP102 %s (0x%02X): %.2f C, config=0x%04X (conv_rate=%s)%s",
          label, chipaddr, temp, config, conv_rate_str[conv_rate], cfg_ok ? "" : " [config read failed]");
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

