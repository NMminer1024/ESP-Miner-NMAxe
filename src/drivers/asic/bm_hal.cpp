#include "bm_hal.h"
#include "logger.h"

BMxxx::BMxxx(HardwareSerial &port, uint32_t init_baud, uint8_t rx, uint8_t tx, uint8_t rst): _serial(port){
    this->_rst_pin = rst;
    this->_rx_pin = rx;
    this->_tx_pin = tx;
    pinMode(this->_rst_pin, OUTPUT);
    this->_serial.setPins(this->_rx_pin, this->_tx_pin);
    this->_serial.begin(init_baud);
}

BMxxx::~BMxxx(){
    this->_serial.end();
}

void BMxxx::reset(){
    digitalWrite(this->_rst_pin, LOW);
    delay(500);
    digitalWrite(this->_rst_pin, HIGH);
    delay(200);
}

void BMxxx::change_uart_baud(uint32_t baudrate){
    this->_serial.updateBaudRate(baudrate);
}

size_t BMxxx::send(uint8_t *cmd, uint16_t len){
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
        if (this->_serial.available()) {
            uint8_t data = this->_serial.read();
            buf[received++] = data;
        }
        if (millis() - start_time >= timeout_ms) break;
        else delay(1);
    }
    return received;
}
