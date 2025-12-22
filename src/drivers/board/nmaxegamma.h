#ifndef __NMAXEGAMMA_BOARD_H_
#define __NMAXEGAMMA_BOARD_H_
#include "BM1370.h"
#include "power_hal.h"

inline BMxxx* create_gamma_asic_instance(HardwareSerial& serial, uint32_t baud, uint8_t rx, uint8_t tx, uint8_t rst) {
    return new BM1370(serial, baud, rx, tx, rst);
}

// AxePowerHal* create_power_instance(axe_pwr_enable_pin_t en_pins, axe_pwr_adc_pin_t adc_pins, uint8_t vcore_regulator_pin, uint8_t pgood_pin, uint8_t dc_plug_pin) {
//     return new AxePowerHal(en_pins, adc_pins, vcore_regulator_pin, pgood_pin, dc_plug_pin);
// }

#endif // __NMAXEGAMMA_BOARD_H_