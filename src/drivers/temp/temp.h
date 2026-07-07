// What: Temperature sensing abstraction for board thermal telemetry.
// Why: Boards may read Vcore and ASIC temperatures from different chips or buses,
// but thermal consumers need a consistent interface.
// Role: Defines the generic thermal read contract exported by BSPs.
// Benefit: Keeps mining policy and UI telemetry code independent from concrete
// sensor parts, buses, and fallback behavior.
#pragma once

namespace nm::drivers {

class TempSensor {
public:
    virtual ~TempSensor() = default;
    virtual bool init() = 0;
    virtual const char* name() const = 0;
    virtual float read_vcore_c() const = 0;
    virtual float read_asic_c() const = 0;
};

// Temporary skeleton-only fallback.
// TODO(agent): remove this class after each active BSP is wired to real temperature sensors.
class NullTempSensor final : public TempSensor {
public:
    NullTempSensor(const char* sensor_name, float vcore_c, float asic_c)
        : _name(sensor_name), _vcore_c(vcore_c), _asic_c(asic_c) {}

    bool init() override { return true; }
    const char* name() const override { return _name; }
    float read_vcore_c() const override { return _vcore_c; }
    float read_asic_c() const override { return _asic_c; }

private:
    const char* _name = "null-temp";
    float _vcore_c = 0.0f;
    float _asic_c = 0.0f;
};

}  // namespace nm::drivers
