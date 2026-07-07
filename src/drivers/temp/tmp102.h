// What: Shared TMP102 temperature-sensor driver.
// Why: Several boards use the same sensor family, so the conversion and config
// logic should live in one reusable driver instead of being duplicated in BSPs.
// Role: Implements the `TempSensor` abstraction on top of an injected I2C bus.
// Benefit: Boards can opt into TMP102 by wiring addresses and bus pins only.
#pragma once

#include <stdint.h>

#include "drivers/i2c/i2c_master.h"
#include "drivers/temp/temp.h"

namespace nm::drivers {

class Tmp102Sensor final : public TempSensor {
public:
    Tmp102Sensor(
        const char* sensor_name,
        i2c::I2cMaster& bus,
        uint8_t vcore_address = 0x48,
        uint8_t asic_address = 0x49)
        : _name(sensor_name), _bus(bus), _vcore_address(vcore_address), _asic_address(asic_address) {}

    bool init() override;
    const char* name() const override { return _name; }
    float read_vcore_c() const override;
    float read_asic_c() const override;

private:
    bool configure_sensor(uint8_t address) const;
    bool read_temperature(uint8_t address, float& value_c) const;

    const char* _name = "tmp102";
    i2c::I2cMaster& _bus;
    uint8_t _vcore_address = 0x48;
    uint8_t _asic_address = 0x49;
    bool _initialized = false;
};

}  // namespace nm::drivers
