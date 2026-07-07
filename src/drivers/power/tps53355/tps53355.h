// What: Shared TPS53355-based power driver for Gamma/NMAxe-class boards.
// Why: Gamma power control is real hardware, but it should still sit behind the
// generic `Power` abstraction rather than leaking ADC/PWM details upward.
// Role: Owns rail enables, Vcore PWM control, and ADC-based telemetry reads.
// Benefit: Future boards using the same regulator can reuse the implementation
// by supplying only their board-specific pin mapping.
#pragma once

#include <stdint.h>

#include "drivers/power/power.h"
#include "hal/adc/adc_sampler.h"

namespace nm::drivers {

struct Tps53355PinConfig {
    int8_t pll_enable_pin = -1;
    int8_t vdd_enable_pin = -1;
    int8_t vcore_enable_pin = -1;
    int8_t vcore_pwm_pin = -1;
    int8_t vcore_pgood_pin = -1;
    int8_t dc_plug_pin = -1;
    int8_t vbus_adc_pin = -1;
    int8_t ibus_adc_pin = -1;
    int8_t vcore_adc_pin = -1;

    Tps53355PinConfig() = default;
    Tps53355PinConfig(
        int8_t pll_enable_pin_value,
        int8_t vdd_enable_pin_value,
        int8_t vcore_enable_pin_value,
        int8_t vcore_pwm_pin_value,
        int8_t vcore_pgood_pin_value,
        int8_t dc_plug_pin_value,
        int8_t vbus_adc_pin_value,
        int8_t ibus_adc_pin_value,
        int8_t vcore_adc_pin_value)
        : pll_enable_pin(pll_enable_pin_value),
          vdd_enable_pin(vdd_enable_pin_value),
          vcore_enable_pin(vcore_enable_pin_value),
          vcore_pwm_pin(vcore_pwm_pin_value),
          vcore_pgood_pin(vcore_pgood_pin_value),
          dc_plug_pin(dc_plug_pin_value),
          vbus_adc_pin(vbus_adc_pin_value),
          ibus_adc_pin(ibus_adc_pin_value),
          vcore_adc_pin(vcore_adc_pin_value) {}
};

class Tps53355Power final : public Power {
public:
    Tps53355Power(const char* power_name, const Tps53355PinConfig& config, hal::adc::AdcSampler& adc)
        : _name(power_name), _config(config), _adc(adc) {}

    bool init() override;
    const char* name() const override { return _name; }
    bool adc_ready() const override { return _adc_ready; }
    bool set_rail_enabled(PowerRail rail, bool enabled) override;
    bool set_vcore_limits(uint16_t min_mv, uint16_t max_mv) override;
    uint16_t min_vcore_mv() const override { return _min_vcore_mv; }
    uint16_t max_vcore_mv() const override { return _max_vcore_mv; }
    bool set_vcore_mv(uint16_t value_mv) override;
    bool is_vcore_ready() const override;
    bool is_dc_plugged() const override;
    uint32_t read_vbus_mv() override;
    uint32_t read_ibus_ma() override;
    uint32_t read_vcore_mv() override;

private:
    uint32_t _sample_adc_mv(int8_t pin) const;

    const char* _name = "tps53355";
    Tps53355PinConfig _config{};
    hal::adc::AdcSampler& _adc;
    bool _initialized = false;
    bool _adc_ready = false;
    uint16_t _min_vcore_mv = 0;
    uint16_t _max_vcore_mv = 0;
    uint16_t _current_vcore_mv = 0;
};

}  // namespace nm::drivers
