#ifndef __NMAXE_BOARD_H_
#define __NMAXE_BOARD_H_
#include "BM1366.h"
#include "tps53355.h"

inline BMxxx* create_axe_asic_instance(HardwareSerial& serial, uint32_t baud, uint8_t rx, uint8_t tx, uint8_t rst) {
    return new BM1366(serial, baud, rx, tx, rst);
}

inline AxePowerHal* create_axe_power_instance(axe_pwr_enable_pin_t en_pins, axe_pwr_adc_pin_t adc_pins, uint8_t vcore_regulator_pwm_pin, uint8_t pgood,  uint8_t plug) {
    return new TPS53355Class(en_pins, adc_pins, vcore_regulator_pwm_pin, pgood, plug);
}


#endif // __NMAXE_BOARD_H_