#include "hwserial_adapter.h"

HwSerialAdapter::HwSerialAdapter(HardwareSerial &hs) :_hs(hs) {
    // Constructor - just store reference
}

void HwSerialAdapter::begin(uint32_t baud) {
    this->_hs.begin(baud);
}

void HwSerialAdapter::end() {
    this->_hs.end();
}

void HwSerialAdapter::setPins(int8_t rxPin, int8_t txPin) {
    this->_hs.setPins(rxPin, txPin);
}

void HwSerialAdapter::updateBaudRate(uint32_t baudrate) {
    this->_hs.updateBaudRate(baudrate);
}

size_t HwSerialAdapter::write(const uint8_t *data, size_t len) {
    return this->_hs.write(data, len);
}

int HwSerialAdapter::available() {
    return this->_hs.available();
}

size_t HwSerialAdapter::read() {
    return this->_hs.read();
}

size_t HwSerialAdapter::readBytes(uint8_t *buffer, size_t length) {
    return this->_hs.readBytes(buffer, length);
}

bool HwSerialAdapter::clear_cache() {
    while (this->_hs.available()) {
        this->_hs.read();
    }
    return true;
}
