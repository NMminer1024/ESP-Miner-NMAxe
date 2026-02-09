#include "stratum.h"
#include "logger.h"
#include "miner.h"
#include <esp_task_wdt.h>
#include "helper.h"
#include <limits> 
#include "global.h"
#include "csha256.h"
#include "helper.h"
#include <deque>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cstdio>

static BMxxx *asic_instance = NULL;

void add_share_diff_history(std::deque<proximity_node_t> &hist, proximity_node_t &node, size_t max_history) {
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
    this->pool_job_now.id = "";
    this->_asic_job_map.clear();
    memset(&this->_asic_job_now, 0, sizeof(asic_job));
}

AsicMinerClass::~AsicMinerClass(){

}

bool AsicMinerClass::begin(uint16_t freq, uint16_t diff, uint32_t baudrate){
    this->_asic->reset();
    this->_asic_count = this->_asic->get_asic_count();
    this->_asic->init(freq, diff);
    if(0 == this->_asic_count){
        LOG_E("xxxxxxx No %s ASIC found xxxxxxx", g_board.info.spec.asic.name);
        return false;
    }

    LOG_I("======= Found %d %s %s (%d/%d)=======", this->_asic_count, g_board.info.spec.asic.name, (this->_asic_count > 1) ? "chips" : "chip" , this->_asic->get_cores(), this->_asic->get_small_cores());
    this->_asic->change_uart_baud(baudrate);
    this->_asic->clear_port_cache();
    return true;
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
    return true;
}

double AsicMinerClass::calculate_diff(uint32_t version, uint8_t *prev_block_hash, uint8_t *merkle_root, uint32_t ntime, uint32_t nbits, uint32_t nonce){
    uint8_t header[4 + 32 + 32 + 4 + 4 + 4] = {0,};
    uint8_t hash[32] = {0,};
    uint8_t prev_block_hash_t[32] = {0}, merkle_root_t[32] = {0};

    memcpy(prev_block_hash_t, prev_block_hash, 32);
    memcpy(merkle_root_t, merkle_root, 32);

    reverse_words(prev_block_hash_t, 32);
    reverse_words(merkle_root_t, 32);

    memcpy(header, (uint8_t*)&version, 4);
    memcpy(header + 4, prev_block_hash_t, 32);
    memcpy(header + 36, merkle_root_t, 32);
    memcpy(header + 68, (uint8_t*)&ntime, 4);
    memcpy(header + 72, (uint8_t*)&nbits, 4);
    memcpy(header + 76, (uint8_t*)&nonce, 4);
    //caculate hash
    csha256d(header, sizeof(header), hash);
    return le_hash_to_diff(hash);
}

double AsicMinerClass::calculate_diff(String nBits){
    static const uint8_t  TARGET_BUFFER_SIZE = 64;
    uint8_t netdiff_array[TARGET_BUFFER_SIZE/2];

    char str[TARGET_BUFFER_SIZE + 1];
    memset(str, '0', TARGET_BUFFER_SIZE);
    int k = (int) strtol(nBits.substring(0, 2).c_str(), 0, 16) - 3; 
    uint8_t index = 58 - 2 * k; 
    memcpy(str + index, nBits.substring(2).c_str(), nBits.length() - 2);
    str[TARGET_BUFFER_SIZE] = 0;

    str_to_byte_array(str, TARGET_BUFFER_SIZE/2, netdiff_array);
    reverse_bytes(netdiff_array, TARGET_BUFFER_SIZE/2);

    return le_hash_to_diff(netdiff_array);
}

String AsicMinerClass::get_extranonce2_by_asic_job_id(uint8_t asic_job_id){
    if(this->_extranonce2_map.find(asic_job_id) == this->_extranonce2_map.end()){
        return "";
    }
    return this->_extranonce2_map[asic_job_id];
}

bool AsicMinerClass::submit_job_share(String extranonce2, uint32_t nonce, uint32_t ntime, uint32_t version){
    return g_board.stratum->submit(this->pool_job_now.id, extranonce2, ntime, nonce, version);
}

bool AsicMinerClass::calculate_hashrate(hashrate_t *phr){
    if (phr == NULL) return false;
    static std::deque<std::pair<uint32_t, double>, PsramAllocator<std::pair<uint32_t, double>>> hr_samples_3m, hr_samples_30m, hr_samples_60m;
    const uint32_t duration_3m  = 3 * 60 * 1000, duration_30m = 30 * 60 * 1000, duration_60m = 60 * 60 * 1000;
    static double sum_3m = 0.0, sum_30m = 0.0, sum_60m = 0.0;
    uint32_t now = millis();
    uint32_t diff = this->_asic->get_asic_difficulty();
    // record hashrate samples
    hr_samples_3m.push_back( {now, diff});
    hr_samples_30m.push_back({now, diff});
    hr_samples_60m.push_back({now, diff});

    sum_3m   += diff;
    sum_30m  += diff;
    sum_60m  += diff;
    //remove samples older than 3 minute
    while(!hr_samples_3m.empty() && (hr_samples_3m.front().first + duration_3m < now)) {
        sum_3m -= hr_samples_3m.front().second;
        hr_samples_3m.pop_front();
    }
    //remove samples older than 30 minute
    while(!hr_samples_30m.empty() && (hr_samples_30m.front().first + duration_30m < now)) {
        sum_30m -= hr_samples_30m.front().second;
        hr_samples_30m.pop_front();
    }
    //remove samples older than 60 minute
    while(!hr_samples_60m.empty() && (hr_samples_60m.front().first + duration_60m < now)) {
        sum_60m -= hr_samples_60m.front().second;
        hr_samples_60m.pop_front();
    }

    phr->_3m  = sum_3m  * 4294967296.0 / (3 * 60.0);
    phr->_30m = sum_30m * 4294967296.0 / (30 * 60.0);
    phr->_1h  = sum_60m * 4294967296.0 / (60 * 60.0);
    return true;
}

bool AsicMinerClass::end(){
    this->_asic->clear_port_cache();
    return true;
}