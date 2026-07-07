#pragma once

namespace nm::drivers {

class Asic {
public:
    virtual ~Asic() = default;
    virtual bool init() = 0;
    virtual const char* name() const = 0;
};

class NullAsic final : public Asic {
public:
    explicit NullAsic(const char* asic_name) : _name(asic_name) {}

    bool init() override { return true; }
    const char* name() const override { return _name; }

private:
    const char* _name = "null-asic";
};

}  // namespace nm::drivers
