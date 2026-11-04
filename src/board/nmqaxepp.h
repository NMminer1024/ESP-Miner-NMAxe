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

// ── QAxe++ (2-phase) / QAxe++ Rev6.1 (3-phase) VRM thermal limits ───────────
// Single source of truth for the TPS53647 over-temperature trip. Both power
// instances below write this value to PMBUS_OT_FAULT_LIMIT (OT_WARN = -10 °C,
// see tps53647.cpp hw_init), and board.cpp derives the *software* limits from it
// so the two layers cannot drift apart.
//
// They DID drift: board.cpp hardcoded pwr.temp_limit.high = 130 °C while the
// regulator shuts its output down at 125 °C. The software over-temperature
// escalation (system_threads: vcore > temp_limit.high for 10 s) could therefore
// never fire first, and the auto-fan clamp (high - 5 = 125 °C) sat exactly ON the
// chip trip instead of below it. Net effect: between 125 and 130 °C the rail was
// disabled by hardware while the firmware still believed everything was fine.
// Keep this in sync with the tfault field of create_qaxepp_2ph/61_*_power_instance.
constexpr float QXPP_VRM_OT_FAULT_C = 125.0f;

// 2-phase board (QAxe++): 0.005 Ω shunt, lower OC limit
inline AxePowerHal* create_qaxepp_2ph_power_instance(axe_pwr_enable_pin_t en_pins, axe_pwr_adc_pin_t adc_pins, uint8_t vcore_regulator_pwm_pin, uint8_t pgood, uint8_t plug) {
    //                    phase, imax,  ifault,  sample reg, tfault, iout_oc_level(0x03=33A), vr_mode
    tps53647_cfg_t cfg = {2,      60,     73.0f,  0.005f,     QXPP_VRM_OT_FAULT_C, 0x03, TPS53647_VR12_0};
    return new TPS53647Class(en_pins, adc_pins, pgood, plug, cfg);
}
// 3-phase board (QAxe++ Rev6.1): 0.003 Ω shunt, higher OC limit
inline AxePowerHal* create_qaxepp61_3ph_power_instance(axe_pwr_enable_pin_t en_pins, axe_pwr_adc_pin_t adc_pins, uint8_t vcore_regulator_pwm_pin, uint8_t pgood, uint8_t plug) {
    //                    phase, imax,  ifault,  sample reg, tfault, iout_oc_level(0x03=33A), vr_mode
    tps53647_cfg_t cfg = {3,      120,    100.0f,    0.003f,  QXPP_VRM_OT_FAULT_C, 0x03, TPS53647_VR12_0};
    return new TPS53647Class(en_pins, adc_pins, pgood, plug, cfg);
}
// 4-phase board (QAxe++ Rev8.1, BM1373): 0.003 Ω shunt, higher OC limit.
inline AxePowerHal* create_qaxepp81_4ph_power_instance(axe_pwr_enable_pin_t en_pins, axe_pwr_adc_pin_t adc_pins, uint8_t vcore_regulator_pwm_pin, uint8_t pgood, uint8_t plug) {
    //                    phase, imax,  ifault,  sample reg, tfault, iout_oc_level(0x04=36A), vr_mode
    tps53647_cfg_t cfg = {4,      120,    160.0f,    0.003f,  125.0f, 0x04, TPS53647_VR12_0};
    return new TPS53647Class(en_pins, adc_pins, pgood, plug, cfg);
}

// ── Nexus VRM thermal limits ────────────────────────────────────────────────
// Single source of truth for the TPS546D24A over-temperature trip. board.cpp
// derives the *software* limits from this value, so the two layers cannot drift
// apart again (they did: software allowed 100 °C while the chip tripped at 85 °C,
// so a fan target of 90 °C sat in the gap where the windings had already been
// shut down by the regulator's own OT_FAULT and the fans never reached full
// speed).
//
// 85 °C is the value written to PMBUS_OT_FAULT_LIMIT (OT_WARN = 75 °C, i.e.
// tfault - 10, see tps546d24a.cpp hw_init()). Do not raise it to widen the
// software range: that trades a visible software trip for an invisible
// regulator trip, which is the failure mode this constant exists to prevent.
constexpr float NEXUS_VRM_OT_FAULT_C = 85.0f;

// 3-phase stack (QAxe++ Nexus, BM1373): 1 master (U8) + 2 slaves (U9, U10), TPS546D24A.
// Hardware pin-strap notes (bring-up board, current schematic as-is unless noted):
//  - Master (U8) ADRSEL: Rtop=46.4k to BP1V5 / Rbot=10.0k to AGND (measured) -> address 0x14,
//    SYNC_IN, 0° phase shift. Confirmed by calculation, not just a boot-time scan guess.
//  - Master (U8) MSEL2 (Rbot=6.81k/Rtop=31.6k, as wired): already decodes to
//    STACK_CONFIG=2 slaves/3-phase + 40/52A per-phase OC tier - no change needed.
//    TON_RISE from this strap is irrelevant anyway; hw_init() overwrites it via PMBus.
//  - Slave1 (U9)  MSEL2: MUST change from 0Ω to 10.0k to AGND only (no top resistor)
//    -> device #1 / 3-phase. Currently 0Ω decodes as "device #1 / 2-phase", same as U10.
//  - Slave2 (U10) MSEL2: MUST change from 0Ω to 21.5k to AGND only (no top resistor)
//    -> device #2 / 3-phase. Both slaves at 0Ω collide as "device #1" and block POR.
//  - Slaves' GOSNS/FLWR must tie to their own BP1V5 (not AGND) to be recognized as slaves
//    (already the case per schematic - do not touch).
inline AxePowerHal* create_nexus_3ph_power_instance(axe_pwr_enable_pin_t en_pins, axe_pwr_adc_pin_t adc_pins, uint8_t vcore_regulator_pwm_pin, uint8_t pgood, uint8_t plug) {
    tps546d24a_cfg_t cfg = {
        0x14,   // i2c_addr (measured: ADRSEL Rtop=46.4k/Rbot=10.0k -> address 0x14)
        3,      // num_phases
        0.25f,  // vout_scale_loop: 1.0 was wrong (assumed no FB divider). Real data says
                //   otherwise - cmd 3000mV keeps measuring ~750mV actual (ratio ~0.25,
                //   not 1.0), and the earlier 0.5 test literally halved the rail
                //   (cmd 3000mV -> ~1500mV), proving output = VOUT_COMMAND * vout_scale_loop
                //   on this part. 0.25 matches a reference single-domain TPS546 driver's
                //   default scale-loop constant; the series-stacked config there uses 0.125.
                //   This also explains why STATUS_VOUT's
                //   MIN_MAX_CLAMP was persistently set: VOUT_MAX is converted through this
                //   same wrong scale internally, so the real DAC-side ceiling was clamped
                //   far below 3.1V, not just a stale latched bit.
        71.0f,  // ifault_total - v3.2.01 shipping value. Measured full-load on the
                //   bench is ~69A, so this is only ~1.03x, NOT the "1.25x margin" an
                //   older comment claimed. It is a last-resort protection threshold,
                //   not an operating margin; operating-point headroom is enforced by
                //   the benchmark margin rule instead.
                //   NOTE: the chip stores this as SLINEAR11 (5-bit mantissa) and ends
                //   up enforcing 70.5A - that is what get_oc_limit_amps() now reports
                //   back instead of echoing this constant.
        68.0f,  // iwarn_total - must sit BELOW ifault_total (71) to be a real leading
                //   indicator, and ABOVE the Turbo working range so it does not
                //   false-alarm. Was 65, which sat INSIDE that range (measured
                //   63-66A), so OC_WARN latched during every Turbo run and the log
                //   read as a constant false alarm. 68 is ~2A above the Turbo peak
                //   (silent in normal Turbo) and ~2.5A below the chip's actual
                //   OC_FAULT (70.5A after SLINEAR11), so it fires only when current
                //   is genuinely approaching the trip point. The live/latched split
                //   in power_loop keeps any residual latched warn honest.
        NEXUS_VRM_OT_FAULT_C,  // tfault - see NEXUS_VRM_OT_FAULT_C above; board.cpp
                //   derives pwr.temp_limit.high from the same constant.
        5.0f,   // ton_rise_ms (written via PMBus regardless of the MSEL2 strap)
        // The 2 BM1373 dies are Vcore-series-stacked, so the rail must cover 2x the
        // per-die 900-1500mV window (1800-3000mV); clamp is widened with margin.
        1700,   // vout_min_mv (hardware protective clamp, wide)
        3100,   // vout_max_mv (hardware protective clamp, wide)
        0.003f, // reg_ibus_sample: 3 mΩ input shunt (confirmed on this board)
        2,      // vcore_series_count: 2 BM1373 dies stacked in SERIES on Vcore
    };
    return new TPS546D24AClass(en_pins, adc_pins, pgood, plug, cfg);
}



#endif // __NMQAXEPP_BOARD_H_