#ifndef __TPS53647_H__
#define __TPS53647_H__
#include <Arduino.h>
#include "power_hal.h"


/* Standard Core PMBus commands */
#define PMBUS_OPERATION 0x01
#define PMBUS_ON_OFF_CONFIG 0x02
#define PMBUS_CLEAR_FAULTS 0x03
#define PMBUS_PHASE 0x04
#define PMBUS_WRITE_PROTECT 0x10
#define PMBUS_STORE_USER_ALL 0x15
#define PMBUS_RESTORE_USER_ALL 0x16
#define PMBUS_CAPABILITY 0x19
#define PMBUS_SMBALERT_MASK 0x1B
#define PMBUS_VOUT_MODE 0x20
#define PMBUS_VOUT_COMMAND 0x21
#define PMBUS_VOUT_TRIM 0x22
#define PMBUS_VOUT_MAX 0x24
#define PMBUS_VOUT_MARGIN_HIGH 0x25
#define PMBUS_VOUT_MARGIN_LOW 0x26
#define PMBUS_VOUT_TRANSITION_RATE 0x27
#define PMBUS_VOUT_SCALE_LOOP 0x29
#define PMBUS_VOUT_MIN 0x2B
#define PMBUS_FREQUENCY_SWITCH 0x33
#define PMBUS_VIN_ON 0x35
#define PMBUS_VIN_OFF 0x36
#define PMBUS_INTERLEAVE 0x37
#define PMBUS_IOUT_CAL_GAIN 0x38
#define PMBUS_IOUT_CAL_OFFSET 0x39
#define PMBUS_VOUT_OV_FAULT_LIMIT 0x40
#define PMBUS_VOUT_OV_FAULT_RESPONSE 0x41
#define PMBUS_VOUT_OV_WARN_LIMIT 0x42
#define PMBUS_VOUT_UV_WARN_LIMIT 0x43
#define PMBUS_VOUT_UV_FAULT_LIMIT 0x44
#define PMBUS_VOUT_UV_FAULT_RESPONSE 0x45
#define PMBUS_IOUT_OC_FAULT_LIMIT 0x46
#define PMBUS_IOUT_OC_FAULT_RESPONSE 0x47
#define PMBUS_IOUT_OC_WARN_LIMIT 0x4A
#define PMBUS_OT_FAULT_LIMIT 0x4F
#define PMBUS_OT_FAULT_RESPONSE 0x50
#define PMBUS_OT_WARN_LIMIT 0x51
#define PMBUS_VIN_OV_FAULT_LIMIT 0x55
#define PMBUS_VIN_OV_FAULT_RESPONSE 0x56
#define PMBUS_VIN_UV_WARN_LIMIT 0x58
#define PMBUS_TON_DELAY 0x60
#define PMBUS_TON_RISE 0x61
#define PMBUS_TON_MAX_FAULT_LIMIT 0x62
#define PMBUS_TON_MAX_FAULT_RESPONSE 0x63
#define PMBUS_TOFF_DELAY 0x64
#define PMBUS_TOFF_FALL 0x65
#define PMBUS_STATUS_BYTE 0x78
#define PMBUS_STATUS_WORD 0x79
#define PMBUS_STATUS_VOUT 0x7A
#define PMBUS_STATUS_IOUT 0x7B
#define PMBUS_STATUS_INPUT 0x7C
#define PMBUS_STATUS_TEMPERATURE 0x7D
#define PMBUS_STATUS_CML 0x7E
#define PMBUS_STATUS_OTHER 0x7F
#define PMBUS_STATUS_MFR_SPECIFIC 0x80
#define PMBUS_READ_VIN 0x88
#define PMBUS_READ_VOUT 0x8B
#define PMBUS_READ_IOUT 0x8C
#define PMBUS_READ_TEMPERATURE_1 0x8D
#define PMBUS_REVISION 0x98
#define PMBUS_MFR_ID 0x99
#define PMBUS_MFR_MODEL 0x9A
#define PMBUS_MFR_REVISION 0x9B
#define PMBUS_MFR_SERIAL 0x9E
#define PMBUS_IC_DEVICE_ID 0xAD
#define PMBUS_IC_DEVICE_REV 0xAE
#define PMBUS_COMPENSATION_CONFIG 0xB1
#define PMBUS_POWER_STAGE_CONFIG 0xB5

/* Manufacturer Specific PMBUS commands used by the TPS546D24A */
#define PMBUS_TELEMETRY_CFG 0xD0
#define PMBUS_READ_ALL 0xDA
#define PMBUS_STATUS_ALL 0xDB
#define PMBUS_SYNC_CONFIG 0xE4
#define PMBUS_STACK_CONFIG 0xEC
#define PMBUS_MISC_OPTIONS 0xED
#define PMBUS_PIN_DETECT_OVERRIDE 0xEE
#define PMBUS_SLAVE_ADDRESS 0xEF
#define PMBUS_NVM_CHECKSUM 0xF0
#define PMBUS_SIMULATE_FAULTS 0xF1
#define PMBUS_FUSION_ID0 0xFC
#define PMBUS_FUSION_ID1 0xFD



class TPS53647Class: public AxePowerHal{
private:
    uint8_t _vcore_pgood_pin;
    uint8_t _dc_plug_pin;
    bool _adc_ready;
    uint16_t _vcore_min_mv;
    uint16_t _vcore_max_mv;
    uint8_t  _read_reg(uint8_t registerAddress, uint8_t *data, uint8_t length);
    void     _write_reg(uint8_t registerAddress, uint8_t data);
public:
    TPS53647Class(axe_pwr_enable_pin_t en_pins, axe_pwr_adc_pin_t adc_pins, uint8_t pgood,  uint8_t plug):AxePowerHal(en_pins, adc_pins){
        this->_vcore_pgood_pin = pgood;
        this->_dc_plug_pin = plug;
        this->_adc_ready = false;
    }
    ~TPS53647Class();
    /** Implementations of pure virtual functions from AxePowerHal */
    bool init(void);
    void set_vdd_1v8(power_state_t state);
    void set_pll_0v8(power_state_t state);
    void set_vcore_status(power_state_t state);
    void set_vcore_voltage(uint16_t req_mv);
    void set_vcore_range(uint16_t min_mv, uint16_t max_mv);
    bool is_vcore_ready(void);
    bool is_dc_pluged(void);
    bool is_adc_ready(void);
    uint16_t get_vcore_min(void){ return this->_vcore_min_mv;};
    uint16_t get_vcore_max(void){ return this->_vcore_max_mv;};
    uint32_t get_vbus(void);
    uint32_t get_ibus(void);
    uint32_t get_vcore(void);
};

#endif 