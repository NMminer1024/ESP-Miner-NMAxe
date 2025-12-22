#include "global.h"
#include "logger.h"
#include "button.h"
#include "display.h"
#include "fan.h"

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

void daemon_thread_entry(void *args){
  char *name = (char*)malloc(20);
  strcpy(name, (char*)args);
  LOG_I("%s thread started on core %d...", name, xPortGetCoreID());
  // free(name);

  while (true){
    delay(1000);

    //check ota status and reboot
    if(xSemaphoreTake(g_board.status.reboot_xsem, 0) == pdTRUE){
      ESP.restart();
    }
    //WiFi daemon
    if(xSemaphoreTake(g_board.info.connection.wifi.reconnect_xsem, 0) == pdTRUE){
      WiFi.begin(g_board.info.connection.wifi.conn_param.ssid.c_str(), g_board.info.connection.wifi.conn_param.pwd.c_str());
    }
    //Stratum daemon
    if(millis() - g_board.info.connection.stratum_update > STRATUM_ALIVE_TIMEOUT){
      LOG_W("Stratum connection seems frozen, restarting...");
      delay(100);
      ESP.restart();
    }
    //ASIC daemon
    if(millis() - g_board.status.miner.asic_update > ASIC_ALIVE_TIMEOUT){
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