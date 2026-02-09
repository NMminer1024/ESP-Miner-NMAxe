#ifndef BM1370_H_
#define BM1370_H_
#include "bm_hal.h"

#define BM1370_CORE_COUNT       128
#define BM1370_SMALL_CORE_COUNT 2040

class BM1370: public BMxxx{
private:
    uint32_t _diff_current;
    void _send_bm1370(uint8_t header, uint8_t * data, uint8_t len);
    void _set_chip_address(uint8_t address);
    void _set_chain_inactive();
    void _set_version_mask(uint32_t version_mask);
    void _set_hash_frequency(int id, float target_freq, float max_diff);
public:
    BM1370(HardwareSerial &sport, uint32_t init_baud, uint8_t rx, uint8_t tx, uint8_t rst):BMxxx(sport, init_baud, rx, tx, rst) {
        this->_diff_current = 0;
    }
    void init(uint64_t freq, int diff, uint8_t asic_count);
    void change_uart_baud(uint32_t baudrate);
    void frequency_ramp_up(float target_frequency);
    uint32_t set_job_difficulty(int difficulty);
    uint8_t get_asic_count();
    uint32_t get_asic_difficulty();
    void send_work_to_asic(asic_job *job);
    uint16_t get_cores();
    uint16_t get_small_cores();
    esp_err_t wait_for_result(miner_result *result, uint32_t timeout_ms = 60*1000);
};
#endif