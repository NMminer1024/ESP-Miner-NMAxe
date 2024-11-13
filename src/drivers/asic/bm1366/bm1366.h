#ifndef BM1366_H_
#define BM1366_H_
#include "bm_hal.h"

#define BM1366_DIFF_THR 512

#define TYPE_JOB 0x20
#define TYPE_CMD 0x40

#define GROUP_SINGLE 0x00
#define GROUP_ALL 0x10

#define CMD_JOB 0x01

#define CMD_SETADDRESS 0x00
#define CMD_WRITE 0x01
#define CMD_READ 0x02
#define CMD_INACTIVE 0x03

#define RESPONSE_CMD 0x00
#define RESPONSE_JOB 0x80

#define SLEEP_TIME 20
#define FREQ_MULT 25.0

#define CLOCK_ORDER_CONTROL_0 0x80
#define CLOCK_ORDER_CONTROL_1 0x84
#define ORDERED_CLOCK_ENABLE 0x20
#define CORE_REGISTER_CONTROL 0x3C
#define PLL3_PARAMETER 0x68
#define FAST_UART_CONFIGURATION 0x28
#define TICKET_MASK 0x14
#define MISC_CONTROL 0x18

typedef enum{
    JOB_PACKET = 0,
    CMD_PACKET = 1,
} packet_type_t;




class BM1366: public BMxxx{
private:
    uint32_t _diff_current;
    void _send_bm1366(uint8_t header, uint8_t * data, uint8_t len);
    void _set_chip_address(uint8_t address);
    void _set_chain_inactive();
    void _set_hash_frequency(float target_freq);
public:
    BM1366(HardwareSerial &sport, uint32_t init_baud, uint8_t rx, uint8_t tx, uint8_t rst):BMxxx(sport, init_baud, rx, tx, rst) {
        this->_diff_current = 0;
    }
    uint8_t init(uint64_t freq, int diff);
    void frequency_ramp_up();
    void set_job_difficulty(int difficulty);
    uint32_t get_asic_difficulty();
    void send_work_to_asic(asic_job *job);
    esp_err_t wait_for_result(asic_result *result, uint32_t timeout_ms = 60*1000);
};
#endif