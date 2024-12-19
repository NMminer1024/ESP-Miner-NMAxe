#ifndef STRATUM_H_
#define STRATUM_H_
#include <WiFi.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <map>
#include <vector>
#include <deque>
#include "helper.h"
#include "pool.h"   

#define  DEFAULT_POOL_DIFFICULTY        512
#define  HELLO_POOL_INTERVAL_MS         1000*30
#define  LOST_POOL_TIMEOUT_MS           1000*60*5

typedef uint32_t stratum_msg_rsp_id_t;

typedef enum {
    STRATUM_DOWN_SUCCESS,
    STRATUM_DOWN_NOTIFY,
    STRATUM_DOWN_SET_DIFFICULTY,
    STRATUM_DOWN_SET_VERSION_MASK,
    STRATUM_DOWN_SET_EXTRANONCE,
    STRATUM_DOWN_UNKNOWN,
    STRATUM_DOWN_ERROR,
    STRATUM_DOWN_PARSE_ERROR
} stratum_method_down;

typedef struct{
    String   method;
    bool     status;
    uint64_t stamp;
}stratum_rsp;

typedef struct{
    String      user;
    String      pwd;
    double      diff;
}stratum_info_t;

typedef struct {
    int32_t                  id;
    stratum_method_down      type;
    String                   name;
    String                   raw;
} stratum_method_data;

typedef struct {
    String id;
    String prevhash;
    String coinb1;
    String coinb2;
    String nbits;
    JsonArray merkle_branch;
    String version;
    String ntime;
    bool clean_jobs;
    uint64_t stamp;//local timestamp, for job order
}pool_job_data_t;

typedef struct {
    String extranonce1;
    String extranonce2;
    int extranonce2_size;
} stratum_subscribe_info_t;

class StratumClass{
private:
    stratum_info_t  _stratum_info;
    bool            _is_subscribed;
    bool            _is_authorized;
    uint64_t        _last_job_clear_stamp;
    uint32_t        _gid;
    uint32_t        _get_msg_id();
    String          _rsp_str;
    bool            _parse_rsp();
    bool            _clear_rsp_id_cache();
    bool            _suggest_diff_support;
    uint32_t        _vr_mask;//version rolling mask
    double          _pool_difficulty;
    StaticJsonDocument<4096> _rsp_json;
    stratum_subscribe_info_t _sub_info;
    uint32_t        _max_rsp_id_cache;
    uint8_t         _pool_job_cache_size;
    std::deque<pool_job_data_t>                  _pool_job_cache;
    std::map<stratum_msg_rsp_id_t, stratum_rsp>  _msg_rsp_map;
public:
    poolClass  pool;
    StratumClass(){};
    StratumClass(pool_info_t pConfig, stratum_info_t sConfig, uint8_t job_cached_max): 
        pool(pConfig), _stratum_info(sConfig), _pool_job_cache_size(job_cached_max)
    {
        this->_max_rsp_id_cache = 20;
        this->_pool_difficulty = DEFAULT_POOL_DIFFICULTY;
        this->_gid = 1;
        this->_rsp_str = "";
        this->_vr_mask = 0xffffffff;
        this->_rsp_json.clear();
        this->_sub_info = {"", "", 0};
        this->_msg_rsp_map.clear();
        this->_suggest_diff_support = true;
        this->_is_subscribed = false;
        this->_is_authorized = false;
        this->_last_job_clear_stamp = micros();
        this->new_job_xsem   = xSemaphoreCreateCounting(5,0);
        this->clear_job_xsem = xSemaphoreCreateCounting(1,0);
    };
    ~StratumClass();
    SemaphoreHandle_t new_job_xsem;
    SemaphoreHandle_t clear_job_xsem;
    void reset();
    bool subscribe();
    bool authorize();
    bool suggest_difficulty();
    bool cfg_version_rolling();
    bool submit(String pool_job_id, String extranonce2, uint32_t ntime, uint32_t nonce, uint32_t version);
    bool hello_pool(uint32_t hello_interval, uint32_t lost_max_time);
    stratum_method_data listen_methods();
    bool is_subscribed();
    bool is_authorized();
    size_t push_job_cache(pool_job_data_t job);
    size_t get_job_cache_size();
    size_t clear_job_cache();
    pool_job_data_t pop_job_cache();
    stratum_rsp get_method_rsp_by_id(uint32_t id);
    bool set_msg_rsp_map(uint32_t id, bool status);
    bool del_msg_rsp_map(uint32_t id);
    bool set_version_mask(uint32_t mask);
    bool set_authorize(bool status);
    uint32_t get_version_mask();
    bool set_pool_difficulty(double diff);
    bool set_job_clear_stamp(uint64_t stamp);
    uint64_t get_job_clear_stamp();
    uint32_t get_last_submit_id();
    double get_pool_difficulty();
    bool   set_sub_extranonce1(String extranonce1);
    bool   set_sub_extranonce2_size(int size);
    String get_sub_extranonce1();
    String get_sub_extranonce2();
};

void stratum_thread_entry(void *args);
#endif