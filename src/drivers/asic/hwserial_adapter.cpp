#include "hwserial_adapter.h"

HwSerialAdapter::HwSerialAdapter(HardwareSerial &hs) : _hs(hs) {
    // Constructor - just store reference
}

void HwSerialAdapter::begin(uint32_t baud) {
    _hs.begin(baud);
}

void HwSerialAdapter::end() {
    _hs.end();
}

void HwSerialAdapter::setPins(int8_t rxPin, int8_t txPin) {
    _hs.setPins(rxPin, txPin);
}

void HwSerialAdapter::updateBaudRate(uint32_t baudrate) {
    _hs.updateBaudRate(baudrate);
}

size_t HwSerialAdapter::write(const uint8_t *data, size_t len) {
    return _hs.write(data, len);
}

int HwSerialAdapter::available() {
    return _hs.available();
}

size_t HwSerialAdapter::read() {
    return _hs.read();
}

size_t HwSerialAdapter::readBytes(uint8_t *buffer, size_t length) {
    return _hs.readBytes(buffer, length);
}

bool HwSerialAdapter::clear_cache() {
    while (_hs.available()) {
        _hs.read();
    }
    return true;
}
