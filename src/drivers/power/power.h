#pragma once

namespace nm::drivers {

class Power {
public:
    virtual ~Power() = default;
    virtual bool init() = 0;
    virtual const char* name() const = 0;
};

class NullPower final : public Power {
public:
    explicit NullPower(const char* power_name) : _name(power_name) {}

    bool init() override { return true; }
    const char* name() const override { return _name; }

private:
    const char* _name = "null-power";
};

}  // namespace nm::drivers
