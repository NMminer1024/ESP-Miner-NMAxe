// What: PWM+tach fan implementation shared by Gamma-style boards.
// Why: Services should ask for fan speed and RPM through the `Fan` abstraction,
// not through direct LEDC or PCNT calls.
// Role: Sets PWM duty, measures tach pulses, and runs the startup self-test.
// Benefit: Cooling behavior stays encapsulated and reusable across BSPs.
#include "drivers/fan/pwm_tach/pwm_tach_fan.h"

#include <Arduino.h>

namespace nm::drivers {

namespace {

constexpr uint8_t kPulsesPerRevolution = 2;

}  // namespace

bool PwmTachFan::init() {
    if (_initialized) {
        return true;
    }

    if (_config.pwm_pin < 0 || _config.tach_pin < 0) {
        return false;
    }

    pinMode(_config.pwm_pin, OUTPUT);
    ledcSetup(_config.pwm_channel, _config.pwm_frequency_hz, _config.pwm_resolution_bits);
    ledcAttachPin(_config.pwm_pin, _config.pwm_channel);

    pcnt_config_t pcnt = {};
    pcnt.pulse_gpio_num = _config.tach_pin;
    pcnt.ctrl_gpio_num = PCNT_PIN_NOT_USED;
    pcnt.lctrl_mode = PCNT_MODE_KEEP;
    pcnt.hctrl_mode = PCNT_MODE_KEEP;
    pcnt.pos_mode = PCNT_COUNT_INC;
    pcnt.neg_mode = PCNT_COUNT_DIS;
    pcnt.counter_h_lim = 30000;
    pcnt.counter_l_lim = 0;
    pcnt.unit = _config.pcnt_unit;
    pcnt.channel = _config.pcnt_channel;

    if (pcnt_unit_config(&pcnt) != ESP_OK) {
        return false;
    }

    pcnt_set_filter_value(_config.pcnt_unit, 100);
    pcnt_filter_enable(_config.pcnt_unit);
    pcnt_counter_pause(_config.pcnt_unit);
    pcnt_counter_clear(_config.pcnt_unit);
    pcnt_counter_resume(_config.pcnt_unit);

    set_speed_percent(100);
    _initialized = true;
    return true;
}

bool PwmTachFan::set_speed_percent(uint8_t percent) {
    if (!_initialized) {
        return false;
    }

    if (percent > 100) {
        percent = 100;
    }

    _speed_percent = percent;
    _apply_speed_percent(percent);
    return true;
}

uint16_t PwmTachFan::read_rpm() {
    return _measure_current_rpm(120);
}

FanPolarityDetectResult PwmTachFan::detect_polarity() {
    if (!_initialized) {
        return {};
    }

    _inverted = false;
    const uint16_t rpm_50 = _measure_rpm_for_duration(50, 1200);
    const uint16_t rpm_100 = _measure_rpm_for_duration(100, 1200);
    _inverted = (static_cast<uint32_t>(rpm_100) * 9u / 10u) <= rpm_50;
    set_speed_percent(100);
    FanPolarityDetectResult result;
    result.inverted = _inverted;
    result.rpm_50 = rpm_50;
    result.rpm_100 = rpm_100;
    return result;
}

FanSelfTestResult PwmTachFan::run_self_test() {
    return run_self_test(nullptr, nullptr);
}

FanSelfTestResult PwmTachFan::run_self_test(FanSelfTestProgressCallback callback, void* ctx) {
    if (!_initialized) {
        return {};
    }

    uint16_t final_rpm = 0;
    for (uint8_t i = 0; i < 3; ++i) {
        final_rpm = _measure_rpm_for_duration(100, 1200);
        if (callback != nullptr) {
            callback(final_rpm, ctx);
        }
    }
    set_speed_percent(100);
    return {final_rpm >= _config.self_test_rpm_threshold, final_rpm};
}

uint16_t PwmTachFan::_measure_rpm_for_duration(uint8_t percent, uint32_t duration_ms) {
    _apply_speed_percent(percent);
    delay(500);
    return _measure_current_rpm(duration_ms);
}

uint16_t PwmTachFan::_measure_current_rpm(uint32_t duration_ms) {
    int16_t pulse_count = 0;
    pcnt_counter_clear(_config.pcnt_unit);
    delay(duration_ms);
    pcnt_get_counter_value(_config.pcnt_unit, &pulse_count);
    return _calculate_rpm(pulse_count, duration_ms);
}

void PwmTachFan::_apply_speed_percent(uint8_t percent) {
    const uint8_t effective_percent = _inverted ? static_cast<uint8_t>(100 - percent) : percent;
    const uint32_t max_duty = (1u << _config.pwm_resolution_bits) - 1u;
    const uint32_t duty = (static_cast<uint32_t>(effective_percent) * max_duty) / 100u;
    ledcWrite(_config.pwm_channel, duty);
}

uint16_t PwmTachFan::_calculate_rpm(int16_t pulse_count, uint32_t duration_ms) {
    if (duration_ms == 0) {
        return 0;
    }

    return static_cast<uint16_t>(
        (static_cast<uint32_t>(pulse_count) * 60000u) /
        (duration_ms * kPulsesPerRevolution));
}

}  // namespace nm::drivers
