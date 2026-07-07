// What: ESP32 Arduino ADC HAL implementation shared by power and sensor drivers.
// Why: Repeated attenuation setup and averaged millivolt reads should live in
// one helper instead of being reimplemented per driver.
// Role: Applies ADC configuration and performs averaged millivolt sampling.
// Benefit: Makes ADC-backed drivers smaller and keeps sampling behavior easy
// to standardize across BSPs.
#include "hal/adc/adc_sampler.h"

#include <Arduino.h>

namespace nm::hal::adc {

bool AdcSampler::init(uint8_t resolution_bits) {
    _resolution_bits = resolution_bits;
    analogReadResolution(_resolution_bits);
    _initialized = true;
    return true;
}

bool AdcSampler::configure_pin(int8_t pin, AdcAttenuation attenuation) {
    if (!_initialized || pin < 0) {
        return false;
    }

    switch (attenuation) {
        case AdcAttenuation::Db0:
            analogSetPinAttenuation(pin, ADC_0db);
            break;
        case AdcAttenuation::Db2p5:
            analogSetPinAttenuation(pin, ADC_2_5db);
            break;
        case AdcAttenuation::Db6:
            analogSetPinAttenuation(pin, ADC_6db);
            break;
        case AdcAttenuation::Db11:
            analogSetPinAttenuation(pin, ADC_11db);
            break;
    }

    return true;
}

uint32_t AdcSampler::sample_mv(int8_t pin, uint8_t samples, uint8_t sample_delay_ms) const {
    if (!_initialized || pin < 0 || samples == 0) {
        return 0;
    }

    uint32_t total_mv = 0;
    for (uint8_t i = 0; i < samples; ++i) {
        total_mv += static_cast<uint32_t>(analogReadMilliVolts(pin));
        if (sample_delay_ms > 0 && i + 1 < samples) {
            delay(sample_delay_ms);
        }
    }

    return total_mv / samples;
}

}  // namespace nm::hal::adc
