#include "pool.h"
#include "logger.h"

poolClass::poolClass(pool_info_t config){
    this->_pool_cfg         = config;
    this->_pool_ip          = IPAddress(0,0,0,0);
    this->_wificlientSecure = WiFiClientSecure();
    this->_wificlient       = WiFiClient();
    this->_pwclient = &this->_wificlient;
}

poolClass::~poolClass(){
    this->_pwclient->stop();
}
bool poolClass::begin(bool ssl){
    if(ssl){
        this->_wificlientSecure.setInsecure();
        this->_pwclient  = &this->_wificlientSecure;
        LOG_I("ssl enabled.");
    }
    return true;
}

bool poolClass::connect(){
    if(this->_pwclient->connected()){
        return true;
    }
    LOG_I("Trying to resolve pool address %s use dns1 : %s, dns2 : %s", this->_pool_cfg.url.c_str(), WiFi.dnsIP(0).toString().c_str(), WiFi.dnsIP(1).toString().c_str());
    WiFi.hostByName(this->_pool_cfg.url.c_str(), this->_pool_ip);
    LOG_I("Resolving pool address [%s] to  [%s]", this->_pool_cfg.url.c_str(), this->_pool_ip.toString().c_str());

    static uint16_t err_cnt = 0;
    if(!this->_pwclient->connect(this->_pool_ip, this->_pool_cfg.port)){
        err_cnt++;
        LOG_E("Failed to connect to pool %s:%d, %d times", this->_pool_cfg.url.c_str(), this->_pool_cfg.port, err_cnt);
        if(err_cnt > 100) ESP.restart();
        return false;
    }
    else  err_cnt = 0;
    LOG_I("Connected to pool %s:%d", this->_pool_cfg.url.c_str(), this->_pool_cfg.port);
    return true;
}

void poolClass::end(){
    if(this->_pwclient->connected()){
        this->_pwclient->stop();
    }
}

bool poolClass::is_connected(){
    return this->_pwclient->connected();
}

bool poolClass::available(){
    return this->_pwclient->available();
}

size_t poolClass::write(const String data){
    size_t ret = this->_pwclient->print(data);
    this->_last_write = (ret > 0) ? millis() : this->_last_write;
    return ret;
}

String poolClass::readline(uint32_t timeout_ms) {
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

uint32_t poolClass::get_last_write_ms(){
    return this->_last_write;
}

uint32_t poolClass::get_last_read_ms(){
    return this->_last_read;
}

pool_info_t poolClass::get_pool_info(){
    return this->_pool_cfg;
}