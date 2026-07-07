// What: Cooling fan abstraction exported from the BSP layer.
// Why: Different boards may have different fan counts or control hardware, but
// higher layers should not know those implementation details.
// Role: Defines the common identity and initialization surface for fan drivers.
// Benefit: Fan management can evolve per board without changing the framework
// contract that application, policy, or UI code depends on.
#pragma once

#include <stdint.h>

namespace nm::drivers {

struct FanSelfTestResult {
    bool passed = false;
    uint16_t rpm = 0;

    FanSelfTestResult() = default;
    FanSelfTestResult(bool passed_value, uint16_t rpm_value)
        : passed(passed_value), rpm(rpm_value) {}
};

struct FanPolarityDetectResult {
    bool inverted = false;
    uint16_t rpm_50 = 0;
    uint16_t rpm_100 = 0;
};

using FanSelfTestProgressCallback = void (*)(uint16_t rpm, void* ctx);

class Fan {
public:
    virtual ~Fan() = default;
    virtual bool init() = 0;
    virtual const char* name() const = 0;
    virtual bool set_speed_percent(uint8_t percent) = 0;
    virtual uint8_t speed_percent() const = 0;
    virtual uint16_t read_rpm() = 0;
    virtual FanPolarityDetectResult detect_polarity() = 0;
    virtual uint16_t self_test_rpm_threshold() const = 0;
    virtual FanSelfTestResult run_self_test() = 0;
    virtual FanSelfTestResult run_self_test(FanSelfTestProgressCallback callback, void* ctx) {
        FanSelfTestResult result = run_self_test();
        if (callback != nullptr) {
            callback(result.rpm, ctx);
        }
        return result;
    }
};

}  // namespace nm::drivers
