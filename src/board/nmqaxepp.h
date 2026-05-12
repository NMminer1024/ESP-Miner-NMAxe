#ifndef __NMQAXEPP_BOARD_H_
#define __NMQAXEPP_BOARD_H_
#include "drivers/asic/bm1370/bm1370.h"
#include "drivers/power/tps53647/tps53647.h"




inline BMxxx* create_qaxepp_asic_instance(HardwareSerial& serial, uint32_t baud, uint8_t rx, uint8_t tx, uint8_t rst) {
    return new BM1370(serial, baud, rx, tx, rst);
}

// 2-phase board (QAxe++): 0.005 Ω shunt, lower OC limit
inline AxePowerHal* create_qaxepp_2ph_power_instance(axe_pwr_enable_pin_t en_pins, axe_pwr_adc_pin_t adc_pins, uint8_t vcore_regulator_pwm_pin, uint8_t pgood, uint8_t plug) {
    tps53647_cfg_t cfg = {2, 40, 80.0f, 0.005f};
    return new TPS53647Class(en_pins, adc_pins, pgood, plug, cfg);
}

// 3-phase board (QAxe++ Rev6.1): 0.0025 Ω shunt, higher OC limit
inline AxePowerHal* create_qaxepp_rev61_power_instance(axe_pwr_enable_pin_t en_pins, axe_pwr_adc_pin_t adc_pins, uint8_t vcore_regulator_pwm_pin, uint8_t pgood, uint8_t plug) {
    tps53647_cfg_t cfg = {3, 40, 130.0f, 0.0025f};
    return new TPS53647Class(en_pins, adc_pins, pgood, plug, cfg);
}
#endif // __NMQAXEPP_BOARD_H_