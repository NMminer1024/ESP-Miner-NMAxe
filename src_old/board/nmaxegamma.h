#ifndef __NMAXEGAMMA_BOARD_H_
#define __NMAXEGAMMA_BOARD_H_
#include "drivers/asic/bm1370/bm1370.h"
#include "drivers/power/tps53355/tps53355.h"

inline BMxxx* create_gamma_asic_instance(HardwareSerial& serial, uint32_t baud, uint8_t rx, uint8_t tx, uint8_t rst) {
    return new BM1370(serial, baud, rx, tx, rst);
}

inline AxePowerHal* create_gamma_power_instance(axe_pwr_enable_pin_t en_pins, axe_pwr_adc_pin_t adc_pins, uint8_t vcore_regulator_pwm_pin, uint8_t pgood,  uint8_t plug) {
    return new TPS53355Class(en_pins, adc_pins, vcore_regulator_pwm_pin, pgood, plug);
}

#endif // __NMAXEGAMMA_BOARD_H_