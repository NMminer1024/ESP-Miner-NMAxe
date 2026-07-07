// What: Phase-1 BM1370 driver implementation for the new framework.
// Why: Before mining logic is migrated, the framework still needs one place
// that owns BM1370 UART wiring and reset-pin configuration.
// Role: Prepares the serial transport and reset line, but intentionally does
// not yet send BM1370 mining protocol commands.
// Benefit: Gamma now exposes a real chip-specific driver without forcing the
// old mining stack or BM protocol details back into application startup.
#include "drivers/asic/bm1370/bm1370.h"

#include <Arduino.h>
#include <string.h>

namespace nm::drivers {

namespace {

constexpr uint32_t kAsicDefaultVersionMask = 0x1fffe000;
constexpr uint8_t kTypeCmd = 0x40;
constexpr uint8_t kGroupAll = 0x10;
constexpr uint8_t kCmdWrite = 0x01;
constexpr uint8_t kProbePacket[] = {0x55, 0xAA, 0x52, 0x05, 0x00, 0x00, 0x0A};
constexpr uint8_t kProbeReply[] = {0xAA, 0x55, 0x13, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10};

uint8_t crc5_calc(const uint8_t* data, uint8_t len) {
    uint8_t index = 0;
    uint8_t crc = 0x1F;
    uint8_t crcin[5] = {1, 1, 1, 1, 1};
    uint8_t crcout[5] = {1, 1, 1, 1, 1};

    len *= 8;
    for (uint8_t mask = 0x80, bit_count = 0, i = 0; i < len; ++i) {
        const uint8_t input_bit = (data[index] & mask) != 0;
        crcout[0] = crcin[4] ^ input_bit;
        crcout[1] = crcin[0];
        crcout[2] = crcin[1] ^ crcin[4] ^ input_bit;
        crcout[3] = crcin[2];
        crcout[4] = crcin[3];

        mask >>= 1;
        ++bit_count;
        if (bit_count == 8) {
            mask = 0x80;
            bit_count = 0;
            ++index;
        }
        memcpy(crcin, crcout, sizeof(crcin));
    }

    crc = 0;
    if (crcin[4]) crc |= 0x10;
    if (crcin[3]) crc |= 0x08;
    if (crcin[2]) crc |= 0x04;
    if (crcin[1]) crc |= 0x02;
    if (crcin[0]) crc |= 0x01;
    return crc;
}

}  // namespace

bool Bm1370Asic::init() {
    if (_initialized) {
        return true;
    }

    if (!_configure_uart()) {
        return false;
    }

    _configure_reset_pin();
    _clear_port_cache();

    Serial.printf(
        "[asic.bm1370] transport ready name=%s init_baud=%lu work_baud=%lu rx=%d tx=%d rst=%d\n",
        name(),
        static_cast<unsigned long>(_config.init_baud),
        static_cast<unsigned long>(_config.work_baud),
        static_cast<int>(_config.port->rx_pin()),
        static_cast<int>(_config.port->tx_pin()),
        static_cast<int>(_config.reset_pin));

    // TODO(agent): migrate the old BM1370 command protocol here when the
    // mining service layer is introduced. At this stage the driver only owns
    // the physical transport and reset line.
    _status.transport_ready = true;
    _initialized = true;
    return true;
}

uint8_t Bm1370Asic::probe_count() {
    if (!_initialized || _config.port == nullptr) {
        return 0;
    }

    _config.port->set_baud(_config.init_baud);
    _reset_chip();
    _clear_port_cache();

    for (uint8_t i = 0; i < 4; ++i) {
        _set_version_mask(kAsicDefaultVersionMask);
    }

    _send_raw(kProbePacket, sizeof(kProbePacket));

    uint8_t chip_counter = 0;
    uint8_t response[128] = {};
    while (true) {
        memset(response, 0, sizeof(response));
        const size_t received = _receive(response, sizeof(response), 1000);
        if (received == 0) {
            break;
        }
        if (received < sizeof(kProbeReply)) {
            continue;
        }

        for (const uint8_t* cursor = response;
             cursor <= response + received - sizeof(kProbeReply);
             ++cursor) {
            if (memcmp(cursor, kProbeReply, sizeof(kProbeReply)) == 0) {
                ++chip_counter;
            }
        }
    }

    _status.detected_asic_count = chip_counter;
    Serial.printf("[asic.bm1370] probe detected=%u\n", static_cast<unsigned>(chip_counter));
    return chip_counter;
}

bool Bm1370Asic::bringup(uint16_t target_freq_mhz, uint8_t) {
    if (!_initialized) {
        return false;
    }

    // Phase-1 temporary bring-up:
    // The service layer now owns mining lifecycle sequencing, but the full
    // BM1370 command migration from `src_old` is still pending. For now this
    // marks transport-backed standby only; chain detection and PLL programming
    // must be added here before real mining starts.
    if (_config.port != nullptr && _config.work_baud != _config.port->baud()) {
        _config.port->set_baud(_config.work_baud);
    }
    _status.target_freq_mhz = target_freq_mhz;
    _status.bringup_complete = true;
    return true;
}

void Bm1370Asic::_reset_chip() {
    if (_config.reset_pin < 0) {
        return;
    }

    digitalWrite(_config.reset_pin, LOW);
    delay(50);
    digitalWrite(_config.reset_pin, HIGH);
    delay(20);
}

bool Bm1370Asic::_configure_uart() {
    if (_config.port == nullptr) {
        return false;
    }

    return _config.port->init(_config.init_baud, 20);
}

void Bm1370Asic::_configure_reset_pin() {
    if (_config.reset_pin < 0) {
        return;
    }

    pinMode(_config.reset_pin, OUTPUT);
    digitalWrite(_config.reset_pin, HIGH);
}

void Bm1370Asic::_clear_port_cache() {
    if (_config.port == nullptr) {
        return;
    }

    _config.port->clear_rx();
}

size_t Bm1370Asic::_send_raw(const uint8_t* data, size_t size) {
    if (_config.port == nullptr || data == nullptr || size == 0) {
        return 0;
    }

    const size_t written = _config.port->write(data, size);
    _config.port->flush();
    return written;
}

size_t Bm1370Asic::_receive(uint8_t* data, size_t size, uint32_t timeout_ms) {
    if (_config.port == nullptr || data == nullptr || size == 0) {
        return 0;
    }

    size_t received = 0;
    const uint32_t start_ms = millis();
    while (received < size) {
        int available = _config.port->available();
        while (available-- > 0 && received < size) {
            data[received++] = static_cast<uint8_t>(_config.port->read());
        }

        if (received >= size) {
            break;
        }
        if (millis() - start_ms >= timeout_ms) {
            break;
        }
        delay(1);
    }

    return received;
}

void Bm1370Asic::_send_command_packet(uint8_t header, const uint8_t* data, uint8_t size) {
    const uint8_t total_length = static_cast<uint8_t>(size + 5);
    uint8_t buffer[32] = {};
    if (total_length > sizeof(buffer)) {
        return;
    }

    buffer[0] = 0x55;
    buffer[1] = 0xAA;
    buffer[2] = header;
    buffer[3] = static_cast<uint8_t>(size + 3);
    if (data != nullptr && size > 0) {
        memcpy(buffer + 4, data, size);
    }
    buffer[4 + size] = crc5_calc(buffer + 2, static_cast<uint8_t>(size + 2));
    _send_raw(buffer, total_length);
}

void Bm1370Asic::_set_version_mask(uint32_t) {
    const uint8_t version_cmd[] = {0x00, 0xA4, 0x90, 0x00, 0xFF, 0xFF};
    _send_command_packet(static_cast<uint8_t>(kTypeCmd | kGroupAll | kCmdWrite), version_cmd, sizeof(version_cmd));
}

}  // namespace nm::drivers
