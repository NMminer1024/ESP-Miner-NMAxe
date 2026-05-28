#include "stratum/stratum.h"
#include "utils/logger/logger.h"
#include "miner.h"
#include <esp_task_wdt.h>
#include "utils/helper.h"
#include <limits> 
#include "global.h"
#include "utils/sha/csha256.h"
#include <deque>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cstdio>

void add_share_diff_history(std::deque<proximity_node_t, PsramAllocator<proximity_node_t>> &hist, proximity_node_t &node, size_t max_history) {
    hist.push_back(node);

    if (hist.size() <= max_history) return;

    const size_t n = hist.size();
    const size_t keep = std::min<size_t>(3, n);

    // find indices of the top 'keep' share_diff values
    std::vector<size_t> idx(n);
    for (size_t i = 0; i < n; ++i) idx[i] = i;

    std::partial_sort(idx.begin(), idx.begin() + keep, idx.end(),
                      [&](size_t a, size_t b) {
                          return hist[a].share_diff > hist[b].share_diff;
                      });

    // collect indices of elements that are not among the top 'keep' and not the last element
    std::vector<size_t> unprotected_indices;
    for (size_t i = 0; i < n; ++i) {
        bool is_protected = false;
        
        // check if it's among the top 'keep' elements
        for (size_t j = 0; j < keep; ++j) {
            if (i == idx[j]) {
                is_protected = true;
                break;
            }
        }
        // check if it's the last element
        if (i == n - 1) {
            is_protected = true;
        }
        if (!is_protected) {
            unprotected_indices.push_back(i);
        }
    }

    // randomly remove one unprotected element
    if (!unprotected_indices.empty()) {
        size_t random_index = unprotected_indices[millis() % unprotected_indices.size()];
        hist.erase(hist.begin() + random_index);
    }
}

AsicMinerClass::AsicMinerClass(BMxxx *asic){
    this->_asic = asic;
    this->_asic_count = 0;
    this->_asic_freq_current = 0;
    this->_asic_freq_target = 0;
    this->_asic_freq_update_pending = false;
    this->_asic_freq_updating = false;
    this->_asic_ready = false;
    this->_asic_freq_mutex = xSemaphoreCreateMutex();
    this->pool_job_now.id = "";
    this->_asic_job_map.clear();
    memset(&this->_asic_job_now, 0, sizeof(asic_job));
}

AsicMinerClass::~AsicMinerClass(){
    if (this->_asic_freq_mutex != NULL) {
        vSemaphoreDelete(this->_asic_freq_mutex);
        this->_asic_freq_mutex = NULL;
    }
}

bool AsicMinerClass::begin(uint16_t freq, uint16_t diff, uint32_t baudrate){
    if (this->_asic == NULL) return false;
    this->_asic->init(freq, diff, this->_asic_count);
    this->_asic_freq_current = freq;
    this->_asic_ready = true;
    if (this->_asic_freq_mutex != NULL && xSemaphoreTake(this->_asic_freq_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (this->_asic_freq_target == freq) this->_asic_freq_update_pending = false;
        xSemaphoreGive(this->_asic_freq_mutex);
    }
    this->_asic->change_uart_baud(baudrate);
    this->_asic->clear_port_cache();
    return true;
}

bool AsicMinerClass::request_asic_frequency(uint16_t target_freq){
    if (target_freq == 0) return false;

    if (this->_asic_freq_mutex != NULL && xSemaphoreTake(this->_asic_freq_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        LOG_W("ASIC frequency request busy, target %uMHz not queued", target_freq);
        return false;
    }

    if (this->_asic_freq_current == target_freq && !this->_asic_freq_update_pending && !this->_asic_freq_updating) {
        if (this->_asic_freq_mutex != NULL) xSemaphoreGive(this->_asic_freq_mutex);
        return true;
    }

    this->_asic_freq_target = target_freq;
    this->_asic_freq_update_pending = true;
    LOG_W("ASIC frequency change requested: %uMHz -> %uMHz", this->_asic_freq_current, target_freq);

    if (this->_asic_freq_mutex != NULL) xSemaphoreGive(this->_asic_freq_mutex);
    return true;
}

bool AsicMinerClass::apply_pending_asic_frequency(){
    if (this->_asic == NULL || !this->_asic_ready) return false;

    uint16_t current_freq = 0;
    uint16_t target_freq = 0;

    if (this->_asic_freq_mutex != NULL && xSemaphoreTake(this->_asic_freq_mutex, 0) != pdTRUE) return false;

    if (!this->_asic_freq_update_pending || this->_asic_freq_updating) {
        if (this->_asic_freq_mutex != NULL) xSemaphoreGive(this->_asic_freq_mutex);
        return false;
    }

    current_freq = this->_asic_freq_current;
    target_freq = this->_asic_freq_target;
    this->_asic_freq_update_pending = false;

    if (target_freq == 0 || current_freq == target_freq) {
        if (this->_asic_freq_mutex != NULL) xSemaphoreGive(this->_asic_freq_mutex);
        return false;
    }

    this->_asic_freq_updating = true;
    if (this->_asic_freq_mutex != NULL) xSemaphoreGive(this->_asic_freq_mutex);

    LOG_W("Applying ASIC PLL frequency change: %uMHz -> %uMHz", current_freq, target_freq);
    this->clear_asic_job_cache();
    this->reset_hashrate();
    this->_asic->clear_port_cache();
    bool ok = this->_asic->set_frequency((float)current_freq, (float)target_freq);
    this->_asic->clear_port_cache();

    if (this->_asic_freq_mutex != NULL && xSemaphoreTake(this->_asic_freq_mutex, portMAX_DELAY) == pdTRUE) {
        if (ok) this->_asic_freq_current = target_freq;
        this->_asic_freq_updating = false;
        xSemaphoreGive(this->_asic_freq_mutex);
    } else if (this->_asic_freq_mutex == NULL) {
        if (ok) this->_asic_freq_current = target_freq;
        this->_asic_freq_updating = false;
    }

    if (ok) LOG_W("ASIC PLL frequency change applied: %uMHz", target_freq);
    else    LOG_E("ASIC PLL frequency change failed: %uMHz -> %uMHz", current_freq, target_freq);
    return ok;
}

bool AsicMinerClass::is_asic_frequency_updating(){
    bool updating = false;
    if (this->_asic_freq_mutex != NULL && xSemaphoreTake(this->_asic_freq_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        updating = this->_asic_freq_updating;
        xSemaphoreGive(this->_asic_freq_mutex);
    } else {
        updating = this->_asic_freq_updating;
    }
    return updating;
}

uint16_t AsicMinerClass::get_asic_frequency_current(){
    uint16_t freq = this->_asic_freq_current;
    if (this->_asic_freq_mutex != NULL && xSemaphoreTake(this->_asic_freq_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        freq = this->_asic_freq_current;
        xSemaphoreGive(this->_asic_freq_mutex);
    }
    return freq;
}

esp_err_t AsicMinerClass::listen_asic_rsp(miner_result *result, uint32_t timeout_ms){
    /* logic from project bitaxe: https://github.com/skot/bitaxe */
    /* Thanks for their efforts on this project */
    esp_err_t err = this->_asic->wait_for_result(result, timeout_ms);
    return err;
}

bool AsicMinerClass::mining(pool_job_data_t *pool_job){
    if(this->_asic == NULL) return false;
    ////////////////////////////////////////construct asic job//////////////////////////////////
    uint8_t step = 8;
    if(g_board.info.spec.asic.name == CHIP_NMAXE_NAME)                  step = 8;
    else if (g_board.info.spec.asic.name == CHIP_NMAXE_GAMMA_NAME)      step = 24;
    else if (g_board.info.spec.asic.name == CHIP_NMQAXE_PLUS_PLUS_NAME) step = 24;
    else LOG_W("Unknown ASIC model, using default step 8");

    this->_asic_job_now.id = (this->_asic_job_now.id + step) % 128;

    this->pool_job_now.id  = pool_job->id;
    String  extranonce2    = g_board.stratum->get_sub_extranonce2();
    /**************************************** coinhash ****************************************/
    String coinbaseStr = pool_job->coinb1 + g_board.stratum->get_sub_extranonce1() + extranonce2 + pool_job->coinb2;
    uint8_t merkle_root[32], coinbase[coinbaseStr.length()/2];
    size_t res = str_to_byte_array(coinbaseStr.c_str(), coinbaseStr.length(), coinbase);
    if(res <= 0){
        LOG_E("Failed to convert coinbase string to byte array");
        return false;
    }
    csha256d(coinbase, coinbaseStr.length()/2, merkle_root);
    /**************************************** markle root *************************************/
    byte merkle_concatenated[32 * 2];
    for (size_t k = 0; k < pool_job->merkle_branch.size(); k++) {
        const char* merkle_element = (const char*) pool_job->merkle_branch[k];
        uint8_t node[32];
        res = str_to_byte_array(merkle_element, 64, node);
        if(res <= 0){
            LOG_E("Failed to convert merkle element string to byte array");
            return false;
        }
        memcpy(merkle_concatenated, merkle_root, 32);
        memcpy(merkle_concatenated + 32, node, 32);
        csha256d(merkle_concatenated, 64, merkle_root);
    }
    /**************************************** Version ****************************************/
    *(uint32_t*)this->_asic_job_now.version         = strtoul(pool_job->version.c_str(), NULL, 16);
    /**************************************** prevhash ****************************************/
    res = str_to_byte_array(pool_job->prevhash.c_str(), pool_job->prevhash.length(), this->_asic_job_now.prev_block_hash);
    if(res <= 0){
        LOG_E("Failed to convert prevhash string to byte array");
        return false;
    }
    reverse_bytes(this->_asic_job_now.prev_block_hash, sizeof(this->_asic_job_now.prev_block_hash));
    /**************************************** merkle_root *************************************/
    memcpy(this->_asic_job_now.merkle_root, merkle_root, sizeof(merkle_root));
    reverse_words(this->_asic_job_now.merkle_root, sizeof(merkle_root));

    *(uint32_t*)this->_asic_job_now.ntime           = strtoul(pool_job->ntime.c_str(), NULL, 16);
    *(uint32_t*)this->_asic_job_now.nbits           = strtoul(pool_job->nbits.c_str(), NULL, 16);
    *(uint32_t*)this->_asic_job_now.starting_nonce  = 0x00000000;
    this->_asic_job_map[this->_asic_job_now.id]     = this->_asic_job_now;
    this->_extranonce2_map[this->_asic_job_now.id]  = extranonce2;
    this->_pool_job_id_map[this->_asic_job_now.id]  = pool_job->id;   // bind pool_job_id to this asic slot

    LOG_D("ASIC job [%03d] with ext2 [%s]", this->_asic_job_now.id, extranonce2.c_str());

    ////////////////////////////////////////send asic job//////////////////////////////////
    this->_asic->send_work_to_asic(&this->_asic_job_now);
    return true;
}

uint32_t AsicMinerClass::set_asic_diff(uint64_t diff){
    return this->_asic->set_job_difficulty(diff);
}

double AsicMinerClass::get_asic_diff(){
    return this->_asic->get_asic_difficulty();
}

uint8_t AsicMinerClass::connect_chip(){
    this->_asic->reset();
    this->_asic_count = this->_asic->get_asic_count();
    if(0 == this->_asic_count) {
        LOG_E("xxxxxxx No %s ASIC found xxxxxxx", g_board.info.spec.asic.name);
        return 0;
    }
    LOG_I("======= Found %d %s %s (%d/%d)=======", this->_asic_count, g_board.info.spec.asic.name, (this->_asic_count > 1) ? "chips" : "chip" , this->_asic->get_cores(), this->_asic->get_small_cores());
    return this->_asic_count;
}

uint8_t AsicMinerClass::get_asic_count(){
    return this->_asic_count;
}

uint16_t AsicMinerClass::get_asic_small_cores(){
    return this->_asic->get_small_cores();
}

bool AsicMinerClass::find_job_by_asic_job_id(uint8_t asic_job_id, asic_job* job){
    if(this->_asic_job_map.find(asic_job_id) == this->_asic_job_map.end()){
        job = NULL;
        return false;
    }   
    memcpy(job, &this->_asic_job_map[asic_job_id], sizeof(asic_job));
    return true;
}

bool AsicMinerClass::clear_asic_job_cache(){
    this->_asic_job_map.clear();
    this->_extranonce2_map.clear();
    this->_pool_job_id_map.clear();
    return true;
}

String AsicMinerClass::get_extranonce2_by_asic_job_id(uint8_t asic_job_id){
    if(this->_extranonce2_map.find(asic_job_id) == this->_extranonce2_map.end()){
        return "";
    }
    return this->_extranonce2_map[asic_job_id];
}

String AsicMinerClass::get_pool_job_id_by_asic_job_id(uint8_t asic_job_id){
    auto it = this->_pool_job_id_map.find(asic_job_id);
    if(it == this->_pool_job_id_map.end()) return "";
    return it->second;
}

bool AsicMinerClass::submit_job_share(String pool_job_id, String extranonce2, uint32_t nonce, uint32_t ntime, uint32_t version){
    if(pool_job_id.length() == 0) return false;
    return g_board.stratum->submit(pool_job_id, extranonce2, ntime, nonce, version);
}

bool AsicMinerClass::calculate_hashrate(hashrate_t *phr){
    if (phr == NULL) return false;

    const uint32_t W3M  =  3 * 60 * 1000u;
    const uint32_t W30M = 30 * 60 * 1000u;
    const uint32_t W60M = 60 * 60 * 1000u;
    uint32_t now = millis();

    // Pruning order matters: shorter windows FIRST, then 60m pop.
    // If 60m pop ran first and a sample had aged past all three windows since
    // the previous call (e.g. monitor thread was starved >30m), the sample
    // would be removed from _s60m and the deque before _off_30m / _off_3m had
    // a chance to advance past it — leaving its diff stuck in _s30m / _s3m.
    // By advancing 3m and 30m cursors first, the 60m pop only needs to
    // decrement the cursors (sample is guaranteed already excluded from _s3m
    // and _s30m).

    // 3m: advance front cursor past samples that have aged out of 3m window.
    while (_off_3m < _hr_deque.size() && (now - _hr_deque[_off_3m].first) > W3M) {
        _s3m -= _hr_deque[_off_3m].second;
        _off_3m++;
    }
    // 30m: same idea.
    while (_off_30m < _hr_deque.size() && (now - _hr_deque[_off_30m].first) > W30M) {
        _s30m -= _hr_deque[_off_30m].second;
        _off_30m++;
    }
    // 60m: drop samples older than 60 min from deque AND from _s60m.
    // The popped sample is already excluded from _s30m / _s3m (older than 60m
    // implies older than 30m and 3m, handled above), so just shrink the
    // cursors by 1 to account for the front element going away.
    while (!_hr_deque.empty() && (now - _hr_deque.front().first) > W60M) {
        _s60m -= _hr_deque.front().second;
        _hr_deque.pop_front();
        if (_off_30m > 0) _off_30m--;
        if (_off_3m  > 0) _off_3m--;
    }

    // Guard against floating-point drift: if a window's cursor has caught up
    // to deque end (no samples in window), force the sum to exactly 0.
    if (_off_3m  >= _hr_deque.size()) _s3m  = 0.0;
    if (_off_30m >= _hr_deque.size()) _s30m = 0.0;
    if (_hr_deque.empty())            _s60m = 0.0;

    // Denominator is fixed to the full window length. During startup
    // (uptime < window) this lets the displayed hashrate ramp up smoothly
    // from 0 to the true value, instead of being amplified by a tiny
    // elapsed-time divisor (which can spike a single early nonce to 10T+).
    phr->_3m  = _s3m  * 4294967296.0 / (double)(W3M  / 1000u);
    phr->_30m = _s30m * 4294967296.0 / (double)(W30M / 1000u);
    phr->_1h  = _s60m * 4294967296.0 / (double)(W60M / 1000u);
    return true;
}

// Record one valid nonce into the hashrate sample ring.
// Called by the ASIC RX thread whenever a nonce meets the ASIC difficulty threshold.
void AsicMinerClass::record_nonce() {
    double diff = this->_asic->get_asic_difficulty();
    if (diff <= 0.0) return;
    _hr_deque.push_back({millis(), diff});
    // New sample is inside all three windows by definition; cursors are
    // front-relative so push_back doesn't invalidate them.
    _s3m  += diff;
    _s30m += diff;
    _s60m += diff;
}

// Clear all hashrate samples (call on stratum disconnect).
void AsicMinerClass::reset_hashrate() {
    _hr_deque.clear();
    _s3m = _s30m = _s60m = 0.0;
    _off_3m = _off_30m = 0;
}

bool AsicMinerClass::end(){
    this->_asic->clear_port_cache();
    return true;
}