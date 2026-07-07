// What: ESP32 Arduino UART HAL implementation shared by chip drivers.
// Why: Drivers such as BM1370 should not each own HardwareSerial pin binding,
// timeout setup, and RX cache handling.
// Role: Wraps a configured serial port and exposes a small transport API.
// Benefit: Keeps UART wiring decisions in BSP/hal while ASIC drivers remain
// reusable and easier to migrate toward full protocol implementations.
#include "hal/uart/uart_port.h"

namespace nm::hal::uart {

bool UartPort::init(uint32_t baud, uint32_t timeout_ms, uint32_t serial_config) {
    if (_config.controller == nullptr || _config.rx_pin < 0 || _config.tx_pin < 0) {
        return false;
    }

    _timeout_ms = timeout_ms;
    _serial_config = serial_config;
    _baud = baud;

    if (_config.cts_pin >= 0 || _config.rts_pin >= 0) {
        _controller().setPins(_config.rx_pin, _config.tx_pin, _config.cts_pin, _config.rts_pin);
        _controller().begin(_baud, _serial_config);
    } else {
        _controller().begin(_baud, _serial_config, _config.rx_pin, _config.tx_pin);
    }
    _controller().setTimeout(_timeout_ms);
    _initialized = true;
    return true;
}

bool UartPort::set_baud(uint32_t baud) {
    if (!_initialized) {
        return init(baud, _timeout_ms, _serial_config);
    }

    _baud = baud;
    _controller().begin(_baud, _serial_config, _config.rx_pin, _config.tx_pin);
    _controller().setTimeout(_timeout_ms);
    return true;
}

void UartPort::set_timeout(uint32_t timeout_ms) {
    _timeout_ms = timeout_ms;
    if (_initialized) {
        _controller().setTimeout(_timeout_ms);
    }
}

int UartPort::available() const {
    if (!_initialized) {
        return 0;
    }

    return _controller().available();
}

int UartPort::read() {
    if (!_initialized) {
        return -1;
    }

    return _controller().read();
}

size_t UartPort::write(const uint8_t* data, size_t size) {
    if (!_initialized || data == nullptr || size == 0) {
        return 0;
    }

    return _controller().write(data, size);
}

void UartPort::flush() {
    if (!_initialized) {
        return;
    }

    _controller().flush();
}

void UartPort::clear_rx() {
    while (available() > 0) {
        read();
    }
}

HardwareSerial& UartPort::_controller() const {
    return *_config.controller;
}

}  // namespace nm::hal::uart
