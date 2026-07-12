#ifndef BM1373_H_
#define BM1373_H_
#include "drivers/asic/bm_hal.h"

#define BM1373_CORE_COUNT       128
// Derived from expected-hashrate measurements at 400/600/740 MHz:
// 2764.8 / 4147.2 / 5114.88 GH/s => exactly 6912 small cores (128 x 54).
#define BM1373_SMALL_CORE_COUNT 6912
// Periodic hash-counter register poll address.
#define BM1373_REG_POLL_ADDR    0x90
// Daisy-chain address step. Each chip is assigned SETADDRESS = i * interval,
// and the chip stamps its core/chip address into nonce bits[17:24]
// (big-endian view). The chip index occupies the top log2(asic_count) bits of
// that field, so it is recovered as address_field / (256 / asic_count).
// SETADDRESS assignment in init() uses BM1373_ADDR_INTERVAL and is independent
// of the decode divisor below.
#define BM1373_ADDR_INTERVAL    16

class BM1373: public BMxxx{
private:
    uint32_t _diff_current;
    uint8_t  _asic_count = 1;   // chips detected at init; used to decode asic_id
    void _send_bm1373(uint8_t header, uint8_t * data, uint8_t len);
    void _set_chip_address(uint8_t address);
    void _set_chain_inactive();
    void _set_version_mask(uint32_t version_mask);
    bool _set_hash_frequency(int id, float target_freq);
public:
    BM1373(HardwareSerial &sport, uint32_t init_baud, uint8_t rx, uint8_t tx, uint8_t rst):BMxxx(sport, init_baud, rx, tx, rst) {
        this->_diff_current = 0;
    }
    void init(uint64_t freq, int diff, uint8_t asic_count) override;
    void change_uart_baud(uint32_t baudrate) override;
    void frequency_ramp_up(float target_frequency) override;
    bool set_frequency(float current_frequency, float target_frequency) override;
    uint32_t set_job_difficulty(int difficulty) override;
    uint8_t get_asic_count() override;
    uint32_t get_asic_difficulty() override;
    void send_work_to_asic(asic_job *job) override;
    void poll_hcn_register() override;
    bool decode_hcn_response_0x90(const uint8_t *rsp, asic_hcn_result *hcn) override;
    uint16_t get_cores() override;
    uint16_t get_small_cores() override;
    asic_rx_result wait_for_result(uint32_t timeout_ms = 60*1000) override;
};
#endif
