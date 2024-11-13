#include "stratum.h"
#include "logger.h"
#include "miner.h"
#include <esp_task_wdt.h>
#include "monitor.h"
#include "helper.h"
#include <limits> 
#include "global.h"
#include "csha256.h"
#include "helper.h"

#define ESP32_TO_BM1366_INIT_BUAD 115200
#define ESP32_TO_BM1366_WORK_BUAD 1000000
//Asic chip instance
BM1366 *bm1366  = new BM1366(Serial1, ESP32_TO_BM1366_INIT_BUAD, NM_AXE_ESP32_RX_TO_BM1366, NM_AXE_ESP32_TX_TO_BM1366, NM_AXE_ESP32_RST_TO_BM1366);
//Asic chip instance
//BM1368 *bm1368  = new BM1368(Serial1, ESP32_TO_BM1368_INIT_BUAD, NM_AXE_ESP32_RX_TO_BM1368, NM_AXE_ESP32_TX_TO_BM1368, NM_AXE_ESP32_RST_TO_BM1368);
//Asic chip instance
//BM1397 *bm1397  = new BM1397(Serial1, ESP32_TO_BM1397_INIT_BUAD, NM_AXE_ESP32_RX_TO_BM1397, NM_AXE_ESP32_TX_TO_BM1397, NM_AXE_ESP32_RST_TO_BM1397);


AsicMinerClass::AsicMinerClass(BMxxx *asic){
    this->_asic = asic;
    this->_pool_job_id_now = "";
    this->_asic_job_map.clear();
    memset(&this->_asic_job_now, 0, sizeof(asic_job));
}

AsicMinerClass::~AsicMinerClass(){

}

bool AsicMinerClass::begin(uint16_t freq, uint16_t diff){
    this->_asic->reset();
    this->calculate_hashrate();
    uint8_t asics = this->_asic->init(freq, diff);
    if(asics == 0){
        LOG_E("xxxxxxx No BM1366 ASIC(s) found xxxxxxx");
        return false;
    }
    LOG_I("======= Found %d BM1366 ASICs =======", asics);
    this->_asic->change_uart_baud(ESP32_TO_BM1366_WORK_BUAD);
    return this->_asic->clear_port_cache();
}

esp_err_t AsicMinerClass::listen_asic_rsp(asic_result *result, uint32_t timeout_ms){
    /* logic from project bitaxe: https://github.com/skot/bitaxe */
    /* Thanks for their efforts on this project */
    esp_err_t err = this->_asic->wait_for_result(result, timeout_ms);
    if(ESP_OK == err){
        uint32_t core_id   =    ((result->nonce >> 24) & 0xff) |       // Move byte 3 to byte 0
                                ((result->nonce  << 8) & 0xff0000) |   // Move byte 1 to byte 2
                                ((result->nonce  >> 8) & 0xff00) |     // Move byte 2 to byte 1
                                ((result->nonce  << 24)& 0xff000000);  // Move byte 0 to byte 3
        core_id = (uint8_t)((core_id >> 25) & 0x7f);
        uint8_t small_core = result->job_id & 0x07;
        result->job_id     = result->job_id & 0xf8; 
    }
    return err;
}

bool AsicMinerClass::mining(pool_job_data_t *pool_job){
    if(this->_asic == NULL) return false;
    ////////////////////////////////////////construct asic job//////////////////////////////////
    this->_asic_job_now.id = (this->_asic_job_now.id + 8) % 128;
    this->_pool_job_id_now = pool_job->id;
    String  extranonce2    = g_nmaxe.stratum.get_sub_extranonce2();
    /**************************************** coinhash ****************************************/
    String coinbaseStr = pool_job->coinb1 + g_nmaxe.stratum.get_sub_extranonce1() + extranonce2 + pool_job->coinb2;
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

    // LOG_W("Job id [%03d] constructed with extranonce2 [%s]", this->_asic_job_now.id, extranonce2.c_str());

    ////////////////////////////////////////send asic job//////////////////////////////////
    this->_asic->send_work_to_asic(&this->_asic_job_now);
    return true;
}

bool AsicMinerClass::set_asic_diff(uint64_t diff){
    this->_asic->set_job_difficulty(diff);
    return true;
}

double AsicMinerClass::get_asic_diff(){
    return this->_asic->get_asic_difficulty();
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
    return g_nmaxe.stratum.submit(this->_pool_job_id_now, extranonce2, ntime, nonce, version);
}

double AsicMinerClass::calculate_hashrate(){
    static uint32_t hr_cal_time_begin = millis();
    static std::vector<double>    diff_samples;
    static std::vector<uint32_t>  time_samples;
    static const uint8_t          hr_sample_len_max = 50;
    double total_diff = 0.0, duration = 0.0, hashrate = 0.0;

    diff_samples.push_back(this->_asic->get_asic_difficulty());
    time_samples.push_back(millis());

    if(diff_samples.size() > hr_sample_len_max){
        diff_samples.erase(diff_samples.begin());
        time_samples.erase(time_samples.begin());
        hr_cal_time_begin = time_samples.front();
    }

    for(auto diff : diff_samples){
        total_diff += diff;
    }

    duration = (double)(millis() - hr_cal_time_begin) / 1000.0;//convert to second  

    hashrate = (duration > 0) ? ((total_diff * 4294967296.0) / duration) : 0.0;

    return hashrate;
}

bool AsicMinerClass::end(){
    this->_asic->clear_port_cache();
    return true;
}




void miner_asic_tx_thread_entry(void *args){
    char *name = (char*)malloc(20);
    strcpy(name, (char*)args);
    LOG_I("%s thread started on core %d...", name, xPortGetCoreID());
    free(name);

    g_nmaxe.miner = new AsicMinerClass(bm1366);

    pool_job_data_t pool_job;
    double last_diff = BM1366_DIFF_THR;

    //wait for first job cache ready forever
    xSemaphoreTake(g_nmaxe.stratum.new_job_xsem, portMAX_DELAY);
    //begin asic hardware
    if(!g_nmaxe.miner->begin(g_nmaxe.asic.frequency_req, BM1366_DIFF_THR)){
        LOG_E("Miner begin failed, Miner thread exit");
        delete g_nmaxe.miner, delete bm1366;
        vTaskDelete(NULL);
    }
    //forever loop
    while (true){
        //null loop if not subscribed
        if(!g_nmaxe.stratum.is_subscribed()){
            g_nmaxe.miner->end();
            g_nmaxe.mstatus.hashrate = 0.0;
            delay(1000);
            continue;   
        }
        //wait for new job signal 1000ms max
        if(xSemaphoreTake(g_nmaxe.stratum.new_job_xsem, 1000) != pdTRUE) {
            continue;
        }

        //get job from pool job caches
        pool_job = g_nmaxe.stratum.pop_job_cache();
        if(pool_job.id == "")continue;

        g_nmaxe.mstatus.network_diff = g_nmaxe.miner->calculate_diff(pool_job.nbits);
        LOG_I("Get [%s] net %s", pool_job.id.c_str(), formatNumber(g_nmaxe.mstatus.network_diff, 4).c_str());
        while (true){
            //construct asic job and send to asic every 2s
            if(!g_nmaxe.miner->mining(&pool_job)) continue;
            //exit if pool disconnected
            if(!g_nmaxe.stratum.is_subscribed()) break;

            //set asic diff as pool diff if pool diff < initial asic diff
            if(g_nmaxe.stratum.get_pool_difficulty() <= BM1366_DIFF_THR){
                static double last_diff = BM1366_DIFF_THR;
                if(g_nmaxe.stratum.get_pool_difficulty() != last_diff){
                    LOG_W("Change asic diff from [%s] to [%s]", formatNumber(g_nmaxe.miner->get_asic_diff(), 4).c_str(), formatNumber(g_nmaxe.stratum.get_pool_difficulty(), 4).c_str());
                    last_diff = g_nmaxe.stratum.get_pool_difficulty();
                    g_nmaxe.miner->set_asic_diff(last_diff);
                }
            }

            //interval 2000ms every asic job, exit if a new pool job arrived
            if(xSemaphoreTake(g_nmaxe.stratum.new_job_xsem, 2000) == pdTRUE) {
                //avoid some stale share submit, clear job cache if clean job signal received
                if(xSemaphoreTake(g_nmaxe.stratum.clear_job_xsem, 0) == pdTRUE) {
                    g_nmaxe.miner->clear_asic_job_cache();
                    LOG_W("Job cache clear...");
                }
                xSemaphoreGive(g_nmaxe.stratum.new_job_xsem);//release the semaphore for next pool job
                break;
            }
        }
    }
}

void miner_asic_rx_thread_entry(void *args){
    char *name = (char*)malloc(20);
    strcpy(name, (char*)args);
    LOG_I("%s thread started on core %d...", name, xPortGetCoreID());
    free(name);

    asic_job job = {0,};
    asic_result result = {0,};
    esp_err_t err = ESP_FAIL;

    //wait for miner instance ready
    while (g_nmaxe.miner == NULL)delay(10);
    //wait for first job cache ready forever
    xSemaphoreTake(g_nmaxe.stratum.new_job_xsem, portMAX_DELAY);

    //forever loop
    while(true){
        if(!g_nmaxe.stratum.is_subscribed()){
            delay(1000);
            continue;
        }
        err = g_nmaxe.miner->listen_asic_rsp(&result);
        if(ESP_OK == err){
            if(!g_nmaxe.stratum.is_subscribed()) continue;
            if(g_nmaxe.miner->find_job_by_asic_job_id(result.job_id, &job)){
                uint32_t version_bits = (reverse_uint16(result.version) << 13);  //logic from project bitaxe: https://github.com/skot/bitaxe 
                uint32_t version      = version_bits | (*(uint32_t*)job.version);//logic from project bitaxe: https://github.com/skot/bitaxe 

                double diff = g_nmaxe.miner->calculate_diff(version, job.prev_block_hash, job.merkle_root, *(uint32_t*)job.ntime, *(uint32_t*)job.nbits, result.nonce);

                //skip if diff <= 0.0001 or diff is nan or diff is inf
                if((diff <= 0.0001) || std::isnan(diff) || (diff == std::numeric_limits<double>::infinity())) continue;

                //update hashrate anyway, even if diff < pool diff, some high diff pool may need this, avoid local hashrate freeze. 
                g_nmaxe.mstatus.hashrate        = g_nmaxe.miner->calculate_hashrate();
                //update diff record
                g_nmaxe.mstatus.last_diff       = diff;
                g_nmaxe.mstatus.best_session    = (diff > g_nmaxe.mstatus.best_session) ? diff : g_nmaxe.mstatus.best_session;
                g_nmaxe.mstatus.best_ever       = (diff > g_nmaxe.mstatus.best_ever) ? diff : g_nmaxe.mstatus.best_ever;

                //submit share if diff >= pool diff
                if(diff >= g_nmaxe.stratum.get_pool_difficulty()){
                    uint32_t version_submit = version ^ (*(uint32_t*)job.version);
                    String   extra2_submit = g_nmaxe.miner->get_extranonce2_by_asic_job_id(result.job_id);
                    g_nmaxe.miner->submit_job_share(extra2_submit, result.nonce, *(uint32_t*)job.ntime, version_submit);

                    LOG_I("+---------------------+");
                    LOG_I("|        %sH/s      |", formatNumber(g_nmaxe.mstatus.hashrate, 2).c_str());
                    LOG_I("+---------------------+");
                    LOG_I("| Last diff | %s  |", formatNumber(g_nmaxe.mstatus.last_diff, 4).c_str());
                    LOG_I("| Pool diff | %s  |", formatNumber(g_nmaxe.stratum.get_pool_difficulty(), 4).c_str());
                    LOG_I("| From boot | %s  |", formatNumber(g_nmaxe.mstatus.best_session, 4).c_str());
                    LOG_I("| Best ever | %s  |", formatNumber(g_nmaxe.mstatus.best_ever, 4).c_str());
                    LOG_I("| Netw diff | %s  |", formatNumber(g_nmaxe.mstatus.network_diff, 4).c_str());
                    LOG_I("+---------------------+");
                }
                else {
                    LOG_W("Diff [%s/%s/%s]", formatNumber(g_nmaxe.miner->get_asic_diff(), 3).c_str(), formatNumber(diff, 3).c_str(), formatNumber(g_nmaxe.stratum.get_pool_difficulty(), 4).c_str());
                    continue;
                }

                //update the block hit counter
                if(diff >= g_nmaxe.mstatus.network_diff){
                    g_nmaxe.mstatus.block_hits ++;

                    uint8_t header[4 + 32 + 32 + 4 + 4 + 4] = {0,};
                    uint8_t hash[32] = {0,};
                    uint8_t prev_block_hash_t[32] = {0}, merkle_root_t[32] = {0};
                    memcpy(prev_block_hash_t, job.prev_block_hash, 32);
                    memcpy(merkle_root_t, job.merkle_root, 32);
                    reverse_words(prev_block_hash_t, 32);
                    reverse_words(merkle_root_t, 32);
                    memcpy(header, (uint8_t*)&version, 4);
                    memcpy(header + 4, prev_block_hash_t, 32);
                    memcpy(header + 36, merkle_root_t, 32);
                    memcpy(header + 68, (uint8_t*)&job.ntime, 4);
                    memcpy(header + 72, (uint8_t*)&job.nbits, 4);
                    memcpy(header + 76, (uint8_t*)&result.nonce, 4);
                    //caculate hash
                    csha256d(header, sizeof(header), hash);

                    LOG_W("*************** Your Are The Chosen One *****************");
                    LOG_I("!!!!!!!!!!!!!!!!!!!!!!BLOCK FOUND!!!!!!!!!!!!!!!!!!!!!!!!");
                    log_i("Nonce       : %08x", result.nonce);
                    log_i("\r\nVersion     : %08x", version);

                    log_i("\r\nBlock header: ");
                    for(int i = 0; i < 40; i++)log_i("%02x", header[i]);
                    log_i("\r\n              ");
                    for(int i = 40; i < 80; i++)log_i("%02x", header[i]);

                    log_i("\r\nBlock hash  : ");
                    for(int i = 0; i < sizeof(hash); i++)log_i("%02x", hash[i]);

                    log_i("\r\n");
                    LOG_I("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\r\n");
                    xSemaphoreGive(g_nmaxe.mstatus.nvs_save_xsem);
                }
                //update all time best diff
                if(diff == g_nmaxe.mstatus.best_ever){
                    xSemaphoreGive(g_nmaxe.mstatus.nvs_save_xsem);
                }
            }
        }
        else if(ESP_ERR_INVALID_SIZE == err) {
            LOG_W("Asic response size error.");
        }
        else if(ESP_ERR_TIMEOUT == err) {
            LOG_W("Asic response timeout.");
        }
        else if(ESP_ERR_INVALID_RESPONSE == err) {
            LOG_W("Asic response header error.");
        }
        else{
            LOG_W("Asic response error: %s", esp_err_to_name(err));
        }
    }
}