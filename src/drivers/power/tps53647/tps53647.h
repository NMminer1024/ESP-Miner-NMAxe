#ifndef __TPS53647_H__
#define __TPS53647_H__
#include <Arduino.h>
#include "drivers/power/power_hal.h"


/* Standard Core PMBus commands */
#define PMBUS_OPERATION 0x01
#define PMBUS_ON_OFF_CONFIG 0x02
#define PMBUS_CLEAR_FAULTS 0x03
#define PMBUS_WRITE_PROTECT 0x10
#define PMBUS_STORE_DEFAULT_ALL 0x11
#define PMBUS_RESTORE_DEFAULT_ALL 0x12
#define PMBUS_CAPABILITY 0x19
#define PMBUS_VOUT_MODE 0x20
#define PMBUS_VOUT_COMMAND 0x21
#define PMBUS_VOUT_MAX 0x24
#define PMBUS_VOUT_MARGIN_HIGH 0x25
#define PMBUS_VOUT_MARGIN_LOW 0x26
#define PMBUS_IOUT_CAL_OFFSET 0x39
#define PMBUS_VOUT_OV_FAULT_RESPONSE 0x41
#define PMBUS_VOUT_UV_FAULT_RESPONSE 0x45
#define PMBUS_IOUT_OC_FAULT_LIMIT 0x46
#define PMBUS_IOUT_OC_FAULT_RESPONSE 0x47
#define PMBUS_IOUT_OC_WARN_LIMIT 0x4A
#define PMBUS_OT_FAULT_LIMIT 0x4F
#define PMBUS_OT_FAULT_RESPONSE 0x50
#define PMBUS_OT_WARN_LIMIT 0x51
#define PMBUS_VIN_OV_FAULT_LIMIT 0x55
#define PMBUS_IIN_OC_FAULT_LIMIT 0x5b
#define PMBUS_IIN_OC_FAULT_RESPONSE 0x5c
#define PMBUS_IIN_OC_WARN_LIMIT 0x5d
#define PMBUS_STATUS_BYTE 0x78
#define PMBUS_STATUS_WORD 0x79
#define PMBUS_STATUS_VOUT 0x7A
#define PMBUS_STATUS_IOUT 0x7B
#define PMBUS_STATUS_INPUT 0x7C
#define PMBUS_STATUS_TEMPERATURE 0x7D
#define PMBUS_STATUS_CML 0x7E
#define PMBUS_STATUS_MFR_SPECIFIC 0x80
#define PMBUS_READ_VIN 0x88
#define PMBUS_READ_IIN 0x89
#define PMBUS_READ_VOUT 0x8B
#define PMBUS_READ_IOUT 0x8C
#define PMBUS_READ_TEMPERATURE_1 0x8D
#define PMBUS_READ_POUT 0x96
#define PMBUS_READ_PIN 0x97
#define PMBUS_REVISION 0x98
#define PMBUS_MFR_ID 0x99
#define PMBUS_MFR_MODEL 0x9A
#define PMBUS_MFR_REVISION 0x9B
#define PMBUS_MFR_DATE 0x9D
#define PMBUS_MFR_VOUT_MIN 0xA4

/* Manufacturer Specific PMBUS commands used by the TPS53647 */
#define PMBUS_MFR_SPECIFIC_00 0xD0
#define PMBUS_MFR_SPECIFIC_01 0xD1
#define PMBUS_MFR_SPECIFIC_04 0xD4
#define PMBUS_MFR_SPECIFIC_05 0xD5
#define PMBUS_MFR_SPECIFIC_07 0xD7
#define PMBUS_MFR_SPECIFIC_08 0xD8
#define PMBUS_MFR_SPECIFIC_09 0xD9
#define PMBUS_MFR_SPECIFIC_10 0xDA
#define PMBUS_MFR_SPECIFIC_11 0xDB
#define PMBUS_MFR_SPECIFIC_12 0xDC
#define PMBUS_MFR_SPECIFIC_13 0xDD
#define PMBUS_MFR_SPECIFIC_14 0xDE
#define PMBUS_MFR_SPECIFIC_15 0xDF
#define PMBUS_MFR_SPECIFIC_16 0xE0
#define PMBUS_MFR_SPECIFIC_20 0xE4
#define PMBUS_MFR_SPECIFIC_44 0xFC

// Phil31 hack
/* Manufacturer Specific PMBUS commands used by the TPS53667 */  
#define PMBUS_MFR_SPECIFIC_19 0xE3
#define PMBUS_MFR_SPECIFIC_20 0xE4
#define PMBUS_MFR_SPECIFIC_21 0xE5
#define PMBUS_MFR_SPECIFIC_22 0xE6
#define PMBUS_MFR_SPECIFIC_23 0xE7
#define PMBUS_MFR_SPECIFIC_24 0xE8



struct tps53647_cfg_t {
    uint8_t num_phases;       // 2 or 3
    uint8_t imax;             // max current in A (PMBUS_MFR_SPECIFIC_10)
    float   ifault;           // OC fault/warn limit in A
    float   reg_ibus_sample;  // shunt resistance: 0.005 Ω (2-ph) / 0.0025 Ω (3-ph)
};

class TPS53647Class: public AxePowerHal{
private:
    uint8_t       _vcore_pgood_pin;
    float         _chip_min_output_vlot_mv;  // TPS53647 hardware min output voltage in V
    uint16_t      _vcore_min_mv;             // Vcore range min in mV, ASIC-related
    uint16_t      _vcore_max_mv;             // Vcore range max in mV, ASIC-related
    tps53647_cfg_t _cfg;                     // board-specific phase / current config
    uint8_t  _read_reg(uint8_t regaddr, uint8_t *data, uint8_t length);
    void     _write_byte(uint8_t regaddr, uint8_t data);
    void     _write_word(uint8_t regaddr, uint16_t data);
    void     _write_cmd(uint8_t cmd);
    uint8_t  _mv_to_vid(uint16_t mv);
    uint16_t _vid_to_mv(uint8_t reg);
    float    _slinear11_to_float(uint16_t value);
    uint16_t _float_to_slinear11(float x);
    void     _set_phases(int num_phases);
public:
    TPS53647Class(axe_pwr_enable_pin_t en_pins, axe_pwr_adc_pin_t adc_pins, uint8_t pgood, uint8_t plug, tps53647_cfg_t cfg)
        : AxePowerHal(en_pins, adc_pins), _cfg(cfg) {
        this->_vcore_pgood_pin         = pgood;
        this->_chip_min_output_vlot_mv = 0.25f; // TPS53647 minimum output voltage 0.25 V
    }
    ~TPS53647Class();
    /** Implementations of pure virtual functions from AxePowerHal */
    void hw_init(void) override;
    void set_vdd_1v8(power_state_t state) override;
    void set_pll_0v8(power_state_t state) override;
    void set_vcore_status(power_state_t state) override;
    void set_vcore_voltage(uint16_t req_mv) override;
    void set_vcore_range(uint16_t min_mv, uint16_t max_mv) override;
    bool is_vcore_ready(void) override;
    bool is_dc_pluged(void) override;
    uint16_t get_vcore_min(void) override { return this->_vcore_min_mv;};
    uint16_t get_vcore_max(void) override { return this->_vcore_max_mv;};
    uint32_t get_vbus(void) override;
    uint32_t get_ibus(void) override;
    uint32_t get_vcore(void) override;
    void debugPrint(void) override;
    bool is_oc_fault(void) override;
    bool is_oc_warn(void)  override;
    void clear_faults(void) override { this->_write_cmd(PMBUS_CLEAR_FAULTS); }
};

#endif 