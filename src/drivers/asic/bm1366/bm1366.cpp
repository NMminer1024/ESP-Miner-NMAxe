#include "bm1366.h"
#include "utils/logger/logger.h"
#include "utils/crc/crc.h"
#include "utils/helper.h"


void BM1366::_send_bm1366(uint8_t header, uint8_t * data, uint8_t len){
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

void BM1366::_set_chain_inactive(){
    uint8_t read_address[] = {0x00,0x00};

    this->_send_bm1366((TYPE_CMD | GROUP_ALL | CMD_INACTIVE), read_address, 2);
}

void BM1366::_set_chip_address(uint8_t address){
    uint8_t read_address[] = {address,0x00};
    this->_send_bm1366((TYPE_CMD | GROUP_SINGLE | CMD_SETADDRESS), read_address, 2);
}

bool BM1366::_set_hash_frequency(float target_freq){
    // default 200Mhz if it fails
    unsigned char freqbuf[9] = {0x00, 0x08, 0x40, 0xA0, 0x02, 0x41}; // freqbuf - pll0_parameter
    float newf = 200.0;

    uint8_t fb_divider = 0;
    uint8_t post_divider1 = 0, post_divider2 = 0;
    uint8_t ref_divider = 0;
    float min_difference = 10;

    // refdiver is 2 or 1
    // postdivider 2 is 1 to 7
    // postdivider 1 is 1 to 7 and less than postdivider 2
    // fbdiv is 144 to 235
    for (uint8_t refdiv_loop = 2; refdiv_loop > 0 && fb_divider == 0; refdiv_loop--) {
        for (uint8_t postdiv1_loop = 7; postdiv1_loop > 0 && fb_divider == 0; postdiv1_loop--) {
            for (uint8_t postdiv2_loop = 1; postdiv2_loop < postdiv1_loop && fb_divider == 0; postdiv2_loop++) {
                int temp_fb_divider = round(((float) (postdiv1_loop * postdiv2_loop * target_freq * refdiv_loop) / 25.0));

                if (temp_fb_divider >= 144 && temp_fb_divider <= 235) {
                    float temp_freq = 25.0 * (float) temp_fb_divider / (float) (refdiv_loop * postdiv2_loop * postdiv1_loop);
                    float freq_diff = fabs(target_freq - temp_freq);

                    if (freq_diff < min_difference) {
                        fb_divider = temp_fb_divider;
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
        LOG_W("Failed to find PLL settings for target frequency %.2f", target_freq);
        return false;
    } else {
        newf = 25.0 * (float) (fb_divider) / (float) (ref_divider * post_divider1 * post_divider2);
        // LOG_I("final refdiv: %d, fbdiv: %d, postdiv1: %d, postdiv2: %d, min diff value: %f", ref_divider, fb_divider,
            //    post_divider1, post_divider2, min_difference);

        freqbuf[3] = fb_divider;
        freqbuf[4] = ref_divider;
        freqbuf[5] = (((post_divider1 - 1) & 0xf) << 4) + ((post_divider2 - 1) & 0xf);

        if (fb_divider * 25 / (float) ref_divider >= 2400) {
            freqbuf[2] = 0x50;
        }
    }

    this->_send_bm1366((TYPE_CMD | GROUP_ALL | CMD_WRITE), freqbuf, 6);

    // LOG_W("Setting clock frequency to %.2fMHz (%.2f)", target_freq, newf);
    return true;
}

void BM1366::_set_version_mask(uint32_t version_mask) {
    int versions_to_roll = version_mask >> 13;
    uint8_t version_byte0 = (versions_to_roll >> 8);
    uint8_t version_byte1 = (versions_to_roll & 0xFF); 
    uint8_t version_cmd[] = {0x00, 0xA4, 0x90, 0x00, version_byte0, version_byte1};
    this->_send_bm1366(TYPE_CMD | GROUP_ALL | CMD_WRITE, version_cmd, 6);
}

uint32_t BM1366::set_job_difficulty(int difficulty){
    // Default mask of 256 diff
    uint8_t job_difficulty_mask[9] = {0x00, TICKET_MASK, 0b00000000, 0b00000000, 0b00000000, 0b11111111};

    // The mask must be a power of 2 so there are no holes
    // Correct:  {0b00000000, 0b00000000, 0b11111111, 0b11111111}
    // Incorrect: {0b00000000, 0b00000000, 0b11100111, 0b11111111}
    // (difficulty - 1) if it is a pow 2 then step down to second largest for more hashrate sampling
    int diff_mask = largest_power_of_two(difficulty) - 1;

    // convert difficulty into char array
    // Ex: 256 = {0b00000000, 0b00000000, 0b00000000, 0b11111111}, {0x00, 0x00, 0x00, 0xff}
    // Ex: 512 = {0b00000000, 0b00000000, 0b00000001, 0b11111111}, {0x00, 0x00, 0x01, 0xff}
    for (int i = 0; i < 4; i++) {
        char value = (diff_mask >> (8 * i)) & 0xFF;
        // The char is read in backwards to the register so we need to reverse them
        // So a mask of 512 looks like 0b00000000 00000000 00000001 1111111
        // and not 0b00000000 00000000 10000000 1111111

        job_difficulty_mask[5 - i] = reverse_bits(value);
    }
    this->_diff_current = diff_mask + 1;
    LOG_W("Setting ASIC diff mask to %d", diff_mask);
    this->_send_bm1366((TYPE_CMD | GROUP_ALL | CMD_WRITE), job_difficulty_mask, 6);
    return this->_diff_current;
}

uint32_t BM1366::get_asic_difficulty(){
    return this->_diff_current;
}


uint8_t BM1366::get_asic_count(){
    for (int i = 0; i < 3; i++) {
        this->_set_version_mask(ASIC_DEFAULT_VSERSION_MASK);
    }
    
    // read chip responses
    uint8_t init3[7] = {0x55, 0xAA, 0x52, 0x05, 0x00, 0x00, 0x0A};
    this->send(init3, 7);
    uint8_t chip_counter = 0, rsp[100] = {0,};
    while (true) {
        uint8_t len = this->receive(rsp, sizeof(rsp), 1000);
        if(len == 0) break;
        // dbg::hex_print(rsp, len, "asic rsp");
        uint8_t *rsp_ptr = rsp;
        while (rsp_ptr <= rsp + len - 11) {
            if(memcmp(rsp_ptr, "\xaa\x55\x13\x66\x00\x00", 6) == 0){
                // dbg::hex_print(rsp_ptr, 11, "found chip");
                chip_counter++;
                break;
            }
            else{
                rsp_ptr++;
            }
        }
    }

    return chip_counter;
}


void BM1366::frequency_ramp_up(float target_frequency){
    this->set_frequency(56.25, target_frequency);
}

bool BM1366::set_frequency(float current_frequency, float target_frequency){
    float step = 6.25;
    float current = current_frequency;
    float target = target_frequency;

    if (target <= 0) {
        LOG_W("Skipping invalid frequency target %.2fMHz", target);
        return false;
    }

    if (fabs(target - current) < 0.001f) {
        LOG_W("Clock frequency already at %.2fMHz", target);
        return true;
    }

    float direction = (target > current) ? step : -step;

    if (fmod(current, step) != 0) {
        float next_dividable;
        if (direction > 0) {
            next_dividable = ceil(current / step) * step;
        } else {
            next_dividable = floor(current / step) * step;
        }
        current = next_dividable;
        if (!this->_set_hash_frequency(current)) return false;
        delay(1);
    }

    while ((direction > 0 && current < target) || (direction < 0 && current > target)) {
        float next_step = fmin(fabs(direction), fabs(target - current));
        current += direction > 0 ? next_step : -next_step;
        if (!this->_set_hash_frequency(current)) return false;
        delay(1);
    }
    if (!this->_set_hash_frequency(target)) return false;
    LOG_W("Setting clock frequency from %.2fMHz to %.2fMHz", current_frequency, target);
    return true;
}


void BM1366::change_uart_baud(uint32_t baudrate){
    // set baud rate 
    uint8_t init173[11] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0x28, 0x11, 0x30, 0x02, 0x00, 0x03};
    this->send(init173, 11);
    LOG_D("set ASIC baudrate to %d, wait 500ms...", baudrate);
    delay(500);
    BMxxx::change_uart_baud(baudrate);
}

void BM1366::init(uint64_t freq, int diff, uint8_t asic_count){

    uint8_t init4[11] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0xA8, 0x00, 0x07, 0x00, 0x00, 0x03};
    this->send(init4, 11);

    uint8_t init5[11] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0x18, 0xFF, 0x0F, 0xC1, 0x00, 0x00};
    this->send(init5, 11);

    this->_set_chain_inactive();

    // Assign unique addresses to each chip on the daisy-chain SPI bus.
    // BM1366 splits the 256-wide address space evenly: interval = 256 / asic_count.
    // Each chip reserves multiple address slots for internal sub-unit
    // (core-domain / PLL group) selection within a single chip.
    uint8_t address_interval = (uint8_t) (256 / asic_count);
    for (uint8_t i = 0; i < asic_count; i++) {
      this->_set_chip_address(i * address_interval);
    }

    uint8_t init135[11] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0x3C, 0x80, 0x00, 0x85, 0x40, 0x0C};
    this->send(init135, 11);

    uint8_t init136[11] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0x3C, 0x80, 0x00, 0x80, 0x20, 0x19};
    this->send(init136, 11);

    this->set_job_difficulty(diff);//BM1366_set_job_difficulty_mask(BM1366_DIFF_THR);

    uint8_t init138[11] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0x54, 0x00, 0x00, 0x00, 0x03, 0x1D};
    this->send(init138, 11);

    uint8_t init139[11] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0x58, 0x02, 0x11, 0x11, 0x11, 0x06};
    this->send(init139, 11);

    uint8_t init171[11] = {0x55, 0xAA, 0x41, 0x09, 0x00, 0x2C, 0x00, 0x7C, 0x00, 0x03, 0x03};
    this->send(init171, 11);

    // //set BM1366 baudrate 
    // uint8_t init173[11] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0x28, 0x11, 0x30, 0x02, 0x00, 0x03};
    // this->send(init173, 11);

    for (uint8_t i = 0; i < asic_count; i++) {
        uint8_t set_a8_register[6] = {(uint8_t)(i * address_interval), 0xA8, 0x00, 0x07, 0x01, 0xF0};
        this->_send_bm1366((TYPE_CMD | GROUP_SINGLE | CMD_WRITE), set_a8_register, 6);
        uint8_t set_18_register[6] = {(uint8_t)(i * address_interval), 0x18, 0xF0, 0x00, 0xC1, 0x00};
        this->_send_bm1366((TYPE_CMD | GROUP_SINGLE | CMD_WRITE), set_18_register, 6);
        uint8_t set_3c_register_first[6] = {(uint8_t)(i * address_interval), 0x3C, 0x80, 0x00, 0x85, 0x40};
        this->_send_bm1366((TYPE_CMD | GROUP_SINGLE | CMD_WRITE), set_3c_register_first, 6);
        uint8_t set_3c_register_second[6] = {(uint8_t)(i * address_interval), 0x3C, 0x80, 0x00, 0x80, 0x20};
        this->_send_bm1366((TYPE_CMD | GROUP_SINGLE | CMD_WRITE), set_3c_register_second, 6);
        uint8_t set_3c_register_third[6] = {(uint8_t)(i * address_interval), 0x3C, 0x80, 0x00, 0x82, 0xAA};
        this->_send_bm1366((TYPE_CMD | GROUP_SINGLE | CMD_WRITE), set_3c_register_third, 6);
    }

    this->frequency_ramp_up((float)freq);//do_frequency_ramp_up();

    // this->_set_hash_frequency(freq);//BM1366_send_hash_frequency(frequency);

    //register 10 is still a bit of a mystery. discussion: https://github.com/skot/ESP-Miner/pull/167

    // uint8_t set_10_hash_counting[6] = {0x00, 0x10, 0x00, 0x00, 0x11, 0x5A}; //S19k Pro Default
    // uint8_t set_10_hash_counting[6] = {0x00, 0x10, 0x00, 0x00, 0x14, 0x46}; //S19XP-Luxos Default
    uint8_t set_10_hash_counting[6] = {0x00, 0x10, 0x00, 0x00, 0x15, 0x1C}; //S19XP-Stock Default
    // uint8_t set_10_hash_counting[6] = {0x00, 0x10, 0x00, 0x0F, 0x00, 0x00}; //supposedly the "full" 32bit nonce range
    this->_send_bm1366((TYPE_CMD | GROUP_ALL | CMD_WRITE), set_10_hash_counting, 6);

    uint8_t init795[11] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0xA4, 0x90, 0x00, 0xFF, 0xFF, 0x1C};
    this->send(init795, 11);
}

void BM1366::send_work_to_asic(asic_job *job){
    job->num_midstates = 0x01;
    this->_send_bm1366((TYPE_JOB | GROUP_SINGLE | CMD_WRITE), (uint8_t*)job, sizeof(asic_job));
}

esp_err_t BM1366::wait_for_result(miner_result *result, uint32_t timeout_ms){
    uint8_t rsp[11] = {0,};
    uint16_t len = this->receive(rsp, sizeof(rsp), timeout_ms);
    if(len == 0) return ESP_ERR_TIMEOUT;

    if(len != 11){
        this->clear_port_cache();
        return ESP_ERR_INVALID_SIZE;
    }
    if(rsp[0] != 0xAA && rsp[1] != 0x55){
        this->clear_port_cache();
        return ESP_ERR_INVALID_RESPONSE;
    }

    asic_result asic;
    asic = *(asic_result*)(rsp);
    asic.job_id     = asic.job_id & 0xf8;// upper 5 bits are job id for BM1366

    result->asic    = asic;
    result->asic_id = (uint8_t) ((asic.nonce & 0x0000fc00) >> 10);

    LOG_D("ASIC[%d] found nonce: 0x%08X (job id: %d, version: 0x%04X)", 
          result->asic_id, 
          result->asic.nonce, 
          result->asic.job_id, 
          result->asic.version);

    // /* logic from project bitaxe: https://github.com/skot/bitaxe */
    // /* Thanks for their efforts on this project */
    // uint32_t core_id   =    ((result->nonce >> 24) & 0xff) |       // Move byte 3 to byte 0
    //                         ((result->nonce  << 8) & 0xff0000) |   // Move byte 1 to byte 2
    //                         ((result->nonce  >> 8) & 0xff00) |     // Move byte 2 to byte 1
    //                         ((result->nonce  << 24)& 0xff000000);  // Move byte 0 to byte 3
    // core_id = (uint8_t)((core_id >> 25) & 0x7f);
    // uint8_t small_core = result->job_id & 0x07;


    return ESP_OK;
}

uint16_t BM1366::get_cores(){
    return BM1366_CORE_COUNT;
}   
uint16_t BM1366::get_small_cores(){
    return BM1366_SMALL_CORE_COUNT;
}