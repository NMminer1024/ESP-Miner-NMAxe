// What: Phase-1 BM1366 driver implementation for the new framework.
// Why: The new BSP split needs a real NMAxe chip binding now, even though the
// old BM1366 mining command stack has not been fully migrated yet.
// Role: Owns UART/reset bring-up plus the minimal probe-and-standby path used
// by the current mining service skeleton.
// Benefit: NMAxe can diverge from Gamma at the BSP/product level immediately
// without blocking on the full mining protocol port.
#include "drivers/asic/bm1366/bm1366.h"

#include <Arduino.h>
#include <math.h>
#include <string.h>

#include "utils/crc/crc.h"
#include "utils/helper.h"
#include "utils/logger/logger.h"

namespace nm::drivers {

namespace {

constexpr uint32_t kAsicDefaultVersionMask = 0x1fffe000;
constexpr uint8_t kTypeCmd = 0x40;
constexpr uint8_t kTypeJob = 0x20;
constexpr uint8_t kGroupAll = 0x10;
constexpr uint8_t kGroupSingle = 0x00;
constexpr uint8_t kCmdSetAddress = 0x00;
constexpr uint8_t kCmdWrite = 0x01;
constexpr uint8_t kCmdInactive = 0x03;
constexpr uint8_t kTicketMask = 0x14;
constexpr uint8_t kProbePacket[] = {0x55, 0xAA, 0x52, 0x05, 0x00, 0x00, 0x0A};
constexpr uint8_t kProbeReplyPrefix[] = {0xAA, 0x55, 0x13, 0x66, 0x00, 0x00};

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

bool Bm1366Asic::init() {
    if (_initialized) {
        return true;
    }

    if (!_configure_uart()) {
        return false;
    }

    _configure_reset_pin();
    clear_port_cache();

    LOG_I(
        "[asic.bm1366] transport ready name=%s init_baud=%lu work_baud=%lu rx=%d tx=%d rst=%d",
        name(),
        static_cast<unsigned long>(_config.init_baud),
        static_cast<unsigned long>(_config.work_baud),
        static_cast<int>(_config.port->rx_pin()),
        static_cast<int>(_config.port->tx_pin()),
        static_cast<int>(_config.reset_pin));

    _status.transport_ready = true;
    _initialized = true;
    return true;
}

uint8_t Bm1366Asic::probe_count() {
    if (!_initialized || _config.port == nullptr) {
        return 0;
    }

    _config.port->set_baud(_config.init_baud);
    _reset_chip();
    clear_port_cache();

    for (uint8_t i = 0; i < 3; ++i) {
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
        if (received < sizeof(kProbeReplyPrefix)) {
            continue;
        }

        for (const uint8_t* cursor = response;
             cursor <= response + received - sizeof(kProbeReplyPrefix);
             ++cursor) {
            if (memcmp(cursor, kProbeReplyPrefix, sizeof(kProbeReplyPrefix)) == 0) {
                ++chip_counter;
                break;
            }
        }
    }

    _status.detected_asic_count = chip_counter;
    LOG_I("[asic.bm1366] probe detected=%u", static_cast<unsigned>(chip_counter));
    return chip_counter;
}

bool Bm1366Asic::bringup(uint16_t target_freq_mhz, uint8_t expected_asic_count, uint32_t initial_difficulty) {
    if (!_initialized) {
        return false;
    }

    if (expected_asic_count == 0) {
        LOG_E("[asic.bm1366] bringup failed, asic_count=0");
        return false;
    }

    const uint8_t init4[] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0xA8, 0x00, 0x07, 0x00, 0x00, 0x03};
    _send_raw(init4, sizeof(init4));
    const uint8_t init5[] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0x18, 0xFF, 0x0F, 0xC1, 0x00, 0x00};
    _send_raw(init5, sizeof(init5));

    _set_chain_inactive();

    const uint8_t address_interval = static_cast<uint8_t>(256 / expected_asic_count);
    for (uint8_t i = 0; i < expected_asic_count; ++i) {
        _set_chip_address(static_cast<uint8_t>(i * address_interval));
    }

    const uint8_t init135[] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0x3C, 0x80, 0x00, 0x85, 0x40, 0x0C};
    _send_raw(init135, sizeof(init135));
    const uint8_t init136[] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0x3C, 0x80, 0x00, 0x80, 0x20, 0x19};
    _send_raw(init136, sizeof(init136));

    set_job_difficulty(initial_difficulty);

    const uint8_t init138[] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0x54, 0x00, 0x00, 0x00, 0x03, 0x1D};
    _send_raw(init138, sizeof(init138));
    const uint8_t init139[] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0x58, 0x02, 0x11, 0x11, 0x11, 0x06};
    _send_raw(init139, sizeof(init139));
    const uint8_t init171[] = {0x55, 0xAA, 0x41, 0x09, 0x00, 0x2C, 0x00, 0x7C, 0x00, 0x03, 0x03};
    _send_raw(init171, sizeof(init171));

    for (uint8_t i = 0; i < expected_asic_count; ++i) {
        const uint8_t addr = static_cast<uint8_t>(i * address_interval);
        const uint8_t set_a8_register[] = {addr, 0xA8, 0x00, 0x07, 0x01, 0xF0};
        _send_packet(static_cast<uint8_t>(kTypeCmd | kGroupSingle | kCmdWrite), set_a8_register, sizeof(set_a8_register));
        const uint8_t set_18_register[] = {addr, 0x18, 0xF0, 0x00, 0xC1, 0x00};
        _send_packet(static_cast<uint8_t>(kTypeCmd | kGroupSingle | kCmdWrite), set_18_register, sizeof(set_18_register));
        const uint8_t set_3c_register_first[] = {addr, 0x3C, 0x80, 0x00, 0x85, 0x40};
        _send_packet(static_cast<uint8_t>(kTypeCmd | kGroupSingle | kCmdWrite), set_3c_register_first, sizeof(set_3c_register_first));
        const uint8_t set_3c_register_second[] = {addr, 0x3C, 0x80, 0x00, 0x80, 0x20};
        _send_packet(static_cast<uint8_t>(kTypeCmd | kGroupSingle | kCmdWrite), set_3c_register_second, sizeof(set_3c_register_second));
        const uint8_t set_3c_register_third[] = {addr, 0x3C, 0x80, 0x00, 0x82, 0xAA};
        _send_packet(static_cast<uint8_t>(kTypeCmd | kGroupSingle | kCmdWrite), set_3c_register_third, sizeof(set_3c_register_third));
    }

    if (!_set_frequency(56.25f, static_cast<float>(target_freq_mhz))) {
        return false;
    }

    const uint8_t set_10_hash_counting[] = {0x00, 0x10, 0x00, 0x00, 0x15, 0x1C};
    _send_packet(static_cast<uint8_t>(kTypeCmd | kGroupAll | kCmdWrite), set_10_hash_counting, sizeof(set_10_hash_counting));
    const uint8_t init795[] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0xA4, 0x90, 0x00, 0xFF, 0xFF, 0x1C};
    _send_raw(init795, sizeof(init795));

    _change_uart_baud(_config.work_baud);
    clear_port_cache();
    _status.target_freq_mhz = target_freq_mhz;
    _status.current_difficulty = _difficulty_current;
    _status.bringup_complete = true;
    LOG_I("[asic.bm1366] bringup complete freq=%u diff=%lu chips=%u",
          static_cast<unsigned>(target_freq_mhz),
          static_cast<unsigned long>(_difficulty_current),
          static_cast<unsigned>(expected_asic_count));
    return true;
}

void Bm1366Asic::_reset_chip() {
    if (_config.reset_pin < 0) {
        return;
    }

    digitalWrite(_config.reset_pin, LOW);
    delay(50);
    digitalWrite(_config.reset_pin, HIGH);
    delay(20);
}

bool Bm1366Asic::_configure_uart() {
    if (_config.port == nullptr) {
        return false;
    }

    return _config.port->init(_config.init_baud, 20);
}

void Bm1366Asic::_configure_reset_pin() {
    if (_config.reset_pin < 0) {
        return;
    }

    pinMode(_config.reset_pin, OUTPUT);
    digitalWrite(_config.reset_pin, HIGH);
}

bool Bm1366Asic::clear_port_cache() {
    if (_config.port == nullptr) {
        return false;
    }

    _config.port->clear_rx();
    return true;
}

size_t Bm1366Asic::_send_raw(const uint8_t* data, size_t size) {
    if (_config.port == nullptr || data == nullptr || size == 0) {
        return 0;
    }

    const size_t written = _config.port->write(data, size);
    _config.port->flush();
    return written;
}

size_t Bm1366Asic::_receive(uint8_t* data, size_t size, uint32_t timeout_ms) {
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

void Bm1366Asic::_send_command_packet(uint8_t header, const uint8_t* data, uint8_t size) {
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

void Bm1366Asic::_send_packet(uint8_t header, const uint8_t* data, uint8_t size) {
    const bool job_packet = (header & kTypeJob) != 0;
    const uint8_t total_length = static_cast<uint8_t>(size + (job_packet ? 6 : 5));
    uint8_t buffer[96] = {};
    if (total_length > sizeof(buffer)) {
        LOG_E("[asic.bm1366] packet too large len=%u", static_cast<unsigned>(total_length));
        return;
    }

    buffer[0] = 0x55;
    buffer[1] = 0xAA;
    buffer[2] = header;
    buffer[3] = static_cast<uint8_t>(size + (job_packet ? 4 : 3));
    if (data != nullptr && size > 0) {
        memcpy(buffer + 4, data, size);
    }

    if (job_packet) {
        const uint16_t packet_crc = crc16_false(buffer + 2, static_cast<uint16_t>(size + 2));
        buffer[4 + size] = static_cast<uint8_t>((packet_crc >> 8) & 0xFF);
        buffer[5 + size] = static_cast<uint8_t>(packet_crc & 0xFF);
    } else {
        buffer[4 + size] = crc5(buffer + 2, static_cast<uint8_t>(size + 2));
    }
    _send_raw(buffer, total_length);
}

void Bm1366Asic::_set_chain_inactive() {
    const uint8_t payload[] = {0x00, 0x00};
    _send_packet(static_cast<uint8_t>(kTypeCmd | kGroupAll | kCmdInactive), payload, sizeof(payload));
}

void Bm1366Asic::_set_chip_address(uint8_t address) {
    const uint8_t payload[] = {address, 0x00};
    _send_packet(static_cast<uint8_t>(kTypeCmd | kGroupSingle | kCmdSetAddress), payload, sizeof(payload));
}

void Bm1366Asic::_set_version_mask(uint32_t version_mask) {
    const int versions_to_roll = static_cast<int>(version_mask >> 13);
    const uint8_t version_byte0 = static_cast<uint8_t>(versions_to_roll >> 8);
    const uint8_t version_byte1 = static_cast<uint8_t>(versions_to_roll & 0xFF);
    const uint8_t version_cmd[] = {0x00, 0xA4, 0x90, 0x00, version_byte0, version_byte1};
    _send_command_packet(static_cast<uint8_t>(kTypeCmd | kGroupAll | kCmdWrite), version_cmd, sizeof(version_cmd));
}

bool Bm1366Asic::_set_hash_frequency(float target_freq_mhz) {
    uint8_t freqbuf[] = {0x00, 0x08, 0x40, 0xA0, 0x02, 0x41};
    uint8_t fb_divider = 0;
    uint8_t post_divider1 = 0;
    uint8_t post_divider2 = 0;
    uint8_t ref_divider = 0;
    float min_difference = 10.0f;

    for (uint8_t refdiv_loop = 2; refdiv_loop > 0 && fb_divider == 0; --refdiv_loop) {
        for (uint8_t postdiv1_loop = 7; postdiv1_loop > 0 && fb_divider == 0; --postdiv1_loop) {
            for (uint8_t postdiv2_loop = 1; postdiv2_loop < postdiv1_loop && fb_divider == 0; ++postdiv2_loop) {
                const int temp_fb_divider =
                    round((postdiv1_loop * postdiv2_loop * target_freq_mhz * refdiv_loop) / 25.0f);
                if (temp_fb_divider >= 144 && temp_fb_divider <= 235) {
                    const float temp_freq =
                        25.0f * temp_fb_divider / (refdiv_loop * postdiv2_loop * postdiv1_loop);
                    const float freq_diff = fabs(target_freq_mhz - temp_freq);
                    if (freq_diff < min_difference) {
                        fb_divider = static_cast<uint8_t>(temp_fb_divider);
                        post_divider1 = postdiv1_loop;
                        post_divider2 = postdiv2_loop;
                        ref_divider = refdiv_loop;
                        min_difference = freq_diff;
                        break;
                    }
                }
            }
        }
    }

    if (fb_divider == 0) {
        LOG_W("[asic.bm1366] failed to find PLL settings for %.2fMHz", static_cast<double>(target_freq_mhz));
        return false;
    }

    freqbuf[3] = fb_divider;
    freqbuf[4] = ref_divider;
    freqbuf[5] = static_cast<uint8_t>((((post_divider1 - 1) & 0x0F) << 4) | ((post_divider2 - 1) & 0x0F));
    if (fb_divider * 25 / static_cast<float>(ref_divider) >= 2400.0f) {
        freqbuf[2] = 0x50;
    }
    _send_packet(static_cast<uint8_t>(kTypeCmd | kGroupAll | kCmdWrite), freqbuf, sizeof(freqbuf));
    return true;
}

bool Bm1366Asic::_set_frequency(float current_freq_mhz, float target_freq_mhz) {
    constexpr float kStepMhz = 6.25f;
    float current = current_freq_mhz;
    if (target_freq_mhz <= 0.0f) {
        return false;
    }
    if (fabs(target_freq_mhz - current) < 0.001f) {
        return true;
    }

    const float direction = target_freq_mhz > current ? kStepMhz : -kStepMhz;
    if (fmod(current, kStepMhz) != 0.0f) {
        current = direction > 0.0f ? ceil(current / kStepMhz) * kStepMhz : floor(current / kStepMhz) * kStepMhz;
        if (!_set_hash_frequency(current)) {
            return false;
        }
        delay(1);
    }

    while ((direction > 0.0f && current < target_freq_mhz) ||
           (direction < 0.0f && current > target_freq_mhz)) {
        const float next_step = fminf(fabs(direction), fabs(target_freq_mhz - current));
        current += direction > 0.0f ? next_step : -next_step;
        if (!_set_hash_frequency(current)) {
            return false;
        }
        delay(1);
    }

    if (!_set_hash_frequency(target_freq_mhz)) {
        return false;
    }
    LOG_W("[asic.bm1366] clock %.2fMHz -> %.2fMHz",
          static_cast<double>(current_freq_mhz),
          static_cast<double>(target_freq_mhz));
    return true;
}

void Bm1366Asic::_change_uart_baud(uint32_t baudrate) {
    const uint8_t baud_cmd[] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0x28, 0x11, 0x30, 0x02, 0x00, 0x03};
    _send_raw(baud_cmd, sizeof(baud_cmd));
    LOG_D("[asic.bm1366] set ASIC baudrate to %lu", static_cast<unsigned long>(baudrate));
    delay(500);
    if (_config.port != nullptr) {
        _config.port->set_baud(baudrate);
    }
}

uint32_t Bm1366Asic::set_job_difficulty(uint32_t difficulty) {
    if (difficulty == 0) {
        difficulty = 1;
    }
    uint8_t job_difficulty_mask[] = {0x00, kTicketMask, 0x00, 0x00, 0x00, 0xFF};
    const int diff_mask = largest_power_of_two(static_cast<int>(difficulty)) - 1;
    for (int i = 0; i < 4; ++i) {
        const uint8_t value = static_cast<uint8_t>((diff_mask >> (8 * i)) & 0xFF);
        job_difficulty_mask[5 - i] = reverse_bits(value);
    }

    _difficulty_current = static_cast<uint32_t>(diff_mask + 1);
    _status.current_difficulty = _difficulty_current;
    LOG_W("[asic.bm1366] Setting ASIC diff mask to %d", diff_mask);
    _send_packet(static_cast<uint8_t>(kTypeCmd | kGroupAll | kCmdWrite), job_difficulty_mask, sizeof(job_difficulty_mask));
    return _difficulty_current;
}

bool Bm1366Asic::send_work(const AsicJob& job) {
    AsicJob copy = job;
    copy.num_midstates = 0x01;
    _send_packet(static_cast<uint8_t>(kTypeJob | kGroupSingle | kCmdWrite),
                 reinterpret_cast<const uint8_t*>(&copy),
                 sizeof(copy));
    return true;
}

esp_err_t Bm1366Asic::wait_for_result(MinerResult& result, uint32_t timeout_ms) {
    uint8_t response[sizeof(AsicRawResult)] = {};
    const size_t received = _receive(response, sizeof(response), timeout_ms);
    if (received == 0) {
        return ESP_ERR_TIMEOUT;
    }
    if (received != sizeof(response)) {
        clear_port_cache();
        return ESP_ERR_INVALID_SIZE;
    }
    if (response[0] != 0xAA || response[1] != 0x55) {
        clear_port_cache();
        return ESP_ERR_INVALID_RESPONSE;
    }

    AsicRawResult asic{};
    memcpy(&asic, response, sizeof(asic));
    asic.job_id = asic.job_id & 0xF8;
    result.asic = asic;
    result.asic_id = static_cast<uint8_t>((asic.nonce & 0x0000FC00) >> 10);

    LOG_D("[asic.bm1366] ASIC[%u] nonce=0x%08lX job=%u version=0x%04X",
          static_cast<unsigned>(result.asic_id),
          static_cast<unsigned long>(result.asic.nonce),
          static_cast<unsigned>(result.asic.job_id),
          static_cast<unsigned>(result.asic.version));
    return ESP_OK;
}

}  // namespace nm::drivers
