#ifndef __NMQAXEPP_BOARD_H_
#define __NMQAXEPP_BOARD_H_
#include "drivers/asic/bm1370/bm1370.h"
#include "drivers/asic/bm1373/bm1373.h"
#include "drivers/power/tps53647/tps53647.h"
#include "drivers/power/tps546d24a/tps546d24a.h"




inline BMxxx* create_qaxepp_asic_instance(HardwareSerial& serial, uint32_t baud, uint8_t rx, uint8_t tx, uint8_t rst) {
    return new BM1370(serial, baud, rx, tx, rst);
}

inline BMxxx* create_qaxepp81_asic_instance(HardwareSerial& serial, uint32_t baud, uint8_t rx, uint8_t tx, uint8_t rst) {
    return new BM1373(serial, baud, rx, tx, rst);
}

// 2-phase board (QAxe++): 0.005 Ω shunt, lower OC limit
inline AxePowerHal* create_qaxepp_2ph_power_instance(axe_pwr_enable_pin_t en_pins, axe_pwr_adc_pin_t adc_pins, uint8_t vcore_regulator_pwm_pin, uint8_t pgood, uint8_t plug) {
    //                    phase, imax,  ifault,  sample reg, tfault, iout_oc_level(0x03=33A), vr_mode
    tps53647_cfg_t cfg = {2,      60,     73.0f,  0.005f,     125.0f, 0x03, TPS53647_VR12_0};
    return new TPS53647Class(en_pins, adc_pins, pgood, plug, cfg);
}
// 3-phase board (QAxe++ Rev6.1): 0.003 Ω shunt, higher OC limit
inline AxePowerHal* create_qaxepp61_3ph_power_instance(axe_pwr_enable_pin_t en_pins, axe_pwr_adc_pin_t adc_pins, uint8_t vcore_regulator_pwm_pin, uint8_t pgood, uint8_t plug) {
    //                    phase, imax,  ifault,  sample reg, tfault, iout_oc_level(0x03=33A), vr_mode
    tps53647_cfg_t cfg = {3,      120,    100.0f,    0.003f,  125.0f, 0x03, TPS53647_VR12_0};
    return new TPS53647Class(en_pins, adc_pins, pgood, plug, cfg);
}
// 4-phase board (QAxe++ Rev8.1, BM1373): 0.003 Ω shunt, higher OC limit.
inline AxePowerHal* create_qaxepp81_4ph_power_instance(axe_pwr_enable_pin_t en_pins, axe_pwr_adc_pin_t adc_pins, uint8_t vcore_regulator_pwm_pin, uint8_t pgood, uint8_t plug) {
    //                    phase, imax,  ifault,  sample reg, tfault, iout_oc_level(0x04=36A), vr_mode
    tps53647_cfg_t cfg = {4,      120,    160.0f,    0.003f,  125.0f, 0x04, TPS53647_VR12_0};
    return new TPS53647Class(en_pins, adc_pins, pgood, plug, cfg);
}

// 3-phase stack (QAxe++ Nexus, BM1373): 1 master (U8) + 2 slaves (U9, U10), TPS546D24A.
// Hardware pin-strap notes (bring-up board, current schematic as-is unless noted):
//  - Master (U8) ADRSEL: Rtop=46.4k to BP1V5 / Rbot=10.0k to AGND (measured) -> address 0x14,
//    SYNC_IN, 0° phase shift. Confirmed by calculation, not just a boot-time scan guess.
//  - Master (U8) MSEL2 (Rbot=6.81k/Rtop=31.6k, as wired): already decodes to
//    STACK_CONFIG=2 slaves/3-phase + 40/52A per-phase OC tier — no change needed.
//    TON_RISE from this strap is irrelevant anyway; hw_init() overwrites it via PMBus.
//  - Slave1 (U9)  MSEL2: MUST change from 0Ω to 10.0k to AGND only (no top resistor)
//    -> device #1 / 3-phase. Currently 0Ω decodes as "device #1 / 2-phase", same as U10.
//  - Slave2 (U10) MSEL2: MUST change from 0Ω to 21.5k to AGND only (no top resistor)
//    -> device #2 / 3-phase. Both slaves at 0Ω collide as "device #1" and block POR.
//  - Slaves' GOSNS/FLWR must tie to their own BP1V5 (not AGND) to be recognized as slaves
//    (already the case per schematic — do not touch).
inline AxePowerHal* create_nexus_3ph_power_instance(axe_pwr_enable_pin_t en_pins, axe_pwr_adc_pin_t adc_pins, uint8_t vcore_regulator_pwm_pin, uint8_t pgood, uint8_t plug) {
    tps546d24a_cfg_t cfg = {
        0x14,   // i2c_addr (measured: ADRSEL Rtop=46.4k/Rbot=10.0k -> address 0x14)
        3,      // num_phases
        0.25f,  // vout_scale_loop: 1.0 was wrong (assumed no FB divider). Real data says
                //   otherwise — cmd 3000mV keeps measuring ~750mV actual (ratio ~0.25,
                //   not 1.0), and the earlier 0.5 test literally halved the rail
                //   (cmd 3000mV -> ~1500mV), proving output = VOUT_COMMAND * vout_scale_loop
                //   on this part. 0.25 matches shufps/NerdQAxePlus's own single-domain
                //   TPS546 default (rev7/TPS546.h TPS546_INIT_SCALE_LOOP); their series-
                //   stacked config uses 0.125. This also explains why STATUS_VOUT's
                //   MIN_MAX_CLAMP was persistently set: VOUT_MAX is converted through this
                //   same wrong scale internally, so the real DAC-side ceiling was clamped
                //   far below 3.1V, not just a stale latched bit.
        156.0f, // ifault_total (52A/phase x 3, chip divides by phase count automatically)
        120.0f, // iwarn_total  (40A/phase x 3)
        125.0f, // tfault
        5.0f,   // ton_rise_ms (written via PMBus regardless of the MSEL2 strap)
        // The 2 BM1373 dies are Vcore-series-stacked, so the rail must cover 2x the
        // per-die 900-1500mV window (1800-3000mV); clamp is widened with margin.
        1700,   // vout_min_mv (hardware protective clamp, wide)
        3100,   // vout_max_mv (hardware protective clamp, wide)
        0.003f, // reg_ibus_sample -- TODO: confirm actual ibus shunt value on this board
        2,      // vcore_series_count: 2 BM1373 dies stacked in SERIES on Vcore
    };
    return new TPS546D24AClass(en_pins, adc_pins, pgood, plug, cfg);
}



#endif // __NMQAXEPP_BOARD_H_