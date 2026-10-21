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

// CMD_READ a 32-bit register from the chain and return it big-endian.
// Register-read replies are SHORTER than the 11-byte nonce/HCN frames:
//   AA 55 <data...> <chipaddr> <reg> <crc5>
// with data width = register width (PLL0 0x08 is 32-bit -> 9 bytes total;
// HCN 0x90 is 48-bit -> 11 bytes). Verified on hardware: reading 0x08 at
// 575MHz replies "AA 55 D0 E6 02 40 00 08 07".
// Only reliable on single-chip chains; NMAxe is a single BM1366.
bool BM1366::_read_register(uint8_t reg, uint32_t *value){
    uint8_t reg_read[2] = {0x00, reg};
    this->_send_bm1366((TYPE_CMD | GROUP_ALL | CMD_READ), reg_read, 2);

    uint8_t rsp[11] = {0,};
    // A 9-byte reply never fills the 11-byte buffer, so receive() returns after
    // the timeout with len=9 — that is the expected normal case for 0x08.
    uint16_t len = this->receive(rsp, sizeof(rsp), 300);
    bool valid = (len == 9 || len == 11) && rsp[0] == 0xAA && rsp[1] == 0x55 && rsp[7] == reg;
    if (valid && len == 11 && (rsp[8] != 0x00 || rsp[9] != 0x00)) valid = false;
    if (!valid) {
        char hex[40] = {0};
        int off = 0;
        for (uint16_t i = 0; i < len && off < (int)sizeof(hex) - 4; i++) {
            off += snprintf(hex + off, sizeof(hex) - off, "%02X ", rsp[i]);
        }
        LOG_W("REG 0x%02X read: no valid reply (len=%u, got: %s)", reg, len, hex);
        this->clear_port_cache();
        return false;
    }
    if (value) {
        *value = ((uint32_t)rsp[2] << 24) | ((uint32_t)rsp[3] << 16) |
                 ((uint32_t)rsp[4] << 8)  |  (uint32_t)rsp[5];
    }
    return true;
}

// Decode a PLL0(0x08) readback into the actual hash clock in MHz. The register
// is {ctrl, fbdiv, refdiv, postdiv1-1<<4|postdiv2-1} big-endian; the chip also
// sets status/lock bits in ctrl (we write 0x40, it reads back 0xD0 when locked),
// so raw-byte comparison is wrong — decode the dividers and compare frequencies.
// Example: D0 E6 02 40 -> fbdiv 230, refdiv 2, pd1 5, pd2 1 -> 575.00 MHz.
static float _pll_mhz_from_reg(uint32_t reg){
    uint8_t fbdiv   = (reg >> 16) & 0xFF;
    uint8_t refdiv  = (reg >>  8) & 0xFF;
    uint8_t pd1 = ((reg & 0xFF) >> 4) + 1;
    uint8_t pd2 = ( reg & 0x0F)       + 1;
    if (refdiv == 0 || fbdiv == 0) return -1.0f;
    return 25.0f * (float)fbdiv / ((float)refdiv * (float)pd1 * (float)pd2);
}

// Verify PLL0(0x08) actually latched the target frequency and re-ramp on
// mismatch. Every PLL write in the ramp is fire-and-forget, so a lost UART
// byte would leave the chip silently running at an intermediate frequency.
bool BM1366::_ramp_and_verify(float target_freq){
    if (!this->set_frequency(56.25, target_freq)) return false;

    for (int attempt = 0; attempt < 3; attempt++) {
        uint32_t readback = 0;
        if (!this->_read_register(0x08, &readback)) {
            // Read failed (frame lost / reg unreadable). Don't block mining.
            LOG_W("PLL0(0x08) readback failed, cannot verify %.2fMHz", target_freq);
            return true;
        }
        float actual_mhz = _pll_mhz_from_reg(readback);
        if (fabsf(actual_mhz - target_freq) < 0.5f) {
            LOG_I("PLL0(0x08) verified: 0x%08X -> %.2fMHz (target %.2fMHz)", readback, actual_mhz, target_freq);
            return true;
        }
        LOG_E("PLL0(0x08) latched %.2fMHz (raw 0x%08X), target %.2fMHz — re-ramping (attempt %d/3)",
              actual_mhz, readback, target_freq, attempt + 1);
        // Continue the ramp from wherever the chip actually is, not from 56.25.
        if (actual_mhz > 0.0f) this->set_frequency(actual_mhz, target_freq);
        else                   this->_set_hash_frequency(target_freq);
        delay(5);
    }

    uint32_t final_readback = 0;
    float final_mhz = -1.0f;
    if (this->_read_register(0x08, &final_readback)) {
        final_mhz = _pll_mhz_from_reg(final_readback);
        if (fabsf(final_mhz - target_freq) < 0.5f) {
            LOG_I("PLL0(0x08) verified after retry: 0x%08X -> %.2fMHz", final_readback, final_mhz);
            return true;
        }
    }
    LOG_E("PLL0(0x08) still at %.2fMHz (raw 0x%08X) after retries, target %.2fMHz - ASIC will under-perform",
          final_mhz, final_readback, target_freq);
    return false;
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
        // 1ms -> 20ms: same write-loss mitigation as BM1373 (commit bb82021b).
        // The ASIC needs time to latch each PLL write; back-to-back 1ms steps
        // can drop writes and leave the chip at an intermediate frequency.
        delay(20);
    }

    while ((direction > 0 && current < target) || (direction < 0 && current > target)) {
        float next_step = fmin(fabs(direction), fabs(target - current));
        current += direction > 0 ? next_step : -next_step;
        if (!this->_set_hash_frequency(current)) return false;
        delay(20);
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
        this->_hcn_addr_interval = asic_count > 1 ? address_interval : 255;
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

    this->_ramp_and_verify((float)freq);//do_frequency_ramp_up();

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

void BM1366::poll_hcn_register(){
    uint8_t reg_read[2] = {0x00, BM1366_REG_POLL_ADDR};
    this->_send_bm1366((TYPE_CMD | GROUP_ALL | CMD_READ), reg_read, 2);
    static const uint8_t dom_regs[6] = {0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x4C};
    for (uint8_t reg : dom_regs) {
        delay(2);   // back-to-back CMD_READs drop replies
        uint8_t rd[2] = {0x00, reg};
        this->_send_bm1366((TYPE_CMD | GROUP_ALL | CMD_READ), rd, 2);
    }
}

bool BM1366::_decode_domain_counter(const uint8_t *rsp){
    if (rsp[10] & 0x80) return false;   // job (nonce) response
    int idx;
    switch (rsp[7]) {
        case 0x88: idx = 0; break;
        case 0x89: idx = 1; break;
        case 0x8A: idx = 2; break;
        case 0x8B: idx = 3; break;
        case 0x8C: idx = 4; break;
        case 0x4C: idx = 5; break;
        default: return false;
    }
    const uint32_t v = ((uint32_t)rsp[2] << 24) | ((uint32_t)rsp[3] << 16) | ((uint32_t)rsp[4] << 8) | rsp[5];
    const uint32_t now = millis();
    if (this->_dom_prev_ms[idx] != 0 && now - this->_dom_prev_ms[idx] >= 1000) {
        this->_dom_ghs[idx] = (float)((double)(v - this->_dom_prev[idx]) * 4294967296.0 /
                                      ((double)(now - this->_dom_prev_ms[idx]) * 1e6));
    }
    this->_dom_prev[idx] = v;
    this->_dom_prev_ms[idx] = now;
    if (idx == 5 && now - this->_dom_log_ms >= 10000) {
        this->_dom_log_ms = now;
        const float *g = this->_dom_ghs;
        LOG_W("[DOMAIN] d0=%.0f d1=%.0f d2=%.0f d3=%.0f sum=%.0f total(0x8C)=%.0f err(0x4C)=%.1f GH/s",
              g[0], g[1], g[2], g[3], g[0] + g[1] + g[2] + g[3], g[4], g[5]);
    }
    return true;
}

// Broadcast-write zero to arm/start the counter on every chip (same as BM1373;
// a reference vendor driver does this once before its first poll).
void BM1366::reset_hcn_register(){
    uint8_t reg_reset[6] = {0x00, BM1366_REG_POLL_ADDR, 0x00, 0x00, 0x00, 0x00};
    this->_send_bm1366((TYPE_CMD | GROUP_ALL | CMD_WRITE), reg_reset, 6);
}

bool BM1366::decode_hcn_response_0x90(const uint8_t *rsp, asic_hcn_result *hcn){
    if (rsp[7] != 0x90) return false;
    this->_hcn_tag_seen++;
    if (rsp[8] != 0x00 || rsp[9] != 0x00) return false;
    this->_hcn_decoded_ok++;

    hcn->chip_addr  = rsp[6];
    hcn->asic_id    = (uint8_t)(rsp[6] / this->_hcn_addr_interval);
    if (hcn->asic_id >= 16) hcn->asic_id = 0;
    hcn->reg_addr   = 0x90;
    hcn->hash_count = ((uint32_t)rsp[2] << 24) | ((uint32_t)rsp[3] << 16) |
                      ((uint32_t)rsp[4] << 8)  |  (uint32_t)rsp[5];
    return true;
}

asic_rx_result BM1366::wait_for_result(uint32_t timeout_ms){
    asic_rx_result rx = {};
    rx.status = ASIC_RX_STATUS_INVALID_RESPONSE;
    rx.type = ASIC_RX_TYPE_NONE;

    uint8_t rsp[11] = {0,};
    uint16_t len = this->receive(rsp, sizeof(rsp), timeout_ms);
    if(len == 0) {
        rx.status = ASIC_RX_STATUS_TIMEOUT;
        return rx;
    }

    if(len != 11){
        LOG_W("Invalid asic response length: %d", len);
        this->clear_port_cache();
        rx.status = ASIC_RX_STATUS_INVALID_SIZE;
        return rx;
    }
    if(rsp[0] != 0xAA || rsp[1] != 0x55){
        LOG_W("Invalid asic response preamble: %02X %02X", rsp[0], rsp[1]);
        this->clear_port_cache();
        rx.status = ASIC_RX_STATUS_INVALID_RESPONSE;
        return rx;
    }

    if (this->decode_hcn_response_0x90(rsp, &rx.data.hcn)) {
        rx.status = ASIC_RX_STATUS_OK;
        rx.type = ASIC_RX_TYPE_HCN;
        return rx;
    }

    if (this->_decode_domain_counter(rsp)) {
        rx.status = ASIC_RX_STATUS_OK;
        rx.type = ASIC_RX_TYPE_NONE;
        return rx;
    }

    asic_result asic;
    asic = *(asic_result*)(rsp);
    asic.job_id     = asic.job_id & 0xf8;// upper 5 bits are job id for BM1366

        rx.data.nonce.asic    = asic;
        rx.data.nonce.asic_id = (uint8_t) ((asic.nonce & 0x0000fc00) >> 10);
        rx.status        = ASIC_RX_STATUS_OK;
        rx.type          = ASIC_RX_TYPE_NONCE;

    LOG_D("ASIC[%d] found nonce: 0x%08X (job id: %d, version: 0x%04X)", 
            rx.data.nonce.asic_id, 
            rx.data.nonce.asic.nonce, 
            rx.data.nonce.asic.job_id, 
            rx.data.nonce.asic.version);

    // /* logic from project bitaxe: https://github.com/skot/bitaxe */
    // /* Thanks for their efforts on this project */
    // uint32_t core_id   =    ((result->nonce >> 24) & 0xff) |       // Move byte 3 to byte 0
    //                         ((result->nonce  << 8) & 0xff0000) |   // Move byte 1 to byte 2
    //                         ((result->nonce  >> 8) & 0xff00) |     // Move byte 2 to byte 1
    //                         ((result->nonce  << 24)& 0xff000000);  // Move byte 0 to byte 3
    // core_id = (uint8_t)((core_id >> 25) & 0x7f);
    // uint8_t small_core = result->job_id & 0x07;


    return rx;
}

uint16_t BM1366::get_cores(){
    return BM1366_CORE_COUNT;
}   
uint16_t BM1366::get_small_cores(){
    return BM1366_SMALL_CORE_COUNT;
}