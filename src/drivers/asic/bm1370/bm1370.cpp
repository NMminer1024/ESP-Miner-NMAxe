#include "bm1370.h"
#include "logger.h"
#include "crc.h"
#include "helper.h"


void BM1370::_send_bm1370(uint8_t header, uint8_t * data, uint8_t len){
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

void BM1370::_set_chain_inactive(){
    uint8_t read_address[] = {0x00,0x00};

    this->_send_bm1370((TYPE_CMD | GROUP_ALL | CMD_INACTIVE), read_address, 2);
}

void BM1370::_set_chip_address(uint8_t address){
    uint8_t read_address[] = {address,0x00};
    this->_send_bm1370((TYPE_CMD | GROUP_SINGLE | CMD_SETADDRESS), read_address, 2);
}

void BM1370::_set_hash_frequency(float target_freq){
// void BM1370::_set_hash_frequency(int id, float target_freq, float max_diff){
    // uint8_t freqbuf[6] = {0x00, 0x08, 0x40, 0xA0, 0x02, 0x41};
    // uint8_t postdiv_min = 255;
    // uint8_t postdiv2_min = 255;
    // float best_freq = 0;
    // uint8_t best_refdiv = 0, best_fbdiv = 0, best_postdiv1 = 0, best_postdiv2 = 0;

    // for (uint8_t refdiv = 2; refdiv > 0; refdiv--) {
    //     for (uint8_t postdiv1 = 7; postdiv1 > 0; postdiv1--) {
    //         for (uint8_t postdiv2 = 7; postdiv2 > 0; postdiv2--) {
    //             uint16_t fb_divider = round(target_freq / 25.0 * (refdiv * postdiv2 * postdiv1));
    //             float newf = 25.0 * fb_divider / (refdiv * postdiv2 * postdiv1);
                
    //             if (fb_divider >= 0xa0 && fb_divider <= 0xef &&
    //                 fabs(target_freq - newf) < max_diff &&
    //                 postdiv1 >= postdiv2 &&
    //                 postdiv1 * postdiv2 < postdiv_min &&
    //                 postdiv2 <= postdiv2_min) {
                    
    //                 postdiv2_min = postdiv2;
    //                 postdiv_min = postdiv1 * postdiv2;
    //                 best_freq = newf;
    //                 best_refdiv = refdiv;
    //                 best_fbdiv = fb_divider;
    //                 best_postdiv1 = postdiv1;
    //                 best_postdiv2 = postdiv2;
    //             }
    //         }
    //     }
    // }

    // if (best_fbdiv == 0) {
    //     LOG_E("Failed to find PLL settings for target frequency %.2f", target_freq);
    //     return;
    // }

    // freqbuf[2] = (best_fbdiv * 25 / best_refdiv >= 2400) ? 0x50 : 0x40;
    // freqbuf[3] = best_fbdiv;
    // freqbuf[4] = best_refdiv;
    // freqbuf[5] = (((best_postdiv1 - 1) & 0xf) << 4) | ((best_postdiv2 - 1) & 0xf);

    // if (id != -1) {
    //     freqbuf[0] = id * 2;
    //     this->_send_bm1370(TYPE_CMD | GROUP_SINGLE | CMD_WRITE, freqbuf, 6);
    // } else {
    //     this->_send_bm1370(TYPE_CMD | GROUP_ALL | CMD_WRITE, freqbuf, 6);
    // }
    // LOG_I("Setting Frequency to %.2fMHz (%.2f)", target_freq, best_freq);


    // default 200Mhz if it fails
    unsigned char freqbuf[6] = {0x00, 0x08, 0x40, 0xA0, 0x02, 0x41}; // freqbuf - pll0_parameter
    float newf = 200.0;

    uint8_t fb_divider = 0;
    uint8_t post_divider1 = 0, post_divider2 = 0;
    uint8_t ref_divider = 0;
    float min_difference = 10;
    float max_diff = 1.0;

    // refdiver is 2 or 1
    // postdivider 2 is 1 to 7
    // postdivider 1 is 1 to 7 and greater than or equal to postdivider 2
    // fbdiv is 0xa0 to 0xef
    for (uint8_t refdiv_loop = 2; refdiv_loop > 0 && fb_divider == 0; refdiv_loop--) {
        for (uint8_t postdiv1_loop = 7; postdiv1_loop > 0 && fb_divider == 0; postdiv1_loop--) {
            for (uint8_t postdiv2_loop = 7; postdiv2_loop > 0 && fb_divider == 0; postdiv2_loop--) {
                if (postdiv1_loop >= postdiv2_loop) {
                    int temp_fb_divider = round(((float) (postdiv1_loop * postdiv2_loop * target_freq * refdiv_loop) / 25.0));

                    if (temp_fb_divider >= 0xa0 && temp_fb_divider <= 0xef) {
                        float temp_freq = 25.0 * (float) temp_fb_divider / (float) (refdiv_loop * postdiv2_loop * postdiv1_loop);
                        float freq_diff = fabs(target_freq - temp_freq);

                        if (freq_diff < min_difference && freq_diff < max_diff) {
                            fb_divider = temp_fb_divider;
                            post_divider1 = postdiv1_loop;
                            post_divider2 = postdiv2_loop;
                            ref_divider = refdiv_loop;
                            min_difference = freq_diff;
                            newf = temp_freq;
                        }
                    }
                }
            }
        }
    }

    if (fb_divider == 0) {
        LOG_E("Failed to find PLL settings for target frequency %.2f", target_freq);
        return;
    }

    freqbuf[3] = fb_divider;
    freqbuf[4] = ref_divider;
    freqbuf[5] = (((post_divider1 - 1) & 0xf) << 4) + ((post_divider2 - 1) & 0xf);

    if (fb_divider * 25 / (float) ref_divider >= 2400) {
        freqbuf[2] = 0x50;
    }

    this->_send_bm1370(TYPE_CMD | GROUP_ALL | CMD_WRITE, freqbuf, 6);

    LOG_I("Setting Frequency to %.2fMHz (%.2f)", target_freq, newf);
}

void BM1370::_set_version_mask(uint32_t version_mask) {
    int versions_to_roll = version_mask >> 13;
    uint8_t version_byte0 = (versions_to_roll >> 8);
    uint8_t version_byte1 = (versions_to_roll & 0xFF); 
    uint8_t version_cmd[] = {0x00, 0xA4, 0x90, 0x00, version_byte0, version_byte1};
    this->_send_bm1370(TYPE_CMD | GROUP_ALL | CMD_WRITE, version_cmd, 6);
}

void BM1370::set_job_difficulty(int difficulty){
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
    this->_send_bm1370((TYPE_CMD | GROUP_ALL | CMD_WRITE), job_difficulty_mask, 6);
}

uint32_t BM1370::get_asic_difficulty(){
    return this->_diff_current;
}

void BM1370::frequency_ramp_up(float target_frequency){
    // float current = 56.25;
    // float step = 6.25;

    // if (target_frequency == 0) {
    //     LOG_W("Skipping frequency ramp");
    //     return;
    // }

    // LOG_I("Ramping up frequency from %.2f MHz to %.2f MHz with step %.2f MHz", current, target_frequency, step);

    // this->_set_hash_frequency(-1, current, 0.001);
    
    // while (current < target_frequency) {
    //     float next_step = fminf(step, target_frequency - current);
    //     current += next_step;
    //     this->_set_hash_frequency(-1, current, 0.001);
    //     // delay(100);
    // }
    static float current_frequency = 56.25;
    float step = 6.25;
    float current = current_frequency;
    float target = target_frequency;

    float direction = (target > current) ? step : -step;

    // If current frequency is not a multiple of step, adjust to the nearest multiple
    if (fmod(current, step) != 0) {
        float next_dividable;
        if (direction > 0) {
            next_dividable = ceil(current / step) * step;
        } else {
            next_dividable = floor(current / step) * step;
        }
        current = next_dividable;
        
        // Call the provided hash frequency function
        this->_set_hash_frequency(current);
        
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    // Gradually adjust frequency in steps until target is reached
    while ((direction > 0 && current < target) || (direction < 0 && current > target)) {
        float next_step = fmin(fabs(direction), fabs(target - current));
        current += direction > 0 ? next_step : -next_step;
        
        // Call the provided hash frequency function
        this->_set_hash_frequency(current);
        
        // vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    
    // Set the final target frequency
    this->_set_hash_frequency(target);
    current_frequency = target;
    
    LOG_D("Successfully transitioned to %.2f MHz", target);
}

uint8_t BM1370::init(uint64_t freq, int diff){
    for (int i = 0; i < 3; i++) {
        this->_set_version_mask(ASIC_DEFAULT_VSERSION_MASK);
    }

    // read register 00 on all chips
    uint8_t init3[7] = {0x55, 0xAA, 0x52, 0x05, 0x00, 0x00, 0x0A};
    this->send(init3, 7);

    uint8_t chip_counter = 0, rsp[100] = {0,};
    while (true) {
        uint8_t len = this->receive(rsp, sizeof(rsp), 1000);
        if(len == 0) break;

        // dbg::hex_print(rsp, len, "init3");
        uint8_t *rsp_ptr = rsp;
        while (rsp_ptr <= rsp + len - 11) {
            if(memcmp(rsp_ptr, "\xaa\x55\x13\x70\x00\x00", 6) == 0){
                // dbg::hex_print(rsp_ptr, 11, "found chip");
                chip_counter++;
                break;
            }
            else{
                rsp_ptr++;
            }
        }
    }

    if(chip_counter == 0)  return 0;

    this->_set_version_mask(ASIC_DEFAULT_VSERSION_MASK);

    
    // uint8_t init4[11] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0xA8, 0x00, 0x07, 0x00, 0x00, 0x03};
    // this->send(init4, 11);

    uint8_t init4[6] = {0x00, 0xA8, 0x00, 0x07, 0x00, 0x00}; 
    this->_send_bm1370(TYPE_CMD | GROUP_ALL | CMD_WRITE, init4, 6);

    // uint8_t init5[11] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0x18, 0xF0, 0x00, 0xC1, 0x00, 0x00};//from S21 dump
    // this->send(init5, 11);   

    uint8_t init5[6] = {0x00, 0x18, 0xF0, 0x00, 0xC1, 0x00}; // from S21 dump
    this->_send_bm1370(TYPE_CMD | GROUP_ALL | CMD_WRITE, init5, 6);

    this->_set_chain_inactive();

    // split the chip address space evenly
    uint8_t address_interval = (uint8_t) (256 / chip_counter);
    for (uint8_t i = 0; i < chip_counter; i++) {
      this->_set_chip_address(i * address_interval);
    }
                                                         
    uint8_t init135[11] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0x3C, 0x80, 0x00, 0x85, 0x40, 0x0C};
    this->send(init135, 11);

    uint8_t init136[11] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0x3C, 0x80, 0x00, 0x80, 0x20, 0x19};
    this->send(init136, 11);

    this->set_job_difficulty(diff);

    uint8_t init138[11] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0x54, 0x00, 0x00, 0x00, 0x03, 0x1D};
    this->send(init138, 11);

    uint8_t init139[11] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0x58, 0x02, 0x11, 0x11, 0x11, 0x06};
    this->send(init139, 11);

    uint8_t init171[11] = {0x55, 0xAA, 0x41, 0x09, 0x00, 0x2C, 0x00, 0x7C, 0x00, 0x03, 0x03};
    this->send(init171, 11);

    uint8_t init173[11] = {0x55, 0xAA, 0x51, 0x09, 0x00, 0x28, 0x11, 0x30, 0x02, 0x00, 0x03};
    this->send(init173, 11);

    for (uint8_t i = 0; i < chip_counter; i++) {
        uint8_t set_a8_register[6] = {(uint8_t)(i * address_interval), 0xA8, 0x00, 0x07, 0x01, 0xF0};
        this->_send_bm1370((TYPE_CMD | GROUP_SINGLE | CMD_WRITE), set_a8_register, 6);
        uint8_t set_18_register[6] = {(uint8_t)(i * address_interval), 0x18, 0xF0, 0x00, 0xC1, 0x00};
        this->_send_bm1370((TYPE_CMD | GROUP_SINGLE | CMD_WRITE), set_18_register, 6);
        uint8_t set_3c_register_first[6] = {(uint8_t)(i * address_interval), 0x3C, 0x80, 0x00, 0x8B, 0x00};
        this->_send_bm1370((TYPE_CMD | GROUP_SINGLE | CMD_WRITE), set_3c_register_first, 6);
        uint8_t set_3c_register_second[6] = {(uint8_t)(i * address_interval), 0x3C, 0x80, 0x00, 0x80, 0x0C};
        this->_send_bm1370((TYPE_CMD | GROUP_SINGLE | CMD_WRITE), set_3c_register_second, 6);
        uint8_t set_3c_register_third[6] = {(uint8_t)(i * address_interval), 0x3C, 0x80, 0x00, 0x82, 0xAA};
        this->_send_bm1370((TYPE_CMD | GROUP_SINGLE | CMD_WRITE), set_3c_register_third, 6);
    }

    uint8_t int171[6] = {0x00, 0xB9, 0x00, 0x00, 0x44, 0x80};
    this->_send_bm1370((TYPE_CMD | GROUP_ALL | CMD_WRITE), int171, 6);
    uint8_t int173[6] = {0x00, 0x54, 0x00, 0x00, 0x00, 0x02};
    this->_send_bm1370((TYPE_CMD | GROUP_ALL | CMD_WRITE), int173, 6);
    uint8_t int174[6] = {0x00, 0xB9, 0x00, 0x00, 0x44, 0x80};
    this->_send_bm1370((TYPE_CMD | GROUP_ALL | CMD_WRITE), int174, 6);
    uint8_t int175[6] = {0x00, 0x3C, 0x80, 0x00, 0x8D, 0xEE};
    this->_send_bm1370((TYPE_CMD | GROUP_ALL | CMD_WRITE), int175, 6);


    this->frequency_ramp_up((float)freq);//do_frequency_ramp_up();

    //register 10 is still a bit of a mystery. discussion: https://github.com/skot/ESP-Miner/pull/167
    // unsigned char set_10_hash_counting[6] = {0x00, 0x10, 0x00, 0x00, 0x11, 0x5A}; //S19k Pro Default
    // unsigned char set_10_hash_counting[6] = {0x00, 0x10, 0x00, 0x00, 0x14, 0x46}; //S19XP-Luxos Default
    // unsigned char set_10_hash_counting[6] = {0x00, 0x10, 0x00, 0x00, 0x15, 0x1C}; //S19XP-Stock Default
    //unsigned char set_10_hash_counting[6] = {0x00, 0x10, 0x00, 0x00, 0x15, 0xA4}; //S21-Stock Default
    unsigned char set_10_hash_counting[6] = {0x00, 0x10, 0x00, 0x00, 0x1E, 0xB5}; //S21 Pro-Stock Default
    // unsigned char set_10_hash_counting[6] = {0x00, 0x10, 0x00, 0x0F, 0x00, 0x00}; //supposedly the "full" 32bit nonce range
    this->_send_bm1370((TYPE_CMD | GROUP_ALL | CMD_WRITE), set_10_hash_counting, 6);

    return chip_counter;
}

void BM1370::send_work_to_asic(asic_job *job){
    job->num_midstates = 0x01;
    this->_send_bm1370((TYPE_JOB | GROUP_SINGLE | CMD_WRITE), (uint8_t*)job, sizeof(asic_job));
}

esp_err_t BM1370::wait_for_result(asic_result *result, uint32_t timeout_ms){
    uint8_t rsp[11] = {0,};
    uint16_t len = this->receive(rsp, sizeof(rsp), timeout_ms);
    if(len == 0) return ESP_ERR_TIMEOUT;
    // dbg::hex_print(rsp, len, "asic result");


    if(len != 11){
        this->clear_port_cache();
        return ESP_ERR_INVALID_SIZE;
    }
    if(rsp[0] != 0xAA && rsp[1] != 0x55){
        this->clear_port_cache();
        return ESP_ERR_INVALID_RESPONSE;
    }
    *result            = *(asic_result*)(rsp);
    // /* logic from project bitaxe: https://github.com/skot/bitaxe */
    // /* Thanks for their efforts on this project */
    // uint32_t core_id   =    ((result->nonce >> 24) & 0xff) |       // Move byte 3 to byte 0
    //                         ((result->nonce  << 8) & 0xff0000) |   // Move byte 1 to byte 2
    //                         ((result->nonce  >> 8) & 0xff00) |     // Move byte 2 to byte 1
    //                         ((result->nonce  << 24)& 0xff000000);  // Move byte 0 to byte 3
    // core_id = (uint8_t)((core_id >> 25) & 0x7f);
    // uint8_t small_core = result->job_id & 0x07;
    result->job_id     = (result->job_id & 0xf0) >> 1; 
    return ESP_OK;
}

uint16_t BM1370::get_cores(){
    return BM1370_CORE_COUNT;
}   

uint16_t BM1370::get_small_cores(){
    return BM1370_SMALL_CORE_COUNT;
}