#include <Arduino.h>
#include "stratum.h"
#include "utils/logger/logger.h"
#include "utils/sha/csha256.h"
#include <cfloat>
#include "utils/helper.h"
#include "esp_log.h"
#include "global.h"
#include <sstream>
#include <iomanip>


StratumClass::~StratumClass(){
    this->_rsp_json.garbageCollect();
}

void StratumClass::reset(){
    this->_job_counter = 0;
    this->_rsp_str = "";
    this->_rsp_json.clear();
    this->_msg_rsp_map.clear();
    this->_sub_info.extranonce1 = "";
    this->_sub_info.extranonce2 = "0";
    this->_sub_info.extranonce2_size = 0;
    this->_sub_info = {"", "", 0};
    this->_is_subscribed = false;
    this->_is_authorized = false;
    this->_pool_difficulty = 0.0;
    this->_vr_mask = 0xffffffff;
    this->_suggest_diff_support = true;
    this->_gid = 1;
}

void StratumClass::reset(pool_info_t pConfig, stratum_info_t sConfig){
    if(this->pool == NULL) return;
    delete this->pool;
    
    this->pool = new PoolClass(pConfig);

    this->_job_counter = 0;
    this->_stratum_info = sConfig;
    this->_rsp_str = "";
    this->_rsp_json.clear();
    this->_msg_rsp_map.clear();
    this->_sub_info.extranonce1 = "";
    this->_sub_info.extranonce2 = "0";
    this->_sub_info.extranonce2_size = 0;
    this->_sub_info = {"", "", 0};
    this->_is_subscribed = false;
    this->_is_authorized = false;
    this->_pool_difficulty = 0.0;
    this->_vr_mask = 0xffffffff;
    this->_suggest_diff_support = true;
    this->_gid = 1;
}

uint32_t StratumClass::_get_msg_id(){
    return this->_gid++;
}

bool StratumClass::_parse_rsp(){
    DeserializationError error = deserializeJson(this->_rsp_json, this->_rsp_str);
    if (error) {
        LOG_E("StratumClass::_parse_rsp Failed to parse JSON, error: %s", error.c_str());
        LOG_E("  Raw string len=%d : [%s]", this->_rsp_str.length(), this->_rsp_str.c_str());
        return false;
    }
    return true;
}

bool StratumClass::_clear_rsp_id_cache(){
    if(this->_msg_rsp_map.size() > this->_max_rsp_id_cache){
        for(auto it = this->_msg_rsp_map.begin(); it != this->_msg_rsp_map.end();){
            if(it->first < this->_gid - this->_max_rsp_id_cache){
                it = this->_msg_rsp_map.erase(it);
                LOG_D("Message ID [%d] [%s] cleared from cache, cache size %d", it->first, it->second.method.c_str(), this->_msg_rsp_map.size());
            }else{
                it++;
            }
        }
    }
    return true;
}

bool StratumClass::hello_pool(uint32_t hello_interval, uint32_t lost_max_time){
    if(!this->pool->is_connected()) return false;
    this->_clear_rsp_id_cache();//clear cache of msg id
    if((millis() - this->pool->get_last_write_ms() > hello_interval) && this->_suggest_diff_support){
        uint32_t id = this->_get_msg_id();
        String payload = "{\"id\": " + String(id) + ", \"method\": \"mining.suggest_difficulty\", \"params\": [" + String(this->_pool_difficulty, 4) + "]}\n";
        if(this->pool->write(payload) != 0){
            this->_msg_rsp_map[id] = {"mining.suggest_difficulty", false, millis()};
            LOG_W("Hello pool...");
            return true;
        }
        else{
            LOG_W("Failed to send mining.suggest_difficulty, last sent to pool %lu s ago, reconnecting...", (millis() - this->pool->get_last_write_ms()) / 1000);
            this->reset();
            this->pool->end();
            return false;
        }
    }
    if(millis() - this->pool->get_last_read_ms() > lost_max_time){
        LOG_W("It seems pool inactive, last received from pool %lu s ago, reconnecting...", (millis() - this->pool->get_last_read_ms()) / 1000);
        this->reset();
        this->pool->end();
        return false;
    }
    return true;
}

stratum_method_data StratumClass::listen_methods(){
    this->_rsp_str = this->pool->readline();
    if(this->_rsp_str == ""){
        return {-1, STRATUM_DOWN_PARSE_ERROR, "", ""};
    }

    if(!this->_parse_rsp()){
        return {-1, STRATUM_DOWN_PARSE_ERROR, "", ""};
    }

    int32_t id = (this->_rsp_json["id"] == nullptr) ? -1 : this->_rsp_json["id"];

    if(this->_rsp_json.containsKey("method")){
        if(this->_rsp_json["method"] == "mining.notify"){
            return {id, STRATUM_DOWN_NOTIFY, "mining.notify", this->_rsp_str};
        }
        if(this->_rsp_json["method"] == "mining.set_difficulty"){
            return {id, STRATUM_DOWN_SET_DIFFICULTY, "mining.set_difficulty", this->_rsp_str};
        }
        if(this->_rsp_json["method"] == "mining.set_version_mask"){
            return {id, STRATUM_DOWN_SET_VERSION_MASK, "mining.set_version_mask", this->_rsp_str};
        }
        if(this->_rsp_json["method"] == "mining.set_extranonce"){
            return {id, STRATUM_DOWN_SET_EXTRANONCE, "mining.set_extranonce", this->_rsp_str};
        }
    }
    else{
        if(this->_rsp_json["error"].isNull()){
            return {id, STRATUM_DOWN_SUCCESS, "", this->_rsp_str};
        }else{
            //'suggest_difficulty' method didn't support 
            if(4 == id){
                this->_suggest_diff_support = false;
                LOG_W("Pool doesn't support suggest_difficulty!");
            }
            return {id, STRATUM_DOWN_ERROR, "", this->_rsp_str};
        }
    }
    return {id, STRATUM_DOWN_UNKNOWN, "", this->_rsp_str};
}

String StratumClass::get_sub_extranonce1(){
    return this->_sub_info.extranonce1;
}

String StratumClass::get_sub_extranonce2() {
    uint64_t ext2 = strtoull(this->_sub_info.extranonce2.c_str(), NULL, 16); 
    ext2++;
    ext2 = ext2 & ((1ULL << (8 * this->_sub_info.extranonce2_size)) - 1);

    char buffer[2 * this->_sub_info.extranonce2_size + 1];
    snprintf(buffer, sizeof(buffer), "%0*llx", 2 * this->_sub_info.extranonce2_size, ext2);
    String next_ext2(buffer);

    this->_sub_info.extranonce2 = next_ext2;
    return next_ext2;
}

bool StratumClass::clear_sub_extranonce2(){
    this->_sub_info.extranonce2 = "0";
    return (this->_sub_info.extranonce2 == "0");
}   

void StratumClass::set_sub_extranonce1(String extranonce1){
    this->_sub_info.extranonce1 = extranonce1;
}

void StratumClass::set_sub_extranonce2_size(int size){
    this->_sub_info.extranonce2_size = size;
}

bool StratumClass::subscribe(){
    this->_sub_info.extranonce2 = "";
    this->_sub_info.extranonce2_size = 0;
    this->_is_subscribed = false;
    
    uint32_t id = this->_get_msg_id();
    String payload = "{\"id\": " + String(id) + ", \"method\": \"mining.subscribe\", \"params\": [\"" +  g_board.info.spec.name + "/" + BOARD_CURRENT_FW_VERSION +"\"]}\n";
    if(this->pool->write(payload) == 0){
        LOG_E("Failed to send mining.subscribe request");
        return false;
    }


    //wait for response
    uint32_t start = millis();
    while (true){
        this->_rsp_str = this->pool->readline(100);
        if(this->_rsp_str == "" ) {
            if(millis() - start > 1000*10){
                LOG_E("Failed to read mining.subscribe response");
                return false;
            }
        }else{
            break;
        }
    }
    
    if(!this->_parse_rsp()){
        LOG_E("Failed to parse mining.subscribe response");
        this->_rsp_json.clear();
        return false;
    }


    if(!this->_rsp_json.containsKey("result")) {
        LOG_E("Response missing 'result' field");
        this->_rsp_json.clear();
        return false;
    }
    
    if(!this->_rsp_json["result"].is<JsonArray>()) {
        LOG_E("'result' is not an array");
        this->_rsp_json.clear();
        return false;
    }
    
    JsonArray result = this->_rsp_json["result"].as<JsonArray>();
    if(result.size() < 3) {
        LOG_E("'result' array size < 3, size: %d", result.size());
        this->_rsp_json.clear();
        return false;
    }


    this->_sub_info.extranonce1 = String((const char*)this->_rsp_json["result"][1]);
    this->_sub_info.extranonce2_size = this->_rsp_json["result"][2];
    this->_is_subscribed = true;
    this->_msg_rsp_map[id] = {"mining.subscribe", false, millis()};
    log_i("Sending mining.subscribe : %s", payload.c_str());
    LOG_I("extranonce1 : %s", this->_sub_info.extranonce1.c_str());
    LOG_I("extranonce2 size : %d", this->_sub_info.extranonce2_size);
    return true;
}

bool StratumClass::authorize(){
    uint32_t id = this->_get_msg_id();
    String payload = "{\"id\": " + String(id) + ", \"method\": \"mining.authorize\", \"params\": [\"" + this->_stratum_info.user+ "\", \"" + this->_stratum_info.pwd + "\"]}\n";
    if(this->pool->write(payload) != payload.length()){
        LOG_E("Failed to send mining.authorize request");
        return false;
    }
    this->_msg_rsp_map[id] = {"mining.authorize", false, millis()};
    log_i("Sending mining.authorize : %s", payload.c_str());
    delay(100);
    return true;
}

bool StratumClass::suggest_difficulty(){
    uint32_t id = this->_get_msg_id();
    String payload = "{\"id\": " + String(id) + ", \"method\": \"mining.suggest_difficulty\", \"params\": [" + String(this->_pool_difficulty, 4) + "]}\n";
    if(this->pool->write(payload) != payload.length()){
        LOG_E("Failed to send mining.suggest_difficulty request");
        return false;
    }
    this->_msg_rsp_map[id] = {"mining.suggest_difficulty", false, millis()};
    log_i("Sending mining.suggest_difficulty : %s", payload.c_str());
    delay(100);
    return true;
}

bool StratumClass::config_version_rolling(){
    uint32_t id = this->_get_msg_id();
    String payload = "{\"id\": " + String(id) + ", \"method\": \"mining.configure\", \"params\": [[\"version-rolling\"], {\"version-rolling.mask\": \"ffffffff\"}]}\n";
    if(this->pool->write(payload) != payload.length()){
        LOG_E("Failed to send mining.configure request");
        return false;
    }
    this->_msg_rsp_map[id] = {"mining.configure", false, millis()};
    log_i("Sending mining.configure : %s", payload.c_str());
    delay(100);
    return true;
}

bool StratumClass::submit(String pool_job_id, String extranonce2, uint32_t ntime, uint32_t nonce, uint32_t version){
    if(!this->pool->is_connected()) return false;
    uint32_t msgid = this->_get_msg_id();
    char version_str[9] = {0,}, nonce_str[9] = {0,};
    sprintf(version_str, "%08x", version);
    sprintf(nonce_str, "%08x", nonce);

    String payload = "{\"id\": " + String(msgid) + ", \"method\": \"mining.submit\", \"params\": [\"" + 
    this->_stratum_info.user + "\", \"" + 
    pool_job_id + "\", \"" + 
    extranonce2 + "\", \"" + 
    String(ntime, 16) + "\", \"" + 
    String(nonce_str) + "\", \"" + 
    String(version_str) + "\"]}\n";

    if(this->pool->write(payload) != payload.length()){
        LOG_E("Failed to send mining.submit request");
        return false;
    }
    this->_msg_rsp_map[msgid] = {"mining.submit", false, millis()};
    // log_i("%s", payload.c_str());

    //wait for response from pool
    uint32_t start = millis();
    while(true){
        if(this->_msg_rsp_map[msgid].status) return true;
        if(millis() - start > 1000*20) return false;
        delay(1);
    }
    return false;
}

bool StratumClass::is_submit_timeout(){
    bool timeout = true, has_submit = false;
    
    //cache size check
    if(this->_msg_rsp_map.size() <= this->_max_rsp_id_cache / 2) return false;

    //check if there is any submit request
    for(auto it = this->_msg_rsp_map.begin(); it != this->_msg_rsp_map.end();it++){
        if(it->second.method == "mining.submit"){
            has_submit = true;
            break;
        }
    }
    if(!has_submit) return false;

    //timeout check, if all submit has no response, return true
    for(auto it = this->_msg_rsp_map.begin(); it != this->_msg_rsp_map.end();it++){
        if((it->second.method == "mining.submit") && (it->second.status)){//submit response received
            timeout = false;
            break;
        }
    }
    return timeout;
}

size_t StratumClass::push_job_cache(pool_job_data_t job){
    LOG_D("");
    if (this->_pool_job_cache.size() >= this->_pool_job_cache_size) {
        LOG_D("Job [%s] popped from cache...", this->_pool_job_cache.front().id.c_str());
        this->_pool_job_cache.pop_front();
    }
    this->_pool_job_cache.push_back(job);
    LOG_D("---Job cache [%02d]---", this->_pool_job_cache.size());
    for(size_t i =0; i < this->_pool_job_cache.size(); i++){
        LOG_D("Job id : %s", this->_pool_job_cache[i].id.c_str());
    }
    LOG_D("--------------------");
    return this->_pool_job_cache.size();
}

size_t StratumClass::get_job_cache_size(){
    return this->_pool_job_cache.size();
}

size_t StratumClass::clear_job_cache(){
    this->_pool_job_cache.clear();
    return this->_pool_job_cache.size();
}

pool_job_data_t StratumClass::pop_job_cache(){
    if(this->_pool_job_cache.empty()){
        return pool_job_data_t();
    }
    pool_job_data_t job = this->_pool_job_cache.front();
    this->_pool_job_cache.pop_front();
    return job;
}

bool StratumClass::set_msg_rsp_map(uint32_t id, bool status){
    auto it = this->_msg_rsp_map.find(id);
    if(it == this->_msg_rsp_map.end()){
        LOG_E("Message ID [%d] not found in response map", id);
        return false;
    }
    LOG_D("Message [%s] with ID [%d] status set to [%s]", it->second.method.c_str(), id, status ? "true" : "false");
    it->second.status = status;
    return true;
}

bool StratumClass::del_msg_rsp_map(uint32_t id){
    auto it = this->_msg_rsp_map.find(id);
    if(it == this->_msg_rsp_map.end()){
        LOG_E("Message ID [%d] not found in response map", id);
        return false;
    }
    LOG_D("Message [%s] with ID [%d] deleted from response map, cache size %d", it->second.method.c_str(), id, this->_msg_rsp_map.size());
    this->_msg_rsp_map.erase(it);
    return true;
}

stratum_rsp StratumClass::get_method_rsp_by_id(uint32_t id){
    stratum_rsp rsp = {
        .method = "",
        .status = false
    };
    if (!this->_msg_rsp_map.empty()) {
       if(this->_msg_rsp_map.find(id) != this->_msg_rsp_map.end()){
           rsp = this->_msg_rsp_map[id];
       }
    }
    return rsp;
}

bool StratumClass::is_primary_pool_available(String url, uint16_t port){
    WiFiClient client;
    client.setTimeout(3000);
    bool connected = client.connect(url.c_str(), port);
    if (connected) {
        client.stop();
        return true;
    }
    return false;
}
