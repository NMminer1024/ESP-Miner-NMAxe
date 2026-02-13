#include "power_hal.h"
#include "logger.h"
#include "global.h"
#include <driver/adc.h>

#define DEFAULT_VREF            1100
#define SAMPLES_N               5  
#define SAMPLES_INTERVAL        10


static adc1_channel_t get_adc1_channel_from_gpio(int gpio_num) {
    switch (gpio_num) {
        case 1: return ADC1_CHANNEL_0;
        case 2: return ADC1_CHANNEL_1;
        case 3: return ADC1_CHANNEL_2;
        case 4: return ADC1_CHANNEL_3;
        case 5: return ADC1_CHANNEL_4;
        case 6: return ADC1_CHANNEL_5;
        case 7: return ADC1_CHANNEL_6;
        case 8: return ADC1_CHANNEL_7;
        case 9: return ADC1_CHANNEL_8;
        case 10: return ADC1_CHANNEL_9;
        default: return (adc1_channel_t)-1; 
    }
}

static adc2_channel_t get_adc2_channel_from_gpio(int gpio_num) {
    switch (gpio_num) {
        case 11: return ADC2_CHANNEL_0;
        case 12: return ADC2_CHANNEL_1;
        case 13: return ADC2_CHANNEL_2;
        case 14: return ADC2_CHANNEL_3;
        case 15: return ADC2_CHANNEL_4;
        case 16: return ADC2_CHANNEL_5;
        case 17: return ADC2_CHANNEL_6;
        case 18: return ADC2_CHANNEL_7;
        case 19: return ADC2_CHANNEL_8;
        case 20: return ADC2_CHANNEL_9;
        default: return (adc2_channel_t)-1; 
    }
}

AxePowerHal::AxePowerHal(axe_pwr_enable_pin_t en_pins, axe_pwr_adc_pin_t adc_pins){
    this->_asic_pwr_en_pins  = en_pins;
    this->_asic_pwr_adc_pins = adc_pins;
    if(-1 != this->_asic_pwr_en_pins.pwr_pll_0v8) pinMode(this->_asic_pwr_en_pins.pwr_pll_0v8, OUTPUT);
    if(-1 != this->_asic_pwr_en_pins.pwr_vdd_1v8) pinMode(this->_asic_pwr_en_pins.pwr_vdd_1v8, OUTPUT);
    if(-1 != this->_asic_pwr_en_pins.pwr_vcore)   pinMode(this->_asic_pwr_en_pins.pwr_vcore, OUTPUT);

    this->_is_vbus_adc_configured = false;
    this->_is_ibus_adc_configured = false;
    this->_is_vcore_adc_configured = false;
}

AxePowerHal::~AxePowerHal(){
    if (this->_vbus_adc_chars != NULL) {
        free(this->_vbus_adc_chars);
    }
    if (this->_ibus_adc_chars != NULL) {
        free(this->_ibus_adc_chars);
    }
    if (this->_vcore_adc_chars != NULL) {
        free(this->_vcore_adc_chars);
    }
}

void AxePowerHal::init(){
    //config adc
    adc1_config_width(ADC_WIDTH_BIT_12);
    esp_adc_cal_value_t ret = ESP_ADC_CAL_VAL_NOT_SUPPORTED;

    this->_vbus_adc_chars  = (esp_adc_cal_characteristics_t *)calloc(1, sizeof(esp_adc_cal_characteristics_t));
    this->_ibus_adc_chars  = (esp_adc_cal_characteristics_t *)calloc(1, sizeof(esp_adc_cal_characteristics_t));
    this->_vcore_adc_chars = (esp_adc_cal_characteristics_t *)calloc(1, sizeof(esp_adc_cal_characteristics_t));

    //vbus
    if(this->_asic_pwr_adc_pins.vbus >= 1 && this->_asic_pwr_adc_pins.vbus <= 10){
        adc1_config_channel_atten(get_adc1_channel_from_gpio(this->_asic_pwr_adc_pins.vbus), ADC_ATTEN_DB_11); 
        ret = esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, DEFAULT_VREF, this->_vbus_adc_chars);
        this->_is_vbus_adc_configured = (ret != ESP_ADC_CAL_VAL_NOT_SUPPORTED);
        LOG_D("Vbus ADC characterization: %s, type: %d", this->_is_vbus_adc_configured ? "success" : "not supported", ret);
    }
    else if(this->_asic_pwr_adc_pins.vbus >= 11 && this->_asic_pwr_adc_pins.vbus <= 20){
        adc2_config_channel_atten(get_adc2_channel_from_gpio(this->_asic_pwr_adc_pins.vbus), ADC_ATTEN_DB_11); 
        ret = esp_adc_cal_characterize(ADC_UNIT_2, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, DEFAULT_VREF, this->_vbus_adc_chars);
        this->_is_vbus_adc_configured = (ret != ESP_ADC_CAL_VAL_NOT_SUPPORTED);
        LOG_D("Vbus ADC characterization: %s, type: %d", this->_is_vbus_adc_configured ? "success" : "not supported", ret);
    }
    else{
        this->_is_vbus_adc_configured = false;
        LOG_E("Vbus ADC pin invalid!");
    }

    //ibus
    if(this->_asic_pwr_adc_pins.ibus >= 1 && this->_asic_pwr_adc_pins.ibus <= 10){
        adc1_config_channel_atten(get_adc1_channel_from_gpio(this->_asic_pwr_adc_pins.ibus), ADC_ATTEN_DB_11); 
        ret = esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, DEFAULT_VREF, this->_ibus_adc_chars);
        this->_is_ibus_adc_configured = (ret != ESP_ADC_CAL_VAL_NOT_SUPPORTED);
        LOG_D("Ibus ADC characterization: %s, type: %d", this->_is_ibus_adc_configured ? "success" : "not supported", ret);
    }
    else if(this->_asic_pwr_adc_pins.ibus >= 11 && this->_asic_pwr_adc_pins.ibus <= 20){
        adc2_config_channel_atten(get_adc2_channel_from_gpio(this->_asic_pwr_adc_pins.ibus), ADC_ATTEN_DB_11); 
        ret = esp_adc_cal_characterize(ADC_UNIT_2, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, DEFAULT_VREF, this->_ibus_adc_chars);
        this->_is_ibus_adc_configured = (ret != ESP_ADC_CAL_VAL_NOT_SUPPORTED);
        LOG_D("Ibus ADC characterization: %s, type: %d", this->_is_ibus_adc_configured ? "success" : "not supported", ret);
    }
    else{
        this->_is_ibus_adc_configured = false;
        LOG_E("Ibus ADC pin invalid!");
    }

    //vcore
    if(this->_asic_pwr_adc_pins.vcore >= 1 && this->_asic_pwr_adc_pins.vcore <= 10){
        adc1_config_channel_atten(get_adc1_channel_from_gpio(this->_asic_pwr_adc_pins.vcore), ADC_ATTEN_DB_6); 
        ret = esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_6, ADC_WIDTH_BIT_12, DEFAULT_VREF, this->_vcore_adc_chars);
        this->_is_vcore_adc_configured = (ret != ESP_ADC_CAL_VAL_NOT_SUPPORTED);
        LOG_D("Vcore ADC characterization: %s, type: %d", this->_is_vcore_adc_configured ? "success" : "not supported", ret);
    }
    else if(this->_asic_pwr_adc_pins.vcore >= 11 && this->_asic_pwr_adc_pins.vcore <= 20){
        adc2_config_channel_atten(get_adc2_channel_from_gpio(this->_asic_pwr_adc_pins.vcore), ADC_ATTEN_DB_6); 
        ret = esp_adc_cal_characterize(ADC_UNIT_2, ADC_ATTEN_DB_6, ADC_WIDTH_BIT_12, DEFAULT_VREF, this->_vcore_adc_chars);
        this->_is_vcore_adc_configured = (ret != ESP_ADC_CAL_VAL_NOT_SUPPORTED);
        LOG_D("Vcore ADC characterization: %s, type: %d", this->_is_vcore_adc_configured ? "success" : "not supported", ret);
    }
    else{
        this->_is_vcore_adc_configured = false;
        LOG_E("Vcore ADC pin invalid!");
    }
}

uint32_t AxePowerHal::get_vbus_adc(void){
    uint32_t adc = 0, voltage = 0;
    if(this->_asic_pwr_adc_pins.vbus >= 1 && this->_asic_pwr_adc_pins.vbus <= 10){
        for (int i = 0; i < SAMPLES_N; i++) {
            adc += adc1_get_raw(get_adc1_channel_from_gpio(this->_asic_pwr_adc_pins.vbus));
            delay(SAMPLES_INTERVAL);
        }
    }
    else if(this->_asic_pwr_adc_pins.vbus >= 11 && this->_asic_pwr_adc_pins.vbus <= 20){
        for (int i = 0; i < SAMPLES_N; i++) {
            int tmp = 0;
            adc2_get_raw(get_adc2_channel_from_gpio(this->_asic_pwr_adc_pins.vbus), ADC_WIDTH_BIT_12, &tmp);
            adc += tmp;
            delay(SAMPLES_INTERVAL);
        }
    }
    else{
        LOG_E("Vbus ADC pin invalid!");
        return 0;
    }

    adc /= SAMPLES_N;
    voltage = esp_adc_cal_raw_to_voltage(adc, this->_vbus_adc_chars);
    LOG_D("Vbus ADC raw: %d, voltage: %d mV", adc, voltage);
    return voltage;
}

uint32_t AxePowerHal::get_ibus_adc(void){
    uint32_t adc = 0, voltage = 0;
    if(this->_asic_pwr_adc_pins.ibus >= 1 && this->_asic_pwr_adc_pins.ibus <= 10){
        for (int i = 0; i < SAMPLES_N; i++) {
            adc += adc1_get_raw(get_adc1_channel_from_gpio(this->_asic_pwr_adc_pins.ibus));
            delay(SAMPLES_INTERVAL);
        }
    }
    else if(this->_asic_pwr_adc_pins.ibus >= 11 && this->_asic_pwr_adc_pins.ibus <= 20){
        for (int i = 0; i < SAMPLES_N; i++) {
            int tmp = 0;
            adc2_get_raw(get_adc2_channel_from_gpio(this->_asic_pwr_adc_pins.ibus), ADC_WIDTH_BIT_12, &tmp);
            adc += tmp;
            delay(SAMPLES_INTERVAL);
        }
    }
    else{
        LOG_E("Vbus ADC pin invalid!");
        return 0;
    }

    adc /= SAMPLES_N;
    voltage = esp_adc_cal_raw_to_voltage(adc, this->_ibus_adc_chars);

    LOG_D("Ibus ADC raw: %d, voltage: %d mV", adc, voltage);

    return voltage;
}

uint32_t AxePowerHal::get_vcore_adc(void){
    uint32_t adc = 0, voltage = 0;
    if(this->_asic_pwr_adc_pins.vcore >= 1 && this->_asic_pwr_adc_pins.vcore <= 10){
        for (int i = 0; i < SAMPLES_N; i++) {
            adc += adc1_get_raw(get_adc1_channel_from_gpio(this->_asic_pwr_adc_pins.vcore));
            delay(SAMPLES_INTERVAL);
        }
    }
    else if(this->_asic_pwr_adc_pins.vcore >= 11 && this->_asic_pwr_adc_pins.vcore <= 20){
        for (int i = 0; i < SAMPLES_N; i++) {
            int tmp = 0;
            adc2_get_raw(get_adc2_channel_from_gpio(this->_asic_pwr_adc_pins.vcore), ADC_WIDTH_BIT_12, &tmp);
            adc += tmp;
            delay(SAMPLES_INTERVAL);
        }
    }
    else{
        LOG_E("Vbus ADC pin invalid!");
        return 0;
    }

    adc /= SAMPLES_N;
    voltage = esp_adc_cal_raw_to_voltage(adc, this->_vcore_adc_chars);
    return voltage;
}

bool AxePowerHal::is_adc_ready(void){
    return this->_is_vbus_adc_configured && this->_is_ibus_adc_configured && this->_is_vcore_adc_configured;
}