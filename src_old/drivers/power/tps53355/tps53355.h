#ifndef __TPS53355_H__
#define __TPS53355_H__
#include <Arduino.h>
#include "drivers/power/power_hal.h"

class TPS53355Class: public AxePowerHal{
private:
    uint8_t _vcore_regulator_pwm_pin;
    uint8_t _vcore_pgood_pin;
    uint8_t _dc_plug_pin;
    uint16_t _vcore_min_mv;
    uint16_t _vcore_max_mv;
public:
    TPS53355Class(axe_pwr_enable_pin_t en_pins, axe_pwr_adc_pin_t adc_pins, uint8_t vcore_regulator_pwm_pin, uint8_t pgood,  uint8_t plug):AxePowerHal(en_pins, adc_pins){
        this->_vcore_regulator_pwm_pin = vcore_regulator_pwm_pin;
        this->_vcore_pgood_pin = pgood;
        this->_dc_plug_pin = plug;
    }
    ~TPS53355Class();
    /** Implementations of pure virtual functions from AxePowerHal */
    void hw_init(void) override;
    void set_vdd_1v8(power_state_t state) override;
    void set_pll_0v8(power_state_t state) override;
    void set_vcore_status(power_state_t state) override;
    void set_vcore_voltage(uint16_t req_mv) override;
    void set_vcore_range(uint16_t min_mv, uint16_t max_mv) override;
    bool is_vcore_ready(void) override;
    bool is_dc_pluged(void) override;
    uint32_t get_vbus(void) override;
    uint32_t get_ibus(void) override;
    uint32_t get_vcore(void) override;
    uint16_t get_vcore_min(void) override { return this->_vcore_min_mv;};
    uint16_t get_vcore_max(void) override { return this->_vcore_max_mv;};
};

#endif 