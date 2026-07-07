// What: Shared PWM+tach fan driver for the current board family.
// Why: Fan control needs real RPM feedback and self-test behavior, but those
// mechanics should live in a reusable driver rather than in services or BSP glue.
// Role: Implements the `Fan` abstraction using LEDC PWM plus PCNT tach counting.
// Benefit: Cooling control becomes portable across boards that share the same
// electrical fan pattern.
#pragma once

#include <driver/pcnt.h>

#include "drivers/fan/fan.h"

namespace nm::drivers {

struct PwmTachFanConfig {
    int8_t pwm_pin = -1;
    uint8_t pwm_channel = 0;
    uint32_t pwm_frequency_hz = 1000 * 100;
    uint8_t pwm_resolution_bits = 8;
    int8_t tach_pin = -1;
    pcnt_unit_t pcnt_unit = PCNT_UNIT_0;
    pcnt_channel_t pcnt_channel = PCNT_CHANNEL_0;
    uint16_t self_test_rpm_threshold = 0;
    uint16_t danger_rpm_threshold = 0;

    PwmTachFanConfig() = default;
    PwmTachFanConfig(
        int8_t pwm_pin_value,
        uint8_t pwm_channel_value,
        uint32_t pwm_frequency_hz_value,
        uint8_t pwm_resolution_bits_value,
        int8_t tach_pin_value,
        pcnt_unit_t pcnt_unit_value,
        pcnt_channel_t pcnt_channel_value,
        uint16_t self_test_rpm_threshold_value,
        uint16_t danger_rpm_threshold_value)
        : pwm_pin(pwm_pin_value),
          pwm_channel(pwm_channel_value),
          pwm_frequency_hz(pwm_frequency_hz_value),
          pwm_resolution_bits(pwm_resolution_bits_value),
          tach_pin(tach_pin_value),
          pcnt_unit(pcnt_unit_value),
          pcnt_channel(pcnt_channel_value),
          self_test_rpm_threshold(self_test_rpm_threshold_value),
          danger_rpm_threshold(danger_rpm_threshold_value) {}
};

class PwmTachFan final : public Fan {
public:
    PwmTachFan(const char* fan_name, const PwmTachFanConfig& config)
        : _name(fan_name), _config(config) {}

    bool init() override;
    const char* name() const override { return _name; }
    bool set_speed_percent(uint8_t percent) override;
    uint8_t speed_percent() const override { return _speed_percent; }
    uint16_t read_rpm() override;
    FanPolarityDetectResult detect_polarity() override;
    uint16_t self_test_rpm_threshold() const override { return _config.self_test_rpm_threshold; }
    FanSelfTestResult run_self_test() override;
    FanSelfTestResult run_self_test(FanSelfTestProgressCallback callback, void* ctx) override;

private:
    uint16_t _measure_rpm_for_duration(uint8_t percent, uint32_t duration_ms);
    uint16_t _measure_current_rpm(uint32_t duration_ms);
    void _apply_speed_percent(uint8_t percent);
    static uint16_t _calculate_rpm(int16_t pulse_count, uint32_t duration_ms);

    const char* _name = "fan";
    PwmTachFanConfig _config{};
    bool _initialized = false;
    bool _inverted = false;
    uint8_t _speed_percent = 0;
};

}  // namespace nm::drivers
