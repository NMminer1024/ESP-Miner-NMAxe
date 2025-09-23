#ifndef BM_HAL_H_
#define BM_HAL_H_
#include <Arduino.h>

#define ASIC_DEFAULT_VSERSION_MASK  (0x1fffe000)

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

typedef struct{
    uint8_t id;
    uint8_t num_midstates;
    uint8_t starting_nonce[4];
    uint8_t nbits[4];
    uint8_t ntime[4];
    uint8_t merkle_root[32];
    uint8_t prev_block_hash[32];
    uint8_t version[4];
}asic_job;

typedef struct __attribute__((__packed__)){
    uint8_t     preamble[2];
    uint32_t    nonce;
    uint8_t     midstate_num;
    uint8_t     job_id;
    uint16_t    version;
    uint8_t     crc;
} asic_result;


class BMxxx{
private:
    HardwareSerial& _serial;
    uint8_t         _rst_pin;
    uint8_t         _rx_pin;
    uint8_t         _tx_pin;
public:
    BMxxx(HardwareSerial &port, uint32_t init_baud, uint8_t rx, uint8_t tx, uint8_t rst);
    ~BMxxx();

    void reset();
    void change_uart_baud(uint32_t baudrate);
    size_t send(uint8_t *cmd, uint16_t len);
    size_t receive(uint8_t *buf, uint16_t len, uint32_t timeout_ms);
    bool clear_port_cache();
    virtual uint8_t init(uint64_t freq, int diff) = 0;
    virtual void frequency_ramp_up(float target_frequency) = 0;
    virtual void set_job_difficulty(int difficulty) = 0;
    virtual uint32_t get_asic_difficulty() = 0;
    virtual void send_work_to_asic(asic_job *job) = 0;
    virtual esp_err_t wait_for_result(asic_result *result, uint32_t timeout_ms) = 0;
    virtual uint16_t get_cores() = 0;
    virtual uint16_t get_small_cores() = 0;
};
#endif