#ifndef __NMQAXEPP_BOARD_H_
#define __NMQAXEPP_BOARD_H_
#include "BM1370.h"
#include "tps53467.h"

inline BMxxx* create_qaxepp_asic_instance(HardwareSerial& serial, uint32_t baud, uint8_t rx, uint8_t tx, uint8_t rst) {
    return new BM1370(serial, baud, rx, tx, rst);
}

inline AxePowerHal* create_qaxepp_power_instance(axe_pwr_enable_pin_t en_pins, axe_pwr_adc_pin_t adc_pins, uint8_t vcore_regulator_pwm_pin, uint8_t pgood,  uint8_t plug) {
    return new TPS53467Class(en_pins, adc_pins, vcore_regulator_pwm_pin, pgood, plug);
}
#endif // __NMQAXEPP_BOARD_H_