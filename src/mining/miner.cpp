#include "stratum.h"
#include "logger.h"
#include "miner.h"
#include <esp_task_wdt.h>
#include "monitor.h"
#include "helper.h"
#include <limits> 
#include "global.h"
#include "csha256.h"
#include "hwserial_adapter.h"
#include "helper.h"

static BMxxx *asic_instance = NULL;

AsicMinerClass::AsicMinerClass(BMxxx *asic){
    this->_asic = asic;
    this->_asic_count = 0;
    this->pool_job_now.id = "";
    this->_asic_job_map.clear();
    memset(&this->_asic_job_now, 0, sizeof(asic_job));
}

AsicMinerClass::~AsicMinerClass(){

}

bool AsicMinerClass::begin(uint16_t freq, uint16_t diff){
    this->_asic->reset();
    this->_asic_count = this->_asic->init(freq, diff);
    if(0 == this->_asic_count){
        LOG_E("xxxxxxx No %s ASIC found xxxxxxx", g_nmaxe.asic.model);
        return false;
    }
    LOG_I("======= Found %d %s %s (%d/%d)=======", this->_asic_count, g_nmaxe.asic.model, (this->_asic_count > 1) ? "chips" : "chip" , this->_asic->get_cores(), this->_asic->get_small_cores());
    this->_asic->change_uart_baud(ESP32_TO_BM13xx_WORK_BUAD);
    this->_asic->clear_port_cache();
    return true;
}

esp_err_t AsicMinerClass::listen_asic_rsp(asic_result *result, uint32_t timeout_ms){
    /* logic from project bitaxe: https://github.com/skot/bitaxe */
    /* Thanks for their efforts on this project */
    esp_err_t err = this->_asic->wait_for_result(result, timeout_ms);
    return err;
}

bool AsicMinerClass::mining(pool_job_data_t *pool_job){
    if(this->_asic == NULL) return false;
    ////////////////////////////////////////construct asic job//////////////////////////////////
    uint8_t step = 8;
    if(g_nmaxe.asic.model == "BM1366")       step = 8;
    else if (g_nmaxe.asic.model == "BM1370") step = 24;
    else LOG_W("Unknown ASIC model, using default step 8");

    this->_asic_job_now.id = (this->_asic_job_now.id + step) % 128;

    this->pool_job_now.id  = pool_job->id;
    String  extranonce2    = g_nmaxe.stratum->get_sub_extranonce2();
    /**************************************** coinhash ****************************************/
    String coinbaseStr = pool_job->coinb1 + g_nmaxe.stratum->get_sub_extranonce1() + extranonce2 + pool_job->coinb2;
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

bool AsicMinerClass::set_asic_diff(uint64_t diff){
    this->_asic->set_job_difficulty(diff);
    return true;
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
    return g_nmaxe.stratum->submit(this->pool_job_now.id, extranonce2, ntime, nonce, version);
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



void miner_asic_init_thread_entry(void *args){
    char *name = (char*)malloc(20);
    strcpy(name, (char*)args);
    LOG_I("%s thread started on core %d...", name, xPortGetCoreID());
    free(name);
    
    static HwSerialAdapter serial_adapter = HwSerialAdapter(Serial1);

    if(g_nmaxe.asic.model == "BM1366")      asic_instance = new BM1366(serial_adapter, ESP32_TO_BM13xx_INIT_BUAD, NM_AXE_ESP32_RX_TO_BM13xx, NM_AXE_ESP32_TX_TO_BM13xx, NM_AXE_ESP32_RST_TO_BM13xx);
    else if(g_nmaxe.asic.model == "BM1370") asic_instance = new BM1370(serial_adapter, ESP32_TO_BM13xx_INIT_BUAD, NM_AXE_ESP32_RX_TO_BM13xx, NM_AXE_ESP32_TX_TO_BM13xx, NM_AXE_ESP32_RST_TO_BM13xx);
    else{
        LOG_W("Unknown board model %s", g_nmaxe.board.hw_model.c_str());
        return;
    }

    //miner instance
    g_nmaxe.miner = new AsicMinerClass(asic_instance);

    //begin asic hardware
    if(!g_nmaxe.miner->begin(g_nmaxe.asic.frequency_req, ASIC_DIFF_THR)){
        while (true){
            LOG_E("Miner low power!");
            delay(1000);
        }
    }
    
    LOG_I("ASIC job interval set to %d ms", g_nmaxe.asic.job_frq_ms);
    delay(1000);//wait for asic init stable
    vTaskDelete(NULL);
}

void miner_asic_tx_thread_entry(void *args){
    char *name = (char*)malloc(20);
    strcpy(name, (char*)args);
    LOG_I("%s thread started on core %d...", name, xPortGetCoreID());
    free(name);

    //wait for first job cache ready forever
    xSemaphoreTake(g_nmaxe.stratum->new_job_xsem, portMAX_DELAY);
    delay(2000);//necessary delay for first job cache ready

    //forever loop
    while (true){
        //null loop if not subscribed
        if(!g_nmaxe.stratum->is_subscribed()){
            g_nmaxe.miner->end();
            g_nmaxe.mstatus.hashrate._3m = 0.0;
            delay(1000);
            continue;   
        }
        //wait for new job signal 1000ms max
        if(xSemaphoreTake(g_nmaxe.stratum->new_job_xsem, 1000) != pdTRUE) {
            continue;
        }

        //get job from pool job caches
        g_nmaxe.miner->pool_job_now = g_nmaxe.stratum->pop_job_cache();
        if(g_nmaxe.miner->pool_job_now.id == "")continue;
        
        //calculate network diff
        g_nmaxe.mstatus.diff.network = g_nmaxe.miner->calculate_diff(g_nmaxe.miner->pool_job_now.nbits);
        //update pool diff
        g_nmaxe.mstatus.diff.pool = g_nmaxe.stratum->get_pool_difficulty();
        
        LOG_W("Job [%s] from %s:%d", g_nmaxe.miner->pool_job_now.id.c_str(), g_nmaxe.stratum->pool->get_pool_info().url.c_str(), g_nmaxe.stratum->pool->get_pool_info().port);
        while (true){
            //construct asic job and send to asic every 2s
            if(!g_nmaxe.miner->mining(&g_nmaxe.miner->pool_job_now)) continue;
            //exit if pool disconnected
            if(!g_nmaxe.stratum->is_subscribed()) break;

            //set asic diff as pool diff if pool diff < initial asic diff
            if(g_nmaxe.stratum->get_pool_difficulty() <= ASIC_DIFF_THR){
                static double last_diff = ASIC_DIFF_THR;
                if(g_nmaxe.stratum->get_pool_difficulty() != last_diff){
                    LOG_W("Try to change asic diff from [%s] to [%s]", formatNumber(g_nmaxe.miner->get_asic_diff(), 4).c_str(), formatNumber(g_nmaxe.stratum->get_pool_difficulty(), 4).c_str());
                    last_diff = g_nmaxe.stratum->get_pool_difficulty();
                    g_nmaxe.miner->set_asic_diff(last_diff);
                }
            }

            //interval 2000ms every asic job, exit if a new pool job arrived
            if(xSemaphoreTake(g_nmaxe.stratum->new_job_xsem, g_nmaxe.asic.job_frq_ms) == pdTRUE) {
                g_nmaxe.stratum->clear_sub_extranonce2();
                //avoid some stale share submit, clear job cache if clean job signal received
                if(xSemaphoreTake(g_nmaxe.stratum->clear_job_xsem, 0) == pdTRUE) {
                    g_nmaxe.miner->clear_asic_job_cache();
                    LOG_D("Job cache clear...");
                }
                xSemaphoreGive(g_nmaxe.stratum->new_job_xsem);//release the semaphore for next pool job
                break;
            }
        }
    }
}







#include <algorithm>
#include <numeric>
#include <vector>
#include <unordered_set>
void add_share_diff_history(std::deque<proximity_node_t, PsramAllocator<proximity_node_t>> &hist, const proximity_node_t &node, size_t max_history){
    hist.push_back(node);

    if (hist.size() <= max_history) return;

    const size_t n = hist.size();
    const size_t keep = std::min<size_t>(3, n);

    // 索引数组，按 share_diff 降序部分排序
    std::vector<size_t> idx(n);
    std::iota(idx.begin(), idx.end(), 0);
    std::nth_element(idx.begin(), idx.begin() + keep, idx.end(),
                     [&](size_t a, size_t b) {
                         return hist[a].share_diff > hist[b].share_diff;});

    std::unordered_set<uint64_t> protected_epochs;
    for (size_t i = 0; i < keep; ++i) protected_epochs.insert(hist[idx[i]].epoch);

    // 从前向后删除第一个不受保护的元素，直到长度满足
    while (hist.size() > max_history) {
        if (protected_epochs.find(hist.front().epoch) == protected_epochs.end()) {
            hist.pop_front();
            continue;
        }
        auto it = std::find_if(hist.begin(), hist.end(),
                               [&](const proximity_node_t &p) {
                                   return protected_epochs.find(p.epoch) == protected_epochs.end();
                               });
        if (it == hist.end()) {
            // 所有元素都受保护（极端情况），退出以避免无限循环
            break;
        }
        hist.erase(it);
    }
}






void miner_asic_rx_thread_entry(void *args){
    char *name = (char*)malloc(20);
    strcpy(name, (char*)args);
    LOG_I("%s thread started on core %d...", name, xPortGetCoreID());
    free(name);

    asic_job job = {0,};
    asic_result result = {0,};

    //wait for first job cache ready forever
    xSemaphoreTake(g_nmaxe.stratum->new_job_xsem, portMAX_DELAY);

    //forever loop
    while(true){
        if(!g_nmaxe.stratum->is_subscribed()){
            delay(1000);
            continue;
        }
        esp_err_t err = g_nmaxe.miner->listen_asic_rsp(&result, 1000*30);
        if(ESP_OK == err){
            if(!g_nmaxe.stratum->is_subscribed()) continue;
            if(g_nmaxe.miner->find_job_by_asic_job_id(result.job_id, &job)){
                g_nmaxe.mstatus.asic_update = millis();
                uint32_t version_bits       = (reverse_uint16(result.version) << 13);  //logic from project bitaxe: https://github.com/skot/bitaxe 
                uint32_t version            = version_bits | (*(uint32_t*)job.version);//logic from project bitaxe: https://github.com/skot/bitaxe 
                double diff                 = g_nmaxe.miner->calculate_diff(version, job.prev_block_hash, job.merkle_root, *(uint32_t*)job.ntime, *(uint32_t*)job.nbits, result.nonce);

                //skip if diff <= 0.0001 or diff is nan or diff is inf
                if((diff <= std::numeric_limits<double>::epsilon()) || std::isnan(diff) || std::isinf(diff) || (diff < 0.1)) continue;

                //update hashrate anyway, even if diff < pool diff, some high diff pool may need this, avoid local hashrate freeze. 
                g_nmaxe.miner->calculate_hashrate(&g_nmaxe.mstatus.hashrate);

                //print summary to log
                static uint32_t summary_start = millis();
                if(millis() - summary_start >= 1000*60){
                    summary_start = millis();
                    LOG_I(" ============%s=========== ",g_nmaxe.board.fw_version.c_str());
                    LOG_I("|            Summary           |");
                    LOG_I("+------------Uptime------------+");
                    LOG_I("|%s | %s |", convert_uptime_to_string(g_nmaxe.mstatus.uptime_session).c_str(), convert_uptime_to_string(g_nmaxe.mstatus.uptime_ever).c_str());
                    LOG_I("+-----------HashRate-----------+");
                    LOG_I("|   3m    |    30m   |    1h   |");
                    LOG_I("|%-4sH/s| %-4sH/s|%-4sH/s|", 
                        formatNumber(g_nmaxe.mstatus.hashrate._3m, 4).c_str(), 
                        formatNumber(g_nmaxe.mstatus.hashrate._30m, 4).c_str(),
                        formatNumber(g_nmaxe.mstatus.hashrate._1h, 4).c_str());
                    LOG_I("+----------Difficulty----------+");
                    LOG_I("|From boot| Best ever| Network |");
                    LOG_I("| %-6s |  %-5s | %-7s |", 
                        formatNumber(g_nmaxe.mstatus.diff.best_session, 5).c_str(), 
                        formatNumber(g_nmaxe.mstatus.diff.best_ever, 5).c_str(),
                        formatNumber(g_nmaxe.mstatus.diff.network, 5).c_str());
                    LOG_I("+---Free heap-----Efficiency---+");
                    LOG_I("|    %-3sKB   |   %-3sJ/TH   |", formatNumber(ESP.getFreeHeap() / 1024.0f, 4).c_str(), formatNumber(g_nmaxe.board.efficiency, 4).c_str());
                    LOG_I(" ============================== ");
                    log_i("\r\n");
                    LOG_I(" ++++++++++ Real Time +++++++++");
                    LOG_I("| ASIC | Last | Pool | Network |");
                    LOG_I("|------|------|------|---------|");
                }
                
                LOG_I("|%-6s|%-6s|%-6s|%-7s|", 
                    formatNumber(g_nmaxe.miner->get_asic_diff(), 4).c_str(), 
                    formatNumber(diff, 4).c_str(), 
                    formatNumber(g_nmaxe.stratum->get_pool_difficulty(), 4).c_str(),
                    formatNumber(g_nmaxe.mstatus.diff.network, 7).c_str());

                //skip if diff < pool diff
                if(diff < g_nmaxe.stratum->get_pool_difficulty())continue; 
                
                //submit sulution
                uint32_t version_submit = version ^ (*(uint32_t*)job.version);
                String   extra2_submit = g_nmaxe.miner->get_extranonce2_by_asic_job_id(result.job_id);
                bool res = g_nmaxe.miner->submit_job_share(extra2_submit, result.nonce, *(uint32_t*)job.ntime, version_submit);
                if(!res) continue;
                
                //update the block hit counter
                if(diff >= g_nmaxe.mstatus.diff.network){
                    g_nmaxe.mstatus.hits = (g_nmaxe.mstatus.hits >= 99) ? 0 : (g_nmaxe.mstatus.hits);
                    g_nmaxe.mstatus.hits++;

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

                    LOG_W("******************************* Your Are The Chosen One ********************************");
                    LOG_I("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!BLOCK FOUND!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
                    log_i("Nonce       : %08x", result.nonce);
                    log_i("\r\nVersion     : %08x", version);

                    log_i("\r\nBlock header: ");
                    for(int i = 0; i < 40; i++)log_i("%02x", header[i]);
                    log_i("\r\n              ");
                    for(int i = 40; i < 80; i++)log_i("%02x", header[i]);

                    log_i("\r\nBlock hash  : ");
                    for(int i = 0; i < sizeof(hash); i++)log_i("%02x", hash[i]);

                    log_i("\r\n");
                    LOG_I("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\r\n");

                    xSemaphoreGive(g_nmaxe.mstatus.nvs_save_xsem);
                }

                //update miner status
                g_nmaxe.mstatus.diff.last            = diff;
                g_nmaxe.mstatus.diff.best_session    = (diff > g_nmaxe.mstatus.diff.best_session) ? diff : g_nmaxe.mstatus.diff.best_session;
                g_nmaxe.mstatus.diff.best_ever       = (diff > g_nmaxe.mstatus.diff.best_ever) ? diff : g_nmaxe.mstatus.diff.best_ever;

                //update all time best diff
                if(diff == g_nmaxe.mstatus.diff.best_ever){
                    xSemaphoreGive(g_nmaxe.mstatus.nvs_save_xsem);
                }
                
                //add share to History of block proximity
                if(xSemaphoreTake(g_nmaxe.mstatus.block_proximity_mutex, portMAX_DELAY) == pdTRUE){
                    proximity_node_t node;
                    node.block_proximity = diff / g_nmaxe.mstatus.diff.network;
                    node.share_diff      = diff;
                    node.net_diff        = g_nmaxe.mstatus.diff.network;
                    node.epoch           = g_nmaxe.mstatus.utc * 1000ULL;
                    add_share_diff_history(g_nmaxe.mstatus.block_proximity_history, node, 1000);
                    xSemaphoreGive(g_nmaxe.mstatus.block_proximity_mutex);
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