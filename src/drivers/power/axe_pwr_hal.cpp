#include "axe_pwr_hal.h"
#include "logger.h"
#include "global.h"
#include <driver/adc.h>

#define DEFAULT_VREF    1100
#define SAMPLES_N   32  


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
    this->_asic_pwr_enable_pins = en_pins;
    this->_asic_pwr_adc_pins    = adc_pins;
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


bool AxePowerHal::init(){
    pinMode(this->_asic_pwr_enable_pins.pwr_0v8, OUTPUT);
    pinMode(this->_asic_pwr_enable_pins.pwr_1v8, OUTPUT);
    pinMode(this->_asic_pwr_enable_pins.pwr_vcore, OUTPUT);

    this->set_pll_0v8(PWR_OFF);
    this->set_vdd_1v8(PWR_OFF);
    this->set_vcore(PWR_OFF);

    //config adc
    adc1_config_width(ADC_WIDTH_BIT_12);
    esp_adc_cal_value_t ret = ESP_ADC_CAL_VAL_NOT_SUPPORTED;

    //vbus
    adc1_config_channel_atten(get_adc1_channel_from_gpio(this->_asic_pwr_adc_pins.vbus), ADC_ATTEN_DB_11); 
    this->_vbus_adc_chars = (esp_adc_cal_characteristics_t *)calloc(1, sizeof(esp_adc_cal_characteristics_t));
    ret = esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, DEFAULT_VREF, this->_vbus_adc_chars);
    //ibus
    adc1_config_channel_atten(get_adc1_channel_from_gpio(this->_asic_pwr_adc_pins.ibus), ADC_ATTEN_DB_2_5); 
    this->_ibus_adc_chars = (esp_adc_cal_characteristics_t *)calloc(1, sizeof(esp_adc_cal_characteristics_t));
    ret = esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_2_5, ADC_WIDTH_BIT_12, DEFAULT_VREF, this->_ibus_adc_chars);
    //vcore
    adc1_config_channel_atten(get_adc1_channel_from_gpio(this->_asic_pwr_adc_pins.vcore), ADC_ATTEN_DB_2_5); 
    this->_vcore_adc_chars = (esp_adc_cal_characteristics_t *)calloc(1, sizeof(esp_adc_cal_characteristics_t));
    ret = esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_2_5, ADC_WIDTH_BIT_12, DEFAULT_VREF, this->_vcore_adc_chars);

    return true;
}

uint32_t AxePowerHal::get_vbus_adc(void){
   uint32_t adc = 0;
    for (int i = 0; i < SAMPLES_N; i++) {
        adc += adc1_get_raw(get_adc1_channel_from_gpio(this->_asic_pwr_adc_pins.vbus));
    }
    adc /= SAMPLES_N;

    uint32_t voltage = esp_adc_cal_raw_to_voltage(adc, this->_vbus_adc_chars);
    return voltage;
}

uint32_t AxePowerHal::get_ibus_adc(void){
    uint32_t adc = 0;
    for (int i = 0; i < SAMPLES_N; i++) {
        adc += adc1_get_raw(get_adc1_channel_from_gpio(this->_asic_pwr_adc_pins.ibus));
    }
    adc /= SAMPLES_N;

    uint32_t voltage = esp_adc_cal_raw_to_voltage(adc, this->_ibus_adc_chars);
    return voltage;
}

uint32_t AxePowerHal::get_vcore_adc(void){
    uint32_t adc = 0;
    for (int i = 0; i < SAMPLES_N; i++) {
        adc += adc1_get_raw(get_adc1_channel_from_gpio(this->_asic_pwr_adc_pins.vcore));
    }
    adc /= SAMPLES_N;

    uint32_t voltage = esp_adc_cal_raw_to_voltage(adc, this->_vcore_adc_chars);
    return voltage;
}