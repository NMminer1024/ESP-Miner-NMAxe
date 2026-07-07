// What: TMP102 implementation shared by BSPs that use external I2C sensors.
// Why: The new framework wants thermal reads behind the `TempSensor` interface,
// not behind board-specific helper functions.
// Role: Configures both TMP102 channels and converts raw register values into C.
// Benefit: Thermal services can stay sensor-agnostic while board code simply
// instantiates this driver with the correct bus and addresses.
#include "drivers/temp/tmp102.h"

#include <math.h>

namespace nm::drivers {

namespace {

constexpr uint8_t kTemperatureRegister = 0x00;
constexpr uint8_t kConfigRegister = 0x01;
constexpr float kResolutionCPerBit = 0.0625f;

}  // namespace

bool Tmp102Sensor::init() {
    if (_initialized) {
        return true;
    }

    if (!_bus.init()) {
        return false;
    }

    const bool vcore_ok = configure_sensor(_vcore_address);
    const bool asic_ok = configure_sensor(_asic_address);
    _initialized = vcore_ok || asic_ok;
    return _initialized;
}

float Tmp102Sensor::read_vcore_c() const {
    float value_c = NAN;
    return read_temperature(_vcore_address, value_c) ? value_c : NAN;
}

float Tmp102Sensor::read_asic_c() const {
    float value_c = NAN;
    return read_temperature(_asic_address, value_c) ? value_c : NAN;
}

bool Tmp102Sensor::configure_sensor(uint8_t address) const {
    uint8_t data[2] = {};
    if (!_bus.read_register(address, kConfigRegister, data, sizeof(data))) {
        return false;
    }

    uint16_t config = (static_cast<uint16_t>(data[0]) << 8) | data[1];
    config |= static_cast<uint16_t>(0b11u << 6);  // 8Hz update rate, matching src_old.

    return _bus.write_register_byte(address, kConfigRegister, static_cast<uint8_t>(config & 0xFF));
}

bool Tmp102Sensor::read_temperature(uint8_t address, float& value_c) const {
    uint8_t data[2] = {};
    if (!_initialized || !_bus.read_register(address, kTemperatureRegister, data, sizeof(data))) {
        return false;
    }

    uint16_t raw = static_cast<uint16_t>(data[0]) << 8;
    raw |= data[1];
    raw >>= 4;
    value_c = static_cast<float>(raw) * kResolutionCPerBit;
    return true;
}

}  // namespace nm::drivers
