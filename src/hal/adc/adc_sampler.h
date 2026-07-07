// What: Reusable HAL-level ADC sampler helper for board drivers.
// Why: ADC attenuation and millivolt sampling are transport-level concerns and
// should not be duplicated inside each power or sensor driver.
// Role: Configures Arduino ADC settings and provides averaged millivolt reads.
// Benefit: Drivers can depend on one shared sampling helper while BSPs and HAL
// retain control over sampling policy and pin configuration.
#pragma once

#include <stdint.h>

namespace nm::hal::adc {

enum class AdcAttenuation : uint8_t {
    Db0,
    Db2p5,
    Db6,
    Db11,
};

class AdcSampler {
public:
    bool init(uint8_t resolution_bits = 12);
    bool configure_pin(int8_t pin, AdcAttenuation attenuation);
    uint32_t sample_mv(int8_t pin, uint8_t samples = 1, uint8_t sample_delay_ms = 0) const;

private:
    uint8_t _resolution_bits = 12;
    bool _initialized = false;
};

}  // namespace nm::hal::adc
