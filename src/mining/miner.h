#ifndef MINER_H_
#define MINER_H_
#include <Arduino.h>
#include <map>
#include "stratum.h"
#include "board.h"
#include "bm1366.h"
#include "bm1370.h"
#include <deque>

typedef struct{
    double   _3m;
    double   _30m;
    double   _1h;
}hashrate_t;

// ["hashRate","temp","vrTemp","power","voltage","current","coreVoltageActual","fanspeed","fanrpm","wifiRSSI","freeram","freepsram","timestamp"],
typedef struct{
    String         hashrate;      // hashrate, GH/s
    String         asic_temp;     // asic temperature, C
    String         vcore_temp;    // vcore temperature, C
    String         pbus;          // power, W
    String         vbus;          // voltage, V
    String         ibus;          // current, A
    uint16_t       vcore;         // vcore measured, mV
    uint16_t       fanspeed;      // fan speed, %
    uint16_t       fanrpm;        // fan rpm, RPM
    int8_t         wifi_rssi;     // wifi rssi, dBm
    uint32_t       free_ram;      // free ram, Kbytes
    uint32_t       free_psram;    // free psram, Kbytes
    uint64_t       epoch;         // timestamp, milliseconds since epoch
}history_node_t;

typedef struct{
    float           block_proximity; // block share, percentage 0-100%
    float           share_diff;      // share difficulty
    float           net_diff;        // network difficulty
    uint64_t        epoch;           // timestamp, milliseconds since epoch
}proximity_node_t;




class AsicMinerClass{
private:
    BMxxx                       *_asic;  
    float                       _asic_diff_thr;
    uint8_t                     _asic_count;
    asic_job                    _asic_job_now;
    std::map<uint8_t, asic_job> _asic_job_map;
    std::map<uint8_t, String>   _extranonce2_map;
public:
    pool_job_data_t             pool_job_now;
    AsicMinerClass(BMxxx *asic);
    ~AsicMinerClass();
    bool begin(uint16_t freq, uint16_t diff, uint32_t baudrate);
    bool mining(pool_job_data_t *pool_job);
    bool set_asic_diff(uint64_t diff);
    uint8_t get_asic_count();
    uint16_t get_asic_small_cores();
    double get_asic_diff();
    String get_extranonce2_by_asic_job_id(uint8_t asic_job_id);
    esp_err_t listen_asic_rsp(miner_result *result, uint32_t timeout_ms = 1000*60);
    bool submit_job_share(String extranonce2, uint32_t nonce, uint32_t ntime, uint32_t version);
    bool find_job_by_asic_job_id(uint8_t asic_job_id, asic_job* job);
    bool clear_asic_job_cache();
    double calculate_diff(uint32_t version, uint8_t *prev_block_hash, uint8_t *merkle_root, uint32_t ntime, uint32_t nbits, uint32_t nonce);
    double calculate_diff(String nBits);
    bool calculate_hashrate(hashrate_t *phr);
    bool end();
};

void add_share_diff_history(std::deque<proximity_node_t> &hist, proximity_node_t &node, size_t max_history);

#endif