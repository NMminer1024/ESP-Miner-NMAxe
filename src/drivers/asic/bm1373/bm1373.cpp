#include "bm1373.h"
#include "utils/logger/logger.h"
#include "utils/crc/crc.h"
#include "utils/helper.h"


void BM1373::_send_bm1373(uint8_t header, uint8_t * data, uint8_t len){
    packet_type_t packet_type = (header & TYPE_JOB) ? JOB_PACKET : CMD_PACKET;
    uint8_t total_length = (packet_type == JOB_PACKET) ? (len + 6) : (len + 5);
    uint8_t * buf = (uint8_t *)malloc(total_length); 
    
    // add the preamble
    buf[0] = 0x55;
    buf[1] = 0xAA;
    // add the header field
    buf[2] = header;
    // add the length field
    buf[3] = (packet_type == JOB_PACKET) ? (len + 4) : (len + 3);
    // add the data
    memcpy(buf + 4, data, len);
    // add the correct crc type
    if (packet_type == JOB_PACKET) {
        uint16_t crc16_total = crc16_false(buf + 2, len + 2);
        buf[4 + len] = (crc16_total >> 8) & 0xFF;
        buf[5 + len] = crc16_total & 0xFF;
    } else {
        buf[4 + len] = crc5(buf + 2, len + 2);
    }
    this->send(buf, total_length);
    free(buf);
}

void BM1373::_set_chain_inactive(){
    uint8_t read_address[] = {0x00,0x00};
    this->_send_bm1373((TYPE_CMD | GROUP_ALL | CMD_INACTIVE), read_address, 2);
}

void BM1373::_set_chip_address(uint8_t address){
    uint8_t chip_addr[] = {address,0x00};
    this->_send_bm1373((TYPE_CMD | GROUP_SINGLE | CMD_SETADDRESS), chip_addr, 2);
}

bool BM1373::_set_hash_frequency(int id, float target_freq, float max_diff){
    uint8_t freqbuf[6] = {0x00, 0x08, 0x40, 0xA0, 0x02, 0x41};
    uint8_t postdiv_min = 255;
    uint8_t postdiv2_min = 255;
    float best_freq = 0;
    uint8_t best_refdiv = 0, best_fbdiv = 0, best_postdiv1 = 0, best_postdiv2 = 0;

    for (uint8_t refdiv = 2; refdiv > 0; refdiv--) {
        for (uint8_t postdiv1 = 7; postdiv1 > 0; postdiv1--) {
            for (uint8_t postdiv2 = 7; postdiv2 > 0; postdiv2--) {
                uint16_t fb_divider = round(target_freq / 25.0 * (refdiv * postdiv2 * postdiv1));
                float newf = 25.0 * fb_divider / (refdiv * postdiv2 * postdiv1);
                
                if (fb_divider >= 0xa0 && fb_divider <= 0xef &&
                    fabs(target_freq - newf) < max_diff &&
                    postdiv1 >= postdiv2 &&
                    postdiv1 * postdiv2 < postdiv_min &&
                    postdiv2 <= postdiv2_min) {
                    
                    postdiv2_min = postdiv2;
                    postdiv_min = postdiv1 * postdiv2;
                    best_freq = newf;
                    best_refdiv = refdiv;
                    best_fbdiv = fb_divider;
                    best_postdiv1 = postdiv1;
                    best_postdiv2 = postdiv2;
                }
            }
        }
    }

    if (best_fbdiv == 0) {
        LOG_W("Failed to find PLL settings for target frequency %.2f", target_freq);
        return false;
    }

    freqbuf[0] = (id != -1) ? (id * 2) : freqbuf[0];
    freqbuf[1] = freqbuf[1];
    freqbuf[2] = (best_fbdiv * 25 / best_refdiv >= 2400) ? 0x50 : 0x40;
    freqbuf[3] = best_fbdiv;
    freqbuf[4] = best_refdiv;
    freqbuf[5] = (((best_postdiv1 - 1) & 0xf) << 4) | ((best_postdiv2 - 1) & 0xf);

    this->_send_bm1373(TYPE_CMD | GROUP_ALL | CMD_WRITE, freqbuf, 6);
    return true;
}

void BM1373::_set_version_mask(uint32_t version_mask) {
    int versions_to_roll = version_mask >> 13;
    uint8_t version_byte0 = (versions_to_roll >> 8);
    uint8_t version_byte1 = (versions_to_roll & 0xFF); 
    // BM1373 trace shows 0x80 here (BM1370 uses 0x90)
    uint8_t version_cmd[] = {0x00, 0xA4, 0x80, 0x00, 0xff, 0xff};
    this->_send_bm1373(TYPE_CMD | GROUP_ALL | CMD_WRITE, version_cmd, 6);
}

uint32_t BM1373::set_job_difficulty(int difficulty){
    uint8_t job_difficulty_mask[9] = {0x00, TICKET_MASK, 0b00000000, 0b00000000, 0b00000000, 0b11111111};

    int diff_mask = largest_power_of_two(difficulty) - 1;

    for (int i = 0; i < 4; i++) {
        char value = (diff_mask >> (8 * i)) & 0xFF;
        job_difficulty_mask[5 - i] = reverse_bits(value);
    }
    this->_diff_current = diff_mask + 1;
    LOG_I("Setting ASIC diff mask to %d", diff_mask);
    this->_send_bm1373((TYPE_CMD | GROUP_ALL | CMD_WRITE), job_difficulty_mask, 6);
    return this->_diff_current;
}

uint32_t BM1373::get_asic_difficulty(){
    return this->_diff_current;
}

void BM1373::frequency_ramp_up(float target_frequency){
    this->set_frequency(56.25, target_frequency);
}

bool BM1373::set_frequency(float current_frequency, float target_frequency){
    float current = current_frequency;
    float step    = 6.25;

    if (target_frequency == 0) {
        LOG_W("Skipping frequency ramp");
        return false;
    }

    if (fabs(target_frequency - current) < 0.001f) {
        LOG_W("Clock frequency already at %.2fMHz", target_frequency);
        return true;
    }

    LOG_I("Ramping frequency from %.2f MHz to %.2f MHz with step %.2f MHz", current, target_frequency, step);

    float direction = (target_frequency > current) ? step : -step;

    if (fmod(current, step) != 0) {
        float next_dividable;
        if (direction > 0) {
            next_dividable = ceil(current / step) * step;
        } else {
            next_dividable = floor(current / step) * step;
        }
        current = next_dividable;
        if (!this->_set_hash_frequency(-1, current, 0.002)) return false;
        delay(1);
    }

    while ((direction > 0 && current < target_frequency) || (direction < 0 && current > target_frequency)) {
        float next_step = fminf(fabs(direction), fabs(target_frequency - current));
        current += direction > 0 ? next_step : -next_step;
        if (!this->_set_hash_frequency(-1, current, 0.002)) return false;
        delay(1);
    }
    if (!this->_set_hash_frequency(-1, target_frequency, 0.002)) return false;
    return true;
}

void BM1373::change_uart_baud(uint32_t baudrate){
    uint8_t init_baud[] = {0x00, 0x28, 0x11, 0x30, 0x02, 0x00};
    this->_send_bm1373((TYPE_CMD | GROUP_ALL | CMD_WRITE), init_baud, 6);
    LOG_D("set ASIC baudrate to %d, wait 500ms...", baudrate);
    delay(500);
    BMxxx::change_uart_baud(baudrate);
}

uint8_t BM1373::get_asic_count(){
    for (int i = 0; i < 4; i++) {
        this->_set_version_mask(ASIC_DEFAULT_VSERSION_MASK);
    }
    
    // read chip responses - BM1373 ID is 0x1372
    uint8_t init3[7] = {0x55, 0xAA, 0x52, 0x05, 0x00, 0x00, 0x0A};
    this->send(init3, 7);
    uint8_t chip_counter = 0, rsp[128] = {0,};
    while (true) {
        memset(rsp, 0, sizeof(rsp));
        uint8_t len = this->receive(rsp, sizeof(rsp), 1000);
        if(len == 0) break;
        uint8_t *rsp_ptr = rsp;
        while (rsp_ptr <= rsp + len - 11) {
            if(memcmp(rsp_ptr, "\xaa\x55\x13\x72\x00\x00\x00\x00\x00\x00\x07", 11) == 0){
                chip_counter++;
            }
            rsp_ptr++;
        }
    }

    return chip_counter;
}

void BM1373::init(uint64_t freq, int diff, uint8_t asic_count){
    // 1. Set version mask (one extra after get_asic_count's 4)
    this->_set_version_mask(ASIC_DEFAULT_VSERSION_MASK);

    // 2. Register A8 - all chips (matching trace: GROUP_ALL)
    uint8_t init4[] = {0x00, 0xA8, 0x00, 0x07, 0x00, 0x00};
    this->_send_bm1373((TYPE_CMD | GROUP_ALL | CMD_WRITE), init4, 6);

    // 3. Register 18 - all chips (BM1373 uses 0xFF, not 0xF0 like BM1370)
    uint8_t init5[] = {0x00, 0x18, 0xFF, 0x00, 0xC1, 0x00};
    this->_send_bm1373((TYPE_CMD | GROUP_ALL | CMD_WRITE), init5, 6);

    // 4. Set chain inactive
    this->_set_chain_inactive();

    // 5. Set chip addresses - BM1373 uses interval 16
    uint8_t address_interval = 16;
    for (uint8_t i = 0; i < asic_count; i++) {
        this->_set_chip_address(i * address_interval);
    }

    // 6. Register 3C - all chips (only one write, BM1370 has two here)
    uint8_t init3c[] = {0x00, 0x3C, 0x80, 0x00, 0x80, 0x0C};
    this->_send_bm1373((TYPE_CMD | GROUP_ALL | CMD_WRITE), init3c, 6);

    // 7. Set job difficulty
    this->set_job_difficulty(diff);

    // 8. IO Driver Strength - all chips
    uint8_t init58[] = {0x00, 0x58, 0x00, 0x01, 0x11, 0x11};
    this->_send_bm1373((TYPE_CMD | GROUP_ALL | CMD_WRITE), init58, 6);

    // 9. PLL3 parameter - all chips
    uint8_t init68[] = {0x00, 0x68, 0x5A, 0xA5, 0x5A, 0xA5};
    this->_send_bm1373((TYPE_CMD | GROUP_ALL | CMD_WRITE), init68, 6);

    // 10. Per-chip register configuration
    // BM1373: per-chip writes use interval 4 (0, 4, 8, 12) for the chip address in data
    // Note: only 2 registers writes to 3C (BM1370 has 3)
    for (uint8_t i = 0; i < asic_count; i++) {
        uint8_t set_a8_register[6] = {(uint8_t)(i * 4), 0xA8, 0x00, 0x07, 0x01, 0xF0};
        this->_send_bm1373((TYPE_CMD | GROUP_SINGLE | CMD_WRITE), set_a8_register, 6);
        uint8_t set_18_register[6] = {(uint8_t)(i * 4), 0x18, 0xFF, 0x00, 0xC1, 0x00};
        this->_send_bm1373((TYPE_CMD | GROUP_SINGLE | CMD_WRITE), set_18_register, 6);
        uint8_t set_3c_register_first[6] = {(uint8_t)(i * 4), 0x3C, 0x80, 0x00, 0x80, 0x0C};
        this->_send_bm1373((TYPE_CMD | GROUP_SINGLE | CMD_WRITE), set_3c_register_first, 6);
        uint8_t set_3c_register_second[6] = {(uint8_t)(i * 4), 0x3C, 0x80, 0x00, 0x82, 0xAA};
        this->_send_bm1373((TYPE_CMD | GROUP_SINGLE | CMD_WRITE), set_3c_register_second, 6);
    }

    // 11. Register 54 - all chips
    uint8_t init54[] = {0x00, 0x54, 0x00, 0x00, 0x00, 0x02};
    this->_send_bm1373((TYPE_CMD | GROUP_ALL | CMD_WRITE), init54, 6);

    // 12. Frequency ramp up
    this->frequency_ramp_up((float)freq);

    // 13. Hash counting register 10
    uint8_t set_10_hash_counting[6] = {0x00, 0x10, 0x00, 0x00, 0x1E, 0xB5};
    this->_send_bm1373((TYPE_CMD | GROUP_ALL | CMD_WRITE), set_10_hash_counting, 6);

    // 14. Final version mask
    uint8_t init_a4_final[6] = {0x00, 0xA4, 0x90, 0x00, 0xFF, 0xFF};
    this->_send_bm1373((TYPE_CMD | GROUP_ALL | CMD_WRITE), init_a4_final, 6);

    // 15. Baud rate register setup (matching trace end)
    uint8_t init_baud[] = {0x00, 0x28, 0x11, 0x30, 0x02, 0x00};
    this->_send_bm1373((TYPE_CMD | GROUP_ALL | CMD_WRITE), init_baud, 6);
}

void BM1373::send_work_to_asic(asic_job *job){
    job->num_midstates = 0x01;
    this->_send_bm1373((TYPE_JOB | GROUP_SINGLE | CMD_WRITE), (uint8_t*)job, sizeof(asic_job));
}

esp_err_t BM1373::wait_for_result(miner_result *result, uint32_t timeout_ms){
    uint8_t rsp[11] = {0,};
    uint16_t len = this->receive(rsp, sizeof(rsp), timeout_ms);
    if(len == 0) return ESP_ERR_TIMEOUT;

    if(len != 11){
        LOG_W("Invalid asic response length: %d", len);
        this->clear_port_cache();
        return ESP_ERR_INVALID_SIZE;
    }
    if(rsp[0] != 0xAA || rsp[1] != 0x55){
        LOG_W("Invalid asic response preamble: %02X %02X", rsp[0], rsp[1]);
        this->clear_port_cache();
        return ESP_ERR_INVALID_RESPONSE;
    }

    asic_result asic  = *(asic_result*)(rsp);

    dbg::hex_print((uint8_t*)rsp, sizeof(rsp), "asic rsp");

    asic.job_id       = (asic.job_id & 0xf0) >> 1; // upper 4 bits are job id for BM137x
    int asic_id       = (uint8_t) ((asic.nonce & 0x0000fc00) >> 11);

    result->asic      = asic;
    result->asic_id   = asic_id;

    LOG_D("ASIC[%d] found nonce: 0x%08X (job id: %d, version: 0x%04X)", 
          result->asic_id, 
          result->asic.nonce, 
          result->asic.job_id, 
          result->asic.version);

    return ESP_OK;
}

uint16_t BM1373::get_cores(){
    return BM1373_CORE_COUNT;
}   

uint16_t BM1373::get_small_cores(){
    return BM1373_SMALL_CORE_COUNT;
}
