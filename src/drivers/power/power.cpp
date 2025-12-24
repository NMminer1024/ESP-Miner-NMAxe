#include "power.h"



AxePowerClass::AxePowerClass(AxePowerHal *instance){
    this->_power_instance = instance;
    this->ready_xsem = xSemaphoreCreateCounting(1, 0);
}

AxePowerClass::~AxePowerClass(){
    if(this->_power_instance){
        delete this->_power_instance;
        this->_power_instance = NULL;
    }
}

bool AxePowerClass::init(void){
    return this->_power_instance->init();
}

void AxePowerClass::set_vdd_1v8(power_state_t state){
    this->_power_instance->set_vdd_1v8(state);
}

void AxePowerClass::set_pll_0v8(power_state_t state){
    this->_power_instance->set_pll_0v8(state);
}

void AxePowerClass::set_vcore_status(power_state_t state){
    this->_power_instance->set_vcore_status(state);
}

void AxePowerClass::set_vcore_voltage(uint16_t req_mv){
    this->_power_instance->set_vcore_voltage(req_mv);
}

void AxePowerClass::set_vcore_range(uint16_t min_mv, uint16_t max_mv){
    this->_power_instance->set_vcore_range(min_mv, max_mv);
}

uint32_t AxePowerClass::get_vbus(void){
    return this->_power_instance->get_vbus();
}

uint32_t AxePowerClass::get_ibus(void){
    return this->_power_instance->get_ibus();
}

uint32_t AxePowerClass::get_vcore(void){
    return this->_power_instance->get_vcore();
}

bool AxePowerClass::is_vcore_ready(void){
    return this->_power_instance->is_vcore_ready();
}

bool AxePowerClass::is_dc_pluged(void){
    return this->_power_instance->is_dc_pluged();
}

bool AxePowerClass::is_adc_ready(void){
    return this->_power_instance->is_adc_ready();
}














