#ifndef BM1366_H_
#define BM1366_H_
#include "bm_hal.h"

class BM1366: public BMxxx{
private:
    uint32_t _diff_current;
    void _send_bm1366(uint8_t header, uint8_t * data, uint8_t len);
    void _set_chip_address(uint8_t address);
    void _set_chain_inactive();
    void _set_version_mask(uint32_t version_mask);
    void _set_hash_frequency(float target_freq);
public:
    BM1366(HardwareSerial &sport, uint32_t init_baud, uint8_t rx, uint8_t tx, uint8_t rst):BMxxx(sport, init_baud, rx, tx, rst) {
        this->_diff_current = 0;
    }
    uint8_t init(uint64_t freq, int diff);
    void frequency_ramp_up(float target_frequency);
    void set_job_difficulty(int difficulty);
    uint32_t get_asic_difficulty();
    void send_work_to_asic(asic_job *job);
    esp_err_t wait_for_result(asic_result *result, uint32_t timeout_ms = 60*1000);
};
#endif