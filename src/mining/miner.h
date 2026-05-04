#ifndef MINER_H_
#define MINER_H_
#include <Arduino.h>
#include <map>
#include "stratum/stratum.h"
#include "board/board.h"
#include "drivers/asic/bm1366/bm1366.h"
#include "drivers/asic/bm1370/bm1370.h"
#include <deque>

typedef struct{
    double   _3m;
    double   _30m;
    double   _1h;
}hashrate_t;

// ["hashRate","temp","vrTemp","power","voltage","current","coreVoltageActual","fanspeed","fanrpm","wifiRSSI","freeram","freepsram","timestamp"],
// All numeric fields — eliminates per-node String heap allocations that fragment PSRAM.
typedef struct{
    float          hashrate;      // hashrate, GH/s
    float          asic_temp;     // asic temperature, C
    float          vcore_temp;    // vcore temperature, C
    float          pbus;          // power, W
    float          vbus;          // voltage, V
    float          ibus;          // current, A
    uint16_t       vcore;         // vcore measured, mV
    uint16_t       fanspeed;      // fan speed, %
    uint16_t       fanrpm;        // fan rpm, RPM
    int8_t         wifi_rssi;     // wifi rssi, dBm
    uint32_t       free_ram;      // free ram, Kbytes
    uint32_t       free_psram;    // free psram, Kbytes
    uint32_t       latency;       // latency, ms
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
    std::map<uint8_t, String>   _pool_job_id_map;   // bind asic_job_id -> pool_job_id at construction time
    std::deque<std::pair<uint32_t, double>, PsramAllocator<std::pair<uint32_t, double>>> _hr_deque; // single 60-min sample ring (PSRAM)
    // Running sums + front cursors for incremental O(1) hashrate maintenance.
    // _off_3m / _off_30m are offsets (from current deque front) of the oldest
    // sample STILL inside the 3m / 30m window — i.e. count of front samples
    // already excluded from that window's sum.
    double _s3m  = 0.0, _s30m = 0.0, _s60m = 0.0;
    size_t _off_3m = 0,  _off_30m = 0;
public:
    pool_job_data_t             pool_job_now;
    AsicMinerClass(BMxxx *asic);
    ~AsicMinerClass();
    bool begin(uint16_t freq, uint16_t diff, uint32_t baudrate);
    bool mining(pool_job_data_t *pool_job);
    uint32_t set_asic_diff(uint64_t diff);
    uint8_t connect_chip();
    uint8_t get_asic_count();
    uint16_t get_asic_small_cores();
    double get_asic_diff();
    String get_extranonce2_by_asic_job_id(uint8_t asic_job_id);
    String get_pool_job_id_by_asic_job_id(uint8_t asic_job_id);
    esp_err_t listen_asic_rsp(miner_result *result, uint32_t timeout_ms = 1000*60);
    bool submit_job_share(String pool_job_id, String extranonce2, uint32_t nonce, uint32_t ntime, uint32_t version);
    bool find_job_by_asic_job_id(uint8_t asic_job_id, asic_job* job);
    bool clear_asic_job_cache();
    bool calculate_hashrate(hashrate_t *phr);
    void record_nonce();      // call on every valid ASIC nonce (ASIC RX thread)
    void reset_hashrate();    // call on stratum disconnect to clear stale samples
    bool end();
};

void add_share_diff_history(std::deque<proximity_node_t, PsramAllocator<proximity_node_t>> &hist, proximity_node_t &node, size_t max_history);

#endif