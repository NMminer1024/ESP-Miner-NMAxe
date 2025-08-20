#pragma once
#include <HardwareSerial.h>
#include "uart_hal.h"

/**
 * Adapter class that wraps HardwareSerial to implement SerialAdapter interface
 * This allows BMxxx classes to use physical UART ports through the abstract interface
 */
class HwSerialAdapter : public SerialAdapter {
public:
    /**
     * Constructor
     * @param hs Reference to HardwareSerial instance (e.g., Serial1, Serial2)
     */
    HwSerialAdapter(HardwareSerial &hs);

    // SerialAdapter interface implementation
    void begin(uint32_t baud) ;
    void end() ;
    void setPins(int8_t rxPin, int8_t txPin) ;
    void updateBaudRate(uint32_t baudrate) ;
    size_t write(const uint8_t *data, size_t len) ;
    int available() ;
    size_t read() ;
    size_t readBytes(uint8_t *buffer, size_t length) ;
    bool clear_cache() ;

private:
    HardwareSerial &_hs;
};
