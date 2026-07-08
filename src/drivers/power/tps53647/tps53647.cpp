// What: TPS53647 implementation migrated from the legacy QAxe++ power HAL.
// Why: QAxe++ needs PMBus VID control and regulator telemetry to safely OTA-test
// without falling back to USB flashing after every power-path issue.
// Role: Keeps old register programming and conversion formulas inside the
// concrete driver while presenting the new framework's generic Power API.
#include "drivers/power/tps53647/tps53647.h"

#include <Arduino.h>
#include <math.h>

#include "utils/logger/logger.h"

namespace nm::drivers {

namespace {

constexpr uint16_t kExpectedDeviceCode = 0x01F0;
constexpr uint8_t kAdcSamples = 5;
constexpr uint8_t kAdcSampleDelayMs = 10;
constexpr float kChipMinOutputV = 0.25f;
constexpr float kVidStepV = 0.005f;
constexpr float kIbusGain = 50.0f;
constexpr float kVbusGain = 6.1f;
constexpr float kVcoreGain = 2.0f;

constexpr uint8_t PMBUS_CLEAR_FAULTS = 0x03;
constexpr uint8_t PMBUS_RESTORE_DEFAULT_ALL = 0x12;
constexpr uint8_t PMBUS_VOUT_COMMAND = 0x21;
constexpr uint8_t PMBUS_IOUT_OC_FAULT_LIMIT = 0x46;
constexpr uint8_t PMBUS_IOUT_OC_WARN_LIMIT = 0x4A;
constexpr uint8_t PMBUS_OT_FAULT_LIMIT = 0x4F;
constexpr uint8_t PMBUS_OT_WARN_LIMIT = 0x51;
constexpr uint8_t PMBUS_STATUS_IOUT = 0x7B;
constexpr uint8_t PMBUS_STATUS_TEMPERATURE = 0x7D;
constexpr uint8_t PMBUS_READ_TEMPERATURE_1 = 0x8D;
constexpr uint8_t PMBUS_MFR_SPECIFIC_00 = 0xD0;
constexpr uint8_t PMBUS_MFR_SPECIFIC_10 = 0xDA;
constexpr uint8_t PMBUS_MFR_SPECIFIC_12 = 0xDC;
constexpr uint8_t PMBUS_MFR_SPECIFIC_13 = 0xDD;
constexpr uint8_t PMBUS_MFR_SPECIFIC_20 = 0xE4;
constexpr uint8_t PMBUS_MFR_SPECIFIC_44 = 0xFC;
constexpr uint8_t PMBUS_ON_OFF_CONFIG = 0x02;

}  // namespace

bool Tps53647Power::init() {
    if (_initialized) {
        return true;
    }

    if (_pins.pll_enable_pin >= 0) {
        pinMode(_pins.pll_enable_pin, OUTPUT);
    }
    if (_pins.vdd_enable_pin >= 0) {
        pinMode(_pins.vdd_enable_pin, OUTPUT);
    }
    if (_pins.vcore_enable_pin >= 0) {
        pinMode(_pins.vcore_enable_pin, OUTPUT);
    }
    if (_pins.vcore_pgood_pin >= 0) {
        pinMode(_pins.vcore_pgood_pin, INPUT_PULLUP);
    }
    if (_pins.dc_plug_pin >= 0) {
        pinMode(_pins.dc_plug_pin, INPUT_PULLUP);
    }

    set_rail_enabled(PowerRail::Pll0v8, false);
    set_rail_enabled(PowerRail::Vdd1v8, false);
    set_rail_enabled(PowerRail::Vcore, false);

    bool adc_ok = _adc.init(12);
    if (_pins.vbus_adc_pin >= 0) {
        adc_ok = _adc.configure_pin(_pins.vbus_adc_pin, hal::adc::AdcAttenuation::Db11) && adc_ok;
    }
    if (_pins.ibus_adc_pin >= 0) {
        adc_ok = _adc.configure_pin(_pins.ibus_adc_pin, hal::adc::AdcAttenuation::Db11) && adc_ok;
    }
    if (_pins.vcore_adc_pin >= 0) {
        adc_ok = _adc.configure_pin(_pins.vcore_adc_pin, hal::adc::AdcAttenuation::Db6) && adc_ok;
    }

    _adc_ready = adc_ok &&
                 _pins.vbus_adc_pin >= 0 &&
                 _pins.ibus_adc_pin >= 0 &&
                 _pins.vcore_adc_pin >= 0;

    if (!_bus.init()) {
        LOG_E("[TPS53647] I2C init failed");
        _initialized = true;
        return false;
    }

    const bool hw_ok = _hw_init();
    _initialized = true;

    if (hw_ok && _current_vcore_mv > 0) {
        set_vcore_mv(_current_vcore_mv);
    }

    return hw_ok;
}

bool Tps53647Power::set_rail_enabled(PowerRail rail, bool enabled) {
    int8_t pin = -1;

    switch (rail) {
        case PowerRail::Pll0v8:
            pin = _pins.pll_enable_pin;
            break;
        case PowerRail::Vdd1v8:
            pin = _pins.vdd_enable_pin;
            break;
        case PowerRail::Vcore:
            pin = _pins.vcore_enable_pin;
            break;
    }

    if (pin < 0) {
        return true;
    }

    // QAxe++ TPS53647 rails are active-high in the legacy BSP.
    digitalWrite(pin, enabled ? HIGH : LOW);
    return true;
}

bool Tps53647Power::set_vcore_limits(uint16_t min_mv, uint16_t max_mv) {
    _min_vcore_mv = min_mv;
    _max_vcore_mv = max_mv;
    LOG_I("[TPS53647] Vcore range %umV..%umV", static_cast<unsigned>(_min_vcore_mv), static_cast<unsigned>(_max_vcore_mv));
    return true;
}

bool Tps53647Power::set_vcore_mv(uint16_t value_mv) {
    if (value_mv == 0) {
        _current_vcore_mv = 0;
        return set_rail_enabled(PowerRail::Vcore, false);
    }

    uint16_t target_mv = value_mv;
    if (_min_vcore_mv > 0 && target_mv < _min_vcore_mv) {
        target_mv = _min_vcore_mv;
    }
    if (_max_vcore_mv > 0 && target_mv > _max_vcore_mv) {
        target_mv = _max_vcore_mv;
    }

    _current_vcore_mv = target_mv;
    if (!_initialized) {
        return true;
    }

    const uint8_t vid = _mv_to_vid(target_mv);
    return _write_word(PMBUS_VOUT_COMMAND, vid);
}

bool Tps53647Power::is_vcore_ready() const {
    if (_pins.vcore_pgood_pin < 0) {
        return true;
    }
    if (digitalRead(_pins.vcore_pgood_pin) != HIGH) {
        return false;
    }
    delay(1);
    return digitalRead(_pins.vcore_pgood_pin) == HIGH;
}

bool Tps53647Power::is_dc_plugged() const {
    if (_pins.dc_plug_pin < 0) {
        return true;
    }
    return digitalRead(_pins.dc_plug_pin) == HIGH;
}

uint32_t Tps53647Power::read_vbus_mv() {
    return static_cast<uint32_t>(_sample_adc_mv(_pins.vbus_adc_pin) * kVbusGain);
}

uint32_t Tps53647Power::read_ibus_ma() {
    const float sense_mv = static_cast<float>(_sample_adc_mv(_pins.ibus_adc_pin));
    const float shunt_mv = sense_mv / kIbusGain;
    return static_cast<uint32_t>(shunt_mv / _controller.ibus_shunt_ohm);
}

uint32_t Tps53647Power::read_vcore_mv() {
    return static_cast<uint32_t>(_sample_adc_mv(_pins.vcore_adc_pin) * kVcoreGain);
}

float Tps53647Power::read_temperature_c() const {
    uint16_t raw = 0;
    if (!_read_word(PMBUS_READ_TEMPERATURE_1, raw)) {
        return NAN;
    }
    return _slinear11_to_float(raw);
}

bool Tps53647Power::clear_faults() {
    return _write_cmd(PMBUS_CLEAR_FAULTS);
}

bool Tps53647Power::is_oc_fault() const {
    uint8_t status = 0;
    return _read_byte(PMBUS_STATUS_IOUT, status) && ((status & 0x80u) != 0);
}

bool Tps53647Power::is_oc_warn() const {
    uint8_t status = 0;
    return _read_byte(PMBUS_STATUS_IOUT, status) && ((status & 0x20u) != 0);
}

bool Tps53647Power::is_ot_fault() const {
    uint8_t status = 0;
    return _read_byte(PMBUS_STATUS_TEMPERATURE, status) && ((status & 0x80u) != 0);
}

bool Tps53647Power::is_ot_warn() const {
    uint8_t status = 0;
    return _read_byte(PMBUS_STATUS_TEMPERATURE, status) && ((status & 0x40u) != 0);
}

bool Tps53647Power::_hw_init() {
    uint16_t device_code = 0;
    if (!_read_word(PMBUS_MFR_SPECIFIC_44, device_code)) {
        LOG_E("[TPS53647] device-code read failed");
        return false;
    }

    LOG_D("[TPS53647] Device ID: 0x%04X", static_cast<unsigned>(device_code));
    if (device_code != kExpectedDeviceCode) {
        LOG_E("[TPS53647] unexpected device code 0x%04X", static_cast<unsigned>(device_code));
        return false;
    }

    LOG_I("[TPS53647] controller found");

    bool ok = true;
    ok = _write_cmd(PMBUS_CLEAR_FAULTS) && ok;
    ok = _write_cmd(PMBUS_RESTORE_DEFAULT_ALL) && ok;
    ok = _write_byte(PMBUS_ON_OFF_CONFIG, 0b00010111) && ok;
    ok = _write_byte(PMBUS_MFR_SPECIFIC_12, 0x20) && ok;
    ok = _write_byte(PMBUS_MFR_SPECIFIC_10, _controller.imax_a) && ok;
    ok = _write_byte(PMBUS_MFR_SPECIFIC_13, 0x89) && ok;
    ok = _write_byte(PMBUS_ON_OFF_CONFIG, 0b00010111) && ok;
    ok = _write_byte(PMBUS_MFR_SPECIFIC_00, 0b0011 & 0x0F) && ok;
    ok = _set_phases(_controller.phases) && ok;
    ok = _write_word(PMBUS_OT_WARN_LIMIT, _float_to_slinear11(_controller.tfault_c - 10.0f)) && ok;
    ok = _write_word(PMBUS_OT_FAULT_LIMIT, _float_to_slinear11(_controller.tfault_c)) && ok;
    ok = _write_word(PMBUS_IOUT_OC_WARN_LIMIT, _float_to_slinear11(_controller.ifault_a - 2.0f)) && ok;
    ok = _write_word(PMBUS_IOUT_OC_FAULT_LIMIT, _float_to_slinear11(_controller.ifault_a)) && ok;

    return ok;
}

bool Tps53647Power::_read_word(uint8_t reg, uint16_t& value) const {
    uint8_t data[2] = {};
    if (!_bus.read_register(_controller.i2c_address, reg, data, sizeof(data))) {
        LOG_E("[TPS53647] read word 0x%02X failed", reg);
        return false;
    }
    value = static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
    return true;
}

bool Tps53647Power::_read_byte(uint8_t reg, uint8_t& value) const {
    if (!_bus.read_register(_controller.i2c_address, reg, &value, 1)) {
        LOG_E("[TPS53647] read byte 0x%02X failed", reg);
        return false;
    }
    return true;
}

bool Tps53647Power::_write_byte(uint8_t reg, uint8_t value) const {
    if (!_bus.write_register_byte(_controller.i2c_address, reg, value)) {
        LOG_E("[TPS53647] write byte 0x%02X failed", reg);
        return false;
    }
    return true;
}

bool Tps53647Power::_write_word(uint8_t reg, uint16_t value) const {
    if (!_bus.write_register_word_le(_controller.i2c_address, reg, value)) {
        LOG_E("[TPS53647] write word 0x%02X failed", reg);
        return false;
    }
    return true;
}

bool Tps53647Power::_write_cmd(uint8_t cmd) const {
    if (!_bus.write_command(_controller.i2c_address, cmd)) {
        LOG_E("[TPS53647] write cmd 0x%02X failed", cmd);
        return false;
    }
    return true;
}

bool Tps53647Power::_set_phases(uint8_t phases) const {
    if (phases < 1 || phases > 6) {
        LOG_E("[TPS53647] phase count out of range: %u", static_cast<unsigned>(phases));
        return false;
    }
    LOG_I("[TPS53647] setting %u phases", static_cast<unsigned>(phases));
    return _write_byte(PMBUS_MFR_SPECIFIC_20, static_cast<uint8_t>(phases - 1));
}

uint8_t Tps53647Power::_mv_to_vid(uint16_t mv) const {
    if (mv == 0) {
        return 0;
    }

    const float volts = static_cast<float>(mv) / 1000.0f;
    int reg = static_cast<int>((volts - kChipMinOutputV) / kVidStepV) + 1;
    if (reg > 0xFF) {
        const uint16_t chip_max_mv = static_cast<uint16_t>((0xFF - 1) * 5 + kChipMinOutputV * 1000.0f);
        LOG_W("[TPS53647] target %umV exceeds chip max %umV, clamping", static_cast<unsigned>(mv), static_cast<unsigned>(chip_max_mv));
        reg = 0xFF;
    } else if (reg < 1) {
        const uint16_t chip_min_mv = static_cast<uint16_t>(kChipMinOutputV * 1000.0f);
        LOG_W("[TPS53647] target %umV below chip min %umV, clamping", static_cast<unsigned>(mv), static_cast<unsigned>(chip_min_mv));
        reg = 0x01;
    }
    LOG_D("[TPS53647] %umV -> VID 0x%02X", static_cast<unsigned>(mv), static_cast<unsigned>(reg));
    return static_cast<uint8_t>(reg);
}

uint16_t Tps53647Power::_float_to_slinear11(float value) const {
    if (value <= 0.0f) {
        LOG_W("[TPS53647] SLINEAR11 only supports positive values here");
        return 0;
    }

    int32_t exponent = -16;
    int32_t mantissa = 0;
    while (exponent <= 15) {
        const float scaled = value / powf(2.0f, static_cast<float>(exponent));
        mantissa = static_cast<int32_t>(roundf(scaled));
        if (mantissa >= 0 && mantissa <= 1023) {
            break;
        }
        ++exponent;
    }

    if (exponent > 15) {
        LOG_W("[TPS53647] SLINEAR11 conversion failed");
        return 0;
    }

    const uint16_t mantissa_bits = static_cast<uint16_t>(mantissa) & 0x07FFu;
    const uint16_t exponent_bits = static_cast<uint16_t>(exponent) & 0x001Fu;
    return static_cast<uint16_t>((exponent_bits << 11) | mantissa_bits);
}

float Tps53647Power::_slinear11_to_float(uint16_t value) const {
    int32_t exponent = value >> 11;
    int32_t mantissa = value & 0x07FFu;

    exponent |= (exponent & 0x10) ? 0xFFFFFFE0 : 0;
    mantissa |= (mantissa & 0x0400) ? 0xFFFFF800 : 0;

    return static_cast<float>(mantissa) * powf(2.0f, static_cast<float>(exponent));
}

uint32_t Tps53647Power::_sample_adc_mv(int8_t pin) const {
    return _adc.sample_mv(pin, kAdcSamples, kAdcSampleDelayMs);
}

}  // namespace nm::drivers
