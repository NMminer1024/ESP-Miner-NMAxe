#ifndef BM1366_H_
#define BM1366_H_
#include "drivers/asic/bm_hal.h"

#define BM1366_CORE_COUNT       112
#define BM1366_SMALL_CORE_COUNT 894
#define BM1366_REG_POLL_ADDR    0x90

class BM1366: public BMxxx{
private:
    uint32_t _diff_current;
    uint8_t  _hcn_addr_interval = 255;
    uint32_t _hcn_tag_seen   = 0; // frames where rsp[7]==0x90 (reg-addr byte matched)
    uint32_t _hcn_decoded_ok = 0; // of those, frames that also matched rsp[8]/rsp[9]==0x00
    void _send_bm1366(uint8_t header, uint8_t * data, uint8_t len);
    void _set_chip_address(uint8_t address);
    void _set_chain_inactive();
    void _set_version_mask(uint32_t version_mask);
    bool _set_hash_frequency(float target_freq);
    // CMD_READ a 32-bit register from the chain (single-chip boards). Returns
    // false on timeout/garbage; value is big-endian like the 0x90 HCN frame.
    bool _read_register(uint8_t reg, uint32_t *value);
    // Ramp to target and verify PLL0(0x08) readback, re-ramping on mismatch
    // (every PLL write is fire-and-forget).
    bool _ramp_and_verify(float target_freq);
    // [DIAG] per-domain counters: 0x88..0x8B domains, 0x8C total, 0x4C errors
    uint32_t _dom_prev[6] = {0};
    uint32_t _dom_prev_ms[6] = {0};
    float    _dom_ghs[6] = {0};
    uint32_t _dom_log_ms = 0;
    bool _decode_domain_counter(const uint8_t *rsp);
public:
    BM1366(HardwareSerial &sport, uint32_t init_baud, uint8_t rx, uint8_t tx, uint8_t rst):BMxxx(sport, init_baud, rx, tx, rst) {
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
    void reset_hcn_register() override;
    bool decode_hcn_response_0x90(const uint8_t *rsp, asic_hcn_result *hcn) override;
    uint16_t get_cores() override;
    uint16_t get_small_cores() override;
    asic_rx_result wait_for_result(uint32_t timeout_ms = 60*1000) override;
    void get_hcn_diag_counts(uint32_t *tag_seen, uint32_t *decoded_ok) override {
        if (tag_seen)   *tag_seen   = this->_hcn_tag_seen;
        if (decoded_ok) *decoded_ok = this->_hcn_decoded_ok;
    }
};
#endif