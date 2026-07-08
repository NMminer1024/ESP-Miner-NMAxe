// What: TPS53647 PMBus power driver for QAxe++-class boards.
// Why: QAxe++ Vcore is controlled by a TPS53647 buck controller, not PWM.
// Role: Implements real rail control, Vcore VID programming, ADC telemetry,
// PGOOD readiness, and PMBus temperature/fault reads behind the Power contract.
// Benefit: QAxe++ no longer boots/mines against placeholder power telemetry.
#pragma once

#include <stdint.h>

#include "drivers/power/power.h"
#include "hal/adc/adc_sampler.h"
#include "hal/i2c/i2c_master.h"

namespace nm::drivers {

struct Tps53647PinConfig {
    int8_t pll_enable_pin = -1;
    int8_t vdd_enable_pin = -1;
    int8_t vcore_enable_pin = -1;
    int8_t vcore_pgood_pin = -1;
    int8_t dc_plug_pin = -1;
    int8_t vbus_adc_pin = -1;
    int8_t ibus_adc_pin = -1;
    int8_t vcore_adc_pin = -1;

    Tps53647PinConfig() = default;
    Tps53647PinConfig(
        int8_t pll_enable_pin_value,
        int8_t vdd_enable_pin_value,
        int8_t vcore_enable_pin_value,
        int8_t vcore_pgood_pin_value,
        int8_t dc_plug_pin_value,
        int8_t vbus_adc_pin_value,
        int8_t ibus_adc_pin_value,
        int8_t vcore_adc_pin_value)
        : pll_enable_pin(pll_enable_pin_value),
          vdd_enable_pin(vdd_enable_pin_value),
          vcore_enable_pin(vcore_enable_pin_value),
          vcore_pgood_pin(vcore_pgood_pin_value),
          dc_plug_pin(dc_plug_pin_value),
          vbus_adc_pin(vbus_adc_pin_value),
          ibus_adc_pin(ibus_adc_pin_value),
          vcore_adc_pin(vcore_adc_pin_value) {}
};

struct Tps53647ControllerConfig {
    uint8_t phases = 2;
    uint8_t imax_a = 60;
    float ifault_a = 73.0f;
    float ibus_shunt_ohm = 0.005f;
    float tfault_c = 125.0f;
    uint8_t i2c_address = 0x71;
};

class Tps53647Power final : public Power {
public:
    Tps53647Power(
        const char* power_name,
        const Tps53647PinConfig& pins,
        const Tps53647ControllerConfig& controller,
        hal::i2c::I2cMaster& bus,
        hal::adc::AdcSampler& adc)
        : _name(power_name), _pins(pins), _controller(controller), _bus(bus), _adc(adc) {}

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

    float read_temperature_c() const;
    bool clear_faults();
    bool is_oc_fault() const;
    bool is_oc_warn() const;
    bool is_ot_fault() const;
    bool is_ot_warn() const;

private:
    bool _hw_init();
    bool _read_word(uint8_t reg, uint16_t& value) const;
    bool _read_byte(uint8_t reg, uint8_t& value) const;
    bool _write_byte(uint8_t reg, uint8_t value) const;
    bool _write_word(uint8_t reg, uint16_t value) const;
    bool _write_cmd(uint8_t cmd) const;
    bool _set_phases(uint8_t phases) const;
    uint8_t _mv_to_vid(uint16_t mv) const;
    uint16_t _float_to_slinear11(float value) const;
    float _slinear11_to_float(uint16_t value) const;
    uint32_t _sample_adc_mv(int8_t pin) const;

    const char* _name = "tps53647";
    Tps53647PinConfig _pins{};
    Tps53647ControllerConfig _controller{};
    hal::i2c::I2cMaster& _bus;
    hal::adc::AdcSampler& _adc;
    bool _initialized = false;
    bool _adc_ready = false;
    uint16_t _min_vcore_mv = 0;
    uint16_t _max_vcore_mv = 0;
    uint16_t _current_vcore_mv = 0;
};

}  // namespace nm::drivers
