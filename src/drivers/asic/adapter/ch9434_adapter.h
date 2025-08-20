#pragma once
#include "uart_hal.h"
#include "ch9434.h"

/**
 * Adapter class that wraps CH9434 SPI-to-4xUART chip to implement SerialAdapter interface
 * This allows BMxxx classes to use SPI-multiplexed UART channels through the abstract interface
 */
class CH9434Adapter : public SerialAdapter {
public:
    /**
     * Constructor
     * @param ch Reference to CH9434 controller instance
     * @param uart_idx UART channel index on CH9434 (0-3)
     */
    CH9434Adapter(CH9434 &ch, uint8_t uart_idx);

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
    CH9434 &_ch;
    uint8_t _uart_idx;
};
