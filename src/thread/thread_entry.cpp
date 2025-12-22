#include "global.h"
#include "logger.h"
#include "button.h"
#include "display.h"
#include "fan.h"
#include "csha256.h"
#include "nvs_config.h"
#include "timezone.h"
#include <NTPClient.h>

#define UDP_BOARDCAST_ADDR    IPAddress(255,255,255,255)
#define UDP_BOARDCAST_PORT    (12345)
#define SWARM_OFFLINE_TIMEOUT (3*60*1000) //3min



void power_thread_entry(void *args){
    board_sal_t *board = (board_sal_t*)args;
    String taskName = "(power)";
    LOG_I("%s thread started on core %d...", taskName, xPortGetCoreID());
    LOG_I("Initializing power...");

    board->power->set_vcore_range(board->info.spec.asic.min_vcore, board->info.spec.asic.max_vcore);
    LOG_I("Set vcore range to %d-%d mV", board->power->get_vcore_min(), board->power->get_vcore_max());

    //detect power plug or pd plug
    if(board->power->is_dc_pluged()) LOG_I("DC plug detected...");
    else LOG_I("USB plug detected...");

    //detect vbus voltage, if lower than VBUS_MIN_VOLTAGE , wait for power settle or throw error
    board->power->init();
    delay(500);
    while (board->power->get_vbus() < board->info.spec.pwr.vbus_min_required){
        LOG_W("Vbus is %.2fV , at least %.2fV required, waiting for power setup...", board->power->get_vbus()/1000.0, board->info.spec.pwr.vbus_min_required/1000.0);
        delay(1000);
    }
    while (!board->status.fan.self_test){
        delay(1000);
        LOG_W("Fan self test %d/%d...", board->status.fan.rpm, board->info.spec.fan.self_test_rpm_thr);
    }
    
    //set vdd_1v8 and pll_0v8 power
    board->power->set_pll_0v8(PWR_ON);
    board->power->set_vdd_1v8(PWR_ON);
    delay(50);
    //set vcore voltage to required voltage
    board->power->set_vcore_voltage(board->info.spec.asic.req_vcore);
    delay(50);
    board->power->set_vcore_status(PWR_ON);
    while (!board->power->is_vcore_good()){
        LOG_W("Waiting for vcore power setup...");
        delay(100);
    }
    xSemaphoreGive(board->power->good_xsem);
    LOG_I("Power is ready.");
    while(true){
        uint32_t vcore_measure = board->power->get_vcore();
        int32_t err = vcore_measure - board->info.spec.asic.req_vcore;
        if(abs(err) <= 5) {
            LOG_D("Vcore %d/%dmV, error %d mV, power ready", vcore_measure, board->info.spec.asic.req_vcore, err);
            delay(200);
            continue;
        }
        LOG_D("Vcore %d/%dmV, error %d mV, Adjust vcore voltage for error correction %d mV", vcore_measure, board->info.spec.asic.req_vcore, err, err/5);
        static uint32_t vcore_set = board->info.spec.asic.req_vcore;
        vcore_set -= err/5;//half error correction
        vcore_set = (vcore_set < board->power->get_vcore_min()) ? board->power->get_vcore_min() : vcore_set;
        vcore_set = (vcore_set > board->power->get_vcore_max()) ? board->power->get_vcore_max() : vcore_set;
        board->power->set_vcore_voltage(vcore_set);//half error correction
        delay(200);
    }
    //exit
    vTaskDelete(NULL);
}

void led_thread_entry(void *args){
    board_sal_t *board = (board_sal_t*)args;

    String taskName = "(led)";
    LOG_I("%s thread started on core %d...", taskName, xPortGetCoreID());

    LOG_I("Initializing led...");

    // LED pins setup
    const int pwmChannel = 3;   
    const int freq = 5*1000;    
    const int resolution = 8;   
    pinMode(board->info.spec.led.wifi_pin, OUTPUT);
    digitalWrite(board->info.spec.led.wifi_pin, HIGH);

    pinMode(board->info.spec.led.pool_pin, OUTPUT);
    digitalWrite(board->info.spec.led.pool_pin, HIGH);

    pinMode(board->info.spec.led.sys_pin, OUTPUT);
    ledcSetup(pwmChannel, freq, resolution);
    ledcAttachPin(board->info.spec.led.sys_pin, pwmChannel);
    ledcWrite(pwmChannel, 255);// off

    uint64_t led_cnt = 0;
    const uint8_t  dot = 20;
    while(true){
        delay(10);

        if(board->info.preference.led.sleep) {
            // led_off();
            digitalWrite(board->info.spec.led.wifi_pin, HIGH);
            digitalWrite(board->info.spec.led.pool_pin, HIGH);
            ledcWrite(pwmChannel, 255);
            continue;
        }

        if(!board->info.preference.led.enable){
            // led_off();
            digitalWrite(board->info.spec.led.wifi_pin, HIGH);
            digitalWrite(board->info.spec.led.pool_pin, HIGH);
            ledcWrite(pwmChannel, 255);
            continue;
        }
           

        switch (led_cnt % 201){
        case 1 * dot:
                if(board->info.connection.wifi.status_param.status == WL_CONNECTED) digitalWrite(board->info.spec.led.wifi_pin, LOW);
                else digitalWrite(board->info.spec.led.wifi_pin, HIGH);

                if(board->stratum->is_subscribed()) digitalWrite(board->info.spec.led.pool_pin, LOW);
                else digitalWrite(board->info.spec.led.pool_pin, HIGH);

            break;
        case 2 * dot:
                if(board->info.connection.wifi.status_param.status == WL_CONNECTED) digitalWrite(board->info.spec.led.wifi_pin, HIGH);
                else digitalWrite(board->info.spec.led.wifi_pin, LOW);

                if(board->stratum->is_subscribed()) digitalWrite(board->info.spec.led.pool_pin, HIGH);
                else digitalWrite(board->info.spec.led.pool_pin, LOW);

            break;
        case 3 * dot:
                if(board->info.connection.wifi.status_param.status == WL_CONNECTED) digitalWrite(board->info.spec.led.wifi_pin, HIGH);
                else digitalWrite(board->info.spec.led.wifi_pin, HIGH);

                if(board->stratum->is_subscribed()) digitalWrite(board->info.spec.led.pool_pin, HIGH);
                else digitalWrite(board->info.spec.led.pool_pin, HIGH);
            break;
        case 4 * dot:
                if(board->info.connection.wifi.status_param.status == WL_CONNECTED) digitalWrite(board->info.spec.led.wifi_pin, HIGH);
                else digitalWrite(board->info.spec.led.wifi_pin, LOW);

                if(board->stratum->is_subscribed()) digitalWrite(board->info.spec.led.pool_pin, HIGH);
                else digitalWrite(board->info.spec.led.pool_pin, LOW);

            break;
        case 5 * dot:
                if(board->info.connection.wifi.status_param.status == WL_CONNECTED) digitalWrite(board->info.spec.led.wifi_pin, HIGH);
                else digitalWrite(board->info.spec.led.wifi_pin, HIGH);

                if(board->stratum->is_subscribed()) digitalWrite(board->info.spec.led.pool_pin, HIGH);
                else digitalWrite(board->info.spec.led.pool_pin, HIGH);
            break;
        case 6 * dot:
                if(board->info.connection.wifi.status_param.status == WL_CONNECTED) digitalWrite(board->info.spec.led.wifi_pin, HIGH);
                else digitalWrite(board->info.spec.led.wifi_pin, LOW);

                if(board->stratum->is_subscribed()) digitalWrite(board->info.spec.led.pool_pin, HIGH);
                else digitalWrite(board->info.spec.led.pool_pin, LOW);
            break;
        case 7 * dot:
                if(board->info.connection.wifi.status_param.status == WL_CONNECTED) digitalWrite(board->info.spec.led.wifi_pin, HIGH);
                else digitalWrite(board->info.spec.led.wifi_pin, HIGH);

                if(board->stratum->is_subscribed()) digitalWrite(board->info.spec.led.pool_pin, HIGH);
                else digitalWrite(board->info.spec.led.pool_pin, HIGH);
            break;
        case 8 * dot:
                if(board->info.connection.wifi.status_param.status == WL_CONNECTED) digitalWrite(board->info.spec.led.wifi_pin, HIGH);
                else digitalWrite(board->info.spec.led.wifi_pin, LOW);

                if(board->stratum->is_subscribed()) digitalWrite(board->info.spec.led.pool_pin, HIGH);
                else digitalWrite(board->info.spec.led.pool_pin, LOW);
            break;
        case 9 * dot:
                if(board->info.connection.wifi.status_param.status == WL_CONNECTED) digitalWrite(board->info.spec.led.wifi_pin, HIGH);
                else digitalWrite(board->info.spec.led.wifi_pin, HIGH);

                if(board->stratum->is_subscribed()) digitalWrite(board->info.spec.led.pool_pin, HIGH);
                else digitalWrite(board->info.spec.led.pool_pin, HIGH);
            break;
        case 10 * dot:
                if(board->info.connection.wifi.status_param.status == WL_CONNECTED) digitalWrite(board->info.spec.led.wifi_pin, HIGH);
                else digitalWrite(board->info.spec.led.wifi_pin, LOW);

                if(board->stratum->is_subscribed()) digitalWrite(board->info.spec.led.pool_pin, HIGH);
                else digitalWrite(board->info.spec.led.pool_pin, LOW);
            break;
        }

        // sys led, indicate hashrate
        if(board->status.miner.hashrate._3m > 0){
            ledcWrite(pwmChannel, (uint32_t)((1 + sin(led_cnt/100.0f)) * (1<<resolution - 1)));
        }
        else{
            ledcWrite(pwmChannel, (uint32_t)((1 + sin(20*led_cnt/100.0f)) * (1<<resolution - 1)));
        }
        led_cnt++;
    }
    LOG_I("led thread exit...");
    vTaskDelete(NULL);
}

void button_thread_entry(void *args){
  board_sal_t *board = (board_sal_t*)args;
  String taskName = "(button)";
  LOG_I("%s thread started on core %d...", taskName, xPortGetCoreID());
  LOG_I("Initializing buttons...");

  OneButton boot_btn(board->info.spec.btn.boot_pin, true);
  OneButton user_btn(board->info.spec.btn.user_pin, true);

  // link the boot button functions.
  boot_btn.attachClick(ui_switch_next_page_cb);
  boot_btn.attachDoubleClick(silence_mode_cb);
  boot_btn.attachLongPressStart(NULL);
  boot_btn.attachLongPressStop(NULL);
  boot_btn.attachDuringLongPress(force_config_cb);

  // link the user button functions.
  user_btn.attachClick(NULL);
  user_btn.attachDoubleClick(NULL);
  user_btn.attachLongPressStart(NULL);
  user_btn.attachLongPressStop(NULL);
  user_btn.attachDuringLongPress(recover_factory_cb);

  while (true){
    boot_btn.tick();
    user_btn.tick();
    delay(20);
  }
}

void swarm_thread_entry(void *args){
  board_sal_t *board = (board_sal_t*)args;
  String taskName = "(swarm)";
  LOG_I("%s thread started on core %d...", taskName, xPortGetCoreID());
  LOG_I("Initializing swarm...");

  //malloc udp client
  WiFiUDP* udp_client = new WiFiUDP();
  while (udp_client == nullptr){
    LOG_W("Failed to allocate memory for udp client, retry...");  
    delay(1000);
  }
  
  //udp status boardcast begin
  udp_client->begin(UDP_BOARDCAST_PORT);

  uint64_t swarm_cnt = 0;
  char  jsonbuf[1024] = {0,};
  StaticJsonDocument<1024> jsonDoc;
  while (true){
    delay(100);
    if(board->info.connection.wifi.status_param.status != WL_CONNECTED) continue;
    swarm_cnt++;
    //listen udp status
    if(swarm_cnt % 1 == 0){
      int packetSize = udp_client->parsePacket();

      char udpbuf[512] = {0,}, json_udp_str[512] = {0,};
      if ((packetSize > 0) && (packetSize < sizeof(udpbuf))) {
          int len = udp_client->read(udpbuf, packetSize);
          memcpy(json_udp_str, udpbuf, packetSize);
          jsonDoc.clear();
          DeserializationError error = deserializeJson(jsonDoc, json_udp_str);
          if(error) {
            udp_client->flush();
            continue;
          }
          jsonDoc["Lastseen"] = millis();

          memset(jsonbuf, 0, sizeof(jsonbuf));
          size_t n = serializeJson(jsonDoc, jsonbuf);

          //update swarm list if has this ip
          static std::map<String, uint32_t> last_seen_map;
          if(jsonDoc.containsKey("ip")){
            board->status.swarm[jsonDoc["ip"].as<String>()] = String(jsonbuf);
            last_seen_map[jsonDoc["ip"].as<String>()] = jsonDoc["Lastseen"];
          }

          //update json string
          for(auto it = board->status.swarm.begin(); it != board->status.swarm.end();it++){
            //self status not in last seen map
            if(last_seen_map.find(it->first) == last_seen_map.end()) continue;

            if(deserializeJson(jsonDoc, it->second)) continue;

            jsonDoc["Lastseen"] = millis() - last_seen_map[it->first];
            //remove offline device
            if(jsonDoc["Lastseen"].as<uint32_t>() > SWARM_OFFLINE_TIMEOUT){
              board->status.swarm.erase(it->first);
              continue;
            }

            memset(jsonbuf, 0, sizeof(jsonbuf));
            size_t n = serializeJson(jsonDoc, jsonbuf);
            it->second = (n>0) ? String(jsonbuf) : "";
          }
          udp_client->flush();
        }
    }
    //status udp broadcast
    if(swarm_cnt % 20 == 0){
      jsonDoc.clear();
      jsonDoc["ip"] = board->info.connection.wifi.status_param.ip.toString();
      jsonDoc["HashRate"] = formatNumber(board->status.miner.hashrate._3m, 5) + "H/s";
      uint32_t share_total = board->status.miner.share_accepted + board->status.miner.share_rejected;
      float share_accepted = (share_total == 0) ? 0:(float)(board->status.miner.share_accepted) / (float)(share_total);
      jsonDoc["Share"] = String(board->status.miner.share_rejected) + "/"+ String(board->status.miner.share_accepted) + "/" + String(share_accepted * 100, 1) + "%";
      jsonDoc["NetDiff"] = formatNumber(board->status.miner.diff.network,4);
      jsonDoc["PoolDiff"] = formatNumber(board->status.miner.diff.pool,4);
      jsonDoc["LastDiff"] = formatNumber(board->status.miner.diff.last,4);
      jsonDoc["BestDiff"] = formatNumber(board->status.miner.diff.best_session,4) + "\r" + formatNumber(board->status.miner.diff.best_ever,4);
      jsonDoc["Valid"] = board->status.miner.hits;
      jsonDoc["Temp"] = roundf(board->status.temp.asic * 100) / 100.0f;
      jsonDoc["RSSI"] = board->info.connection.wifi.status_param.rssi;
      jsonDoc["FreeHeap"] = ESP.getFreeHeap() / 1024.0f;
      jsonDoc["Uptime"] = convert_uptime_to_string(board->status.miner.uptime_session) + "\r" + convert_uptime_to_string(board->status.miner.uptime_ever);
      jsonDoc["Version"] = board->info.base.fw_version;
      jsonDoc["BoardType"] = board->info.base.hw_model;
      jsonDoc["Power"]     = String(board->status.power.vbus*board->status.power.ibus/1000.0/1000.0, 1) + "W";
      jsonDoc["PoolInUse"] = String(board->info.connection.pool_use.url) + ":" + String(board->info.connection.pool_use.port);
      static uint32_t last_seen = millis();
      jsonDoc["Lastseen"]  = millis() - last_seen;
      last_seen = millis();

      memset(jsonbuf, 0, sizeof(jsonbuf));
      size_t n = serializeJson(jsonDoc, jsonbuf);
      if(n >= sizeof(jsonbuf) || n == 0){
        LOG_E("Swarm json serialize failed or too long: %d/%d", n, sizeof(jsonbuf));
        continue;
      }
      //broadcast status to udp
      udp_client->beginPacket(UDP_BOARDCAST_ADDR, UDP_BOARDCAST_PORT);
      udp_client->write((uint8_t*)jsonbuf, n);
      int res = udp_client->endPacket();
      if(res != 1) {
        LOG_E("Swarm udp broadcast failed: %d", res);
        continue;
      }

      //add self to swarm list
      board->status.swarm[board->info.connection.wifi.status_param.ip.toString()] = String(jsonbuf);
    }
  }
}


void monitor_thread_entry(void *args){
    board_sal_t *board = (board_sal_t*)args;
    String taskName = "(monitor)";
    LOG_I("%s thread started on core %d...", taskName, xPortGetCoreID());
    LOG_I("Initializing monitor...");
    
    // fetch timezone from ipapi
    TimezoneFetcher *tz = new TimezoneFetcher();
    if(!tz->fetch()){
        LOG_W("Timezone fetch failed, using user setting timezone: %s", board->status.time.tz.c_str()); 
    }else{
        board->status.time.tz = tz->timezone;
        LOG_W("Timezone calibrate to : %s", board->status.time.tz.c_str());
    }
    delete tz;

    //ntp client init
    WiFiUDP          udpNtpClient;
    const String     ntpServerUrl= "europe.pool.ntp.org";
    const uint32_t   ntpInterval = 1000*60*60*24;//24h update interval
    NTPClient        ntpClient(udpNtpClient, ntpServerUrl.c_str());

    ntpClient.begin();
    ntpClient.setTimeOffset(0); // Get UTC time without timezone offset
    ntpClient.setUpdateInterval(ntpInterval);

    //wait for first job cache ready forever when process start
    xSemaphoreTake(board->stratum->new_job_xsem, portMAX_DELAY);

    delay(500);//necessary delay for first job cache ready

    while(true){
        //thread delay 1000ms
        static uint32_t last = millis();
        while(millis() - last < 1000*1){
            static uint16_t brightness = board->info.preference.screen.brightness, last_brightness = board->info.preference.screen.brightness;
            static float    x = 0;
            if(board->status.miner.last_hits != board->status.miner.hits){//screen blink if block hit
            brightness = 100*(1 + sin(x))/2;
            x+=0.1;
            }else brightness = board->info.preference.screen.brightness;

            //update screen brightness only when changed
            if(last_brightness != brightness){
            tft_bl_ctrl(brightness);
            last_brightness = brightness;
            }
            delay(10);
        }
        last = millis();

        // update utc time
        if(ntpClient.update()){
            struct timeval tv;

            tv.tv_sec = ntpClient.getEpochTime(); 
            tv.tv_usec = 0;
            settimeofday(&tv, NULL);
            board->status.time.utc = tv.tv_sec; 
            
            // Convert decimal timezone to UTC±H:MM format
            float tz_offset = board->status.time.tz.toFloat();
            int tz_hour = (int)tz_offset;
            int tz_min = (int)((fabs(tz_offset) - abs(tz_hour)) * 60 + 0.5f); // Round to nearest minute
            
            char tz_buf[16] = {0};
            if (tz_offset >= 0) {
                if (tz_min == 0)
                    sprintf(tz_buf, "UTC-%d", tz_hour);
                else
                    sprintf(tz_buf, "UTC-%d:%02d", tz_hour, tz_min);
            } else {
                if (tz_min == 0)
                    sprintf(tz_buf, "UTC+%d", abs(tz_hour));
                else
                    sprintf(tz_buf, "UTC+%d:%02d", abs(tz_hour), tz_min);
            }
            
            setenv("TZ", tz_buf, 1);
            tzset();
            
            String time_local = convert_time_to_local(board->status.time.utc);
            LOG_W("ntp calibrate time UTC[%llu], local[%s], timezone[%s], tz_env[%s]", 
                    board->status.time.utc, time_local.c_str(), board->status.time.tz.c_str(), tz_buf);
        }
        else{
            // update time now
            time_t now;
            time(&now);
            board->status.time.utc = now; 
        }

        board->status.miner.uptime_ever++;
        board->status.miner.uptime_session++;
        //update temperature and power status
        if(board->status.miner.uptime_session % 1 == 0){
            static uint8_t temp_cnt = 0;

            //update power status
            board->status.power.vbus          = (temp_cnt % 2 == 0) ? board->power->get_vbus() : board->status.power.vbus;
            board->status.power.ibus          = (temp_cnt % 2 == 0) ? board->power->get_ibus() : board->status.power.ibus;
            board->status.miner.efficiency    = ((temp_cnt % 2 == 0) && board->status.miner.hashrate._3m > 0) ? (board->status.power.vbus * board->status.power.ibus/1e6) / (board->status.miner.hashrate._3m/1e12) : board->status.miner.efficiency; //J/TH
            board->status.power.vcore         = (temp_cnt % 2 == 0) ? board->power->get_vcore() : board->status.power.vcore;

            temp_cnt++;
            //update wifi rssi
            board->info.connection.wifi.status_param.rssi = WiFi.RSSI();
            //give miner update signal
            xSemaphoreGive(board->status.miner.update_xsem);
        }
        
        //status check
        if(board->status.miner.uptime_session % 2 == 0){
            //check mcu temperature status
            if(board->status.temp.mcu > BOARD_MCU_DANGER){
            LOG_W("MCU temp is too high, restart...");
            delay(1000);
            ESP.restart();
            }
            //check vcore temperature status
            if(board->status.temp.vcore > VCORE_TEMP_DANGER || board->status.temp.asic > ASIC_TEMP_DANGER){
            uint16_t vcore_now = board->power->get_vcore();
            if(vcore_now >= 1200)vcore_now -= 100;
            else{
                static uint8_t cnt = 0;
                if(++cnt > 5){
                LOG_W("Vcore temp reach danger for 5 seconds, restart...");
                delay(1000);
                ESP.restart();
                }
            }
            board->power->set_vcore_voltage(vcore_now);
            LOG_W("Vcore temp reach danger %.1fC, decrease vcore to %d", board->status.temp.vcore, vcore_now);
            }
            //check fan status
            static uint16_t fan_err_cnt = 0;
            if((board->status.fan.rpm <= 500) && (board->status.temp.asic > ASIC_TEMP_NORMAL)){
            fan_err_cnt++;
            if(fan_err_cnt > 20){//avoid some noise
                LOG_W("Fan rpm is too low, restart miner...");
                ESP.restart();
            }
            }else fan_err_cnt = 0;

            //avoid restart when ota running
            if(board->status.ota.running) continue;

            //check power status
            static uint16_t pwr_err_cnt = 0;
            if((board->status.power.vbus * board->status.power.ibus / 1000.0 / 1000.0) < BOARD_LOW_POWER){
            LOG_W("Power %0.1fW is too low...", board->status.power.vbus * board->status.power.ibus / 1000.0 / 1000.0);
            if(++pwr_err_cnt > 30){//30s
                LOG_W("Power is too low, restart miner...");
                ESP.restart();
            }
            }else pwr_err_cnt = 0;

            //check hashrate
            static uint16_t hr_err_cnt = 0;
            if(board->status.miner.hashrate._3m <= 1){
            if(++hr_err_cnt > 60){//1min
                LOG_W("Hashrate is too low, restart miner...");
                ESP.restart();
            }
            }else hr_err_cnt = 0;
        }

        //save status to NVS
        static uint64_t last_save_time = board->status.miner.uptime_session;
        if(board->status.miner.uptime_session - last_save_time > NVS_SAVE_INTERVAL){
            xSemaphoreGive(board->status.nvs_save_xsem);
        }
        
        //save some status to NVS
        if(xSemaphoreTake(board->status.nvs_save_xsem, 0) == pdTRUE){
            nvs_config_set_string(NVS_CONFIG_BEST_EVER, String(board->status.miner.diff.best_ever).c_str());
            nvs_config_set_u16(NVS_CONFIG_BLOCK_HITS, board->status.miner.hits);
            nvs_config_set_u64(NVS_CONFIG_UPTIME, board->status.miner.uptime_ever);
            last_save_time = board->status.miner.uptime_session;
            LOG_W("Save diff best ever [%s], block hits [%d], uptime [%s]", formatNumber(board->status.miner.diff.best_ever, 4).c_str(), board->status.miner.hits, convert_uptime_to_string(board->status.miner.uptime_ever).c_str());
        }

        //save last ui page to NVS
        if(board->status.page_save_xsem != NULL && xSemaphoreTake(board->status.page_save_xsem, 0) == pdTRUE){
            nvs_config_set_u8(NVS_CONFIG_UI_LAST_PAGE, board->info.spec.ui.last_page);
            LOG_D("Last page %d saved to NVS", board->info.spec.ui.last_page);
        }

        //update miner status history queue
        if(board->status.miner.uptime_session % HISTORY_SAMPLE_INTERVAL == 0){
            history_node_t node;
            node.hashrate     = String(board->status.miner.hashrate._3m /1e9, 3); //Ghash/s
            node.asic_temp    = String(board->status.temp.asic,1);
            node.vcore_temp   = String(board->status.temp.vcore,1);
            node.pbus         = String((board->status.power.vbus * board->status.power.ibus / 1000.0f / 1000.0f),2); //W
            node.vbus         = String((board->status.power.vbus / 1000.0f),1); //V
            node.ibus         = String((board->status.power.ibus / 1000.0f),3); //A
            node.vcore        = board->status.power.vcore;//mV
            node.fanspeed     = board->status.fan.speed; //%
            node.fanrpm       = board->status.fan.rpm;
            node.wifi_rssi    = board->info.connection.wifi.status_param.rssi;
            node.free_ram     = ESP.getFreeHeap() / 1024;  //free sram in Kbytes
            node.free_psram   = ESP.getFreePsram() / 1024; //free psram in Kbytes
            node.epoch        = board->status.time.utc * 1000ULL; // Convert UTC seconds to milliseconds

            // add node to history queue and protect concurrent access
            if (xSemaphoreTake(board->status.miner.history_mutex, portMAX_DELAY) == pdTRUE) {
                board->status.miner.status_history.push_back(node);
                //remove old history
                uint64_t current_time_ms = board->status.time.utc * 1000ULL; // Convert to milliseconds
                while (!board->status.miner.status_history.empty()) {
                    uint64_t oldest_time_ms = board->status.miner.status_history.front().epoch; // Already in milliseconds
                    if(current_time_ms - oldest_time_ms > HISTORY_DEEPTH){ 
                        board->status.miner.status_history.pop_front();
                        LOG_D("Remove old history, current size: %d, removed timestamp: %llu", board->status.miner.status_history.size(), oldest_time_ms);
                    } else {
                        break;
                    }
                }
                xSemaphoreGive(board->status.miner.history_mutex);
            }
        }
    }
}

void daemon_thread_entry(void *args){
  board_sal_t *board = (board_sal_t*)args;
  String taskName = "(button)";
  LOG_I("%s thread started on core %d...", taskName, xPortGetCoreID());
  LOG_I("Initializing buttons...");

  while (true){
    delay(1000);

    //check ota status and reboot
    if(xSemaphoreTake(board->status.reboot_xsem, 0) == pdTRUE){
      ESP.restart();
    }
    //WiFi daemon
    if(xSemaphoreTake(board->info.connection.wifi.reconnect_xsem, 0) == pdTRUE){
      WiFi.begin(board->info.connection.wifi.conn_param.ssid.c_str(), board->info.connection.wifi.conn_param.pwd.c_str());
    }
    //Stratum daemon
    if(millis() - board->info.connection.stratum_update > STRATUM_ALIVE_TIMEOUT){
      LOG_W("Stratum connection seems frozen, restarting...");
      delay(100);
      ESP.restart();
    }
    //ASIC daemon
    if(millis() - board->status.miner.asic_update > ASIC_ALIVE_TIMEOUT){
      LOG_W("ASIC seems frozen, restarting...");
      delay(100);
      ESP.restart();
    }
  }
  LOG_W("Daemon thread exiting...");
  delay(1000);       // Give some time for logging
  vTaskDelete(NULL); // This line is not strictly necessary, but it's good practice to clean up the task when done.
}

void fan_thread_entry(void *args){
    board_sal_t *board = (board_sal_t*)args;
    String taskName = "(fan)";
    LOG_I("%s thread started on core %d...", taskName, xPortGetCoreID());
    LOG_I("Initializing fan...");

    int16_t now_count = 0, last_count = 0, temp_cnt = 0;
    uint32_t start_ms = millis();
    fan_pid_t fan_pid ={
        .Kp = 50.0f,
        .Ki = 1.0f,
        .Kd = 0.0f,
        .prev_error = 0,
        .integral = 0,
        .output_min = 25.0f,
        .output_max = 99.999f
    };

    // Initialize TMP102 temperature sensor
    tmp102_init();

    //fan init
    fan_init_param_t init_param;
    init_param.pwm_pin         = board->info.spec.fan.pwm_pin;
    init_param.torch_pin       = board->info.spec.fan.torch_pin;
    init_param.pwm_ch          = board->info.spec.fan.pwm_channel;
    init_param.pwm_freq        = board->info.spec.fan.pwm_freq;
    init_param.pwm_revolution  = board->info.spec.fan.pwm_resolution;
    init_param.p_cnt_h_limt    = board->info.spec.fan.p_cnt_h_limt;
    fan_init(init_param);

    // polarity detection
    bool fan_invert = guess_fan_polarity();

    // fan self test
    while (true){
        measure_fan_rpm_for_duration(1.0, 5000, board->status.fan.rpm , fan_invert);
        board->status.fan.self_test = (board->status.fan.rpm > board->info.spec.fan.self_test_rpm_thr) ? true : false;
        if(board->status.fan.self_test) break;
        LOG_W("Fan self test failed, please check fan wiring and connection, retrying in 5s...");
    }
    
    while(1){
        delay(125);// 8Hz
        //update board temperature
        board->status.temp.mcu    = (temp_cnt % 300 == 0) ? (float)get_mcu_temperature() : board->status.temp.mcu;
        board->status.temp.vcore  = (temp_cnt % 20 == 0) ? (float)get_vcore_temperature() : board->status.temp.vcore;
        board->status.temp.asic   = (temp_cnt % 1 == 0) ? (float)get_asic_temperature() : board->status.temp.asic;

        // Round to 1 decimal place
        board->status.temp.mcu   = roundf(board->status.temp.mcu * 10) / 10.0f;
        board->status.temp.vcore = roundf(board->status.temp.vcore * 10) / 10.0f;
        board->status.temp.asic  = roundf(board->status.temp.asic * 100) / 100.0f;
        temp_cnt++;
        
        // Calculate fan RPM
        if(millis() - start_ms >= 1000) {
            pcnt_get_counter_value(PCNT_UNIT_0, &now_count);
            uint16_t delta_pcnt = 0;
            if (now_count < last_count) delta_pcnt = (init_param.p_cnt_h_limt - last_count) + now_count;
            else delta_pcnt = now_count - last_count;
            board->status.fan.rpm = calculate_rpm(delta_pcnt, (millis() - start_ms) / 1000.0);
            last_count = now_count;
            start_ms = millis();
        }

        // Adjust fan speed
        if(board->info.preference.fan.is_auto_speed && board->status.fan.self_test){
            static uint32_t pid_start = millis();
            float dt = (millis() - pid_start) / 1000.0f; // Convert to seconds
            board->status.fan.speed = (uint16_t)pid_compute(&fan_pid, board->info.preference.fan.target_temp, board->status.temp.asic, dt);
            pid_start = millis();
        }
        fan_set_speed(board->status.fan.speed / 100.0, fan_invert);
    }
}

void market_thread_entry(void *args){
    board_sal_t *board = (board_sal_t*)args;
    String taskName = "(market)";
    LOG_I("%s thread started on core %d...", taskName, xPortGetCoreID());
    LOG_I("Initializing market...");

    while (board->market == NULL){
        LOG_W("MarketClass instance is NULL, waiting...");
        delay(1000);
    }   

    board->market->lastUpdate = 0;
    
    while(true){
        if(board->info.connection.wifi.status_param.status == WL_CONNECTED){
            // Fetch the 24hr ticker data for the coin
            bool res = board->market->get_coin_ticker_24hr(board->info.base.coin_price + "USDT");
            board->market->lastUpdate = (res) ? millis() : board->market->lastUpdate;
        }
        delay(MARKET_UPDATE_INTERVAL);
    }

    delete board->market;
    board->market = nullptr;
    LOG_W("Market thread exit.");
    vTaskDelete(NULL);
}

void miner_asic_init_thread_entry(void *args){
    board_sal_t *board = (board_sal_t*)args;
    String taskName = "(asic_init)";
    LOG_I("%s thread started on core %d...", taskName, xPortGetCoreID());
    LOG_I("Initializing asic_init...");

    while(board->miner == nullptr){
        LOG_W("Waiting for miner instance ready...");
        delay(1000);
    }
    
    //begin asic hardware
    if(!board->miner->begin(board->info.spec.asic.req_frq, board->info.spec.asic.diff_thr_init, board->info.spec.asic.com_baud_work)){
        while (true){
            LOG_E("Miner low power!");
            delay(1000);
        }
    }
    
    LOG_I("ASIC job interval set to %d ms", board->info.spec.asic.job_interval_ms);
    delay(1000);//wait for asic init stable
    vTaskDelete(NULL);
}

void miner_asic_tx_thread_entry(void *args){
    board_sal_t *board = (board_sal_t*)args;
    String taskName = "(asic_tx)";
    LOG_I("%s thread started on core %d...", taskName, xPortGetCoreID());
    LOG_I("Initializing asic_tx...");

    //wait for first job cache ready forever
    xSemaphoreTake(board->stratum->new_job_xsem, portMAX_DELAY);
    delay(2000);//necessary delay for first job cache ready

    //forever loop
    while (true){
        //null loop if not subscribed
        if(!board->stratum->is_subscribed()){
            board->miner->end();
            board->status.miner.hashrate._3m = 0.0;
            delay(1000);
            continue;   
        }
        //wait for new job signal 1000ms max
        if(xSemaphoreTake(board->stratum->new_job_xsem, 1000) != pdTRUE) {
            continue;
        }

        //get job from pool job caches
        board->miner->pool_job_now = board->stratum->pop_job_cache();
        if(board->miner->pool_job_now.id == "")continue;
        
        //calculate network diff
        board->status.miner.diff.network = board->miner->calculate_diff(board->miner->pool_job_now.nbits);
        //update pool diff
        board->status.miner.diff.pool = board->stratum->get_pool_difficulty();
        
        LOG_W("Job [%s] from %s:%d", board->miner->pool_job_now.id.c_str(), board->stratum->pool->get_pool_info().url.c_str(), board->stratum->pool->get_pool_info().port);
        while (true){
            //construct asic job and send to asic every 2s
            if(!board->miner->mining(&board->miner->pool_job_now)) continue;
            //exit if pool disconnected
            if(!board->stratum->is_subscribed()) break;

            //set asic diff as pool diff if pool diff < initial asic diff
            if(board->stratum->get_pool_difficulty() <= board->info.spec.asic.diff_thr_init){
                static double last_diff = board->info.spec.asic.diff_thr_init;
                if(board->stratum->get_pool_difficulty() != last_diff){
                    LOG_W("Try to change asic diff from [%s] to [%s]", formatNumber(board->miner->get_asic_diff(), 4).c_str(), formatNumber(board->stratum->get_pool_difficulty(), 4).c_str());
                    last_diff = board->stratum->get_pool_difficulty();
                    board->miner->set_asic_diff(last_diff);
                }
            }

            //interval 2000ms every asic job, exit if a new pool job arrived
            if(xSemaphoreTake(board->stratum->new_job_xsem, board->info.spec.asic.job_interval_ms) == pdTRUE) {
                board->stratum->clear_sub_extranonce2();
                //avoid some stale share submit, clear job cache if clean job signal received
                if(xSemaphoreTake(board->stratum->clear_job_xsem, 0) == pdTRUE) {
                    board->miner->clear_asic_job_cache();
                    LOG_D("Job cache clear...");
                }
                xSemaphoreGive(board->stratum->new_job_xsem);//release the semaphore for next pool job
                break;
            }
        }
    }
}

void miner_asic_rx_thread_entry(void *args){
    board_sal_t *board = (board_sal_t*)args;
    String taskName = "(asic_rx)";
    LOG_I("%s thread started on core %d...", taskName, xPortGetCoreID());
    LOG_I("Initializing asic_rx...");

    asic_job job = {0,};
    asic_result result = {0,};

    //wait for first job cache ready forever
    xSemaphoreTake(board->stratum->new_job_xsem, portMAX_DELAY);

    //forever loop
    while(true){
        if(!board->stratum->is_subscribed()){
            delay(1000);
            continue;
        }
        esp_err_t err = board->miner->listen_asic_rsp(&result, 1000*30);
        if(ESP_OK == err){
            if(!board->stratum->is_subscribed()) continue;
            if(board->miner->find_job_by_asic_job_id(result.job_id, &job)){
                board->status.miner.asic_update = millis();
                uint32_t version_bits       = (reverse_uint16(result.version) << 13);  //logic from project bitaxe: https://github.com/skot/bitaxe 
                uint32_t version            = version_bits | (*(uint32_t*)job.version);//logic from project bitaxe: https://github.com/skot/bitaxe 
                double diff                 = board->miner->calculate_diff(version, job.prev_block_hash, job.merkle_root, *(uint32_t*)job.ntime, *(uint32_t*)job.nbits, result.nonce);

                //skip if diff <= 0.0001 or diff is nan or diff is inf
                if((diff <= std::numeric_limits<double>::epsilon()) || std::isnan(diff) || std::isinf(diff) || (diff < 0.1)) continue;

                //update hashrate anyway, even if diff < pool diff, some high diff pool may need this, avoid local hashrate freeze. 
                board->miner->calculate_hashrate(&board->status.miner.hashrate);

                //print summary to log
                static uint32_t summary_start = millis();
                if(millis() - summary_start >= 1000*60){
                    summary_start = millis();
                    LOG_L(" ============%s=========== ",board->info.base.fw_version.c_str());
                    LOG_L("|            Summary           |");
                    LOG_L("+------------Uptime------------+");
                    LOG_L("|%s | %s |", convert_uptime_to_string(board->status.miner.uptime_session).c_str(), convert_uptime_to_string(board->status.miner.uptime_ever).c_str());
                    LOG_L("+-----------HashRate-----------+");
                    LOG_L("|   3m    |    30m   |    1h   |");
                    LOG_L("|%-4sH/s| %-4sH/s|%-4sH/s|", 
                        formatNumber(board->status.miner.hashrate._3m, 4).c_str(), 
                        formatNumber(board->status.miner.hashrate._30m, 4).c_str(),
                        formatNumber(board->status.miner.hashrate._1h, 4).c_str());
                    LOG_L("+----------Difficulty----------+");
                    LOG_L("|From boot| Best ever| Network |");
                    LOG_L("| %-6s |  %-5s | %-7s |", 
                        formatNumber(board->status.miner.diff.best_session, 5).c_str(), 
                        formatNumber(board->status.miner.diff.best_ever, 5).c_str(),
                        formatNumber(board->status.miner.diff.network, 5).c_str());
                    LOG_L("+---Free heap-----Efficiency---+");
                    LOG_L("|    %-3sKB   |   %-3sJ/TH   |", formatNumber(ESP.getFreeHeap() / 1024.0f, 4).c_str(), formatNumber(board->status.miner.efficiency, 4).c_str());
                    LOG_L(" ============================== ");
                    log_i("\r\n");
                    LOG_I(" ++++++++++ Real Time +++++++++");
                    LOG_I("| ASIC | Last | Pool | Network |");
                    LOG_I("|------|------|------|---------|");
                }
                
                LOG_I("|%-6s|%-6s|%-6s|%-7s|", 
                    formatNumber(board->miner->get_asic_diff(), 4).c_str(), 
                    formatNumber(diff, 4).c_str(), 
                    formatNumber(board->stratum->get_pool_difficulty(), 4).c_str(),
                    formatNumber(board->status.miner.diff.network, 7).c_str());

                //skip if diff < pool diff
                if(diff < board->stratum->get_pool_difficulty())continue; 
                
                //submit sulution
                uint32_t version_submit = version ^ (*(uint32_t*)job.version);
                String   extra2_submit = board->miner->get_extranonce2_by_asic_job_id(result.job_id);
                bool res = board->miner->submit_job_share(extra2_submit, result.nonce, *(uint32_t*)job.ntime, version_submit);
                if(!res) continue;
                
                //update the block hit counter
                if(diff >= board->status.miner.diff.network){
                    board->status.miner.hits = (board->status.miner.hits >= 99) ? 0 : (board->status.miner.hits);
                    board->status.miner.hits++;

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

                    xSemaphoreGive(board->status.nvs_save_xsem);
                }

                //update miner status
                board->status.miner.diff.last            = diff;
                board->status.miner.diff.best_session    = (diff > board->status.miner.diff.best_session) ? diff : board->status.miner.diff.best_session;
                board->status.miner.diff.best_ever       = (diff > board->status.miner.diff.best_ever) ? diff : board->status.miner.diff.best_ever;

                //update all time best diff
                if(diff == board->status.miner.diff.best_ever){
                    xSemaphoreGive(board->status.nvs_save_xsem);
                }
                
                //add share to History of block proximity
                if(xSemaphoreTake(board->status.miner.block_proximity_mutex, portMAX_DELAY) == pdTRUE){
                    proximity_node_t node;
                    node.block_proximity = diff / board->status.miner.diff.network;
                    node.share_diff      = diff;
                    node.net_diff        = board->status.miner.diff.network;
                    node.epoch           = board->status.time.utc * 1000ULL;
                    add_share_diff_history(board->status.miner.block_proximity_history, node, 36);
                    xSemaphoreGive(board->status.miner.block_proximity_mutex);
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

void stratum_thread_entry(void *args){
    board_sal_t *board = (board_sal_t*)args;
    String taskName = "(stratum)";
    LOG_I("%s thread started on core %d...", taskName, xPortGetCoreID());
    LOG_I("Initializing stratum...");

    bool is_primary_pool = true;

    board->stratum->set_pool_difficulty(DEFAULT_POOL_DIFFICULTY);
    StaticJsonDocument<1024*4> json;
    while(true){
        static int w_retry = 0, w_maxRetries = 24;
        if(board->info.connection.wifi.status_param.status != WL_CONNECTED){
            w_retry++;
            LOG_W("WiFi reconnecting %d/%d...", w_retry, w_maxRetries);
            if(w_retry >= w_maxRetries) ESP.restart();

            xSemaphoreGive(board->info.connection.wifi.reconnect_xsem);
            board->stratum->reset();
            delay(5000);
            continue;
        } else w_retry = 0;
        
        static uint32_t last = millis();
        if(!is_primary_pool){
            if(millis() - last > 1000 * 60){ // check every 60 seconds
                bool res = board->stratum->is_primary_pool_available(board->info.connection.pool_primary.url, board->info.connection.pool_primary.port);
                if(res){
                    LOG_I("Primary pool [%s] available now, switching to primary pool...", board->info.connection.pool_primary.url.c_str());
                    board->info.connection.pool_use = board->info.connection.pool_primary;
                    board->info.connection.stratum_use = board->info.connection.stratum_primary;

                    board->stratum->reset(board->info.connection.pool_use, board->info.connection.stratum_use);
                    board->stratum->pool->begin(board->info.connection.pool_use.ssl);
                    board->stratum->pool->connect();
                    board->status.miner.diff.last = 0;
                }else{
                    LOG_W("Primary pool [%s] is not available.", board->info.connection.pool_primary.url.c_str());
                }
                last = millis();
            }
        }

        static uint16_t p_retry = 0, p_maxRetries = 5;
        if(!board->stratum->pool->is_connected()){
            static bool    first_connect = true;
            if(first_connect){
                LOG_I("Pool connecting...");
                first_connect = false;
            }else LOG_W("Lost connection to pool, reconnecting %d/%d...", p_retry, p_maxRetries);
            
            if(++p_retry % p_maxRetries == 0){
                if(is_primary_pool){
                    board->info.connection.pool_use    = board->info.connection.pool_fallback;
                    board->info.connection.stratum_use = board->info.connection.stratum_fallback;
                    is_primary_pool                = false;
                    LOG_W(">>>> Set pool to fallback [%s:%d] <<<<", board->info.connection.pool_use.url.c_str(), board->info.connection.pool_use.port);
                }else{
                    board->info.connection.pool_use    = board->info.connection.pool_primary;
                    board->info.connection.stratum_use = board->info.connection.stratum_primary;
                    is_primary_pool                = true;
                    LOG_W(">>>> Set pool to primary [%s:%d] <<<<", board->info.connection.pool_use.url.c_str(), board->info.connection.pool_use.port);
                }
            }
            board->stratum->reset(board->info.connection.pool_use, board->info.connection.stratum_use);
            board->stratum->pool->begin(board->info.connection.pool_use.ssl);
            board->stratum->pool->connect();
            board->status.miner.diff.last = 0;
            delay(5000);
            continue;
        }else p_retry = 0;

        if(!board->stratum->is_subscribed()){
            if(!board->stratum->subscribe()){
                LOG_W("Failed to subscribe to pool, retrying in 5 seconds...");
                delay(100);
                continue;
            }
            if(!board->stratum->authorize()){
                LOG_W("Failed to authorize to pool, retrying in 5 seconds...");
                delay(100);
                continue;
            }
            if(!board->stratum->config_version_rolling()){
                LOG_W("Failed to config version rolling, retrying in 5 seconds...");
                delay(100);
                continue;
            }
            if(!board->stratum->suggest_difficulty()){
                LOG_W("Failed to suggest difficulty to pool, retrying in 5 seconds...");
                delay(100);
                continue;
            }
        }

        if(!board->stratum->hello_pool(HELLO_POOL_INTERVAL_MS, POOL_INACTIVITY_TIME_MS)){
            LOG_W("Pool is inactive, retrying in 5 seconds...");
            delay(5000);
            continue;
        }

        while(board->stratum->pool->available()){
            stratum_method_data method = board->stratum->listen_methods();
            switch (method.type){
                case STRATUM_DOWN_PARSE_ERROR:   
                    if(method.raw != ""){
                        LOG_E("Stratum parse error, id : %d, raw : %s", method.id, method.raw.c_str());
                    }
                    else{
                        LOG_E("Stratum parse error, id : %d", method.id);
                    }
                    break;
                case STRATUM_DOWN_NOTIFY:{
                        LOG_D("Stratum notify, id : %d => %s", method.id, method.raw.c_str());
                        pool_job_data_t job;
                        json.clear();
                        DeserializationError error = deserializeJson(json, method.raw);
                        if (error) {
                            LOG_E("Failed to parse STRATUM_DOWN_NOTIFY json");
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
                        LOG_D("Version mask      : 0x%08x", board->stratum->get_version_mask());
                        LOG_D("Pool difficulty   : %s", formatNumber(board->stratum->get_pool_difficulty(), 5).c_str());

                        if(job.clean_jobs){
                            board->stratum->clear_job_cache();
                            xSemaphoreGive(board->stratum->clear_job_xsem);
                        }
                        size_t cached_size = board->stratum->push_job_cache(job);
                        
                        //Give the new job semaphore to the other threads
                        xSemaphoreGive(board->stratum->new_job_xsem);//asic tx thread
                        static bool first_job = true;
                        if(first_job){
                            //first job will release the asic rx , monitor and ui thread
                            xSemaphoreGive(board->stratum->new_job_xsem);//asic tx thread
                            xSemaphoreGive(board->stratum->new_job_xsem);//asic rx thread
                            xSemaphoreGive(board->stratum->new_job_xsem);//ui thread
                            xSemaphoreGive(board->stratum->new_job_xsem);//monitor thread
                            first_job = false;
                        }
                        board->info.connection.stratum_update = millis();//pool alive timestamp
                    }         
                    break;
                case STRATUM_DOWN_SET_DIFFICULTY: {
                    LOG_D("Stratum set difficulty, id : %d => %s", method.id, method.raw.c_str());
                    json.clear();
                    DeserializationError error = deserializeJson(json, method.raw);
                    if(error){
                        LOG_E("Failed to parse STRATUM_DOWN_SET_DIFFICULTY json");
                        break;
                    }
                    if(json["method"] == "mining.set_difficulty"){
                        if(json["params"].size() > 0){
                            board->stratum->set_pool_difficulty(json["params"][0]);
                            LOG_D("Pool difficulty set : %s", formatNumber(json["params"][0], 5).c_str());
                        }else{
                            LOG_W("Pool difficulty not found in params");
                        }
                    }
                }
                    break;
                case STRATUM_DOWN_SET_VERSION_MASK:{
                    LOG_D("Stratum set version mask , id : %d => %s", method.id, method.raw.c_str());
                    board->stratum->set_msg_rsp_map(method.id, true);
                    json.clear();
                    DeserializationError error = deserializeJson(json, method.raw);
                    if (error) {
                        LOG_E("Failed to parse STRATUM_DOWN_SET_VERSION_MASK json");
                        break;
                    }
                    if(json["method"] == "mining.set_version_mask"){
                        if(json["params"].size() > 0){
                            board->stratum->set_version_mask(strtoul(json["params"][0].as<const char*>(), NULL, 16));
                            LOG_L("Version mask set to %s", json["params"][0].as<const char*>());
                        }else{
                            board->stratum->set_version_mask(0xffffffff);
                            LOG_W("Version mask not found in params");
                        }
                    }else{
                        board->stratum->set_version_mask(0xffffffff);
                        LOG_W("Version rolling key not found in response");
                    }
                    board->stratum->del_msg_rsp_map(method.id);
                }
                    break;
                case STRATUM_DOWN_SET_EXTRANONCE:{
                        LOG_L("Stratum set extranonce => %s", method.id, method.raw.c_str());
                        json.clear();
                        DeserializationError error = deserializeJson(json, method.raw);
                        if (error) {
                        LOG_E("Failed to parse STRATUM_DOWN_SET_EXTRANONCE json");
                            break;
                        }
                        board->stratum->set_sub_extranonce1(json["params"][0]);
                        board->stratum->set_sub_extranonce2_size(json["params"][1]);
                    }
                    break;
                case STRATUM_DOWN_SUCCESS: 
                    if(method.id != -1){
                        board->stratum->set_msg_rsp_map(method.id, true);
                        stratum_rsp rsp = board->stratum->get_method_rsp_by_id(method.id);
                        if(rsp.method == "mining.submit"){
                            uint32_t latency = millis() - rsp.stamp;
                            if (rsp.status == true){
                                board->status.miner.share_accepted++;
                                LOG_L("#%d share accepted, %ldms", board->status.miner.share_accepted + board->status.miner.share_rejected, latency);      
                            }
                            else {
                                board->status.miner.share_rejected++;
                                LOG_E("#%d share rejected, %ldms", board->status.miner.share_accepted + board->status.miner.share_rejected, latency);
                            }
                        }
                        else if(rsp.method == "mining.configure"){
                            json.clear();
                            DeserializationError error = deserializeJson(json, method.raw);
                            if (error) {
                                LOG_E("Failed to parse STRATUM_DOWN_SUCCESS json");
                            } else {
                                board->stratum->set_version_mask(0xffffffff);
                                if (json["result"]["version-rolling"] == true) {
                                    if (json["result"].containsKey("version-rolling.mask")) {
                                        board->stratum->set_version_mask(strtoul(json["result"]["version-rolling.mask"].as<const char*>(), NULL, 16));
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
                                LOG_E("Failed to parse STRATUM_DOWN_NOTIFY json");
                            }
                            else{
                                if(json.containsKey("result")){
                                    board->stratum->set_authorize(json["result"]);
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
                        board->stratum->set_msg_rsp_map(method.id, true);
                        stratum_rsp rsp = board->stratum->get_method_rsp_by_id(method.id);
                        if(rsp.method == "mining.submit"){
                            uint32_t latency = millis() - rsp.stamp;
                            board->status.miner.share_rejected++;
                            LOG_E("#%d share rejected, %ldms", board->status.miner.share_accepted + board->status.miner.share_rejected, latency);
                        }
                        else if(rsp.method == "mining.authorize"){
                            board->stratum->set_authorize(false);
                            LOG_E("Authorization failed.");
                        }
                        else{
                            LOG_E("Unknown error response, id : %d");
                        }
                    }
                    break;
                case STRATUM_DOWN_UNKNOWN:                   
                    LOG_E("Stratum unknown, id : %d");
                    break;
                default :
                    LOG_E("Stratum unknown, id : %d");
                    break;
            }
            delay(10);
        }
        delay(50);
    }
}