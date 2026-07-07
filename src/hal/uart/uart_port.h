// What: Reusable HAL-level UART port helper for chip drivers.
// Why: UART wiring and Arduino serial-port ownership are transport concerns,
// not ASIC-driver concerns.
// Role: Wraps one HardwareSerial instance plus board-selected pin binding.
// Benefit: ASIC drivers can depend on a small reusable serial transport while
// BSP code remains the only place that decides which UART and pins are used.
#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace nm::hal::uart {

struct UartPortConfig {
    HardwareSerial* controller = nullptr;
    int8_t rx_pin = -1;
    int8_t tx_pin = -1;
    int8_t cts_pin = -1;
    int8_t rts_pin = -1;

    UartPortConfig() = default;
    UartPortConfig(
        HardwareSerial* controller_value,
        int8_t rx_pin_value,
        int8_t tx_pin_value,
        int8_t cts_pin_value = -1,
        int8_t rts_pin_value = -1)
        : controller(controller_value),
          rx_pin(rx_pin_value),
          tx_pin(tx_pin_value),
          cts_pin(cts_pin_value),
          rts_pin(rts_pin_value) {}
};

class UartPort {
public:
    explicit UartPort(const UartPortConfig& config)
        : _config(config) {}

    bool init(uint32_t baud, uint32_t timeout_ms = 20, uint32_t serial_config = SERIAL_8N1);
    bool set_baud(uint32_t baud);
    void set_timeout(uint32_t timeout_ms);
    int available() const;
    int read();
    size_t write(const uint8_t* data, size_t size);
    void flush();
    void clear_rx();

    int8_t rx_pin() const { return _config.rx_pin; }
    int8_t tx_pin() const { return _config.tx_pin; }
    uint32_t baud() const { return _baud; }

private:
    HardwareSerial& _controller() const;

    UartPortConfig _config{};
    bool _initialized = false;
    uint32_t _baud = 0;
    uint32_t _timeout_ms = 20;
    uint32_t _serial_config = SERIAL_8N1;
};

}  // namespace nm::hal::uart
