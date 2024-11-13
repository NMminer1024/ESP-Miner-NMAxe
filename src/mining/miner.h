#ifndef MINER_H_
#define MINER_H_
#include <Arduino.h>
#include "stratum.h"
#include "bm1366.h"
#include <map>

class AsicMinerClass{
private:
    BMxxx                       *_asic;  
    asic_job                    _asic_job_now;
    String                      _pool_job_id_now;
    std::map<uint8_t, asic_job> _asic_job_map;
    std::map<uint8_t, String>   _extranonce2_map;
public:
    AsicMinerClass(BMxxx *asic);
    ~AsicMinerClass();
    bool begin(uint16_t freq, uint16_t diff);
    bool mining(pool_job_data_t *pool_job);
    bool set_asic_diff(uint64_t diff);
    double get_asic_diff();
    String get_extranonce2_by_asic_job_id(uint8_t asic_job_id);
    esp_err_t listen_asic_rsp(asic_result *result, uint32_t timeout_ms = 1000*60);
    bool submit_job_share(String extranonce2, uint32_t nonce, uint32_t ntime, uint32_t version);
    bool find_job_by_asic_job_id(uint8_t asic_job_id, asic_job* job);
    bool clear_asic_job_cache();
    double calculate_diff(uint32_t version, uint8_t *prev_block_hash, uint8_t *merkle_root, uint32_t ntime, uint32_t nbits, uint32_t nonce);
    double calculate_diff(String nBits);
    double calculate_hashrate();
    bool end();
};


void miner_asic_tx_thread_entry(void *args);
void miner_asic_rx_thread_entry(void *args);

#endif