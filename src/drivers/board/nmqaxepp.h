#ifndef __NMQAXEPP_BOARD_H_
#define __NMQAXEPP_BOARD_H_
#include "drivers/asic/bm1370/bm1370.h"
#include "drivers/power/tps53647/tps53647.h"




inline BMxxx* create_qaxepp_asic_instance(HardwareSerial& serial, uint32_t baud, uint8_t rx, uint8_t tx, uint8_t rst) {
    return new BM1370(serial, baud, rx, tx, rst);
}

inline AxePowerHal* create_qaxepp_power_instance(axe_pwr_enable_pin_t en_pins, axe_pwr_adc_pin_t adc_pins, uint8_t vcore_regulator_pwm_pin, uint8_t pgood,  uint8_t plug) {
    return new TPS53647Class(en_pins, adc_pins, pgood, plug);
}
#endif // __NMQAXEPP_BOARD_H_