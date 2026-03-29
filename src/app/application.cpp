#include "application.h"
#include "utils/logger/logger.h"
#include "nvs/nvs_config.h"

// Global board instance - declared extern in global.h, defined here
board_sal_t g_board;

/*──────────────────────────────────────────────
  Singleton
──────────────────────────────────────────────*/
MinerApp& MinerApp::instance() {
    static MinerApp app;
    return app;
}

/*──────────────────────────────────────────────
  init()  –  hardware pre-init + board init
──────────────────────────────────────────────*/
bool MinerApp::init() {
    BoardSpecConfig config;
    BoardModelType  model;

    model  = get_board_model();
    config = get_board_config(model);

    hardware_pre_init(config);

    while (!_board_init(config)) {
        LOG_E("Board initialization failed, retrying in 1s...");
        delay(1000);
    }

    // disable USB UART when DC-plug power is active (Apple divider / BC1.2 SDP/CDP/DCP)
    if (!g_board.power->is_dc_pluged()) {
        disable_usb_uart();
    }

    return true;
}

/*──────────────────────────────────────────────
  begin()  –  create and start all threads
──────────────────────────────────────────────*/
void MinerApp::begin() {
    _thread_pool = {
        {"(app_tick)",   _tick_thread_entry,            1024*5,   TASK_PRIORITY_APP_TICK,    1, &_tickTask,       10,  0},
        {"(display)",   display_thread_entry,           1024*5,   TASK_PRIORITY_DISPLAY,     1, NULL,             10,  0},
        {"(lvgl)",      lvgl_tick_thread_entry,         1024*4,   TASK_PRIORITY_LVGL_DRV,    1, &_lvglTask,       10,  0},
        {"(ui)",        ui_thread_entry,                1024*5,   TASK_PRIORITY_UI,          1, &_uiTask,         10,  0},
        {"(led)",       led_thread_entry,               1024*3,   TASK_PRIORITY_LED,         1, &_ledTask,        10,  0},
        {"(button)",    button_thread_entry,            1024*3,   TASK_PRIORITY_BTN,         1, &_btnTask,        10,  0},
        {"(webserver)", webserver_thread_entry,         1024*5,   TASK_PRIORITY_WS,          1, &_wsTask,         10,  0},
        {"(wifi)",      wifi_connect_thread_entry,      1024*6,   TASK_PRIORITY_WIFI,        1, NULL,             10,  0},
        {"(daemon)",    daemon_thread_entry,            1024*4,   TASK_PRIORITY_DAEMON,      0, &_daemonTask,     10,  0},
        {"(power)",     power_thread_entry,             1024*7,   TASK_PRIORITY_PWR,         1, &_powerTask,      10,  0},
        {"(asic_cnt)",  miner_asic_count_thread_entry,  1024*5,   TASK_PRIORITY_ASIC_CNT,    1, NULL,             10,  0},
        {"(asic_init)", miner_asic_init_thread_entry,   1024*6,   TASK_PRIORITY_ASIC_INIT,   1, NULL,             10,  0},
        {"(fan)",       fan_thread_entry,               1024*5,   TASK_PRIORITY_FAN,         0, &_fanTask,        10,  0},
        // synchronisation point: wait for these events before starting the next batch
        {"",            NULL,                           0,        0,                         0, NULL,             0,   INIT_EVENT_ASIC_COUNTED | INIT_EVENT_WIFI_STA_CONNECTED | INIT_EVENT_FAN_READY},
        {"(swarm)",     swarm_thread_entry,             1024*4,   TASK_PRIORITY_SWARM,       0, &_swarmTask,      10,  0},
        {"(market)",    market_thread_entry,            1024*5,   TASK_PRIORITY_MARKET,      0, &_marketTask,     10,  0},
        {"(stratum)",   stratum_thread_entry,           1024*11,  TASK_PRIORITY_STRATUM,     1, &_stratumTask,    10,  0},
        {"(monitor)",   monitor_thread_entry,           1024*5,   TASK_PRIORITY_MONITOR,     1, &_monitorTask,    10,  0},
        {"(neighbor)",  alive_ip_scan_thread_entry,     1024*4,   TASK_PRIORITY_SCAN,        1, &_neighborTask,   10,  0},
        {"(asic_tx)",   miner_asic_tx_thread_entry,     1024*5,   TASK_PRIORITY_MINER_TX,    1, &_minerTxTask,    10,  0},
        {"(asic_rx)",   miner_asic_rx_thread_entry,     1024*5,   TASK_PRIORITY_MINER_RX,    0, &_minerRxTask,    10,  0},
        {"(aphorism)",  aphorism_thread_entry,          1024*6,   TASK_PRIORITY_APHORISM,    0, &_aphorismTask,   10,  0},
    };

    // start threads in order, with optional synchronisation barriers
    for (const auto& t : _thread_pool) {
        if (t.entry != NULL) {
            BaseType_t ret = xTaskCreatePinnedToCore(
                t.entry, t.name, t.stack_size,
                (void*)(&g_board), t.priority, t.handle, t.core_id
            );
            if (ret == pdPASS) LOG_I("Thread %s created on core %d", t.name, t.core_id);
            else               LOG_E("Failed to create thread %s",    t.name);
            delay(t.delay_ms);
        } else {
            // synchronisation barrier: wait for specified init events before proceeding
            xEventGroupWaitBits(g_board.status.init_evt, t.wait_events, pdFALSE, pdTRUE, portMAX_DELAY);
        }
    }
}

/*──────────────────────────────────────────────
  print_stack_hwm()  –  debug: stack usage table
──────────────────────────────────────────────*/
void MinerApp::print_stack_hwm() const {
    static uint32_t last_print_ms = 0;
    if (millis() - last_print_ms < 10000) return; // print every 10s
    last_print_ms = millis();



    LOG_W("=========== Stack High Water Mark (in bytes) ===========");
    LOG_I("+-----------------+----------+------------+------------+");
    LOG_I("| Task Name       | HWM      | Total Stack| Optimizable|");
    LOG_I("+-----------------+----------+------------+------------+");

    for (const auto& t : _thread_pool) {
        if (t.handle == NULL || *t.handle == NULL) continue;

        eTaskState state = eTaskGetState(*t.handle);
        if (state == eDeleted) continue;

        char* taskName = pcTaskGetName(*t.handle);
        if (taskName == NULL || taskName[0] == '\0') continue;

        UBaseType_t hwm        = uxTaskGetStackHighWaterMark(*t.handle);
        uint32_t    total      = t.stack_size;
        uint32_t    optimizable = (hwm > 512) ? (hwm - 512) : 0; // keep 512 B safety margin

        LOG_I("| %-15s | %8u | %10u | %10u |",
              taskName, (unsigned)hwm, (unsigned)total, (unsigned)optimizable);
    }

    LOG_I("+-----------------+----------+------------+------------+");
    LOG_W("Note: Optimizable = HWM - 512 (keeping 512 bytes safety buffer)");
}

/*──────────────────────────────────────────────
  _tick_thread_entry()  –  MinerApp private tick
──────────────────────────────────────────────*/
void MinerApp::_tick_thread_entry(void* args) {
    board_sal_t* board = static_cast<board_sal_t*>(args);
    float    x = 0.0f;

    xEventGroupWaitBits(board->status.init_evt, INIT_EVENT_SCREEN_READY, pdFALSE, pdTRUE, portMAX_DELAY); // wait for ASIC count before proceeding
    //backlight brightness ramp up
    uint16_t brightness = board->status.preference.screen.brightness;
    for(int i = 0; i < brightness; i++) {
        tft_bl_ctrl(i);
        delay(10);
    }

    while(true){
        MinerApp::instance().print_stack_hwm(); // debug: print stack usage

        delay(10);
        // check miner events to trigger backlight effect, such as block hit or high diff achieved
        EventBits_t bits = xEventGroupWaitBits(board->status.sys_evt, SYS_EVENT_MINER_BLOCK_HIT | SYS_EVENT_MINER_HIGH_DIFF_ACHIEVED, pdFALSE, pdFALSE, 0);
        if((bits & SYS_EVENT_MINER_BLOCK_HIT) == SYS_EVENT_MINER_BLOCK_HIT) {
            brightness = static_cast<uint16_t>(100.0f * (1.0f + sinf(x)) / 2.0f);
            x += 0.1f;
        }else if((bits & SYS_EVENT_MINER_HIGH_DIFF_ACHIEVED) == SYS_EVENT_MINER_HIGH_DIFF_ACHIEVED){
            // brightness = static_cast<uint16_t>(100.0f * (1.0f + sinf(x)) / 2.0f);
            // x += 0.06f;
        }else{
            brightness = board->status.preference.screen.brightness;
        }
        tft_bl_ctrl(brightness);


        // sensor reading and other periodic tasks can also be added here if needed
        //update board temperature
        static uint16_t vcore_temp_last_update = 0, asic_temp_last_update = 0;
        if(millis() - vcore_temp_last_update >= 125){
            board->status.temp.vcore = (float)get_vcore_temperature();
            board->status.temp.vcore = roundf(board->status.temp.vcore * 10) / 10.0f;
            xEventGroupSetBits(board->status.sys_evt, SYS_EVENT_MINER_VCORE_TEMP_UPDATE);
            vcore_temp_last_update = millis();
        }
        if(millis() - asic_temp_last_update >= 125){
            board->status.temp.asic = (float)get_asic_temperature();
            board->status.temp.asic  = roundf(board->status.temp.asic * 100) / 100.0f;
            xEventGroupSetBits(board->status.sys_evt, SYS_EVENT_MINER_ASIC_TEMP_UPDATE);
            asic_temp_last_update = millis();
        }


        // consume one aphorism every 30 s: pop from front, log, erase
        static uint32_t last_aphorism_ms = 0;
        if(millis() - last_aphorism_ms >= 30000) {
            xSemaphoreTake(board->status.aphorism.mutex, portMAX_DELAY);
            if(!board->status.aphorism.pool.empty()) {
                auto qt = board->status.aphorism.pool.front();
                board->status.aphorism.pool.erase(board->status.aphorism.pool.begin());
                size_t remaining = board->status.aphorism.pool.size(); // already erased, so this is post-pop count
                xSemaphoreGive(board->status.aphorism.mutex);
                LOG_W("[tick] +--------------------------------------------------+");
                LOG_I("[tick] |  \"%s\"", qt.quote.c_str());
                LOG_I("[tick] |  -- %s  [kw: %s]  [%d left]", qt.author.c_str(), qt.keyword.c_str(), (int)remaining);
                LOG_W("[tick] +--------------------------------------------------+");
            } else {
                xSemaphoreGive(board->status.aphorism.mutex);
            }
            last_aphorism_ms = millis();
        }
    }
}

/*──────────────────────────────────────────────
  _board_init()  –  private board setup
──────────────────────────────────────────────*/
bool MinerApp::_board_init(const BoardSpecConfig& config) {
    /******* board-specific parameters *******/
    g_board.info.spec.name                  = config.name;
    g_board.info.spec.asic                  = config.asic;
    g_board.info.spec.tft                   = config.tft;
    g_board.info.spec.spi                   = config.spi;
    g_board.info.spec.ui                    = config.ui;
    g_board.info.spec.fans                  = config.fans;
    g_board.info.spec.btn                   = config.btn;
    g_board.info.spec.pwr                   = config.pwr;
    g_board.info.spec.led                   = config.led;
    g_board.info.spec.iic                   = config.iic;
    g_board.info.spec.create_asic_instance  = config.create_asic_instance;
    g_board.info.spec.create_power_instance = config.create_power_instance;
    g_board.info.spec.preference            = config.preference;

    /******* common parameters *******/
    String stratum_pri = String(nvs_config_get_string(NVS_CONFIG_STRATUM_URL_PRIMARY,  PRIMARY_POOL_URL));
    String stratum_fb  = String(nvs_config_get_string(NVS_CONFIG_STRATUM_URL_FALLBACK, FALLBACK_POOL_URL));

    g_board.info.connection.pool.primary.ssl  = ((stratum_pri.indexOf("ssl") != -1) || (stratum_pri.indexOf("tls") != -1));
    g_board.info.connection.pool.primary.url  = stratum_pri.substring(stratum_pri.indexOf(":") + 3, stratum_pri.lastIndexOf(":"));
    g_board.info.connection.pool.primary.port = stratum_pri.substring(stratum_pri.lastIndexOf(":") + 1).toInt();
    g_board.info.connection.pool.fallback.ssl  = ((stratum_fb.indexOf("ssl") != -1) || (stratum_fb.indexOf("tls") != -1));
    g_board.info.connection.pool.fallback.url  = stratum_fb.substring(stratum_fb.indexOf(":") + 3, stratum_fb.lastIndexOf(":"));
    g_board.info.connection.pool.fallback.port = stratum_fb.substring(stratum_fb.lastIndexOf(":") + 1).toInt();
    g_board.info.connection.pool.use           = g_board.info.connection.pool.primary;

    g_board.info.base.fw_version         = BOARD_CURRENT_FW_VERSION;
    g_board.info.base.hw_version         = BOARD_CURRENT_HW_VERSION;
    g_board.info.base.devcie_code        = gen_device_code();
    g_board.info.base.fw_latest_release  = "";

    String default_user_pri = String(PRIMARY_USER)  + "." + g_board.info.spec.name + "_" + g_board.info.base.devcie_code.substring(0, 5);
    String default_user_fb  = String(FALLBACK_USER) + "." + g_board.info.spec.name + "_" + g_board.info.base.devcie_code.substring(0, 5);
    g_board.info.connection.stratum.primary.user  = String(nvs_config_get_string(NVS_CONFIG_STRATUM_USER_PRIMARY,  default_user_pri.c_str()));
    g_board.info.connection.stratum.primary.pwd   = String(nvs_config_get_string(NVS_CONFIG_STRATUM_PASS_PRIMARY,  PRIMARY_POOL_PWD));
    g_board.info.connection.stratum.fallback.user = String(nvs_config_get_string(NVS_CONFIG_STRATUM_USER_FALLBACK, default_user_fb.c_str()));
    g_board.info.connection.stratum.fallback.pwd  = String(nvs_config_get_string(NVS_CONFIG_STRATUM_PASS_FALLBACK, FALLBACK_POOL_PWD));
    g_board.info.connection.stratum.use           = g_board.info.connection.stratum.primary;

    g_board.status.wifi.reconnect_xsem            = xSemaphoreCreateCounting(1, 0);
    g_board.info.connection.wifi.ap.ip            = IPAddress(192, 168, 4, 1);
    g_board.info.connection.wifi.ap.info.pwd      = "12345678";

    String ap_default_ssid = g_board.info.spec.name + "_" + g_board.info.base.devcie_code.substring(0, 5);
    g_board.info.connection.wifi.ap.info.ssid     = String(nvs_config_get_string(NVS_CONFIG_AP_SSID, ap_default_ssid.c_str()));
    g_board.status.wifi.force_config_required     = nvs_config_get_u8(NVS_CONFIG_FORCE_CONFIG, false);
    g_board.status.wifi.client_connected          = false;
    g_board.info.connection.wifi.sta.ssid         = String(nvs_config_get_string(NVS_CONFIG_WIFI_SSID, "NMTech-2.4G"));
    g_board.info.connection.wifi.sta.pwd          = String(nvs_config_get_string(NVS_CONFIG_WIFI_PASS, "NMMiner2048"));
    g_board.info.base.hostname                    = String(nvs_config_get_string(NVS_CONFIG_HOSTNAME, g_board.info.connection.wifi.ap.info.ssid.c_str()));

    g_board.status.miner.stratum_update               = millis();
    // g_board.status.preference.fan0.is_auto_speed       = nvs_config_get_u16(NVS_CONFIG_AUTO_ASIC_FAN_SPEED, true);
    // g_board.status.preference.fan0.target_temp         = String(nvs_config_get_string(NVS_CONFIG_ASIC_TARGET_TEMP, String(g_board.info.spec.preference.fan0.target_temp).c_str())).toFloat();
    g_board.status.preference.screen.flip             = nvs_config_get_u8(NVS_CONFIG_FLIP_SCREEN,       g_board.info.spec.preference.screen.flip);
    g_board.status.preference.screen.auto_rolling     = nvs_config_get_u8(NVS_CONFIG_AUTO_SCREEN,       g_board.info.spec.preference.screen.auto_rolling);
    g_board.status.preference.screen.brightness       = nvs_config_get_u8(NVS_CONFIG_SCREEN_BRIGHTNESS, g_board.info.spec.preference.screen.brightness);
    g_board.status.preference.led.enable              = nvs_config_get_u8(NVS_CONFIG_LED_INDICATOR,     g_board.info.spec.preference.led.enable);
    g_board.status.preference.led.sleep               = false;
    g_board.status.preference.led.sleep_last          = g_board.status.preference.led.sleep;
    g_board.info.base.coin_price                      = String(nvs_config_get_string(NVS_CONFIG_PRICE_DISPLAY_COIN, "BTC"));
    g_board.info.base.coin_price.toUpperCase();
    g_board.info.base.coin_watchlist                  = String(nvs_config_get_string(NVS_CONFIG_COIN_WATCHLIST, "BTC,ETH,LTC,BNB,DOGE,XRP,TRX,SOL"));

    g_board.status.reboot_xsem                    = xSemaphoreCreateCounting(1, 0);
    g_board.status.nvs_save_xsem                  = xSemaphoreCreateCounting(1, 0);
    g_board.status.brightness_update_xsem         = xSemaphoreCreateCounting(1, 0);
    g_board.status.recover_factory_xsem           = xSemaphoreCreateCounting(1, 0);
    g_board.status.force_config_xsem              = xSemaphoreCreateCounting(1, 0);
    g_board.status.init_evt                       = xEventGroupCreate();
    g_board.status.sys_evt                        = xEventGroupCreate();
    g_board.status.neighbor.scan_required         = xSemaphoreCreateCounting(1, 0);
    g_board.status.neighbor.mutex                 = xSemaphoreCreateMutex();
    g_board.status.neighbor.scan_generation       = 0;
    g_board.status.neighbor.last_scan_ms          = 0;
    g_board.status.swarm.mutex                    = xSemaphoreCreateMutex();
    g_board.status.swarm.last_scan_gen            = 0;
    g_board.status.swarm.total_workers            = 0;
    g_board.status.swarm.total_hr                 = 0.0f;
    g_board.status.swarm.best_session_bd          = 0.0f;
    g_board.status.swarm.best_ever_bd             = 0.0f;

    g_board.status.miner.status_history.mutex    = xSemaphoreCreateMutex();
    g_board.status.miner.proximity_history.mutex = xSemaphoreCreateMutex();
    g_board.status.miner.update_xsem             = xSemaphoreCreateCounting(1, 0);
    g_board.status.miner.hits                    = nvs_config_get_u16(NVS_CONFIG_BLOCK_HITS, 0);
    g_board.status.ota.running                   = false;
    g_board.status.ota.progress                  = 0;
    g_board.status.ota.filename                  = "";
    g_board.status.miner.diff.best_ever          = strtoull(nvs_config_get_string(NVS_CONFIG_BEST_EVER, "0"), NULL, 10);
    g_board.status.ui.page.countdown.timeout     = BOARD_TOUCH_LONG_PRESS_TO_RECOVER;
    g_board.status.ui.page.last                  = nvs_config_get_u8(NVS_CONFIG_UI_LAST_PAGE, UI_PAGE_MINER);
    g_board.status.ui.page.current               = UI_PAGE_LOADING;
    g_board.status.ui.page.save_xsem             = xSemaphoreCreateCounting(1, 0);
    g_board.status.ui.page.list                  = {NULL, NULL, NULL, NULL, NULL, NULL, NULL};
    g_board.status.ui.lvgl.drv_xMutex            = xSemaphoreCreateMutex();
    g_board.status.touch.evt                     = TOUCH_NONE_EVT;
    g_board.status.miner.uptime_ever             = nvs_config_get_u64(NVS_CONFIG_UPTIME, 0);
    g_board.status.time.tz                       = String(nvs_config_get_string(NVS_CONFIG_TIMEZONE, "8.0"));
    g_board.status.time.format.time              = nvs_config_get_u8(NVS_CONFIG_TIME_FORMAT, 24);
    g_board.status.time.format.date              = nvs_config_get_string(NVS_CONFIG_DATE_FORMAT, "YYYY/MM/DD");
    g_board.status.aphorism.mutex                = xSemaphoreCreateMutex();
    // fan status initialisation
    for (uint8_t i = 0; i < g_board.info.spec.fans.size(); i++) {
        fan_status_t state;
        state.id        = i;
        state.self_test = false;
        state.speed     = nvs_config_get_u16(NVS_CONFIG_ASIC_FAN_SPEED, 100);
        state.rpm       = 0;
        g_board.status.fan.list.push_back(state);
    }

    // touch controller
    g_board.touch = new FT6206Class();
    if (g_board.touch == NULL) { LOG_E("FT6206Class instance creation failed"); return false; }

    // ASIC
    BMxxx* asic = g_board.info.spec.create_asic_instance(
        *config.asic.com_port, config.asic.com_baud_init,
        config.asic.rx_pin, config.asic.tx_pin, config.asic.rst_pin
    );
    if (asic == NULL) { LOG_E("BMxxx instance creation failed"); return false; }

    g_board.miner = new AsicMinerClass(asic);
    if (g_board.miner == NULL) { LOG_E("AsicMinerClass instance creation failed"); return false; }

    // power HAL
    g_board.power = g_board.info.spec.create_power_instance(
        config.pwr.en_pins, config.pwr.adc_pins,
        config.pwr.vcore_regulator_pin, config.pwr.pgood_pin, config.pwr.dc_plug_pin
    );
    if (g_board.power == NULL) { LOG_E("AxePower instance creation failed"); return false; }

    // market
    g_board.market = new MarketClass();
    if (g_board.market == NULL) { LOG_E("MarketClass instance creation failed"); return false; }

    // stratum
    g_board.stratum = new StratumClass(g_board.info.connection.pool.use, g_board.info.connection.stratum.use, 10);
    if (g_board.stratum == NULL) { LOG_E("StratumClass instance creation failed"); return false; }

    delay(2000);
    log_w("\r\n            ___          ___         ");
    log_w("\r\n           /\\__\\        /\\__\\    ");
    log_w("\r\n          /::|  |      /::|  |       ");
    log_w("\r\n         /:|:|  |     /:|:|  |       ");
    log_w("\r\n        /:/|:|  |__  /:/|:|__|__     ");
    log_w("\r\n       /:/ |:| /\\__\\/:/ |::::\\__\\");
    log_w("\r\n       \\/__|:|/:/  /\\/__/~~/:/  /  ");
    log_w("\r\n           |:/:/  /       /:/  /     ");
    log_w("\r\n           |::/  /       /:/  /      ");
    log_w("\r\n           /:/  /       /:/  /       ");
    log_w("\r\n           \\/__/        \\/__/      \r\n");
    log_w("         %s - %s\r\n", g_board.info.spec.name.c_str(), g_board.info.base.fw_version.c_str());
    return true;
}
