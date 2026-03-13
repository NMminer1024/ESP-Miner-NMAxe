#include "drivers/displays/display.h"
#include "utils/logger/logger.h"
#include "drivers/button/button.h"
#include "global.h"
#include "drivers/fan/fan.h"
#include "utils/sha/csha256.h"
#include "nvs/nvs_config.h"
#include <NTPClient.h>
#include "web/http_server.h"
#include "web/recovery_page.h"
#include <Adafruit_NeoPixel.h>

void power_thread_entry(void *args){
    board_sal_t *board = (board_sal_t*)args;

    board->power->set_vcore_range(board->info.spec.asic.min_vcore, board->info.spec.asic.max_vcore);
    LOG_D("Set vcore range to (%d~%d mV)", board->power->get_vcore_min(), board->power->get_vcore_max());

    //detect power plug or pd plug
    if(board->power->is_dc_pluged()) LOG_I("DC plug detected...");
    else LOG_D("USB plug detected...");
    delay(100);
    board->power->init();

    //set vdd_1v8 and pll_0v8 power
    board->power->set_pll_0v8(PWR_ON);
    board->power->set_vdd_1v8(PWR_ON);
    delay(100);//wait for power stable
    xEventGroupSetBits(board->status.init_evt, INIT_EVENT_VDD_VPLL_READY);  


    while ((board->power->get_vbus() < board->info.spec.pwr.vbus_min_required)){
        LOG_W("Vbus is %.2fV , at least %.2fV required...", board->power->get_vbus()/1000.0, board->info.spec.pwr.vbus_min_required/1000.0);
        delay(1000);
    }
    xEventGroupSetBits(board->status.init_evt, INIT_EVENT_VBUS_READY);  


    // wait for fan ready and wifi connected before setting vcore voltage, to avoid too high temperature without proper cooling or network connection for error reporting
    xEventGroupWaitBits(g_board.status.init_evt, INIT_EVENT_FAN_READY | INIT_EVENT_WIFI_STA_CONNECTED | INIT_EVENT_VBUS_READY, pdFALSE, pdTRUE, portMAX_DELAY);
    //set vcore voltage to required voltage
    board->power->set_vcore_voltage(board->info.spec.asic.req_vcore);
    board->power->set_vcore_status(PWR_ON);
    while (!board->power->is_vcore_ready()){
        delay(500);
        LOG_W("Waiting for vcore power setup...");
    }
    xEventGroupSetBits(board->status.init_evt, INIT_EVENT_VCORE_READY);  
    delay(100);

    LOG_D("Vocre ready at %dmV/%dmV", board->power->get_vcore(), board->info.spec.asic.req_vcore);
    while(true){
        delay(100);
        uint32_t vcore_measure = board->power->get_vcore();
        int32_t err = vcore_measure - board->info.spec.asic.req_vcore;
        if(abs(err) <= 5) {
            LOG_D("Vcore %d/%dmV, error %d mV, Vocre within acceptable range", vcore_measure, board->info.spec.asic.req_vcore, err);
            continue;
        }
        LOG_D("Vcore %d/%dmV, error %d mV, Adjust vcore for error correction %d mV", vcore_measure, board->info.spec.asic.req_vcore, err, err/5);
        static uint32_t vcore_set = board->info.spec.asic.req_vcore;
        vcore_set -= err/2;//half error correction
        vcore_set = (vcore_set < board->power->get_vcore_min()) ? board->power->get_vcore_min() : vcore_set;
        vcore_set = (vcore_set > board->power->get_vcore_max()) ? board->power->get_vcore_max() : vcore_set;
        board->power->set_vcore_voltage(vcore_set);//half error correction
    }
    //exit
    vTaskDelete(NULL);
}

void led_thread_entry(void *args){
    board_sal_t *board = (board_sal_t*)args;

    // LED pins setup
    const int pwmChannel = 3;   
    const int freq = 5*1000;    
    const int resolution = 8;   

    Adafruit_NeoPixel *strip = nullptr;

    if(board->info.spec.led.wifi_pin != -1){
        pinMode(board->info.spec.led.wifi_pin, OUTPUT);
        digitalWrite(board->info.spec.led.wifi_pin, HIGH);
    }

    if(board->info.spec.led.pool_pin != -1){
        pinMode(board->info.spec.led.pool_pin, OUTPUT);
        digitalWrite(board->info.spec.led.pool_pin, HIGH);
    }

    if(board->info.spec.led.sys_pin != -1){
        if(board->info.spec.name == BOARD_NMAXE_NAME || board->info.spec.name == BOARD_NMAXE_GAMMA_NAME){
            pinMode(board->info.spec.led.sys_pin, OUTPUT);
            ledcSetup(pwmChannel, freq, resolution);
            ledcAttachPin(board->info.spec.led.sys_pin, pwmChannel);
            ledcWrite(pwmChannel, 255);// off
        }
        else if(board->info.spec.name == BOARD_NMQAXE_PLUS_PLUS_NAME){
            strip = new Adafruit_NeoPixel(8, board->info.spec.led.sys_pin, NEO_GRB + NEO_KHZ800);
            while(!strip) {
                LOG_E("Failed to create NeoPixel instance for SYS LED");
                delay(1000);
            }
            strip->begin();           // INITIALIZE NeoPixel strip object (REQUIRED)
            strip->show();            // Turn OFF all pixels ASAP
            strip->setBrightness(100);
        }
        else{
            LOG_W("Unsupported board type for SYS LED control");
        }
    }

    uint64_t led_cnt = 0;
    const uint8_t  dot = 20;
    while(true){
        delay(10);
        if(board->info.spec.name == BOARD_NMAXE_NAME || board->info.spec.name == BOARD_NMAXE_GAMMA_NAME){
            if(board->status.preference.led.sleep || !board->status.preference.led.enable) {
                if(board->info.spec.led.wifi_pin != -1) digitalWrite(board->info.spec.led.wifi_pin, HIGH); // off
                if(board->info.spec.led.pool_pin != -1) digitalWrite(board->info.spec.led.pool_pin, HIGH); // off
                if(board->info.spec.led.sys_pin != -1) ledcWrite(pwmChannel, 255); // off
                continue;
            }

            // Turn off all LEDs during OTA to avoid unexpected flashes
            if(board->status.ota.running) {
                if(board->info.spec.led.wifi_pin != -1) digitalWrite(board->info.spec.led.wifi_pin, HIGH); // off
                if(board->info.spec.led.pool_pin != -1) digitalWrite(board->info.spec.led.pool_pin, HIGH); // off
                if(board->info.spec.led.sys_pin != -1) ledcWrite(pwmChannel, 255); // off
                continue;
            }

            // Calculate current pattern index (0-9)
            uint8_t pattern_idx = (led_cnt % 201) / dot;
            
            if(pattern_idx > 0 && pattern_idx <= 10) {
                pattern_idx--; // Adjust to 0-based index (0-9)
                
                bool wifi_connected = (board->status.wifi.status == WL_CONNECTED);
                bool pool_connected = board->stratum->is_subscribed();
                
                // WiFi LED: slow blink when connected (only at pattern_idx 0), fast blink when disconnected (odd indices)
                bool wifi_state = wifi_connected ? (pattern_idx == 0) : (pattern_idx % 2 == 1);
                digitalWrite(board->info.spec.led.wifi_pin, wifi_state ? LOW : HIGH);

                // Pool LED: slow blink when connected (only at pattern_idx 0), fast blink when disconnected (odd indices)
                bool pool_state = pool_connected ? (pattern_idx == 0) : (pattern_idx % 2 == 1);
                digitalWrite(board->info.spec.led.pool_pin, pool_state ? LOW : HIGH);
            } else {
                // pattern_idx == 0: gap phase, explicitly hold LEDs off to avoid residual state
                if(board->info.spec.led.wifi_pin != -1) digitalWrite(board->info.spec.led.wifi_pin, HIGH);
                if(board->info.spec.led.pool_pin != -1) digitalWrite(board->info.spec.led.pool_pin, HIGH);
            }

            // SYS LED, slow breathing means hashrate > 0, fast breathing means hashrate == 0
            uint8_t speed = (board->status.miner.hashrate._3m > 0) ? 1 : 20;
            ledcWrite(pwmChannel, (uint32_t)((1 + sin(speed * led_cnt/100.0f)) * (1<<resolution - 1)));
            led_cnt++;
        }
        else if(board->info.spec.name == BOARD_NMQAXE_PLUS_PLUS_NAME){
            // --- sleep / disable: turn off all pixels ---
            if(board->status.preference.led.sleep || !board->status.preference.led.enable) {
                for(int i = 0; i < strip->numPixels(); i++)
                    strip->setPixelColor(i, strip->Color(0, 0, 0));
                strip->show();
                continue;
            }

            const uint8_t n = strip->numPixels(); // 8

            // Track OTA active state to detect start/end transitions
            static bool ota_was_active = false;

            // ---- OTA progress bar effect (overrides normal effects while OTA is running) ----
            if(board->status.ota.running) {
                static uint32_t ota_tick = 0;

                // On OTA start transition: send an explicit all-off frame to clear any residual
                // state from the previous LED effect. This prevents the RMT mid-frame glitch
                // (flash cache pause during spi_flash_write can cut WS2812 transmission, causing
                // the strip to latch a partial frame with leftover colors from the prior effect).
                if(!ota_was_active) {
                    for(int i = 0; i < n; i++)
                        strip->setPixelColor(i, strip->Color(0, 0, 0));
                    strip->show();
                    ota_tick = 0;
                    ota_was_active = true;
                    delay(2); // Let the strip latch the blank frame before entering progress loop
                }
                // Progress: 0-100 mapped to 0-n pixels lit
                // Color transition: Blue(0%) → Cyan(50%) → Green(100%)
                int   progress  = board->status.ota.progress;
                progress = (progress < 0) ? 0 : (progress > 100 ? 100 : progress);
                int   lit       = (progress * n + 99) / 100; // ceil, at least 1 pixel when started

                // Pulsing brightness so the bar visually "breathes" during OTA
                float pulse = (sinf(ota_tick * 0.08f) + 1.0f) / 2.0f * 0.4f + 0.6f; // 0.6 ~ 1.0

                // Color: lerp Blue→Cyan for first half, Cyan→Green for second half
                uint8_t r = 0;
                uint8_t g, b;
                if(progress <= 50) {
                    float t = progress / 50.0f;         // 0.0 → 1.0
                    g = (uint8_t)(255 * t * pulse);     // 0 → 255
                    b = (uint8_t)(255 * (1.0f - t * 0.5f) * pulse); // 255 → 128
                } else {
                    float t = (progress - 50) / 50.0f;
                    g = (uint8_t)(255 * pulse);
                    b = (uint8_t)(128 * (1.0f - t) * pulse); // 128 → 0
                }

                for(int i = 0; i < n; i++) {
                    if(i < lit)
                        strip->setPixelColor(i, strip->Color(r, g, b));
                    else
                        strip->setPixelColor(i, strip->Color(0, 0, 0));
                }
                strip->show();
                ota_tick++;
                continue;
            }

            // OTA just ended: set flag so effect scheduler resets on next iteration
            static bool post_ota_reset = false;
            if(ota_was_active && !board->status.ota.running) {
                ota_was_active = false;
                post_ota_reset = true;
                for(int i = 0; i < n; i++)
                    strip->setPixelColor(i, strip->Color(0, 0, 0));
                strip->show();
            }

            // ---- effect scheduler: 6 effects × 30 s each, then cycle ----
            const uint8_t  NUM_EFFECTS        = 6;
            const uint32_t EFFECT_DURATION_MS = 30000UL; // 30 s per effect
            static uint8_t  cur_effect     = 0;
            static uint32_t eff_start_ms   = 0;
            static uint32_t tick           = 0;

            uint32_t now_ms = millis();
            // Reset effect state on first run or after OTA ends
            if(eff_start_ms == 0 || post_ota_reset) {
                eff_start_ms   = now_ms;
                tick           = 0;
                cur_effect     = 0;
                post_ota_reset = false;
            }

            if(now_ms - eff_start_ms >= EFFECT_DURATION_MS) {
                cur_effect   = (cur_effect + 1) % NUM_EFFECTS;
                eff_start_ms = now_ms;
                tick         = 0;
                for(int i = 0; i < strip->numPixels(); i++)
                    strip->setPixelColor(i, strip->Color(0, 0, 0));
                strip->show();
            }

            // Helper: map 0-255 to a rainbow colour
            auto wheel = [&](uint8_t pos) -> uint32_t {
                pos = 255 - pos;
                if(pos < 85)  return strip->Color(255 - pos * 3, 0,           pos * 3);
                if(pos < 170) { pos -= 85;  return strip->Color(0, pos * 3,   255 - pos * 3); }
                pos -= 170;   return strip->Color(pos * 3,  255 - pos * 3,    0);
            };

            switch(cur_effect) {

                // ── Effect 0: Rainbow Flow ─────────────────────────────────
                // Rainbow hue shifts globally; offset increments by 2 each tick, colors staggered evenly per pixel
                case 0: {
                    uint8_t offset = (uint8_t)(tick * 2);
                    for(int i = 0; i < n; i++)
                        strip->setPixelColor(i, wheel((i * 256 / n + offset) & 0xFF));
                    strip->show();
                    break;
                }

                // ── Effect 1: Breathing Pulse ──────────────────────────────
                // All pixels breathe in sync; cycles R → G → B.
                // Color switches only when brightness dips near zero (sine trough),
                // so there is no visible hard cut between colors.
                case 1: {
                    float bri = (sinf(tick * 0.05f) + 1.0f) / 2.0f;
                    static uint8_t color_idx = 0;
                    static bool    was_dark   = false;
                    // Advance to next color only at the dark trough (bri < 2%)
                    if(bri < 0.02f) {
                        if(!was_dark) {
                            color_idx = (color_idx + 1) % 3;
                            was_dark  = true;
                        }
                    } else {
                        was_dark = false;
                    }
                    uint8_t r = (color_idx == 0) ? (uint8_t)(255 * bri) : 0;
                    uint8_t g = (color_idx == 1) ? (uint8_t)(255 * bri) : 0;
                    uint8_t b = (color_idx == 2) ? (uint8_t)(255 * bri) : 0;
                    for(int i = 0; i < n; i++)
                        strip->setPixelColor(i, strip->Color(r, g, b));
                    strip->show();
                    break;
                }

                // ── Effect 2: Theater Chase ────────────────────────────────
                // Every 3rd pixel lit simultaneously, chasing forward, color cycles over time
                case 2: {
                    for(int i = 0; i < n; i++) strip->setPixelColor(i, strip->Color(0, 0, 0));
                    uint8_t phase = (tick / 5) % 3;
                    uint32_t chase_color = wheel((uint8_t)(tick * 3));
                    for(int i = phase; i < n; i += 3)
                        strip->setPixelColor(i, chase_color);
                    strip->show();
                    break;
                }

                // ── Effect 3: Meteor Rain ──────────────────────────────────
                // White meteor head + fading trail, restarts from the beginning after reaching the end
                case 3: {
                    const uint8_t  METEOR_SIZE  = 3;
                    const float    TRAIL_DECAY  = 0.75f;
                    // decay existing colours
                    for(int i = 0; i < n; i++) {
                        uint32_t c = strip->getPixelColor(i);
                        uint8_t r2 = (uint8_t)(((c >> 16) & 0xFF) * TRAIL_DECAY);
                        uint8_t g2 = (uint8_t)(((c >>  8) & 0xFF) * TRAIL_DECAY);
                        uint8_t b2 = (uint8_t)(((c      ) & 0xFF) * TRAIL_DECAY);
                        strip->setPixelColor(i, strip->Color(r2, g2, b2));
                    }
                    // draw meteor head at current position
                    int meteor_pos = (tick / 2) % (n + METEOR_SIZE);
                    for(int j = 0; j < METEOR_SIZE; j++) {
                        int pos = meteor_pos - j;
                        if(pos >= 0 && pos < n) {
                            uint8_t bright = (j == 0) ? 255 : (j == 1 ? 160 : 80);
                            strip->setPixelColor(pos, strip->Color(bright, bright, bright));
                        }
                    }
                    strip->show();
                    break;
                }

                // ── Effect 4: Sparkle / Twinkle ───────────────────────────
                // Random pixels light up with random colors, others gradually fade out
                case 4: {
                    // fade all pixels
                    for(int i = 0; i < n; i++) {
                        uint32_t c = strip->getPixelColor(i);
                        uint8_t r2 = (uint8_t)(((c >> 16) & 0xFF) * 0.80f);
                        uint8_t g2 = (uint8_t)(((c >>  8) & 0xFF) * 0.80f);
                        uint8_t b2 = (uint8_t)(((c      ) & 0xFF) * 0.80f);
                        strip->setPixelColor(i, strip->Color(r2, g2, b2));
                    }
                    // randomly light one pixel every 3 ticks
                    if(tick % 3 == 0) {
                        int pos = random(0, n);
                        strip->setPixelColor(pos, wheel((uint8_t)random(0, 256)));
                    }
                    strip->show();
                    break;
                }

                // ── Effect 5: Color Wipe Fill ──────────────────────────────
                // Fill the strip with one solid color at a time then clear, cycling through each color
                case 5: {
                    static const uint32_t WIPE_COLORS[] = {
                        0xFF0000UL, // red
                        0x00FF00UL, // green
                        0x0000FFUL, // blue
                        0xFF8800UL, // orange
                    };
                    const uint8_t NUM_WIPE = 4;
                    // Each wipe phase = n*5 ticks (fill) + n*5 ticks (clear) = n*10 ticks total
                    uint32_t phase_len = (uint32_t)n * 10;
                    uint32_t color_idx = (tick / phase_len) % NUM_WIPE;
                    uint32_t phase_tick = tick % phase_len;
                    uint32_t col = WIPE_COLORS[color_idx];
                    uint8_t wr = (col >> 16) & 0xFF;
                    uint8_t wg = (col >>  8) & 0xFF;
                    uint8_t wb = (col      ) & 0xFF;
                    if(phase_tick < (uint32_t)n * 5) {
                        // fill stage
                        int fill_to = (int)(phase_tick / 5);
                        for(int i = 0; i < n; i++)
                            strip->setPixelColor(i, i <= fill_to ? strip->Color(wr, wg, wb) : strip->Color(0, 0, 0));
                    } else {
                        // clear stage
                        int clear_to = (int)((phase_tick - (uint32_t)n * 5) / 5);
                        for(int i = 0; i < n; i++)
                            strip->setPixelColor(i, i <= clear_to ? strip->Color(0, 0, 0) : strip->Color(wr, wg, wb));
                    }
                    strip->show();
                    break;
                }

                default: break;
            }

            tick++;
        }
    }
    LOG_I("led thread exit...");
    vTaskDelete(NULL);
}

void config_monitor_thread_entry(void *args){
    board_sal_t *board = (board_sal_t*)args;
    String taskName = "(wifi)";
    LOG_I("%s thread started on core %d...", taskName, xPortGetCoreID());
    LOG_I("Initializing wifi...");
    
    uint16_t timeout = 0;
    board->status.wifi.config_timeout = MINER_WIFI_CONFIG_TIMEOUT;
    while(true){
        if (WL_CONNECTED == board->status.wifi.status) break;

        if(board->status.wifi.client_connected == false){
            if(timeout++ >= MINER_WIFI_CONFIG_TIMEOUT){
                LOG_W("WiFi configuration timeout, rebooting...");
                delay(1000);
                ESP.restart();
            }
            board->status.wifi.config_timeout = MINER_WIFI_CONFIG_TIMEOUT - timeout;
        }
        delay(1000);
    }
    LOG_I("WiFi configuration monitor exit...");
    vTaskDelete(NULL);
}

void webserver_thread_entry(void *args){
    board_sal_t *board = (board_sal_t*)args;

    bool spiffs_ok = file_system_init();
    if (!spiffs_ok) {
        LOG_W("SPIFFS mount failed — entering recovery mode (firmware-embedded recovery page)");
    }

    // wait for sta or ap ready
    xEventGroupWaitBits(board->status.init_evt, INIT_EVENT_WIFI_STA_CONNECTED | INIT_EVENT_WIFI_AP_READY, pdFALSE, pdFALSE, portMAX_DELAY);
    delay(100);

    if (!spiffs_ok) {
        // ── Recovery mode: SPIFFS is unavailable ─────────────────────────────
        // Serve the firmware-embedded recovery page for every GET request so
        // the user can re-flash SPIFFS via a browser, then restart the device.
        webServer.on("/api/system/restart", HTTP_POST, post_restart);
        // OTA upload endpoints (both canonical and legacy aliases) so the
        // recovery page upload button works regardless of browser-cached URL.
        webServer.on("/api/update/firmware", HTTP_POST, [](AsyncWebServerRequest *request){}, file_upload_handler);
        webServer.on("/api/update/spiffs",   HTTP_POST, [](AsyncWebServerRequest *request){}, file_upload_handler);
        webServer.on("/api/system/OTA",      HTTP_POST, [](AsyncWebServerRequest *request){}, file_upload_handler);
        webServer.on("/api/system/OTAWWW",   HTTP_POST, [](AsyncWebServerRequest *request){}, file_upload_handler);
        // API probe endpoint: checkNewApiSupport() on other devices calls this to
        // decide which OTA URL to use. Returning a valid timeZone JSON lets the
        // swarm panel correctly select /api/update/firmware & /api/update/spiffs
        // instead of the legacy /api/system/OTA & /api/system/OTAWWW aliases.
        webServer.on("/api/setting/time", HTTP_GET, [](AsyncWebServerRequest *request){
            AsyncWebServerResponse *r = request->beginResponse(200, "application/json",
                "{\"timeZone\":\"UTC\",\"ntpServer\":\"pool.ntp.org\"}");
            r->addHeader("Access-Control-Allow-Origin", "*");
            request->send(r);
        });
        // Catch-all GET: serve recovery page from flash (no SPIFFS needed).
        // CORS header is required so the swarm probe (checkNewApiSupport) from
        // another device can read the response and correctly determine this is
        // NOT new-API firmware.
        webServer.on("/*", HTTP_GET, [](AsyncWebServerRequest *request){
            AsyncWebServerResponse *r = request->beginResponse(200, "text/html", recovery_page);
            r->addHeader("Access-Control-Allow-Origin", "*");
            request->send(r);
        });
        // CORS preflight handler — CRITICAL for swarm cross-origin upgrades.
        // When device B's browser upgrades device A via the swarm panel, it sends
        // a cross-origin POST (multipart/form-data). Some browser versions issue
        // an OPTIONS preflight before the actual POST. Without this handler the
        // preflight gets no response, the browser blocks the upload entirely, and
        // the swarm UI reports an immediate error with 0% progress.
        webServer.on("/*", HTTP_OPTIONS, [](AsyncWebServerRequest *request){
            AsyncWebServerResponse *r = request->beginResponse(200);
            r->addHeader("Access-Control-Allow-Origin",  "*");
            r->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, PATCH, DELETE");
            r->addHeader("Access-Control-Allow-Headers", "Accept,Content-Type");
            request->send(r);
        });
        webServer.begin();
        while (true) { delay(250); }
    }

    // ── Normal mode ───────────────────────────────────────────────────────────
    // Register AsyncWebSocket handler on webServer (same port 80, path /ws)
    webSocket.onEvent(webSocketEvent);
    webServer.addHandler(&webSocket);
    webServer.on("/api/system/info",  HTTP_GET, get_system_info);
    webServer.on("/api/system/restart",HTTP_POST, post_restart);
    webServer.on("/api/system/clearhits", HTTP_POST, post_reset_block_hits);
    // ── Dashboard data endpoints ──────────────────────────────────────────────
    webServer.on("/api/dashboard/hr/dist",       HTTP_GET, get_hr_distribution);
    webServer.on("/api/dashboard/gauge/limits",   HTTP_GET, get_gauge_limits);
    webServer.on("/api/dashboard/chart/history",  HTTP_GET, get_status_history);
    webServer.on("/api/dashboard/chart/realtime", HTTP_GET, get_status_realtime);
    webServer.on("/api/dashboard/luck/history",   HTTP_GET, get_lucky_history);
    // ── Swarm endpoints ───────────────────────────────────────────────────────
    webServer.on("/api/swarm", HTTP_GET, get_swarm_info_handler);
    // ── Logging and echo endpoints ───────────────────────────────────────────────
    webServer.on("/api/log", HTTP_GET, echo_handler);
    // ── OTA update endpoints ──────────────────────────────────────────────────
    webServer.on("/api/update/firmware", HTTP_POST, [](AsyncWebServerRequest *request){}, file_upload_handler); // canonical
    webServer.on("/api/update/spiffs",   HTTP_POST, [](AsyncWebServerRequest *request){}, file_upload_handler); // canonical
    webServer.on("/api/system/OTA",      HTTP_POST, [](AsyncWebServerRequest *request){}, file_upload_handler); // compat alias
    webServer.on("/api/system/OTAWWW",   HTTP_POST, [](AsyncWebServerRequest *request){}, file_upload_handler); // compat alias
    // ── Theme endpoints (OPTIONS covered by wildcard handler below) ───────────
    webServer.on("/api/theme", HTTP_GET,  get_theme_handler);
    webServer.on("/api/theme", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL, post_theme_handler);
    // ── Settings per-card endpoints ───────────────────────────────────────────
    webServer.on("/api/setting/network",    HTTP_GET,   get_setting_network);
    webServer.on("/api/setting/network",    HTTP_PATCH, [](AsyncWebServerRequest *request){}, NULL, patch_setting_network);
    webServer.on("/api/setting/time",       HTTP_GET,   get_setting_time);
    webServer.on("/api/setting/time",       HTTP_PATCH, [](AsyncWebServerRequest *request){}, NULL, patch_setting_time);
    webServer.on("/api/setting/mining",     HTTP_GET,   get_setting_mining);
    webServer.on("/api/setting/mining",     HTTP_PATCH, [](AsyncWebServerRequest *request){}, NULL, patch_setting_mining);
    webServer.on("/api/setting/market",     HTTP_GET,   get_setting_market);
    webServer.on("/api/setting/market",     HTTP_PATCH, [](AsyncWebServerRequest *request){}, NULL, patch_setting_market);
    webServer.on("/api/setting/preference", HTTP_GET,   get_setting_preference);
    webServer.on("/api/setting/preference", HTTP_PATCH, [](AsyncWebServerRequest *request){}, NULL, patch_setting_preference);
    webServer.on("/*", HTTP_GET, rest_common_get_handler);
    webServer.on("/*", HTTP_OPTIONS, [](AsyncWebServerRequest *request){
        AsyncWebServerResponse *response = request->beginResponse(200);
        response->addHeader("Access-Control-Allow-Origin", "*");
        response->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, PATCH, DELETE");
        response->addHeader("Access-Control-Allow-Headers", "Accept,Content-Type");
        request->send(response);
    });
    webServer.begin();
    // cleanupClients() must be called periodically so AsyncWebSocket can detect
    // dropped connections and fire WS_EVT_DISCONNECT for stale clients.
    while (true) {
        delay(250);
        webSocket.cleanupClients();
    }
}

void wifi_connect_thread_entry(void *args){
    board_sal_t *board = (board_sal_t*)args;

    auto wifiEvent = [board](WiFiEvent_t event, WiFiEventInfo_t info) {
        const char* reason = NULL;
        static uint8_t retry_cnt = 0, max_retries = 120;
        wifi_event_sta_disconnected_t disconnected;
        switch (event) {
            case SYSTEM_EVENT_STA_CONNECTED:
                LOG_I("WiFi connected to [%s], waiting for IP...", WiFi.SSID().c_str());
                break;
            case SYSTEM_EVENT_STA_GOT_IP:
                board->status.wifi.ip = WiFi.localIP();
                board->status.wifi.gateway = WiFi.gatewayIP();
                board->status.wifi.subnet = WiFi.subnetMask();
                board->status.wifi.dns = WiFi.dnsIP();
                board->status.wifi.status = WL_CONNECTED;
                retry_cnt = 0;
                LOG_I("Got IP : %s", WiFi.localIP().toString().c_str());
                break;
            case SYSTEM_EVENT_STA_DISCONNECTED:
                board->status.wifi.ip = IPAddress(0, 0, 0, 0);
                board->status.wifi.gateway = IPAddress(0, 0, 0, 0);
                board->status.wifi.subnet = IPAddress(0, 0, 0, 0);
                board->status.wifi.dns = IPAddress(0, 0, 0, 0);
                board->status.wifi.status = WL_DISCONNECTED;
                disconnected = info.wifi_sta_disconnected;
                reason = WiFi.disconnectReasonName((wifi_err_reason_t)disconnected.reason);
                retry_cnt++;
                LOG_W("WiFi disconnected, reason: %s", reason);
                break;
            default:
                break;
        }
        if(retry_cnt >= max_retries){
            LOG_W("WiFi connection retry limit reached, rebooting...");
            delay(1000);
            ESP.restart();
        }
    };

    WiFi.mode(WIFI_STA);
    WiFi.setTxPower(WIFI_POWER_15dBm);
    WiFi.onEvent(wifiEvent);
    WiFi.setHostname(board->info.base.hostname.c_str());

    //////////////////////////////// force config mode ////////////////////////////////
    if(board->status.wifi.force_config_required){
        nvs_config_set_u8(NVS_CONFIG_FORCE_CONFIG, false);
        LOG_W("Set softAP [%s]...", board->info.connection.wifi.ap.info.ssid.c_str());
        WiFi.mode(WIFI_AP);
        WiFi.softAP(board->info.connection.wifi.ap.info.ssid);
        WiFi.softAPConfig(board->info.connection.wifi.ap.ip, board->info.connection.wifi.ap.ip, IPAddress(255, 255, 255, 0));
        delay(500);
        xEventGroupSetBits(board->status.init_evt, INIT_EVENT_WIFI_AP_READY);
        //config time out monitor
        String taskName = "(config_monitor)";
        xTaskCreatePinnedToCore(config_monitor_thread_entry, taskName.c_str(), 1024*4, (void*)board, TASK_PRIORITY_CONFIG, NULL, 1);
        while(true){
            board->status.wifi.client_connected = (WiFi.softAPgetStationNum() > 0);
            if (WiFi.softAPgetStationNum() == 0) {
                LOG_W("Force configuration, ssid[%s], timeout: %ds...", board->info.connection.wifi.ap.info.ssid.c_str(), board->status.wifi.config_timeout);
            }
            delay(1000);
        }
    }

    //////////////////////////////// normal wifi connection //////////////////////////
    uint16_t random_delay = random(0, 1000*8);
    LOG_I("Initializing WiFi, delay: %dms...", random_delay);
    delay(random_delay);
    LOG_I("Try to connect [%s]...", board->info.connection.wifi.sta.ssid.c_str());
    WiFi.begin(board->info.connection.wifi.sta.ssid.c_str(), board->info.connection.wifi.sta.pwd.c_str());
    //wait for connection
    int maxRetries = 0;
    while (WiFi.status() != WL_CONNECTED && maxRetries < 60*5) {
        maxRetries++;
        LOG_I("Try to connect [%s] %ds...", board->info.connection.wifi.sta.ssid.c_str(), maxRetries);
        if(maxRetries >= 15){
            LOG_I("Set softAP [%s]...", board->info.base.hostname.c_str());
            WiFi.mode(WIFI_AP);
            WiFi.softAP(board->info.connection.wifi.ap.info.ssid);
            WiFi.softAPConfig(board->info.connection.wifi.ap.ip, board->info.connection.wifi.ap.ip, IPAddress(255, 255, 255, 0));
            delay(500);
            xEventGroupSetBits(board->status.init_evt, INIT_EVENT_WIFI_AP_READY);
            //config time out monitor
            String taskName = "(config_monitor)";
            xTaskCreatePinnedToCore(config_monitor_thread_entry, taskName.c_str(), 1024*4, (void*)board, TASK_PRIORITY_CONFIG, NULL, 1);
            while (true){
                board->status.wifi.client_connected = (WiFi.softAPgetStationNum() > 0);
                if (WiFi.softAPgetStationNum() == 0) {
                    LOG_W("Force configuration, ssid[%s], timeout: %ds...", board->info.connection.wifi.ap.info.ssid.c_str(), board->status.wifi.config_timeout);
                }
                delay(1000);
            }
        }
        delay(1000);
    }
    
    LOG_I("------------------------------------");
    LOG_I("SSID     : %s ", WiFi.SSID().c_str());
    LOG_I("IP       : %s ", WiFi.localIP().toString().c_str());
    LOG_I("RSSI     : %d dBm", WiFi.RSSI());
    LOG_I("Channel  : %d", WiFi.channel());
    LOG_I("DNS      : %s, %s", WiFi.dnsIP(0).toString().c_str(), WiFi.dnsIP(1).toString().c_str());
    LOG_I("Gateway  : %s", WiFi.gatewayIP().toString().c_str());
    LOG_I("Subnet   : %s", WiFi.subnetMask().toString().c_str());
    LOG_I("MAC      : %s", WiFi.macAddress().c_str());
    LOG_I("Hostname : %s", WiFi.getHostname());
    LOG_I("------------------------------------");

    xEventGroupSetBits(board->status.init_evt, INIT_EVENT_WIFI_STA_CONNECTED);
    vTaskDelete(NULL);
}

void button_thread_entry(void *args){
    board_sal_t *board = (board_sal_t*)args;

    OneButton *boot_btn = nullptr;
    OneButton *user_btn = nullptr;

    if(board->info.spec.btn.boot_pin != -1){
        boot_btn = new OneButton(board->info.spec.btn.boot_pin, true);
    }
    if(board->info.spec.btn.user_pin != -1){
        user_btn = new OneButton(board->info.spec.btn.user_pin, true);
    }

    // link the boot button functions.
    if(boot_btn != nullptr){
        static auto click_wrapper = [](void *param){ ui_switch_next_page_cb(*(uint8_t*)param); };
        static uint8_t click_evt = TOUCH_TAP_EVT;
        boot_btn->attachClick(click_wrapper, &click_evt);
        boot_btn->attachDoubleClick(silence_mode_cb);
        boot_btn->attachLongPressStart(NULL);
        boot_btn->attachLongPressStop(NULL);
        boot_btn->attachDuringLongPress(force_config_cb);
    }
    // link the user button functions.
    if(user_btn != nullptr){
        user_btn->attachClick(NULL);
        user_btn->attachDoubleClick(NULL);
        user_btn->attachLongPressStart(NULL);
        user_btn->attachLongPressStop(NULL);
        user_btn->attachDuringLongPress(recover_factory_cb);
    }

    while (true){
        if(boot_btn != nullptr){
            boot_btn->tick();
        }

        if(user_btn != nullptr){
            user_btn->tick();
        }
        delay(20);
    }
}

void swarm_thread_entry(void *args){
  board_sal_t *board = (board_sal_t*)args;

  //malloc udp client
  WiFiUDP* udp_client = new WiFiUDP();
  while (udp_client == nullptr){
    LOG_W("Failed to allocate memory for udp client, retry...");  
    delay(1000);
  }
  
  //udp status boardcast begin
  udp_client->begin(MINER_SWARM_UDP_BOARDCAST_PORT);

  uint64_t swarm_cnt = 0;
  char  jsonbuf[1024] = {0,};
  StaticJsonDocument<1024> jsonDoc;
  while (true){
    delay(100);
    if(board->status.wifi.status != WL_CONNECTED) continue;
    swarm_cnt++;
    //listen udp status
    if(swarm_cnt % 1 == 0){
      int packetSize = udp_client->parsePacket();

      char udpbuf[1152] = {0,}, json_udp_str[1152] = {0,};
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
          static std::map<String, uint32_t, std::less<String>, PsramAllocator<std::pair<const String, uint32_t>>> last_seen_map; // PSRAM
          if(jsonDoc.containsKey("ip")){
            board->status.swarm.map[jsonDoc["ip"].as<String>()] = String(jsonbuf);
            last_seen_map[jsonDoc["ip"].as<String>()] = jsonDoc["Lastseen"];
          }

          //update json string
          for(auto it = board->status.swarm.map.begin(); it != board->status.swarm.map.end();it++){
            //self status not in last seen map
            if(last_seen_map.find(it->first) == last_seen_map.end()) continue;

            if(deserializeJson(jsonDoc, it->second)) continue;

            jsonDoc["Lastseen"] = millis() - last_seen_map[it->first];
            //remove offline device
            if(jsonDoc["Lastseen"].as<uint32_t>() > MINER_SWARM_OFFLINE_TIMEOUT){
              board->status.swarm.map.erase(it->first);
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
      jsonDoc["Hostname"] = board->info.base.hostname;
      jsonDoc["ip"] = board->status.wifi.ip.toString();
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
      jsonDoc["RSSI"] = board->status.wifi.rssi;
      jsonDoc["FreeHeap"] = ESP.getFreeHeap() / 1024.0f;
      jsonDoc["Uptime"] = convert_uptime_to_string(board->status.miner.uptime_session) + "\r" + convert_uptime_to_string(board->status.miner.uptime_ever);
      jsonDoc["Version"] = board->info.base.fw_version;
      jsonDoc["BoardType"] = board->info.spec.name;
      jsonDoc["Power"]     = String(board->status.power.vbus*board->status.power.ibus/1000.0/1000.0, 1) + "W";
      jsonDoc["PoolInUse"] = String(board->info.connection.pool.use.url) + ":" + String(board->info.connection.pool.use.port);
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
      udp_client->beginPacket(MINER_SWARM_UDP_BOARDCAST_ADDR, MINER_SWARM_UDP_BOARDCAST_PORT);
      udp_client->write((uint8_t*)jsonbuf, n);
      int res = udp_client->endPacket();
      if(res != 1) {
        LOG_E("Swarm udp broadcast failed: %d", res);
        continue;
      }

      //add self to swarm list
      board->status.swarm.map[board->status.wifi.ip.toString()] = String(jsonbuf);
    }

    // parse swarm list and update best diff and total hashrate
    if(swarm_cnt % 40 == 0){
        board->status.swarm.map[board->status.wifi.ip.toString()] = String(jsonbuf);
        //calculate total hash rate and best diff of swarm list
        board->status.swarm.total_workers = board->status.swarm.map.size();
        board->status.swarm.total_hr = 0;
        board->status.swarm.best_diff = 0;
        for(auto it = board->status.swarm.map.begin(); it != board->status.swarm.map.end();it++){
            jsonDoc.clear();
            if(deserializeJson(jsonDoc, it->second)) continue;
            if(jsonDoc.containsKey("HashRate")){
                board->status.swarm.total_hr += parseHashRateStr(jsonDoc["HashRate"].as<String>());
            }
            if(jsonDoc.containsKey("BestDiff")){
                int newlineIndex = jsonDoc["BestDiff"].as<String>().indexOf('\r');
                String best_diff_str = "";
                if (newlineIndex != -1) {
                    best_diff_str = jsonDoc["BestDiff"].as<String>().substring(0, newlineIndex);
                    best_diff_str.trim();
                }else continue;
            
                float best_diff = parseDiffStr(best_diff_str);
                board->status.swarm.best_diff = (board->status.swarm.best_diff < best_diff) ? best_diff : board->status.swarm.best_diff;
            }
        }
    }
  }
}

void monitor_thread_entry(void *args){
    board_sal_t *board = (board_sal_t*)args;

    // NTP server list — rotated on consecutive failures
    // Covers all 7 continents; includes China-accessible servers (Alibaba/Tencent/NTSC)
    // to handle regions where Google/Cloudflare may be blocked.
    static const char* const NTP_SERVERS[] = {
        // ── Global anycast (most reliable, work almost everywhere) ──────────
        "pool.ntp.org",             // Global pool, anycast
        "time.cloudflare.com",      // Cloudflare, global anycast
        "time.apple.com",           // Apple, global, accessible in China
        "time.windows.com",         // Microsoft, global
        // ── Asia / China (no Google dependency) ────────────────────────────
        "ntp.aliyun.com",           // Alibaba Cloud, China mainland
        "ntp.tencent.com",          // Tencent Cloud, China mainland
        "ntp.ntsc.ac.cn",           // National Time Service Center of China
        "cn.pool.ntp.org",          // China NTP pool
        "asia.pool.ntp.org",        // Asia-Pacific pool
        // ── Europe ──────────────────────────────────────────────────────────
        "europe.pool.ntp.org",      // Europe pool
        "ntp.ubuntu.com",           // Canonical/Ubuntu, EU hosted
        // ── North America ───────────────────────────────────────────────────
        "north-america.pool.ntp.org",
        "time.google.com",          // Google (blocked in CN, fallback for others)
        // ── South America ───────────────────────────────────────────────────
        "south-america.pool.ntp.org",
        "a.ntp.br",                 // Brazil NTP (NIC.br)
        // ── Africa ──────────────────────────────────────────────────────────
        "africa.pool.ntp.org",
        // ── Oceania ─────────────────────────────────────────────────────────
        "oceania.pool.ntp.org",
        "au.pool.ntp.org",          // Australia
    };
    static const uint8_t  NTP_SERVER_COUNT   = sizeof(NTP_SERVERS) / sizeof(NTP_SERVERS[0]);
    static const uint8_t  NTP_MAX_FAIL       = 3;      // switch server after N consecutive failures
    static const uint32_t NTP_SYNC_INTERVAL  = 3600;   // sync every 3600 s (1 h), unit: uptime_session seconds

    uint8_t  ntp_server_idx  = 0;
    uint8_t  ntp_fail_cnt    = 0;
    bool     ntp_ever_synced = false;
    uint64_t last_ntp_sync   = 0;    // uptime_session value at last successful sync

    WiFiUDP   udpNtpClient;
    NTPClient *ntpClient = new NTPClient(udpNtpClient, NTP_SERVERS[ntp_server_idx]);
    uint64_t  last_nvs_save_time = board->status.miner.uptime_session;

    ntpClient->begin();
    ntpClient->setTimeOffset(0); // Get UTC time without timezone offset
    LOG_I("NTP client started, primary server: %s", NTP_SERVERS[ntp_server_idx]);

    while(true){
        // thread base interval 1000ms
        delay(1000);

        // --- NTP sync logic ---
        // Attempt a real sync:
        //   - immediately on first run once wifi is up
        //   - then every NTP_SYNC_INTERVAL seconds thereafter
        bool wifi_up     = (WL_CONNECTED == board->status.wifi.status);
        bool first_sync  = (!ntp_ever_synced && wifi_up);
        bool interval_ok = (board->status.miner.uptime_session - last_ntp_sync >= NTP_SYNC_INTERVAL);

        if(wifi_up && (first_sync || interval_ok)){
            LOG_I("NTP sync attempt [%d/%d] using server: %s ...", ntp_fail_cnt + 1, NTP_MAX_FAIL, NTP_SERVERS[ntp_server_idx]);
            bool sync_ok = ntpClient->forceUpdate();

            if(sync_ok){
                struct timeval tv;
                tv.tv_sec  = ntpClient->getEpochTime();
                tv.tv_usec = 0;
                settimeofday(&tv, NULL);
                board->status.time.utc = tv.tv_sec;

                // Convert decimal timezone to UTC±H:MM format
                float tz_offset = board->status.time.tz.toFloat();
                int tz_hour = (int)tz_offset;
                int tz_min  = (int)((fabs(tz_offset) - abs(tz_hour)) * 60 + 0.5f);

                char tz_buf[16] = {0};
                if(tz_offset >= 0){
                    if(tz_min == 0) sprintf(tz_buf, "UTC-%d",       tz_hour);
                    else            sprintf(tz_buf, "UTC-%d:%02d",   tz_hour, tz_min);
                } else {
                    if(tz_min == 0) sprintf(tz_buf, "UTC+%d",       abs(tz_hour));
                    else            sprintf(tz_buf, "UTC+%d:%02d",   abs(tz_hour), tz_min);
                }
                setenv("TZ", tz_buf, 1);
                tzset();

                String time_local = convert_time_to_local(board->status.time.utc);
                LOG_W("NTP sync OK  [server: %s] UTC[%llu] local[%s] tz[%s] env[%s]",
                        NTP_SERVERS[ntp_server_idx],
                        board->status.time.utc, time_local.c_str(),
                        board->status.time.tz.c_str(), tz_buf);

                ntp_fail_cnt    = 0;
                ntp_ever_synced = true;
                last_ntp_sync   = board->status.miner.uptime_session;
            } else {
                ntp_fail_cnt++;
                LOG_W("NTP sync FAIL [server: %s] consecutive_fail=%d/%d  ever_synced=%s  last_sync=%llus ago",
                        NTP_SERVERS[ntp_server_idx],
                        ntp_fail_cnt, NTP_MAX_FAIL,
                        ntp_ever_synced ? "yes" : "no",
                        ntp_ever_synced ? (board->status.miner.uptime_session - last_ntp_sync) : 0);

                if(ntp_fail_cnt >= NTP_MAX_FAIL){
                    uint8_t old_idx   = ntp_server_idx;
                    ntp_server_idx    = (ntp_server_idx + 1) % NTP_SERVER_COUNT;
                    ntp_fail_cnt      = 0;
                    ntpClient->end();
                    delete ntpClient;
                    ntpClient = new NTPClient(udpNtpClient, NTP_SERVERS[ntp_server_idx]);
                    ntpClient->begin();
                    ntpClient->setTimeOffset(0);
                    LOG_W("NTP server rotated: [%s] -> [%s]", NTP_SERVERS[old_idx], NTP_SERVERS[ntp_server_idx]);
                }

                // Fall back to system RTC
                time_t now;
                time(&now);
                board->status.time.utc = now;
            }
        } else {
            // Between sync intervals: keep board time updated from system RTC
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
            board->status.wifi.rssi = WiFi.RSSI();
            //give miner update signal
            xSemaphoreGive(board->status.miner.update_xsem);
        }
        
        //status check
        if(board->status.miner.uptime_session % 2 == 0){
            //check mcu temperature status
            if(board->status.temp.mcu > BOARD_MCU_TEMP_DANGER){
                LOG_W("MCU temp reach danger (mcu: %.1fC), restart miner...", board->status.temp.mcu);
                xSemaphoreGive(board->status.reboot_xsem);
            }

            //check vcore temperature status
            static uint8_t vcore_err_cnt = 0;
            if(board->status.temp.vcore > board->info.spec.pwr.temp_limit.high){
                uint16_t vcore_req_last = board->info.spec.asic.req_vcore;
                uint8_t step = 5;

                if(board->info.spec.asic.req_vcore >= board->info.spec.asic.min_vcore + step)
                    board->info.spec.asic.req_vcore -= step;
                else 
                    board->info.spec.asic.req_vcore = board->info.spec.asic.min_vcore;


                LOG_W("Vcore part temp reach danger (vcore: %.1fC), decrease vcore from %d to %d mV", board->status.temp.vcore, vcore_req_last, board->info.spec.asic.req_vcore);
                
                if(++vcore_err_cnt > 10){//avoid some noise
                    LOG_W("Vcore part temp keep danger, restart miner...");
                    xSemaphoreGive(board->status.reboot_xsem);
                }
            }else vcore_err_cnt = 0;

            //check asic temperature status
            static uint8_t asic_err_cnt = 0;
            if(board->status.temp.asic > board->info.spec.asic.temp_limit.high){
                uint16_t vcore_req_last = board->info.spec.asic.req_vcore;
                uint8_t step = 5;

                if(board->info.spec.asic.req_vcore >= board->info.spec.asic.min_vcore + step)
                    board->info.spec.asic.req_vcore -= step;
                else 
                    board->info.spec.asic.req_vcore = board->info.spec.asic.min_vcore;

                LOG_W("ASIC temp reach danger (asic: %.1fC), decrease vcore from %d to %d mV", board->status.temp.asic, vcore_req_last, board->info.spec.asic.req_vcore);
                if(++asic_err_cnt > 10){//avoid some noise
                    LOG_W("ASIC temp keep danger, restart miner...");
                    xSemaphoreGive(board->status.reboot_xsem);
                }
            }else asic_err_cnt = 0;

            //check fan status
            static uint16_t fan_err_cnt = 0;
            if(board->status.temp.asic > board->info.spec.asic.temp_limit.low){
                for(auto &fan : board->status.fan.list){
                    if(fan.rpm < board->info.spec.fans[fan.id].init.danger_rpm_thr){
                        fan_err_cnt++;
                        if(fan_err_cnt > 20){//avoid some noise
                            LOG_W("Fan rpm is too low, restart miner...");
                            xSemaphoreGive(board->status.reboot_xsem);
                        }
                    }
                    else fan_err_cnt = 0;
                }
            }

            //avoid restart when ota running
            if(!board->status.ota.running) {
                //check power status
                static uint16_t pwr_err_cnt = 0;
                if((board->status.power.vbus * board->status.power.ibus / 1000.0 / 1000.0) < board->info.spec.pwr.power_low_threshold){
                    LOG_W("Power %0.1fW is too low...", board->status.power.vbus * board->status.power.ibus / 1000.0 / 1000.0);
                    if(++pwr_err_cnt > 120){//120s
                        LOG_W("Power is too low, restart miner...");
                        xSemaphoreGive(board->status.reboot_xsem);
                    }
                }else pwr_err_cnt = 0;

                //check hashrate
                static uint16_t hr_err_cnt = 0;
                if(board->status.miner.hashrate._3m <= 1){
                    if(++hr_err_cnt > 60){//1min
                        LOG_W("Hashrate is too low, restart miner...");
                        xSemaphoreGive(board->status.reboot_xsem);
                    }
                }else hr_err_cnt = 0;
            }
        }

        // auto screen page scrolling
        if((board->status.miner.uptime_session % 10 == 0) && (true == board->status.preference.screen.auto_rolling)){
            ui_switch_next_page_cb(TOUCH_TAP_EVT);
        }

        //save status to NVS
        if(board->status.miner.uptime_session - last_nvs_save_time > BOARD_NVS_SAVE_INTERVAL){
            xSemaphoreGive(board->status.nvs_save_xsem);
        }
        
        //update miner status history queue
        if(board->status.miner.uptime_session % MINER_HISTORY_SAMPLE_INTERVAL == 0){
            history_node_t node;
            node.hashrate     = String(board->status.miner.hashrate._3m /1e9, 3); //Ghash/s
            node.asic_temp    = String(board->status.temp.asic,1);
            node.vcore_temp   = String(board->status.temp.vcore,1);
            node.pbus         = String((board->status.power.vbus * board->status.power.ibus / 1000.0f / 1000.0f),2); //W
            node.vbus         = String((board->status.power.vbus / 1000.0f),1); //V
            node.ibus         = String((board->status.power.ibus / 1000.0f),3); //A
            node.vcore        = board->status.power.vcore;//mV
            node.fanspeed     = board->status.fan.list[0].speed; //%
            node.fanrpm       = board->status.fan.list[0].rpm;
            node.wifi_rssi    = board->status.wifi.rssi;
            node.free_ram     = ESP.getFreeHeap() / 1024;  //free sram in Kbytes
            node.free_psram   = ESP.getFreePsram() / 1024; //free psram in Kbytes
            node.epoch        = board->status.time.utc * 1000ULL; // Convert UTC seconds to milliseconds
            node.latency      = board->status.miner.latency; // ms

            // add node to history queue and protect concurrent access
            if (xSemaphoreTake(board->status.miner.history_mutex, portMAX_DELAY) == pdTRUE) {
                board->status.miner.status_history.push_back(node);
                //remove old history
                uint64_t current_time_ms = board->status.time.utc * 1000ULL; // Convert to milliseconds
                while (!board->status.miner.status_history.empty()) {
                    uint64_t oldest_time_ms = board->status.miner.status_history.front().epoch; // Already in milliseconds
                    if(current_time_ms - oldest_time_ms > MINER_HISTORY_SAMPLE_DEEPTH){ 
                        board->status.miner.status_history.pop_front();
                        LOG_D("Remove old history, current size: %d, removed timestamp: %llu", board->status.miner.status_history.size(), oldest_time_ms);
                    } else {
                        break;
                    }
                }
                xSemaphoreGive(board->status.miner.history_mutex);
            }
        }

        //sample the hashrate for hashrate chart on ui every second
        if(board->status.miner.uptime_session % 1 == 0){
            static double last_hr_3m = 0;
            board->info.spec.ui.hashrate_dist_page.time = board->status.miner.uptime_session;
            if(board->status.miner.hashrate._3m != last_hr_3m) { // only update when hashrate changed to save some resource
                last_hr_3m = board->status.miner.hashrate._3m;
                static uint16_t SCALE = (board->info.spec.ui.hashrate_dist_page.max_x_hr / board->info.spec.ui.hashrate_dist_page.max_x_bars);
                static uint64_t *counts = NULL;
                if (counts == NULL) {
                    counts = (uint64_t *)malloc(board->info.spec.ui.hashrate_dist_page.max_x_bars * sizeof(uint64_t));
                    memset(counts, 0, board->info.spec.ui.hashrate_dist_page.max_x_bars * sizeof(uint64_t));
                }
                int index = last_hr_3m/1000/1000/1000 / SCALE; // Convert to GH/s and scale
                index = (index >= board->info.spec.ui.hashrate_dist_page.max_x_bars) ? board->info.spec.ui.hashrate_dist_page.max_x_bars - 1 : index;
                counts[index]++;
                board->info.spec.ui.hashrate_dist_page.count++;
                for (int i = 0; i < board->info.spec.ui.hashrate_dist_page.max_x_bars; i++) {
                    uint8_t y = (uint8_t)(100*(float)counts[i] / (float)board->info.spec.ui.hashrate_dist_page.count);
                    board->info.spec.ui.hashrate_dist_page.dist_map[i] = y;// Update the global distribution map
                }
            }
        }
    
        // recover factory if user long press user button
        if(xSemaphoreTake(board->status.recover_factory_xsem, 0) == pdTRUE){
            LOG_W("Factory reset triggered, erasing config and restart...");
            if(erase_all_nvs()){
                xSemaphoreGive(board->status.reboot_xsem);
            }else{
                LOG_E("Factory reset failed!");
            }
        }

        // force config if user long press boot button
        if(xSemaphoreTake(board->status.force_config_xsem, 0) == pdTRUE){
            LOG_W("Force configuration triggered, starting wifi in AP mode when next reboot...");
            nvs_config_set_u8(NVS_CONFIG_FORCE_CONFIG, true);
            xSemaphoreGive(board->status.reboot_xsem);
        }

        //save some status to NVS
        if(xSemaphoreTake(board->status.nvs_save_xsem, 0) == pdTRUE){
            nvs_config_set_string(NVS_CONFIG_BEST_EVER, String(board->status.miner.diff.best_ever).c_str());
            nvs_config_set_u16(NVS_CONFIG_BLOCK_HITS, board->status.miner.hits);
            nvs_config_set_u64(NVS_CONFIG_UPTIME, board->status.miner.uptime_ever);
            last_nvs_save_time = board->status.miner.uptime_session;
            LOG_W("Save diff best ever [%s], block hits [%d], uptime [%s]", formatNumber(board->status.miner.diff.best_ever, 4).c_str(), board->status.miner.hits, convert_uptime_to_string(board->status.miner.uptime_ever).c_str());
        }

        //save last ui page to NVS
        if(xSemaphoreTake(board->status.ui.page.save_xsem, 0) == pdTRUE){
            nvs_config_set_u8(NVS_CONFIG_UI_LAST_PAGE, board->status.ui.page.last);
            LOG_D("Last page %d saved to NVS", board->status.ui.page.last);
        }

        // update bringhtnes
        if(xSemaphoreTake(board->status.brightness_update_xsem, 0) == pdTRUE){
            tft_bl_ctrl(board->status.preference.screen.brightness);
            LOG_D("Update screen brightness to %d", board->info.preference.screen.brightness);
        }
    }
}

void daemon_thread_entry(void *args){
  board_sal_t *board = (board_sal_t*)args;

  while (true){
    delay(1000);
    //check ota status and reboot
    if(xSemaphoreTake(board->status.reboot_xsem, 0) == pdTRUE){
      ESP.restart();
    }

    //avoid restart when ota running
    if(board->status.ota.running) continue;

    //WiFi daemon
    if(xSemaphoreTake(board->status.wifi.reconnect_xsem, 0) == pdTRUE){
      WiFi.begin(board->info.connection.wifi.sta.ssid.c_str(), board->info.connection.wifi.sta.pwd.c_str());
    }

    // skip further checks if wifi not connected
    if(board->status.wifi.status != WL_CONNECTED) continue;

    //Stratum daemon
    if(millis() - board->status.miner.stratum_update > MINER_STRATUM_ALIVE_TIMEOUT){
      LOG_W("Stratum connection seems frozen, restarting...");
      xSemaphoreGive(board->status.reboot_xsem);
    }
    //ASIC daemon
    if(millis() - board->status.miner.asic_update > MINER_ASIC_ALIVE_TIMEOUT){
      LOG_W("ASIC seems frozen, restarting...");
      xSemaphoreGive(board->status.reboot_xsem);
    }
  }
  LOG_W("Daemon thread exiting...");
  delay(1000);       // Give some time for logging
  vTaskDelete(NULL); // This line is not strictly necessary, but it's good practice to clean up the task when done.
}

void fan_thread_entry(void *args){
    board_sal_t *board = (board_sal_t*)args;


    int16_t now_count = 0, last_count = 0, temp_cnt = 0;
    uint32_t start_ms = millis();
    delay(100);

    // Initialize TMP102 temperature sensor
    tmp102_init();

    //fan init
    for(auto &fan : board->info.spec.fans){
        // fan initialize with defined parameters
        fan_drv_init(fan.init);
        LOG_D("Fan[%d] initialized with torch pin %d, pwm pin %d", fan.id, fan.init.torch.pulse_gpio_num, fan.init.pwm.pin);
    }

    // polarity detection
    for(auto &fan : board->info.spec.fans){
        fan.polarity = guess_fan_polarity(fan.init);
        LOG_I("Guess fan[%d] polarity :[%s]", fan.id, fan.polarity ? "inverted" : "normal");
    }

    // Helper function to find fan config by id
    auto get_fan_config = [&](uint8_t fan_id) -> fan_config_t* {
        for(auto &fan_cfg : board->info.spec.fans){
            if(fan_cfg.id == fan_id){
                return &fan_cfg;
            }
        }
        return nullptr;
    };

    // fan self test
    while(true){
        bool self_test_result[board->info.spec.fans.size()] = {false,}; // initialize all to false
        bool all_fan_ok = true; // assume all fans are okay until we find one that isn't

        for(auto &fan : board->status.fan.list){
            if(self_test_result[fan.id])  continue;
            
            fan_config_t* fan_cfg = get_fan_config(fan.id);
            if(fan_cfg == nullptr) continue; // skip if fan config not found
            
            bool fan_invert = fan_cfg->polarity;  // find fan polarity by id from config
            fan_init_t init_param = fan_cfg->init;// find fan init config by id from config

            for(uint8_t i = 0; i < 3; i++){
                fan.rpm = measure_fan_rpm_for_duration(init_param, 1.0, 1000, fan_invert); 
            }

            fan.self_test = (fan.rpm > fan_cfg->init.self_test_rpm_thr) ? true : false;
            self_test_result[fan.id] = fan.self_test;
            LOG_W("Fan[%d] self test result: %s, measured rpm: %d, threshold rpm: %d", fan.id, fan.self_test ? "OK" : "FAIL", fan.rpm, fan_cfg->init.self_test_rpm_thr);
        }

        for(auto &fan : board->status.fan.list){
            // if any fan self test result is false, set all_fan_ok to false
            all_fan_ok = all_fan_ok && self_test_result[fan.id];
        }

        if(all_fan_ok && (board->status.fan.list.size() > 0)) break;
        LOG_E("Fan self test failed, please check fan wiring and connection, retrying in 5s...");
        delay(10);
    }
    xEventGroupSetBits(board->status.init_evt, INIT_EVENT_FAN_READY);

    // fan control loop
    while(true){
        for(auto &fan : board->status.fan.list){
            fan_config_t* fan_cfg = get_fan_config(fan.id);
            if(fan_cfg == nullptr) continue; // skip if fan config not found
            
            bool fan_invert = fan_cfg->polarity;  // find fan polarity by id from config
            fan_init_t init_param = fan_cfg->init;// find fan init config by id from config

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
                uint32_t delta_time = millis() - start_ms;
                pcnt_get_counter_value(init_param.torch.unit, &now_count);
                uint16_t delta_pcnt = 0;
                if (now_count < last_count) delta_pcnt = (init_param.torch.counter_h_lim - last_count) + now_count;
                else delta_pcnt = now_count - last_count;
                fan.rpm = calculate_rpm(delta_pcnt, delta_time / 1000.0);
                last_count = now_count;
                start_ms = millis();
            }

            // Adjust fan speed
            if(board->status.preference.fan.is_auto_speed && fan.self_test){
                static uint32_t pid_start = millis();
                float dt = (millis() - pid_start) / 1000.0f; // Convert to seconds
                fan.speed = (uint16_t)pid_compute(&fan_cfg->pid, board->status.preference.fan.target_temp, board->status.temp.asic, dt);
                pid_start = millis();
            }
            fan_set_speed(init_param, fan.speed / 100.0, fan_invert);
        }
    }
}

void market_thread_entry(void *args){
    board_sal_t *board = (board_sal_t*)args;

    // Wait until the MarketClass instance has been allocated
    while (board->market == NULL){
        LOG_W("MarketClass instance is NULL, waiting...");
        delay(1000);
    }

    // Wait for WiFi, then print all available USDT trading pairs once at startup.
    // This helps users discover which symbols are supported before configuring one.
    while (board->status.wifi.status != WL_CONNECTED) delay(1000);
    LOG_I("Fetching available USDT trading pairs from Binance...");
    board->market->fetch_available_usdt_pairs();

    // Number of consecutive retry attempts before giving up for this cycle
    const uint8_t  MARKET_MAX_RETRIES    = 3;
    // Short pause between retries to avoid hammering the server
    const uint32_t MARKET_RETRY_DELAY_MS = 2000;

    while(true){
        if(board->status.wifi.status == WL_CONNECTED){
            bool fetched = false;
            for(uint8_t attempt = 1; attempt <= MARKET_MAX_RETRIES; attempt++){
                if(board->market->refresh_main_pair(board->info.base.coin_price)){
                    fetched = true;
                    break;
                }
                LOG_D("Market data fetch failed (attempt %d/%d) for symbol [%sUSDT]",
                      attempt, MARKET_MAX_RETRIES, board->info.base.coin_price.c_str());
                if(attempt < MARKET_MAX_RETRIES){
                    delay(MARKET_RETRY_DELAY_MS);
                }
            }
            if(!fetched){
                LOG_E("Market data fetch failed after %d attempts. Please verify that the Binance API is accessible in your country.", MARKET_MAX_RETRIES);
            }

            // Fetch watchlist pairs
            board->market->refresh_watchlist(board->info.base.coin_watchlist);
        } else {
            LOG_D("Market update skipped: WiFi not connected.");
        }
        // Interruptible sleep: wake up immediately when NVS coin settings change.
        for (uint32_t _end = millis() + MINER_MARKET_UPDATE_INTERVAL; millis() < _end; ) {
            if (board->market->consume_refresh_request()) break;
            delay(100);
        }
    }

    delete board->market;
    board->market = nullptr;
    LOG_W("Market thread exit.");
    vTaskDelete(NULL);
}

void miner_asic_count_thread_entry(void *args){
    board_sal_t *board = (board_sal_t*)args;

    while(board->miner == nullptr){
        LOG_W("Waiting for miner instance ready...");
        delay(1000);
    }

    // wait for vdd and vpll ready, avoid some asic chip not detected issue
    xEventGroupWaitBits(board->status.init_evt, INIT_EVENT_VDD_VPLL_READY, pdFALSE, pdTRUE, portMAX_DELAY);

    // wait for asic detected, avoid some usb-sata bridge not ready issue
    while(board->miner->connect_chip() == 0) {
        LOG_W("Waiting for asic chip detected...");
        delay(1000);
    }

    // set asic counted event
    xEventGroupSetBits(board->status.init_evt, INIT_EVENT_ASIC_COUNTED);

    if(board->miner->get_asic_count() != board->info.spec.asic.num_req){
        LOG_E("Detected ASIC count (%d/%d) does not match required ASIC count!!!!", board->miner->get_asic_count(), board->info.spec.asic.num_req);
    }

    vTaskDelete(NULL);
}

void miner_asic_init_thread_entry(void *args){
    board_sal_t *board = (board_sal_t*)args;

    while(board->miner == nullptr){
        LOG_W("Waiting for miner instance ready...");
        delay(1000);
    }

    // wait asic count done
    // wait for vcore ready
    // wait fan self-test event
    // wait wifi connect
    xEventGroupWaitBits(g_board.status.init_evt, INIT_EVENT_ASIC_COUNTED | INIT_EVENT_VCORE_READY | INIT_EVENT_FAN_READY | INIT_EVENT_WIFI_STA_CONNECTED, pdFALSE, pdTRUE, portMAX_DELAY);

    //begin asic hardware
    if(!board->miner->begin(board->info.spec.asic.req_frq, board->info.spec.asic.diff_thr_init, board->info.spec.asic.com_baud_work)){
        while (true){
            LOG_E("Miner ASIC init failed, retrying...");
            delay(1000);
        }
    }
    LOG_I("%s init completed, job interval set to %d ms", board->info.spec.asic.name.c_str(), board->info.spec.asic.job_interval_ms);
    vTaskDelete(NULL);
}

void miner_asic_tx_thread_entry(void *args){
    board_sal_t *board = (board_sal_t*)args;


    auto calculate_diff = [](String nBits) -> double{
        static const uint8_t  TARGET_BUFFER_SIZE = 64;
        uint8_t netdiff_array[TARGET_BUFFER_SIZE/2];

        char str[TARGET_BUFFER_SIZE + 1];
        memset(str, '0', TARGET_BUFFER_SIZE);
        int k = (int) strtol(nBits.substring(0, 2).c_str(), 0, 16) - 3; 
        uint8_t index = 58 - 2 * k; 
        memcpy(str + index, nBits.substring(2).c_str(), nBits.length() - 2);
        str[TARGET_BUFFER_SIZE] = 0;
        
        str_to_byte_array(str, TARGET_BUFFER_SIZE/2, netdiff_array);
        reverse_bytes(netdiff_array, TARGET_BUFFER_SIZE/2);

        return le_hash_to_diff(netdiff_array);
    };

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
        board->status.miner.diff.network = calculate_diff(board->miner->pool_job_now.nbits);
        //update pool diff
        board->status.miner.diff.pool = board->stratum->get_pool_difficulty();
        
        LOG_W("Job [%s] from %s:%d", board->miner->pool_job_now.id.c_str(), board->stratum->pool->get_pool_info().url.c_str(), board->stratum->pool->get_pool_info().port);
        while (true){
            //construct asic job and send to asic every 2s
            if(!board->miner->mining(&board->miner->pool_job_now)) continue;
            //exit if pool disconnected
            if(!board->stratum->is_subscribed()) break;

            //set asic diff as pool diff if pool diff < initial asic diff
            double target_diff = min(board->stratum->get_pool_difficulty(), (double)board->info.spec.asic.diff_thr_init);
            static double last_diff = 0.0; // initialize to 0 to ensure the first update occurs
            if(target_diff != last_diff){
                uint32_t diff = board->miner->set_asic_diff(target_diff);
                LOG_W("Change asic diff from [%.1f] to [%d/%.1f] successfully", last_diff, diff, target_diff);
                last_diff = target_diff;
            }

            //interval 'job_interval_ms' every asic job, exit if a new pool job arrived
            if(xSemaphoreTake(board->stratum->new_job_xsem, board->info.spec.asic.job_interval_ms) == pdTRUE) {
                //avoid some stale share submit, clear job cache if clean job signal received
                if(xSemaphoreTake(board->stratum->clear_job_xsem, 0) == pdTRUE) {
                    board->miner->clear_asic_job_cache();
                    board->stratum->clear_sub_extranonce2();
                    LOG_D("Stratum job cache clear...");
                }
                xSemaphoreGive(board->stratum->new_job_xsem);//release the semaphore for next pool job
                break;
            }
        }
    }
}

void miner_asic_rx_thread_entry(void *args){
    board_sal_t *board = (board_sal_t*)args;


    asic_job job = {0,};
    miner_result result = {0,};

    auto calculate_diff = [](uint32_t version, uint8_t* prev_block_hash, uint8_t* merkle_root, uint32_t ntime, uint32_t nbits, uint32_t nonce) -> double{
            uint8_t header[4 + 32 + 32 + 4 + 4 + 4] = {0,};
            uint8_t hash[32] = {0,};
            uint8_t prev_block_hash_t[32] = {0}, merkle_root_t[32] = {0};

            memcpy(prev_block_hash_t, prev_block_hash, 32);
            memcpy(merkle_root_t, merkle_root, 32);

            reverse_words(prev_block_hash_t, 32);
            reverse_words(merkle_root_t, 32);

            memcpy(header, (uint8_t*)&version, 4);
            memcpy(header + 4, prev_block_hash_t, 32);
            memcpy(header + 36, merkle_root_t, 32);
            memcpy(header + 68, (uint8_t*)&ntime, 4);
            memcpy(header + 72, (uint8_t*)&nbits, 4);
            memcpy(header + 76, (uint8_t*)&nonce, 4);
            //caculate hash
            csha256d(header, sizeof(header), hash);
            return le_hash_to_diff(hash);
    };

    //forever loop
    while(true){
        if(!board->stratum->is_subscribed()){
            delay(1000);
            continue;
        }
        esp_err_t err = board->miner->listen_asic_rsp(&result, 1000*30);
        if(ESP_OK == err){
            if(!board->stratum->is_subscribed()) continue;
            if(board->miner->find_job_by_asic_job_id(result.asic.job_id, &job)){
                board->status.miner.asic_update = millis();
                uint32_t version_bits       = (reverse_uint16(result.asic.version) << 13);  //logic from project bitaxe: https://github.com/skot/bitaxe 
                uint32_t version            = version_bits | (*(uint32_t*)job.version);//logic from project bitaxe: https://github.com/skot/bitaxe 
                double diff                 = calculate_diff(version, job.prev_block_hash, job.merkle_root, *(uint32_t*)job.ntime, *(uint32_t*)job.nbits, result.asic.nonce);

                //continue if diff is nan or diff is inf
                if((diff <= std::numeric_limits<double>::epsilon()) || std::isnan(diff) || std::isinf(diff)) continue;

                //continue if diff < asic diff threshold 
                if(diff < board->miner->get_asic_diff()) continue;

                //update hashrate anyway, even if diff < pool diff, some high diff pool may need this, avoid local hashrate freeze. 
                board->miner->calculate_hashrate(&board->status.miner.hashrate);

                // update specific asic share count, based on asic id
                board->status.miner.asic_rsp_counter[result.asic_id]++;

                //print summary to log
                static uint32_t last = millis();
                if(millis() - last >= MINER_LOG_SUMMARY_INTERVAL){
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
                    last = millis();
                }
                
                LOG_I("|%-6s|%-6s|%-6s|%-7s|", 
                    formatNumber(board->miner->get_asic_diff(), 4).c_str(), 
                    formatNumber(diff, 4).c_str(), 
                    formatNumber(board->stratum->get_pool_difficulty(), 4).c_str(),
                    formatNumber(board->status.miner.diff.network, 7).c_str()
                );

                //continue if diff < pool diff threshold
                if(diff < board->stratum->get_pool_difficulty())continue; 
                
                //submit sulution
                uint32_t version_submit = version ^ (*(uint32_t*)job.version);
                String   extra2_submit = board->miner->get_extranonce2_by_asic_job_id(result.asic.job_id);
                bool res = board->miner->submit_job_share(extra2_submit, result.asic.nonce, *(uint32_t*)job.ntime, version_submit);
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
                    memcpy(header + 76, (uint8_t*)&result.asic.nonce, 4);
                    //caculate hash
                    csha256d(header, sizeof(header), hash);

                    LOG_W("******************************* Your Are The Chosen One ********************************");
                    LOG_I("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!BLOCK FOUND!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
                    log_i("Nonce       : %08x", result.asic.nonce);
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

    StaticJsonDocument<1024*4> json;
    bool is_primary_pool = true;

    double pool_init_diff = board->info.spec.asic.diff_thr_init;
    board->stratum->set_pool_difficulty(pool_init_diff);
    while(true){
        static int w_retry = 0, w_maxRetries = 24;
        if(board->status.wifi.status != WL_CONNECTED){
            w_retry++;
            LOG_W("WiFi reconnecting %d/%d...", w_retry, w_maxRetries);
            if(w_retry >= w_maxRetries) ESP.restart();

            xSemaphoreGive(board->status.wifi.reconnect_xsem);
            board->stratum->reset();
            board->stratum->set_pool_difficulty(pool_init_diff);
            delay(5000);
            continue;
        } else w_retry = 0;


        // check pool status 
        if((board->info.connection.pool.use == board->info.connection.pool.primary) && (board->stratum->pool->is_connected())){
            is_primary_pool = true;   
        }
        else if((board->info.connection.pool.use == board->info.connection.pool.fallback) && (board->stratum->pool->is_connected())){
            is_primary_pool = false;   
        }


        static uint32_t last = millis();
        static uint16_t random_seconds = 60 + esp_random() % (9 * 60 + 1); // randomize 1~10 minutes to avoid all miners check primary pool at the same time
        if((millis() - last > 1000UL * random_seconds) && !is_primary_pool){ // check every 1~10 minutes if primary pool is back, only when currently using fallback pool
            bool res = board->stratum->is_primary_pool_available(board->info.connection.pool.primary.url, board->info.connection.pool.primary.port);
            if(res){
                LOG_I("Primary pool [%s] available now, switching to primary pool...", board->info.connection.pool.primary.url.c_str());
                board->info.connection.pool.use = board->info.connection.pool.primary;
                board->info.connection.stratum.use = board->info.connection.stratum.primary;

                board->stratum->reset(board->info.connection.pool.use, board->info.connection.stratum.use);
                board->stratum->set_pool_difficulty(pool_init_diff);
                board->stratum->pool->begin(board->info.connection.pool.use.ssl);
                board->stratum->pool->connect();
                board->status.miner.diff.last = 0;
            }else{
                LOG_W("Primary pool [%s] is not available.", board->info.connection.pool.primary.url.c_str());
            }
            last = millis();
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
                    board->info.connection.pool.use    = board->info.connection.pool.fallback;
                    board->info.connection.stratum.use = board->info.connection.stratum.fallback;
                    LOG_W(">>>> Set pool to fallback [%s:%d] <<<<", board->info.connection.pool.use.url.c_str(), board->info.connection.pool.use.port);
                }else{
                    board->info.connection.pool.use    = board->info.connection.pool.primary;
                    board->info.connection.stratum.use = board->info.connection.stratum.primary;
                    LOG_W(">>>> Set pool to primary [%s:%d] <<<<", board->info.connection.pool.use.url.c_str(), board->info.connection.pool.use.port);
                }
            }
            board->stratum->reset(board->info.connection.pool.use, board->info.connection.stratum.use);
            board->stratum->set_pool_difficulty(pool_init_diff);
            board->stratum->pool->begin(board->info.connection.pool.use.ssl);
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
                        board->stratum->job_counter_inc();
                        board->status.miner.stratum_update = millis();//pool alive timestamp
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
                            board->status.miner.latency = millis() - rsp.stamp;
                            if (rsp.status == true){
                                board->status.miner.share_accepted++;
                                LOG_L("#%d share accepted, %ldms", board->status.miner.share_accepted + board->status.miner.share_rejected, board->status.miner.latency);      
                            }
                            else {
                                board->status.miner.share_rejected++;
                                LOG_E("#%d share rejected, %ldms", board->status.miner.share_accepted + board->status.miner.share_rejected, board->status.miner.latency);
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
                            board->status.miner.latency = millis() - rsp.stamp;
                            board->status.miner.share_rejected++;
                            LOG_E("#%d share rejected, %ldms", board->status.miner.share_accepted + board->status.miner.share_rejected, board->status.miner.latency);
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
            delay(1);
        }
        // responsive idle: poll available() every 1ms (up to 50ms) to minimize latency measurement error
        for (uint8_t i = 0; i < 50 && !board->stratum->pool->available(); i++) { delay(1); }
    }
}

void lvgl_tick_thread_entry(void *args){
  board_sal_t *board = (board_sal_t*)args;
  uint16_t tick_interval = 5;
  uint32_t last_tick = millis();

  xEventGroupWaitBits(board->status.init_evt, INIT_EVENT_SCREEN_READY, pdFALSE, pdTRUE, portMAX_DELAY);
  // lvgl core init
  lv_init();
  // ui driver register
  ui_drv_register();
  // notify lvgl ready
  xEventGroupSetBits(board->status.init_evt, INIT_EVENT_LVGL_READY);  
  // wait ui thread ready
  xEventGroupWaitBits(board->status.init_evt, INIT_EVENT_UI_READY, pdFALSE, pdTRUE, portMAX_DELAY);
  while(true){
    delay(tick_interval);
    if (xSemaphoreTake(board->status.ui.lvgl.drv_xMutex, tick_interval) == pdTRUE){
      lv_tick_inc(millis() - last_tick);
      lv_timer_handler(); /* let the GUI do its work */
      xSemaphoreGive(board->status.ui.lvgl.drv_xMutex); 
      last_tick = millis();
    }
  }
}

void ui_thread_entry(void *args){
    board_sal_t *board = (board_sal_t*)args;
    // define a map of page update functions, the key is page enum, the value is the corresponding update function
    const std::map<uint8_t, ui_page_update_func_t> ui_page_update_cbs = {
        {UI_PAGE_LOADING,   ui_loading_page_update},
        {UI_PAGE_CONFIG,    ui_config_page_update},
        {UI_PAGE_MINER,     ui_miner_page_update},
        {UI_PAGE_DASHBOARD, ui_dashboard_page_update},
        {UI_PAGE_HR_HEALTH, ui_hr_healthy_page_update},
        {UI_PAGE_CLOCK,     ui_clock_page_update},
        {UI_PAGE_MARKET,    ui_market_page_update},
        {UI_PAGE_SETTING,   ui_setting_page_update}   
    };
    // wait lvgl ready is necessary, otherwise may cause some lvgl api call fail due to lvgl not ready, such as lv_obj_create, which is widely used in ui element init and page update
    xEventGroupWaitBits(board->status.init_evt, INIT_EVENT_LVGL_READY, pdFALSE, pdTRUE, portMAX_DELAY);
    // ui page element init
    ui_page_element_init(board);
    // ui layout init
    ui_layout_init(board);
    // notify ui ready
    xEventGroupSetBits(board->status.init_evt, INIT_EVENT_UI_READY);  
    // make sure the loading page is visible when screen on, in case some critical info like power status is needed during boot
    ui_goto_page(board->status.ui.page.current, LV_ANIM_ON);

    while (true){
        delay(50);
        if(xSemaphoreTake(board->status.ui.lvgl.drv_xMutex, 5) == pdTRUE){
            // find the update function based on current page and call it, if not found, just skip page update
            auto it = ui_page_update_cbs.find(board->status.ui.page.current);
            if(it != ui_page_update_cbs.end()){
                it->second((void*)board);  
            }
            // for(auto& cb : ui_page_update_cbs){
            //     cb.second((void*)board);
            // }

            // countdown page update, if running, cover current page
            ui_countdown_page_update((void*)board);
            // block hits page popup, if hit, cover current page
            ui_hits_page_update((void*)board);
            // OTA page update, if running, cover current page
            ui_ota_page_update((void*)board);
            //release mutex
            xSemaphoreGive(board->status.ui.lvgl.drv_xMutex); 
        }
    }
}

void display_thread_entry(void *args){
  board_sal_t *board = (board_sal_t*)args;

  String vbus_chk_str[]   = {"Vbus check   ","Vbus check.  ","Vbus check.. ","Vbus check..."};
  String vcore_chk_str[]  = {"Vcore check   ","Vcore check.  ","Vcore check.. ","Vcore check..."};
  String asci_init_str[]  = {"ASIC init  ","ASIC init.  ","ASIC init.. ","ASIC init..."};
  String wifi_con_str[]   = {"Wifi connect   ","Wifi connect.  ","Wifi connect.. ","Wifi connect..."};
  String fan_test_str[]   = {"Fan test   ","Fan test.  ","Fan test.. ","Fan test..."};
  String market_con_str[] = {"Market connect   ","Market connect.  ","Market connect.. ","Market connect..."};
  String ver_chk_str[]    = {"Version check ","Version check.","Version check..","Version check..."};
  String pool_con_str[]   = {"Pool connect   ","Pool connect.  ","Pool connect.. ","Pool connect..."};
  String pool_auth_str[]  = {"Pool auth   ","Pool auth.  ","Pool auth.. ","Pool auth..."};
  String wait_job_str[]   = {"Waiting pool job   ","Waiting pool job.  ","Waiting pool job.. ","Waiting pool job..."};
  String config_str[]     = {"Config   ","Config.  ","Config.. ","Config..."};

  // tft hardware init
  tft_init(board);
  // touch hardware init, even if touch not detected, still need to init to avoid some screen with touch panel have no display if touch init fail.
  touch_init(board);
  // notify screen ready
  xEventGroupSetBits(board->status.init_evt, INIT_EVENT_SCREEN_READY);  
  // wait lvgl and ui thread ready
  xEventGroupWaitBits(board->status.init_evt, INIT_EVENT_UI_READY | INIT_EVENT_LVGL_READY, pdFALSE, pdTRUE, portMAX_DELAY);

  //backlight brightness ramp up
  for(int i = 0; i < board->status.preference.screen.brightness; i++) {
    tft_bl_ctrl(i);
    delay(10);
  }

  uint16_t cnt = 0;
  /****************************************wait for Vbus ready*******************************************/
  board->status.ui.page.loading.percent = 0.1;
  while (!board->power->is_adc_ready()){
    board->status.ui.page.loading.details.color = 0xFFFFFF;
    board->status.ui.page.loading.details.msg   = vbus_chk_str[(cnt++)%4];
    delay(500);
  }
  /********************************Vbus type check and voltage check*************************************/
  board->status.ui.page.loading.percent = 0.2;
  board->status.ui.page.loading.details.color = 0x00ff00;
  if(board->power->is_dc_pluged()) board->status.ui.page.loading.details.msg   = "DC pluged.";
  else board->status.ui.page.loading.details.msg   = "USB pluged.";
  delay(500);
  if(board->power->get_vbus() < board->info.spec.pwr.vbus_min_required){
      static bool blink = false;
      board->status.ui.page.loading.details.color = (blink) ? 0xFF0000 : 0xFFFFFF;
      String vbusString = "Vbus " + String(board->power->get_vbus()/1000.0, 1) + "v(at least" + String(board->info.spec.pwr.vbus_min_required / 1000.0, 1) + "v)";
      board->status.ui.page.loading.details.msg   = vbusString;
      blink = !blink;
      delay(1000);
  }
  board->status.ui.page.loading.details.color = 0x00FF00;
  board->status.ui.page.loading.details.msg   = "Vbus " + String(board->power->get_vbus() / 1000.0, 3) + "V.";
  delay(500);
//   xEventGroupWaitBits(board->status.init_evt, INIT_EVENT_VBUS_READY, pdFALSE, pdTRUE, portMAX_DELAY);
  /****************************************wait for wifi connected***************************************/
  cnt = 0;
  board->status.ui.page.loading.percent = 0.3;
  while(board->status.wifi.status != WL_CONNECTED){
    board->status.ui.page.loading.details.color = 0xFFFFFF;
    board->status.ui.page.loading.details.msg   = wifi_con_str[(cnt++)%4]  + String("[") + board->info.connection.wifi.sta.ssid +  String("]");
    delay(300);
    if((xEventGroupWaitBits(board->status.init_evt, INIT_EVENT_WIFI_AP_READY, pdFALSE, pdTRUE, 100) & INIT_EVENT_WIFI_AP_READY)  == INIT_EVENT_WIFI_AP_READY){
      board->status.ui.page.loading.details.color = 0xFF0000;
      board->status.ui.page.loading.details.msg   = String("Timeout!");
      delay(1000);
      ui_goto_page(UI_PAGE_CONFIG, LV_ANIM_ON);
    }
  }
  board->status.ui.page.loading.details.color = 0x00FF00;
  board->status.ui.page.loading.details.msg   = "Wifi Connected!";
  delay(500);
  xEventGroupWaitBits(board->status.init_evt, INIT_EVENT_WIFI_STA_CONNECTED, pdFALSE, pdTRUE, portMAX_DELAY);
  /****************************************wait for asic init********************************************/
  cnt = 0;
  board->status.ui.page.loading.percent = 0.4;
  while(board->miner == nullptr) {
    LOG_W("Miner object not created yet\r\n");
    delay(1000); //wait miner object created
  }
  while(board->miner->get_asic_count() == 0){
    board->status.ui.page.loading.details.color = 0xFFFFFF;
    board->status.ui.page.loading.details.msg   = String(asci_init_str[cnt++ % 4]);
    delay(100);
  }
  uint8_t asic_cnt     = board->miner->get_asic_count();
  String  asic_cnt_str = (asic_cnt > 1) ? (String(asic_cnt) + "/" + String(board->info.spec.asic.num_req) + " chips") : "1 chip";

  board->status.ui.page.loading.details.color = (asic_cnt != board->info.spec.asic.num_req) ? 0xFF0000 : 0x00FF00;
  board->status.ui.page.loading.details.msg   = "Found " + asic_cnt_str;
  delay(3000);
  xEventGroupWaitBits(board->status.init_evt, INIT_EVENT_ASIC_COUNTED, pdFALSE, pdTRUE, portMAX_DELAY);
  /********************************************wait fan self test ****************************************/
  cnt = 0;
  board->status.ui.page.loading.percent = 0.5;
  for(uint8_t i = 0; i < board->info.spec.fans.size(); i++){
    while(true){
      board->status.ui.page.loading.details.color = 0xFFFFFF;
      board->status.ui.page.loading.details.msg   = String(fan_test_str[cnt++ % 4]) + String(board->status.fan.list[i].rpm) + "/ " + String(board->info.spec.fans[i].init.self_test_rpm_thr) + "rpm";
      if((xEventGroupWaitBits(board->status.init_evt, INIT_EVENT_FAN_READY, pdFALSE, pdTRUE, 100) & INIT_EVENT_FAN_READY)  == INIT_EVENT_FAN_READY) break;
    }
    board->status.ui.page.loading.details.color = 0x00FF00;
    board->status.ui.page.loading.details.msg   = "Fan" + ((board->info.spec.fans.size() > 1) ? String(i + 1) : "") + " Pass! [" + String(board->status.fan.list[i].rpm) + "/ " + String(board->info.spec.fans[i].init.self_test_rpm_thr) + " rpm]";
    delay(2000);
  }
  xEventGroupWaitBits(g_board.status.init_evt, INIT_EVENT_FAN_READY, pdFALSE, pdTRUE, portMAX_DELAY);
  /******************************************wait Vcore self test ****************************************/
  cnt = 0;
  board->status.ui.page.loading.details.color = 0xFFFFFF;
  board->status.ui.page.loading.percent = 0.6;
  board->status.ui.page.loading.details.msg   = vcore_chk_str[0];
  delay(500);
  while(true){
    board->status.ui.page.loading.details.msg   = vcore_chk_str[(cnt++)%4];
    if((xEventGroupWaitBits(board->status.init_evt, INIT_EVENT_VCORE_READY, pdFALSE, pdTRUE, 100) & INIT_EVENT_VCORE_READY)  == INIT_EVENT_VCORE_READY) break;
  }
  delay(200);//wait for vcore set to target voltage
  board->status.ui.page.loading.details.color = 0x00FF00;
  board->status.ui.page.loading.details.msg   = String("Vcore ") + String(board->power->get_vcore() / 1000.0, 3) + "v.";
  delay(500);
  xEventGroupWaitBits(g_board.status.init_evt, INIT_EVENT_VCORE_READY, pdFALSE, pdTRUE, portMAX_DELAY);
  /****************************************wait for market connected*************************************/
  cnt = 0;
  board->status.ui.page.loading.percent = 0.7;
  uint32_t start = millis();
  while(0 == board->market->get_last_update()){
    board->status.ui.page.loading.details.color = 0xFFFFFF;
    board->status.ui.page.loading.details.msg   = market_con_str[(cnt++)%4] + "[" + board->info.base.coin_price + "]";
    if(millis() - start - board->market->get_last_update() >= MINER_MARKET_CONNECT_TIMEOUT){
      board->status.ui.page.loading.details.color = 0xFF0000;
      board->status.ui.page.loading.details.msg   = "Market update timeout!";
      delay(500);
      break;
    }
    delay(300);
  }
  delay(500);
  if(0 != board->market->get_last_update()) {
    board->status.ui.page.loading.details.color = 0x00FF00;
    board->status.ui.page.loading.details.msg   = "Market connected!";
  }
  delay(1000);
  /****************************************wait for pool connected**************************************/
  cnt = 0;
  board->status.ui.page.loading.percent = 0.8;
  while(!board->stratum->is_subscribed()){
    if(board->stratum->pool->get_last_errormsg().length() > 0){
      board->status.ui.page.loading.details.color = (cnt % 2 == 0) ? 0xFFFFFF : 0xFF0000;
      board->status.ui.page.loading.details.msg   = board->stratum->pool->get_last_errormsg().c_str();
    }else{
      String con_type = board->info.connection.pool.use.ssl ? "[ssl]" : "[tcp]";
      board->status.ui.page.loading.details.color = 0xFFFFFF;
      board->status.ui.page.loading.details.msg   = String(pool_con_str[(cnt)%4] + con_type);
    }
    cnt++;
    delay(300);
  }
  board->status.ui.page.loading.details.color = 0x00FF00;
  board->status.ui.page.loading.details.msg   = "Pool connected!";
  delay(100);
  /*******************************************wait for pool auth****************************************/
  cnt = 0;
  board->status.ui.page.loading.percent = 0.9;
  while(!board->stratum->is_authorized()){
    board->status.ui.page.loading.details.color = 0xFFFFFF;
    board->status.ui.page.loading.details.msg   = pool_auth_str[(cnt++)%4];
    bool blink = false;
    while (cnt >= 20){
      board->status.ui.page.loading.details.color = (blink) ? 0xFFFFFF : 0xFF0000;
      board->status.ui.page.loading.details.msg   = "Wrong stratum user!";
      delay(500);
      if(board->stratum->is_authorized()) break;
    }
    delay(300);
  }
  board->status.ui.page.loading.details.color = 0x00FF00;
  board->status.ui.page.loading.details.msg   = "Pool authorized!";
  delay(100);
  /****************************************wait for pool job******************************************/
  cnt = 0;
  board->status.ui.page.loading.percent = 1.0;
  while(board->stratum->get_job_counter() == 0){
    board->status.ui.page.loading.details.color = 0xFFFFFF;
    board->status.ui.page.loading.details.msg   = wait_job_str[(cnt++)%4];
    delay(100);
    bool blink = false;
    while ((cnt >= 60*10) && (board->stratum->get_job_counter() == 0)){
      board->status.ui.page.loading.details.color = (blink) ? 0xFFFFFF : 0xFF0000;
      board->status.ui.page.loading.details.msg   = "Pool job timeout!";
      delay(500);
    }
  }
  board->status.ui.page.loading.details.color = 0x00FF00;
  board->status.ui.page.loading.details.msg   = "Miner ready!";
  xEventGroupWaitBits(board->status.init_evt, INIT_EVENT_MINER_READY, pdFALSE, pdTRUE, portMAX_DELAY);
  
  /***************************************scroll to last page******************************************/
  ui_goto_page(board->status.ui.page.last, LV_ANIM_ON);
  //exit this thread
  vTaskDelete(NULL);
}

