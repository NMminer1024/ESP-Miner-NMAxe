#include <Arduino.h>
#include "stratum.h"
#include "logger.h"
#include "csha256.h"
#include <cfloat>
#include "monitor.h"
#include "helper.h"
#include "esp_log.h"
#include "global.h"
#include <sstream>
#include <iomanip>


StratumClass::~StratumClass(){
    this->_rsp_json.garbageCollect();
}

void StratumClass::reset(){
    this->_rsp_str = "";
    this->_rsp_json.clear();
    this->_msg_rsp_map.clear();
    this->_sub_info.extranonce1 = "";
    this->_sub_info.extranonce2 = "0";
    this->_sub_info.extranonce2_size = 0;
    this->_sub_info = {"", "", 0};
    this->_is_subscribed = false;
    this->_is_authorized = false;
    this->_pool_difficulty = DEFAULT_POOL_DIFFICULTY;
    this->_vr_mask = 0xffffffff;
    this->_suggest_diff_support = true;
    this->_gid = 1;
}

void StratumClass::reset(pool_info_t pConfig, stratum_info_t sConfig){
    psramDeallocator(this->pool);
    void* pool_buf  = psramAllocator(sizeof(PoolClass));
    this->pool = new(pool_buf) PoolClass(pConfig);

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
    this->_pool_difficulty = DEFAULT_POOL_DIFFICULTY;
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
        LOG_E("Failed to parse JSON: %s => %s", error.c_str(), this->_rsp_str.c_str());
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
    this->_clear_rsp_id_cache();//clear cache of msg id
    if((millis() - this->pool->get_last_write_ms() > hello_interval) && this->_suggest_diff_support){
        uint32_t id = this->_get_msg_id();
        String payload = "{\"id\": " + String(id) + ", \"method\": \"mining.suggest_difficulty\", \"params\": [" + String(this->_pool_difficulty, 4) + "]}\n";
        if(this->pool->write(payload) != 0){
            this->_msg_rsp_map[id] = {"mining.suggest_difficulty", false, millis()};
            LOG_D("Hello pool...");
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
    String payload = "{\"id\": " + String(id) + ", \"method\": \"mining.subscribe\", \"params\": [\"" +  g_nmaxe.board.hw_model + "/" + CURRENT_FW_VERSION +"\"]}\n";
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
    return true;
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

void stratum_thread_entry(void *args){
    char *name = (char*)malloc(20);
    strcpy(name, (char*)args);
    LOG_I("%s thread started on core %d...", name, xPortGetCoreID());
    free(name);

    g_nmaxe.stratum->set_pool_difficulty(DEFAULT_POOL_DIFFICULTY);
    StaticJsonDocument<1024*4> json;
    while(true){
        static int w_retry = 0, w_maxRetries = 24;
        if(g_nmaxe.connection.wifi.status_param.status != WL_CONNECTED){
            w_retry++;
            LOG_W("WiFi reconnecting %d/%d...", w_retry, w_maxRetries);
            if(w_retry >= w_maxRetries) ESP.restart();

            xSemaphoreGive(g_nmaxe.connection.wifi.reconnect_xsem);
            g_nmaxe.stratum->reset();
            delay(5000);
            continue;
        } else w_retry = 0;
        
        static uint16_t p_retry = 0, p_maxRetries = 5;
        if(!g_nmaxe.stratum->pool->is_connected()){
            static bool    first_connect = true;
            if(first_connect){
                LOG_I("Pool connecting...");
                first_connect = false;
            }else LOG_W("Lost connection to pool, reconnecting %d/%d...", p_retry, p_maxRetries);
            
            if(++p_retry % p_maxRetries == 0){
                static bool sel_fallback = true;
                if(sel_fallback){
                    sel_fallback = false;
                    g_nmaxe.connection.pool_use    = g_nmaxe.connection.pool_fallback;
                    g_nmaxe.connection.stratum_use = g_nmaxe.connection.stratum_fallback;
                    LOG_W(">>>> Set pool to fallback [%s:%d] <<<<", g_nmaxe.connection.pool_use.url.c_str(), g_nmaxe.connection.pool_use.port);
                }else{
                    sel_fallback = true;
                    g_nmaxe.connection.pool_use    = g_nmaxe.connection.pool_primary;
                    g_nmaxe.connection.stratum_use = g_nmaxe.connection.stratum_primary;
                    LOG_W(">>>> Set pool to primary [%s:%d] <<<<", g_nmaxe.connection.pool_use.url.c_str(), g_nmaxe.connection.pool_use.port);
                }
            }
            g_nmaxe.stratum->reset(g_nmaxe.connection.pool_use, g_nmaxe.connection.stratum_use);
            g_nmaxe.stratum->pool->begin(g_nmaxe.connection.pool_use.ssl);
            g_nmaxe.stratum->pool->connect();
            g_nmaxe.mstatus.diff.last = 0;
            delay(5000);
            continue;
        }else p_retry = 0;

        if(!g_nmaxe.stratum->is_subscribed()){
            if(!g_nmaxe.stratum->subscribe()){
                LOG_W("Failed to subscribe to pool, retrying in 5 seconds...");
                delay(100);
                continue;
            }
            if(!g_nmaxe.stratum->authorize()){
                LOG_W("Failed to authorize to pool, retrying in 5 seconds...");
                delay(100);
                continue;
            }
            if(!g_nmaxe.stratum->config_version_rolling()){
                LOG_W("Failed to config version rolling, retrying in 5 seconds...");
                delay(100);
                continue;
            }
            if(!g_nmaxe.stratum->suggest_difficulty()){
                LOG_W("Failed to suggest difficulty to pool, retrying in 5 seconds...");
                delay(100);
                continue;
            }
        }

        if(!g_nmaxe.stratum->hello_pool(HELLO_POOL_INTERVAL_MS, POOL_INACTIVITY_TIME_MS)){
            LOG_W("Pool is inactive, retrying in 5 seconds...");
            delay(5000);
            continue;
        }

        while(g_nmaxe.stratum->pool->available()){
            g_nmaxe.connection.stratum_update = millis();//pool is alive
            stratum_method_data method = g_nmaxe.stratum->listen_methods();
            switch (method.type){
                case STRATUM_DOWN_PARSE_ERROR:   
                    LOG_E("Stratum parse error, id : %d, raw : %s", method.id, method.raw.c_str());
                    break;
                case STRATUM_DOWN_NOTIFY:{
                        LOG_D("Stratum notify, id : %d => %s", method.id, method.raw.c_str());
                        pool_job_data_t job;
                        json.clear();
                        DeserializationError error = deserializeJson(json, method.raw);
                        if (error) {
                            LOG_E("Failed to parse JSON: %s", error.c_str());
                            break;
                        }
                        job.id = String((const char*) json["params"][0]);
                        job.prevhash = String((const char*) json["params"][1]);
                        job.coinb1 = String((const char*) json["params"][2]);
                        job.coinb2 = String((const char*) json["params"][3]);
                        job.merkle_branch = json["params"][4];
                        job.version = String((const char*) json["params"][5]);
                        job.nbits = String((const char*) json["params"][6]);
                        job.ntime = String((const char*) json["params"][7]);
                        job.clean_jobs = json["params"][8]; 

                        LOG_D("Job ID            : %s", job.id.c_str());
                        LOG_D("Prevhash          : %s", job.prevhash.c_str());
                        LOG_D("Coinb1            : %s", job.coinb1.c_str());
                        LOG_D("Coinb2            : %s", job.coinb2.c_str());
                        for(int i = 0; i < job.merkle_branch.size(); i++){
                            LOG_D("Merkle branch[%02d] : %s", i, job.merkle_branch[i].as<String>().c_str());
                        }
                        LOG_D("Version           : %s", job.version.c_str());
                        LOG_D("Nbits             : %s", job.nbits.c_str());
                        LOG_D("Ntime             : %s", job.ntime.c_str());
                        LOG_D("Clean jobs        : %s", job.clean_jobs ? "true" : "false");
                        LOG_D("Stamp             : %lu", job.stamp);
                        LOG_D("Version mask      : 0x%08x", g_nmaxe.stratum->get_version_mask());
                        LOG_D("Pool difficulty   : %s", formatNumber(g_nmaxe.stratum->get_pool_difficulty(), 5).c_str());

                        if(job.clean_jobs){
                            g_nmaxe.stratum->clear_job_cache();
                            xSemaphoreGive(g_nmaxe.stratum->clear_job_xsem);
                        }
                        size_t cached_size = g_nmaxe.stratum->push_job_cache(job);
                        
                        //Give the new job semaphore to the other threads
                        xSemaphoreGive(g_nmaxe.stratum->new_job_xsem);//asic tx thread
                        static bool first_job = true;
                        if(first_job){
                            //first job will release the asic rx , monitor and ui thread
                            xSemaphoreGive(g_nmaxe.stratum->new_job_xsem);//asic tx thread
                            xSemaphoreGive(g_nmaxe.stratum->new_job_xsem);//asic rx thread
                            xSemaphoreGive(g_nmaxe.stratum->new_job_xsem);//ui thread
                            xSemaphoreGive(g_nmaxe.stratum->new_job_xsem);//monitor thread
                            first_job = false;
                        }
                    }         
                    break;
                case STRATUM_DOWN_SET_DIFFICULTY: {
                    LOG_D("Stratum set difficulty, id : %d => %s", method.id, method.raw.c_str());
                    json.clear();
                    DeserializationError error = deserializeJson(json, method.raw);
                    if(error){
                        LOG_E("Failed to parse JSON: %s", error.c_str());
                        break;
                    }
                    if(json["method"] == "mining.set_difficulty"){
                        if(json["params"].size() > 0){
                            g_nmaxe.stratum->set_pool_difficulty(json["params"][0]);
                            LOG_D("Pool difficulty set : %s", formatNumber(json["params"][0], 5).c_str());
                        }else{
                            LOG_W("Pool difficulty not found in params");
                        }
                    }
                }
                    break;
                case STRATUM_DOWN_SET_VERSION_MASK:{
                    LOG_D("Stratum set version mask , id : %d => %s", method.id, method.raw.c_str());
                    g_nmaxe.stratum->set_msg_rsp_map(method.id, true);
                    json.clear();
                    DeserializationError error = deserializeJson(json, method.raw);
                    if (error) {
                        LOG_E("Failed to parse JSON: %s", error.c_str());
                        break;
                    }
                    if(json["method"] == "mining.set_version_mask"){
                        if(json["params"].size() > 0){
                            g_nmaxe.stratum->set_version_mask(strtoul(json["params"][0].as<const char*>(), NULL, 16));
                            LOG_L("Version mask set to %s", json["params"][0].as<const char*>());
                        }else{
                            g_nmaxe.stratum->set_version_mask(0xffffffff);
                            LOG_W("Version mask not found in params");
                        }
                    }else{
                        g_nmaxe.stratum->set_version_mask(0xffffffff);
                        LOG_W("Version rolling key not found in response");
                    }
                    g_nmaxe.stratum->del_msg_rsp_map(method.id);
                }
                    break;
                case STRATUM_DOWN_SET_EXTRANONCE:{
                        LOG_L("Stratum set extranonce => %s", method.id, method.raw.c_str());
                        json.clear();
                        DeserializationError error = deserializeJson(json, method.raw);
                        if (error) {
                            LOG_E("Failed to parse JSON: %s", error.c_str());
                            break;
                        }
                        g_nmaxe.stratum->set_sub_extranonce1(json["params"][0]);
                        g_nmaxe.stratum->set_sub_extranonce2_size(json["params"][1]);
                    }
                    break;
                case STRATUM_DOWN_SUCCESS: 
                    if(method.id != -1){
                        g_nmaxe.stratum->set_msg_rsp_map(method.id, true);
                        stratum_rsp rsp = g_nmaxe.stratum->get_method_rsp_by_id(method.id);
                        if(rsp.method == "mining.submit"){
                            uint32_t latency = millis() - rsp.stamp;
                            if (rsp.status == true){
                                g_nmaxe.mstatus.share_accepted++;
                                LOG_L("#%d share accepted, %ldms", g_nmaxe.mstatus.share_accepted + g_nmaxe.mstatus.share_rejected, latency);      
                            }
                            else {
                                g_nmaxe.mstatus.share_rejected++;
                                LOG_E("#%d share rejected, %ldms", g_nmaxe.mstatus.share_accepted + g_nmaxe.mstatus.share_rejected, latency);
                            }
                        }
                        else if(rsp.method == "mining.configure"){
                            json.clear();
                            DeserializationError error = deserializeJson(json, method.raw);
                            if (error) {
                                LOG_E("Failed to parse JSON: %s", error.c_str());
                            } else {
                                g_nmaxe.stratum->set_version_mask(0xffffffff);
                                if (json["result"]["version-rolling"] == true) {
                                    if (json["result"].containsKey("version-rolling.mask")) {
                                        g_nmaxe.stratum->set_version_mask(strtoul(json["result"]["version-rolling.mask"].as<const char*>(), NULL, 16));
                                        LOG_I("Version mask set to %s", json["result"]["version-rolling.mask"].as<const char*>());
                                    } else {
                                        LOG_W("Version mask not found in response");
                                    }
                                } else {
                                    LOG_W("Version rolling not supported");
                                }
                            }
                        }
                        else if(rsp.method == "mining.authorize"){
                            DeserializationError error = deserializeJson(json, method.raw);
                            if (error) {
                                LOG_E("Failed to parse JSON: %s", error.c_str());
                            }
                            else{
                                if(json.containsKey("result")){
                                    g_nmaxe.stratum->set_authorize(json["result"]);
                                    LOG_W("Authorization %s ", json["result"] ? "success" : "failed");
                                }
                            }
                        }
                        else{
                            LOG_D("Stratum success, id : %d => %s", method.id, method.raw.c_str());
                        }
                    }
                    break;
                case STRATUM_DOWN_ERROR: 
                    if(method.id != -1){
                        g_nmaxe.stratum->set_msg_rsp_map(method.id, true);
                        stratum_rsp rsp = g_nmaxe.stratum->get_method_rsp_by_id(method.id);
                        if(rsp.method == "mining.submit"){
                            uint32_t latency = millis() - rsp.stamp;
                            g_nmaxe.mstatus.share_rejected++;
                            LOG_E("#%d share rejected, %ldms", g_nmaxe.mstatus.share_accepted + g_nmaxe.mstatus.share_rejected, latency);
                        }
                        else if(rsp.method == "mining.authorize"){
                            g_nmaxe.stratum->set_authorize(false);
                            LOG_E("Authorization failed, id %d => %s", method.id, method.raw.c_str());
                        }
                        else{
                            LOG_E("Unknown error response, id : %d => %s", method.id, method.raw.c_str());
                        }
                    }
                    break;
                case STRATUM_DOWN_UNKNOWN:                   
                    LOG_E("Stratum unknown, id : %d => %s", method.id, method.raw.c_str());
                    break;
                default :
                    LOG_E("Stratum unknown, id : %d => %s", method.id, method.raw.c_str());
                    break;
            }
            delay(5);
        }
        delay(50);
    }
}