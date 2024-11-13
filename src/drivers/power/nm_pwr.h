#ifndef _NMAXE_PWR_H_
#define _NMAXE_PWR_H_
#include <Arduino.h>
#include "axe_pwr_hal.h"


#define VBUS_MIN_VOLTAGE    (8000)

class NMAxePowerClass: public AxePowerHal{
private:
    uint8_t _vcore_regulator_pwm_pin;
    uint8_t _vcore_pgood_pin;
    uint8_t _dc_plug_pin;
    bool _adc_ready;
public:
    NMAxePowerClass(axe_pwr_enable_pin_t en_pins, axe_pwr_adc_pin_t adc_pins, uint8_t vcore_regulator_pwm_pin, uint8_t pgood,  uint8_t plug):AxePowerHal(en_pins, adc_pins){
        this->_vcore_regulator_pwm_pin = vcore_regulator_pwm_pin;
        this->_vcore_pgood_pin = pgood;
        this->_dc_plug_pin = plug;
        this->_adc_ready = false;
    }
    ~NMAxePowerClass();
    SemaphoreHandle_t  good_xsem;//vcore power and vbus are good
    bool init(void);
    void set_vdd_1v8(power_state_t state);
    void set_pll_0v8(power_state_t state);
    void set_vcore(power_state_t state);
    void set_vcore_voltage(uint16_t req_mv);

    uint32_t get_vbus(void);
    uint32_t get_ibus(void);
    uint32_t get_vcore(void);
    bool is_vcore_good(void);
    bool is_dc_pluged(void);
    bool is_adc_ready(void);
};




void power_thread_entry(void *args);

#endif 