#include "pool.h"
#include "utils/logger/logger.h"
#include "utils/helper.h"

PoolClass::PoolClass(pool_info_t config){
    this->_pool_cfg         = config;
    this->_pool_ip          = IPAddress(0,0,0,0);
    this->_wificlientSecure = new WiFiClientSecure();
    this->_wificlient       = new WiFiClient();
    this->_last_err_str     = "";
    this->_pwclient         = this->_wificlient;
}

PoolClass::~PoolClass(){
    // stop connection first, before any delete (pwclient points to one of the two below)
    if(this->_pwclient != nullptr) {
        this->_pwclient->stop();
        this->_pwclient = nullptr;
    }
    if(this->_wificlient != nullptr)       { delete this->_wificlient;       this->_wificlient = nullptr; }
    if(this->_wificlientSecure != nullptr) { delete this->_wificlientSecure; this->_wificlientSecure = nullptr; }
}

bool PoolClass::begin(bool ssl){
    if(ssl){
        this->_wificlientSecure->setInsecure();
        this->_pwclient  = this->_wificlientSecure;
        if(this->_wificlient != nullptr) {
            delete this->_wificlient;
            this->_wificlient = nullptr;
        }
        LOG_I("ssl enabled.");
    }else{
        this->_pwclient  = this->_wificlient;
        if(this->_wificlientSecure != nullptr) {
            delete this->_wificlientSecure;
            this->_wificlientSecure = nullptr;
        }
        LOG_I("tcp enabled.");
    }
    return (this->_pwclient != nullptr);
}

bool PoolClass::connect(){
    if(this->_pwclient->connected()) return true;
    this->_last_err_str = "";

    LOG_I("Resolve pool address %s use dns1 : %s, dns2 : %s", this->_pool_cfg.url.c_str(), WiFi.dnsIP(0).toString().c_str(), WiFi.dnsIP(1).toString().c_str());
    int err = WiFi.hostByName(this->_pool_cfg.url.c_str(), this->_pool_ip);
    if(err != 1){
        this->_last_err_str = "Wrong pool URL!!!";
        LOG_E("Failed to resolve pool [%s], pool crashed or wrong pool url.", this->_pool_cfg.url.c_str());
        return false;
    }

    LOG_I("Resolving pool address [%s] to  [%s]", this->_pool_cfg.url.c_str(), this->_pool_ip.toString().c_str());
    if(!this->_pwclient->connect(this->_pool_ip, this->_pool_cfg.port, 1000*10)){
        this->_last_err_str = "Wrong pool port!!!";
        return false;
    }
    LOG_I("Connected to pool %s:%d", this->_pool_cfg.url.c_str(), this->_pool_cfg.port);
    return true;
}

void PoolClass::end(){
    if(this->_pwclient->connected()){
        this->_pwclient->stop();
    }
}

bool PoolClass::is_connected(){
    return this->_pwclient->connected();
}

bool PoolClass::available(){
    return this->_pwclient->available();
}

size_t PoolClass::write(const String data){
    size_t ret = this->_pwclient->print(data);
    this->_last_write = (ret > 0) ? millis() : this->_last_write;
    return ret;
}

String PoolClass::readline(uint32_t timeout_ms) {
    if (!this->_pwclient->connected())  return "";
    this->_line = "";
    uint64_t start_time = millis();
    while (millis() - start_time < timeout_ms) {
        if (this->_pwclient->available()) {
            char c = this->_pwclient->read();
            this->_line += c;
            if (c == '\n') break;
        }
    }
    if (this->_line.length() >= 4096) {
        LOG_E("Response too long %d Bytes, discarding", this->_line.length());
        this->_line = ""; 
    }
    this->_last_read = millis();
    return this->_line;
}

String PoolClass::get_last_errormsg(){
    return this->_last_err_str;
}

uint32_t PoolClass::get_last_write_ms(){
    return this->_last_write;
}

uint32_t PoolClass::get_last_read_ms(){
    return this->_last_read;
}

pool_info_t PoolClass::get_pool_info(){
    return this->_pool_cfg;
}