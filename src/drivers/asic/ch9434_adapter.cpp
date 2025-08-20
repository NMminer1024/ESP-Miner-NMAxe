#include "ch9434_adapter.h"

CH9434Adapter::CH9434Adapter(CH9434 &ch, uint8_t uart_idx) : _ch(ch), _uart_idx(uart_idx) {
    // Constructor - store references and uart index
}

void CH9434Adapter::begin(uint32_t baud) {
    // Configure UART parameters: 8 data bits, 1 stop bit, no parity
    _ch.uartx_para_set(_uart_idx, baud, 8, 1, CH9434_UART_NO_PARITY);
    // Enable FIFO with default level
    _ch.uartx_fifo_set(_uart_idx, CH9434_ENABLE, CH9434_UART_FIFO_MODE_256);
    // Enable interrupts (implementation dependent)
    _ch.uartx_irq_set(_uart_idx, 0, 0, 1, 1); // Enable TX/RX interrupts
}

void CH9434Adapter::end() {
    // Disable UART functionality if needed
    // Implementation depends on CH9434 requirements
}

void CH9434Adapter::setPins(int8_t rxPin, int8_t txPin) {
    // CH9434 uses SPI, physical pins are managed by SPI controller
    // This method is typically no-op for SPI-to-UART adapters
}

void CH9434Adapter::updateBaudRate(uint32_t baudrate) {
    // Update baud rate by reconfiguring UART parameters
    _ch.uartx_para_set(_uart_idx, baudrate, 8, 1, CH9434_UART_NO_PARITY);
}

size_t CH9434Adapter::write(const uint8_t *data, size_t len) {
    // Write data to TX FIFO
    return _ch.uartx_set_tx_fifo_data(_uart_idx, const_cast<uint8_t*>(data), static_cast<uint16_t>(len));
}

int CH9434Adapter::available() {
    // Get number of bytes available in RX FIFO
    return _ch.uartx_get_rx_fifo_len(_uart_idx);
}

size_t CH9434Adapter::read() {
    // Read single byte from RX FIFO
    uint8_t data;
    if (_ch.uartx_get_rx_fifo_data(_uart_idx, &data, 1) > 0) {
        return data;
    }
    return -1; // No data available
}

size_t CH9434Adapter::readBytes(uint8_t *buffer, size_t length) {
    // Read multiple bytes from RX FIFO
    return _ch.uartx_get_rx_fifo_data(_uart_idx, buffer, static_cast<uint16_t>(length));
}

bool CH9434Adapter::clear_cache() {
    // Clear RX FIFO by reading all available data
    uint16_t available_bytes = _ch.uartx_get_rx_fifo_len(_uart_idx);
    while (available_bytes > 0) {
        uint8_t temp_buffer[64];
        uint16_t to_read = (available_bytes > sizeof(temp_buffer)) ? sizeof(temp_buffer) : available_bytes;
        uint16_t read_count = _ch.uartx_get_rx_fifo_data(_uart_idx, temp_buffer, to_read);
        if (read_count == 0) break; // Avoid infinite loop
        available_bytes = _ch.uartx_get_rx_fifo_len(_uart_idx);
    }
    return true;
}
