#pragma once
#include <stddef.h>
#include <stdint.h>

class SerialAdapter {
public:
    virtual ~SerialAdapter() = default;

    virtual void begin(uint32_t baud) = 0;
    virtual void end() = 0;
    virtual void setPins(int8_t rxPin, int8_t txPin) = 0;
    virtual void updateBaudRate(uint32_t baudrate) = 0;
    virtual int  available() = 0;
    virtual size_t  read() = 0;
    virtual size_t  write(const uint8_t *data, size_t len) = 0;
};