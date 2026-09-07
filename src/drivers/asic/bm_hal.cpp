#include "bm_hal.h"
#include "utils/logger/logger.h"

BMxxx::BMxxx(HardwareSerial &port, uint32_t init_baud, uint8_t rx, uint8_t tx, uint8_t rst): _serial(port){
    this->_rst_pin = rst;
    this->_rx_pin = rx;
    this->_tx_pin = tx;
    pinMode(this->_rst_pin, OUTPUT);
    this->_serial.setPins(this->_rx_pin, this->_tx_pin);
    // Default HW UART RX ring (256B) can overflow under bursty low-diff share traffic,
    // silently dropping the infrequent (every 2s) HCN 0x90 poll response frames first.
    // Must be set before begin().
    this->_serial.setRxBufferSize(2048);
    this->_serial.begin(init_baud);
}

BMxxx::~BMxxx(){
    this->_serial.end();
}

void BMxxx::reset(){
    digitalWrite(this->_rst_pin, LOW);
    delay(10);
    digitalWrite(this->_rst_pin, HIGH);
    delay(10);
}

void BMxxx::change_uart_baud(uint32_t baudrate){
    LOG_D("Changing UART baudrate to %d...", baudrate);
    this->_serial.updateBaudRate(baudrate);
    delay(50);
}

size_t BMxxx::send(uint8_t *cmd, uint16_t len){
    // dbg::hex_print(cmd, len, "Send data to ASIC");
    return this->_serial.write(cmd, len);
}

bool BMxxx::clear_port_cache(){
    while(this->_serial.available()){
        this->_serial.read();
    }
    return true;
}

size_t BMxxx::receive(uint8_t *buf, uint16_t len, uint32_t timeout_ms){
    size_t received = 0;
    uint32_t start_time = millis();
    while (received < len) {
        int available = this->_serial.available();
        while ((available-- > 0) && (received < len)) {
            buf[received++] = this->_serial.read();
        }
        if (received >= len) {
            break;
        }
        if (millis() - start_time >= timeout_ms) break;
        else delay(1);
    }
    // dbg::hex_print(buf, received, "Receive data from ASIC");
    return received;
}
