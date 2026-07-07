// What: TPS53355 implementation for the new power abstraction.
// Why: The service layer needs real Gamma power telemetry and Vcore control now,
// but it should only see the generic `Power` interface.
// Role: Maps the old Gamma PWM/ADC formulas into the reusable driver contract.
// Benefit: Main flow can monitor and control power without touching pins,
// attenuation settings, or board-specific enable polarity.
#include "drivers/power/tps53355/tps53355.h"

#include <Arduino.h>

namespace nm::drivers {

namespace {

constexpr uint8_t kPwmChannel = 1;
constexpr uint8_t kPwmResolutionBits = 8;
constexpr uint32_t kPwmFrequencyHz = 1000 * 100;
constexpr uint8_t kAdcSamples = 5;
constexpr uint8_t kAdcSampleDelayMs = 10;
constexpr float kIbusSampleResistorOhm = 0.01f;
constexpr float kIbusGain = 50.0f;
constexpr float kVbusGain = 6.1f;
constexpr float kVcoreGain = 2.0f;

}  // namespace

bool Tps53355Power::init() {
    if (_initialized) {
        return true;
    }

    if (_config.pll_enable_pin >= 0) {
        pinMode(_config.pll_enable_pin, OUTPUT);
    }
    if (_config.vdd_enable_pin >= 0) {
        pinMode(_config.vdd_enable_pin, OUTPUT);
    }
    if (_config.vcore_enable_pin >= 0) {
        pinMode(_config.vcore_enable_pin, OUTPUT);
    }
    if (_config.vcore_pwm_pin >= 0) {
        pinMode(_config.vcore_pwm_pin, OUTPUT);
        ledcSetup(kPwmChannel, kPwmFrequencyHz, kPwmResolutionBits);
        ledcAttachPin(_config.vcore_pwm_pin, kPwmChannel);
        ledcWrite(kPwmChannel, 0);
    }
    if (_config.vcore_pgood_pin >= 0) {
        pinMode(_config.vcore_pgood_pin, INPUT_PULLUP);
    }
    if (_config.dc_plug_pin >= 0) {
        pinMode(_config.dc_plug_pin, INPUT_PULLUP);
    }

    bool adc_ok = _adc.init(12);
    if (_config.vbus_adc_pin >= 0) {
        adc_ok = _adc.configure_pin(_config.vbus_adc_pin, hal::adc::AdcAttenuation::Db11) && adc_ok;
    }
    if (_config.ibus_adc_pin >= 0) {
        adc_ok = _adc.configure_pin(_config.ibus_adc_pin, hal::adc::AdcAttenuation::Db11) && adc_ok;
    }
    if (_config.vcore_adc_pin >= 0) {
        adc_ok = _adc.configure_pin(_config.vcore_adc_pin, hal::adc::AdcAttenuation::Db6) && adc_ok;
    }

    set_rail_enabled(PowerRail::Pll0v8, false);
    set_rail_enabled(PowerRail::Vdd1v8, false);
    set_rail_enabled(PowerRail::Vcore, false);

    _adc_ready = adc_ok &&
                 _config.vbus_adc_pin >= 0 &&
                 _config.ibus_adc_pin >= 0 &&
                 _config.vcore_adc_pin >= 0;
    _initialized = true;
    return true;
}

bool Tps53355Power::set_rail_enabled(PowerRail rail, bool enabled) {
    int8_t pin = -1;
    bool active_high = true;

    switch (rail) {
        case PowerRail::Pll0v8:
            pin = _config.pll_enable_pin;
            active_high = true;
            break;
        case PowerRail::Vdd1v8:
            pin = _config.vdd_enable_pin;
            active_high = true;
            break;
        case PowerRail::Vcore:
            pin = _config.vcore_enable_pin;
            active_high = false;
            break;
    }

    if (pin < 0) {
        return true;
    }

    digitalWrite(pin, (enabled == active_high) ? HIGH : LOW);
    return true;
}

bool Tps53355Power::set_vcore_limits(uint16_t min_mv, uint16_t max_mv) {
    _min_vcore_mv = min_mv;
    _max_vcore_mv = max_mv;
    return true;
}

bool Tps53355Power::set_vcore_mv(uint16_t value_mv) {
    if (_config.vcore_pwm_pin < 0 || value_mv < _min_vcore_mv || value_mv > _max_vcore_mv) {
        return false;
    }

    uint8_t pwm = static_cast<uint8_t>((0.14f * value_mv) - 140.0f);
    ledcWrite(kPwmChannel, pwm);
    _current_vcore_mv = value_mv;
    return true;
}

bool Tps53355Power::is_vcore_ready() const {
    if (_config.vcore_pgood_pin < 0) {
        return true;
    }
    return digitalRead(_config.vcore_pgood_pin) == HIGH;
}

bool Tps53355Power::is_dc_plugged() const {
    if (_config.dc_plug_pin < 0) {
        return false;
    }
    return digitalRead(_config.dc_plug_pin) == HIGH;
}

uint32_t Tps53355Power::read_vbus_mv() {
    return static_cast<uint32_t>(_sample_adc_mv(_config.vbus_adc_pin) * kVbusGain);
}

uint32_t Tps53355Power::read_ibus_ma() {
    const float sense_mv = static_cast<float>(_sample_adc_mv(_config.ibus_adc_pin));
    const float shunt_mv = sense_mv / kIbusGain;
    return static_cast<uint32_t>(shunt_mv / kIbusSampleResistorOhm);
}

uint32_t Tps53355Power::read_vcore_mv() {
    return static_cast<uint32_t>(_sample_adc_mv(_config.vcore_adc_pin) * kVcoreGain);
}

uint32_t Tps53355Power::_sample_adc_mv(int8_t pin) const {
    return _adc.sample_mv(pin, kAdcSamples, kAdcSampleDelayMs);
}

}  // namespace nm::drivers
