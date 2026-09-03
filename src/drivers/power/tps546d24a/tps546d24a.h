#ifndef __TPS546D24A_H__
#define __TPS546D24A_H__
#include <Arduino.h>
#include "drivers/power/power_hal.h"

/* Standard PMBus commands used by the TPS546D24A (ZHCSMI5A) */
#define PMBUS_OPERATION                 0x01
#define PMBUS_ON_OFF_CONFIG             0x02
#define PMBUS_CLEAR_FAULTS              0x03
#define PMBUS_PHASE                     0x04
#define PMBUS_WRITE_PROTECT             0x10
#define PMBUS_STORE_USER_ALL            0x15
#define PMBUS_RESTORE_USER_ALL          0x16
#define PMBUS_CAPABILITY                0x19
#define PMBUS_VOUT_MODE                 0x20
#define PMBUS_VOUT_COMMAND              0x21
#define PMBUS_VOUT_TRIM                 0x22
#define PMBUS_VOUT_MAX                  0x24
#define PMBUS_VOUT_MARGIN_HIGH          0x25
#define PMBUS_VOUT_MARGIN_LOW           0x26
#define PMBUS_VOUT_TRANSITION_RATE      0x27
#define PMBUS_VOUT_SCALE_LOOP           0x29
#define PMBUS_VOUT_MIN                  0x2B
#define PMBUS_FREQUENCY_SWITCH          0x33
#define PMBUS_VIN_ON                    0x35
#define PMBUS_VIN_OFF                   0x36
#define PMBUS_INTERLEAVE                0x37
#define PMBUS_IOUT_CAL_GAIN             0x38
#define PMBUS_IOUT_CAL_OFFSET           0x39
#define PMBUS_VOUT_OV_FAULT_LIMIT       0x40
#define PMBUS_VOUT_OV_FAULT_RESPONSE    0x41
#define PMBUS_VOUT_OV_WARN_LIMIT        0x42
#define PMBUS_VOUT_UV_WARN_LIMIT        0x43
#define PMBUS_VOUT_UV_FAULT_LIMIT       0x44
#define PMBUS_VOUT_UV_FAULT_RESPONSE    0x45
#define PMBUS_IOUT_OC_FAULT_LIMIT       0x46
#define PMBUS_IOUT_OC_FAULT_RESPONSE    0x47
#define PMBUS_IOUT_OC_WARN_LIMIT        0x4A
#define PMBUS_OT_FAULT_LIMIT            0x4F
#define PMBUS_OT_FAULT_RESPONSE         0x50
#define PMBUS_OT_WARN_LIMIT             0x51
#define PMBUS_VIN_OV_FAULT_LIMIT        0x55
#define PMBUS_VIN_OV_FAULT_RESPONSE     0x56
#define PMBUS_VIN_UV_WARN_LIMIT         0x58
#define PMBUS_TON_DELAY                 0x60
#define PMBUS_TON_RISE                  0x61
#define PMBUS_TON_MAX_FAULT_LIMIT       0x62
#define PMBUS_TON_MAX_FAULT_RESPONSE    0x63
#define PMBUS_TOFF_DELAY                0x64
#define PMBUS_TOFF_FALL                 0x65
#define PMBUS_STATUS_BYTE               0x78
#define PMBUS_STATUS_WORD               0x79
#define PMBUS_STATUS_VOUT               0x7A
#define PMBUS_STATUS_IOUT               0x7B
#define PMBUS_STATUS_INPUT              0x7C
#define PMBUS_STATUS_TEMPERATURE        0x7D
#define PMBUS_STATUS_CML                0x7E
#define PMBUS_STATUS_OTHER              0x7F
#define PMBUS_STATUS_MFR_SPECIFIC       0x80
#define PMBUS_READ_VIN                  0x88
#define PMBUS_READ_VOUT                 0x8B
#define PMBUS_READ_IOUT                 0x8C
#define PMBUS_READ_TEMPERATURE_1        0x8D
#define PMBUS_PMBUS_REVISION            0x98
#define PMBUS_MFR_ID                    0x99
#define PMBUS_MFR_MODEL                 0x9A
#define PMBUS_MFR_REVISION              0x9B
#define PMBUS_IC_DEVICE_ID              0xAD  // 6-byte block read
#define PMBUS_IC_DEVICE_REV             0xAE
#define PMBUS_USER_DATA_01              0xB1  // COMPENSATION_CONFIG, 5-byte block (left at HW/NVM default, see .cpp)
#define PMBUS_USER_DATA_05              0xB5  // POWER_STAGE_CONFIG (VDD5 LDO trim)
#define PMBUS_SYNC_CONFIG               0xE4  // SYNC_IN/OUT role; must match single vs. multi-phase stack (see .cpp)
#define PMBUS_MFR_SPECIFIC_28           0xEC  // STACK_CONFIG
#define PMBUS_MFR_SPECIFIC_29           0xED  // MISC_OPTIONS
#define PMBUS_MFR_SPECIFIC_30           0xEE  // PIN_DETECT_OVERRIDE
#define PMBUS_MFR_SPECIFIC_31           0xEF  // SLAVE_ADDRESS
#define PMBUS_MFR_SPECIFIC_32           0xF0  // NVM_CHECKSUM (read-only)

// PHASE (0x04) broadcast value — address the whole stack at once.
#define TPS546D24A_PHASE_ALL            0xFF

// tps546d24a supports 1 (independent) up to 4 (1 master + 3 slaves) stacked devices
// sharing one output rail; only the master answers PMBus, slaves are addressed via
// the internal BCX bus. num_phases here is informational / used to size total
// current thresholds, NOT written as a register (that's done by MSEL2 pin-straps
// on each physical chip, see board notes in nmqaxepp.h).
struct tps546d24a_cfg_t {
    uint8_t  i2c_addr;        // master's 7-bit PMBus address (set by ADRSEL strap)
    uint8_t  num_phases;      // physical phases in the stack (1, 2, 3 or 4)
    float    vout_scale_loop; // internal feedback divider: 1.0 / 0.5 / 0.25 / 0.125 (must match VSEL strap intent)
    float    ifault_total;    // total OC fault current for the whole stack, in A
    float    iwarn_total;     // total OC warn current for the whole stack, in A
    float    tfault;          // OT fault threshold in °C; warn = tfault - 10 °C
    float    ton_rise_ms;     // soft-start ramp time in ms (TON_RISE)
    uint16_t vout_min_mv;     // hardware protective VOUT_MIN clamp, mV (wide; tight range is set_vcore_range())
    uint16_t vout_max_mv;     // hardware protective VOUT_MAX clamp, mV (wide; tight range is set_vcore_range())
    float    reg_ibus_sample; // ibus shunt scale for the external ADC path (chip has no READ_IIN register)
};

class TPS546D24AClass: public AxePowerHal{
private:
    uint8_t       _i2c_addr;
    bool          _device_ok;                // false until hw_init() confirms the device responds; guards runtime reads
    int8_t        _vcore_pgood_pin;          // -1 => no PGOOD GPIO wired, fall back to PMBus STATUS_WORD
    uint16_t      _vcore_min_mv;             // Vcore range min in mV, ASIC-related
    uint16_t      _vcore_max_mv;             // Vcore range max in mV, ASIC-related
    tps546d24a_cfg_t _cfg;
    uint8_t  _read_reg(uint8_t regaddr, uint8_t *data, uint8_t length);
    uint8_t  _read_block(uint8_t regaddr, uint8_t *data, uint8_t data_length); // SMBus block read: consumes the leading byte-count byte, returns it (0xFF on I2C error)
    void     _write_byte(uint8_t regaddr, uint8_t data);
    void     _write_word(uint8_t regaddr, uint16_t data);
    void     _write_cmd(uint8_t cmd);
    void     _scan_bus(void);   // diagnostic: log every I2C address that ACKs, used when the configured address doesn't respond
    void     _check_smbus_alert(void); // diagnostic: single-byte STATUS_BYTE read + SMBus ARA (0x0C) probe
    float    _slinear11_to_float(uint16_t value);
    uint16_t _float_to_slinear11(float x);
    uint16_t _mv_to_ulinear16(uint16_t mv);
    uint16_t _ulinear16_to_mv(uint16_t raw);
public:
    TPS546D24AClass(axe_pwr_enable_pin_t en_pins, axe_pwr_adc_pin_t adc_pins, uint8_t pgood, uint8_t plug, tps546d24a_cfg_t cfg)
        : AxePowerHal(en_pins, adc_pins), _cfg(cfg) {
        this->_i2c_addr        = cfg.i2c_addr;
        this->_device_ok       = false;
        this->_vcore_pgood_pin = (int8_t)pgood;
    }
    ~TPS546D24AClass();
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
    int8_t   detect_phase(void) override;     // reads STACK_CONFIG, returns 1 + slave count
    uint32_t get_vbus(void) override;
    uint32_t get_ibus(void) override;
    uint32_t get_vcore(void) override;
    float    get_temperature(void);   // reads PMBUS_READ_TEMPERATURE_1 via SLINEAR11 (hottest phase in stack)
    void debugPrint(void) override;
    void dump(void) override;
    bool is_oc_fault(void) override;
    bool is_oc_warn(void)  override;
    bool is_ot_fault(void) override;
    bool is_ot_warn(void)  override;
    void clear_faults(void) override;
};

// Register TPS546D24A instance as the Vcore temperature source in temp_hal
void tps546d24a_register_vcore_temp_hal(TPS546D24AClass *instance);

#endif
