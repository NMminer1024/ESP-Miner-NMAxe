#include "drivers/displays/display.h"
#include "utils/logger/logger.h"
#include "utils/reboot_log/reboot_log.h"
#include "global.h"
#include "OneButton.h"
#include "drivers/fan/fan.h"
#include "utils/sha/csha256.h"
#include "nvs/nvs_config.h"
#include <NTPClient.h>
#include "web/http_server.h"
#include "web/recovery_page.h"
#include <Adafruit_NeoPixel.h>
#include "lwip/sockets.h"
#include "lwip/icmp.h"
#include "lwip/inet_chksum.h"
#include "lwip/ip4.h"
#include "lwip/etharp.h"
#include "lwip/netif.h"
#include "lwip/tcpip.h"
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <unordered_set>

void power_init_thread_entry(void *args){
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

    vTaskDelete(NULL);
}

void power_loop_thread_entry(void *args){
    board_sal_t *board = (board_sal_t*)args;

    xEventGroupWaitBits(g_board.status.init_evt, INIT_EVENT_VCORE_READY, pdFALSE, pdTRUE, portMAX_DELAY);

    uint32_t _pwr_debug_last_ms = 0;
    while(true){
        delay(100);
#if 0
        // debugPrint throttled to once every 3 seconds
        uint32_t _now = millis();
        if (_now - _pwr_debug_last_ms >= 2000) {
            board->power->debugPrint();
            _pwr_debug_last_ms = _now;
        }
#endif
        bool oc_warn  = board->power->is_oc_warn();
        bool oc_fault = board->power->is_oc_fault();
        if(oc_fault) {
            LOG_W("Overcurrent FAULT detected! Taking safety actions...");
            // Immediate safety action: shut down ASIC power
            board->power->set_vcore_status(PWR_OFF);
            // Optionally, shut down other power rails if supported
            board->power->set_vdd_1v8(PWR_OFF);
            board->power->set_pll_0v8(PWR_OFF);
            // Signal the display thread to show the OC alert overlay
            xEventGroupSetBits(board->status.sys_evt, SYS_EVENT_POWER_OC_FAULT);
            // Park this thread — display/UI thread keeps running to handle user interaction
            while(true) {
                delay(1000);
                LOG_E("ASIC powered down due to Power OverCurrent. Please check your ASICs OverClocking settings, and cooling.");
            }
        }
        else if(oc_warn) {
            static uint32_t last = millis(); // count in seconds
            if(millis() - last >= 3000) { // if OC warning persists for 10 seconds, log a warning
                LOG_W("Overcurrent WARNING detected...");
                last = millis(); // reset count after logging
            }
        }

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
                static int      last_ota_progress = -1;
                static uint32_t last_ota_show_ms  = 0;

                // On OTA start: blank the strip first to clear residual state from previous effect.
                // This prevents WS2812 from latching a partial frame caused by the RMT transmission
                // being interrupted when the flash cache is paused during spi_flash_write.
                if(!ota_was_active) {
                    for(int i = 0; i < n; i++)
                        strip->setPixelColor(i, strip->Color(0, 0, 0));
                    strip->show();
                    last_ota_progress = -1;
                    last_ota_show_ms  = 0;
                    ota_was_active    = true;
                    delay(10); // Let strip latch the blank frame fully before entering progress loop
                }

                int progress = board->status.ota.progress;
                progress = (progress < 0) ? 0 : (progress > 100 ? 100 : progress);

                uint32_t now_ms = millis();
                // KEY FIX: only refresh when progress value changes, and rate-limit to once per 500ms.
                // During OTA the flash cache is periodically disabled for each SPI flash write; calling
                // strip->show() (which runs from flash) inside that window stalls the CPU mid-RMT-frame,
                // causing WS2812 to latch a partial/corrupt frame — the visible "ghost flicker".
                // Reducing show() calls to ≤2/s makes it statistically unlikely to land inside a write
                // window, eliminating the flicker without sacrificing progress bar visibility.
                if(progress != last_ota_progress || (now_ms - last_ota_show_ms) >= 500) {
                    last_ota_progress = progress;
                    last_ota_show_ms  = now_ms;

                    // Static color bar (no pulsing): Blue(0%) → Cyan(50%) → Green(100%)
                    int lit = (progress * n + 99) / 100; // ceil: at least 1 pixel lit once started
                    uint8_t r = 0, g, b;
                    if(progress <= 50) {
                        float t = progress / 50.0f;
                        g = (uint8_t)(200 * t);
                        b = (uint8_t)(200 * (1.0f - t * 0.5f));
                    } else {
                        float t = (progress - 50) / 50.0f;
                        g = 200;
                        b = (uint8_t)(100 * (1.0f - t));
                    }
                    for(int i = 0; i < n; i++)
                        strip->setPixelColor(i, (i < lit) ? strip->Color(r, g, b) : strip->Color(0, 0, 0));
                    strip->show();
                }
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
    
    board->status.wifi.config_timeout = MINER_WIFI_CONFIG_TIMEOUT;
    while(true){
        if (WL_CONNECTED == board->status.wifi.status) break;

        if(board->status.wifi.client_connected == false){
            // config_timeout is decremented here; UI thread may reset it to
            // MINER_WIFI_CONFIG_TIMEOUT on touch activity to extend the window.
            // For NMQAxe++: the UI thread owns the decrement (to use lv_indev for
            // touch detection). Thread only triggers the reboot when timeout reaches 0.
            if(board->info.spec.name == BOARD_NMQAXE_PLUS_PLUS_NAME){
                if(board->status.wifi.config_timeout == 0){
                    LOG_W("WiFi configuration timeout, rebooting...");
                    reboot_intent_set(REBOOT_INTENT_WIFI_CONFIG_TIMEOUT,
                                      "no client connected during AP setup window");
                    delay(1000);
                    ESP.restart();
                }
                // Do NOT decrement here — UI thread handles it
            } else {
                if(board->status.wifi.config_timeout == 0){
                    LOG_W("WiFi configuration timeout, rebooting...");
                    reboot_intent_set(REBOOT_INTENT_WIFI_CONFIG_TIMEOUT,
                                      "no client connected during AP setup window");
                    delay(1000);
                    ESP.restart();
                }
                board->status.wifi.config_timeout--;
            }
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
        webServer.on("/api/system/info",  HTTP_GET, get_system_info);
        // ── Recovery mode: wakeup endpoint so the swarm batch-upgrade flow (which calls
        // /api/wakeup before streaming the binary) does not fail immediately on devices
        // in recovery mode. Without this the wakeup GET falls through to the catch-all,
        // returns HTML, Angular httpClient fails to parse JSON → OTA never starts.
        webServer.on("/api/wakeup", HTTP_GET, [](AsyncWebServerRequest *request){
            AsyncWebServerResponse *r = request->beginResponse(200, "application/json", "{\"ok\":true}");
            r->addHeader("Access-Control-Allow-Origin", "*");
            request->send(r);
        });
        // ── Find me: also available in recovery mode so the swarm panel can locate
        // a device that is stuck in recovery (SPIFFS failed) by blinking its screen.
        webServer.on("/api/swarm/find", HTTP_POST, [board](AsyncWebServerRequest* request) {
            xEventGroupSetBits(board->status.sys_evt, SYS_EVENT_FIND_NEIGHBOR_TRIGGERED);
            AsyncWebServerResponse *r = request->beginResponse(200, "application/json", "{\"status\":\"ok\"}");
            r->addHeader("Access-Control-Allow-Origin", "*");
            request->send(r);
        });
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
        // for miner probe endpoint, swarm panel calls this to get hashrate and difficulty for swarm mining display , Keep this endpoint lightweight and fast.
        webServer.on("/probe", HTTP_GET, [board](AsyncWebServerRequest* request) {
            // Snapshot double fields once: double is non-atomic on 32-bit ESP32,
            // reading directly in printf could yield a torn value.
            double hr  = board->status.miner.hashrate._3m;
            double sbd = board->status.miner.diff.best_session;
            double ebd = board->status.miner.diff.best_ever;
            if (!isfinite(hr)  || hr  < 0.0) hr  = 0.0;
            if (!isfinite(sbd) || sbd < 0.0) sbd = 0.0;
            if (!isfinite(ebd) || ebd < 0.0) ebd = 0.0;
            AsyncResponseStream* resp = request->beginResponseStream("application/json");
            resp->addHeader("Access-Control-Allow-Origin", "*");
            resp->print("{");
            resp->printf("\"model\":\"%s\",",    board->info.spec.name.c_str());  // board model name
            resp->printf("\"hostname\":\"%s\",",    board->info.base.hostname.c_str()); // hostname
            resp->printf("\"ver\":\"%s\",", board->info.base.fw_version.c_str()); // firmware version
            resp->printf("\"sw\":%d,",           board->info.spec.tft.width);   // TFT width from board spec
            resp->printf("\"sh\":%d,",           board->info.spec.tft.height);  // TFT height from board spec
            resp->printf("\"hr\":%.0f,",       hr);  // hashrate in H/s, 3m average
            resp->printf("\"sbd\":%.0f,",      sbd); // best session difficulty
            resp->printf("\"ebd\":%.0f,",      ebd); // best ever difficulty
            resp->printf("\"ut\":%d",      board->status.miner.uptime_session);   // uptime in seconds for current session
            resp->print("}");
            request->send(resp);
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

    // Register AsyncWebSocket handler on webServer (same port 80, path /ws)
    webSocket.onEvent(webSocketEvent);
    webServer.addHandler(&webSocket);
    webServer.on("/api/system/info", HTTP_GET, get_system_info);
    webServer.on("/api/system/restart",HTTP_POST, post_restart);
    webServer.on("/api/system/clearhits", HTTP_POST, post_reset_block_hits);
    // ── Reboot history (planned vs crash, persisted across boots) ───────────
    webServer.on("/api/reboot/last", HTTP_GET,    get_reboot_last);
    webServer.on("/api/reboot/list", HTTP_GET,    get_reboot_list);
    webServer.on("/api/reboot/list", HTTP_DELETE, delete_reboot_list);
    // ── Coredump (post-mortem summary in flash, cleared on demand) ─────────
    // The full ELF blob is intentionally NOT exposed: the on-card summary
    // (PC + backtrace + task + sha) carries everything we need to triage.
    webServer.on("/api/coredump/info", HTTP_GET,    get_coredump_info);
    webServer.on("/api/coredump",      HTTP_DELETE, delete_coredump);
    // ── Wakeup: any caller (local or cross-origin swarm panel) can wake this device's screensaver.
    // Dashboard and swarm pages call this to keep the screensaver inactive.
    // Setting / update / log pages do NOT call this, so the screensaver can activate
    // while the user is away from the real-time monitoring pages.
    webServer.on("/api/wakeup", HTTP_GET, [board](AsyncWebServerRequest *request){
        board->status.ui.last_active_ms = millis();
        xEventGroupClearBits(board->status.sys_evt, SYS_EVENT_SCREEN_SAVER_TRIGGERED);
        AsyncWebServerResponse *r = request->beginResponse(200, "application/json", "{\"ok\":true}");
        r->addHeader("Access-Control-Allow-Origin", "*");
        request->send(r);
    });
    // ── Dashboard data endpoints ──────────────────────────────────────────────
    webServer.on("/api/dashboard/hr/dist",        HTTP_GET, get_hr_distribution);
    webServer.on("/api/dashboard/gauge/limits",   HTTP_GET, get_gauge_limits);
    webServer.on("/api/dashboard/chart/history",  HTTP_GET, get_status_history);
    webServer.on("/api/dashboard/chart/realtime", HTTP_GET, get_status_realtime);
    webServer.on("/api/dashboard/luck/history",   HTTP_GET, get_lucky_history);
    // ── Swarm endpoints ───────────────────────────────────────────────────────
    webServer.on("/api/swarm/scan", HTTP_POST, [board](AsyncWebServerRequest* request) {
        if (board->status.neighbor.scan_required){
            xSemaphoreGive(board->status.neighbor.scan_required);
            LOG_I("Triggered alive IP scan by swarm request");
        }
        AsyncWebServerResponse *r = request->beginResponse(200, "application/json", "{\"status\":\"ok\"}");
        r->addHeader("Access-Control-Allow-Origin", "*");
        request->send(r);
    }); // trigger alive_ip_scan_thread immediately
    // ── Find me: blink screen to help user locate a specific device ───────────
    webServer.on("/api/swarm/find", HTTP_POST, [board](AsyncWebServerRequest* request) {
        xEventGroupSetBits(board->status.sys_evt, SYS_EVENT_FIND_NEIGHBOR_TRIGGERED);
        AsyncWebServerResponse *r = request->beginResponse(200, "application/json", "{\"status\":\"ok\"}");
        r->addHeader("Access-Control-Allow-Origin", "*");
        request->send(r);
    });
    // ── Logging and echo endpoints ───────────────────────────────────────────────
    webServer.on("/api/log", HTTP_GET, echo_handler);
    // ── OTA update endpoints ──────────────────────────────────────────────────
    webServer.on("/api/update/progress", HTTP_GET,  get_ota_progress);                                          // progress poll
    // Wakeup is handled by the frontend: swarm/update pages call /api/wakeup before uploading.
    webServer.on("/api/update/firmware", HTTP_POST, [](AsyncWebServerRequest *request){}, file_upload_handler); // canonical
    webServer.on("/api/update/spiffs",   HTTP_POST, [](AsyncWebServerRequest *request){}, file_upload_handler); // canonical
    webServer.on("/api/system/OTA",      HTTP_POST, [](AsyncWebServerRequest *request){}, file_upload_handler); // compat alias
    webServer.on("/api/system/OTAWWW",   HTTP_POST, [](AsyncWebServerRequest *request){}, file_upload_handler); // compat alias
    webServer.on("/api/update/screensaver",HTTP_POST,  [](AsyncWebServerRequest *request){}, file_upload_handler);
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
    // ── Benchmark endpoints ────────────────────────────────────────────────────
    webServer.on("/api/benchmark",         HTTP_GET,    get_benchmark);
    webServer.on("/api/benchmark",         HTTP_PATCH,  [](AsyncWebServerRequest *request){}, NULL, patch_benchmark);
    webServer.on("/api/benchmark/start",   HTTP_POST,   [](AsyncWebServerRequest *request){}, NULL, post_benchmark_start);
    webServer.on("/api/benchmark/stop",    HTTP_POST,   post_benchmark_stop);
    webServer.on("/api/benchmark/results", HTTP_DELETE, delete_benchmark_results);
    webServer.on("/api/benchmark/reset",   HTTP_POST,   post_benchmark_reset);
    webServer.on("/api/benchmark/apply",   HTTP_POST,   [](AsyncWebServerRequest *request){}, NULL, post_benchmark_apply);

    // for miner probe endpoint, swarm panel calls this to get hashrate and difficulty for swarm mining display , Keep this endpoint lightweight and fast.
    webServer.on("/probe", HTTP_GET, [board](AsyncWebServerRequest* request) {
        // Return 503 during OTA — avoid competing with the flash write for TCP/lwIP resources.
        if (board->status.ota.running) {
            request->send(503, "text/plain", "OTA in progress");
            return;
        }
        // Snapshot double fields once: double is non-atomic on 32-bit ESP32,
        // reading directly in printf could yield a torn value.
        double hr  = board->status.miner.hashrate._3m;
        double sbd = board->status.miner.diff.best_session;
        double ebd = board->status.miner.diff.best_ever;
        if (!isfinite(hr)  || hr  < 0.0) hr  = 0.0;
        if (!isfinite(sbd) || sbd < 0.0) sbd = 0.0;
        if (!isfinite(ebd) || ebd < 0.0) ebd = 0.0;
        AsyncResponseStream* resp = request->beginResponseStream("application/json");
        resp->addHeader("Access-Control-Allow-Origin", "*");
        resp->print("{");
        resp->printf("\"model\":\"%s\",",    board->info.spec.name.c_str());  // board model name
        resp->printf("\"hostname\":\"%s\",",    board->info.base.hostname.c_str()); // hostname
        resp->printf("\"ver\":\"%s\",", board->info.base.fw_version.c_str()); // firmware version
        resp->printf("\"sw\":%d,",           board->info.spec.tft.width);   // TFT width from board spec
        resp->printf("\"sh\":%d,",           board->info.spec.tft.height);  // TFT height from board spec
        resp->printf("\"hr\":%.0f,",       hr);  // hashrate in H/s, 3m average
        resp->printf("\"sbd\":%.0f,",      sbd); // best session difficulty
        resp->printf("\"ebd\":%.0f,",      ebd); // best ever difficulty
        resp->printf("\"ut\":%d",      board->status.miner.uptime_session);   // uptime in seconds for current session
        resp->print("}");
        request->send(resp);
    });
    // alive ip return from miner, just alive ip, maybe phone, pc, or miner itself, for miner probe endpoint, AxeOS probe one by one .
    webServer.on("/alive", HTTP_GET, [board](AsyncWebServerRequest* request) {
        // Return 503 during OTA — avoid competing with the flash write for TCP/lwIP resources.
        if (board->status.ota.running) {
            request->send(503, "text/plain", "OTA in progress");
            return;
        }
        String self_ip = WiFi.localIP().toString();
        AsyncResponseStream* resp = request->beginResponseStream("application/json");
        resp->addHeader("Access-Control-Allow-Origin", "*");
        bool scanning   = board->status.neighbor.is_scanning;
        uint16_t prog   = board->status.neighbor.scan_progress;
        uint32_t lms    = 0;
        resp->printf("{\"self\":\"%s\",\"scanning\":%s,\"progress\":%u,\"total\":254",
            self_ip.c_str(), scanning ? "true" : "false", (unsigned)prog);
        if (xSemaphoreTake(board->status.neighbor.mutex, pdMS_TO_TICKS(300)) == pdTRUE) {
            lms = board->status.neighbor.last_scan_ms;
            xSemaphoreGive(board->status.neighbor.mutex);
        }
        uint32_t next_in = 0;
        if (!scanning && lms > 0) {
            uint32_t elapsed_s = (millis() - lms) / 1000;
            next_in = (elapsed_s < 300) ? (300 - elapsed_s) : 0;
        }
        resp->printf(",\"next_scan_in\":%u", (unsigned)next_in);
        resp->printf(",\"ips\":[\"%s\"", self_ip.c_str());
        if (xSemaphoreTake(board->status.neighbor.mutex, pdMS_TO_TICKS(300)) == pdTRUE) {
                for (const auto& ip : board->status.neighbor.alive_ips)
                    resp->printf(",\"%s\"", ip.c_str());
                xSemaphoreGive(board->status.neighbor.mutex);
        }
        resp->print("]}");
        request->send(resp);
    });
    webServer.on("/api-doc", HTTP_GET, [](AsyncWebServerRequest *request){
        request->redirect("/api-doc.html");
    });
    webServer.on("/*", HTTP_GET, [board](AsyncWebServerRequest *request){
        // Wake screensaver when the user loads or refreshes any page on this device.
        // /probe and /alive are automated (other devices), handled by dedicated routes above
        // and never reach this wildcard handler, so no false-wake risk.
        if(xEventGroupGetBits(board->status.sys_evt) & SYS_EVENT_SCREEN_SAVER_TRIGGERED) {
            board->status.ui.last_active_ms = millis();
            xEventGroupClearBits(board->status.sys_evt, SYS_EVENT_SCREEN_SAVER_TRIGGERED);
        }
        rest_common_get_handler(request);
    });
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

        // OTA stall watchdog: runs independently of the upload callback.
        // If the TCP stream stalls (client drops, network error, etc.) the
        // upload callback is never called again, so the in-callback check
        // can never fire. This loop detects the condition externally.
        if (board->status.ota.running && board->status.ota.last_progress_ms != 0) {
            if (millis() - board->status.ota.last_progress_ms > 60*1000UL) {
                LOG_E("[OTA watchdog] upload stalled >60s at %d%%, triggering reboot", board->status.ota.progress);
                reboot_intent_set(REBOOT_INTENT_OTA_STALL,
                                  "upload stalled at %d%% for >60s",
                                  board->status.ota.progress);
                xSemaphoreGive(board->status.reboot_xsem);
            }
        }
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
            reboot_intent_set(REBOOT_INTENT_WIFI_RECONNECT_FAIL,
                              "reached %d/%d retries", retry_cnt, max_retries);
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
        auto click_wrapper = [](void *param){
            board_sal_t *b = static_cast<board_sal_t*>(param);
            // Suppress page switch when screensaver OR find-neighbor is active — both require
            // a single interaction to dismiss the overlay before navigation is permitted.
            bool no_page_switch = (xEventGroupGetBits(b->status.sys_evt) & (SYS_EVENT_SCREEN_SAVER_TRIGGERED | SYS_EVENT_FIND_NEIGHBOR_TRIGGERED)) != 0;
            xEventGroupWaitBits(b->status.init_evt, INIT_EVENT_MINER_READY, pdFALSE, pdTRUE, portMAX_DELAY);// ensure miner is ready before allowing page switch
            xEventGroupClearBits(b->status.sys_evt, SYS_EVENT_MINER_BLOCK_HIT | SYS_EVENT_MINER_HIGH_DIFF_ACHIEVED | SYS_EVENT_SCREEN_SAVER_TRIGGERED | SYS_EVENT_FIND_NEIGHBOR_TRIGGERED);
            b->status.ui.last_active_ms = millis();
            if(no_page_switch) return; // Dismiss overlay only; do not switch page

            ui_switch_next_page_cb();
        };
        auto double_click_wrapper = [](void *param){
            board_sal_t *b = static_cast<board_sal_t*>(param);
            bool no_page_switch = (xEventGroupGetBits(b->status.sys_evt) & (SYS_EVENT_SCREEN_SAVER_TRIGGERED | SYS_EVENT_FIND_NEIGHBOR_TRIGGERED)) != 0;
            xEventGroupWaitBits(b->status.init_evt, INIT_EVENT_MINER_READY, pdFALSE, pdTRUE, portMAX_DELAY);// ensure miner is ready before allowing page switch
            xEventGroupClearBits(b->status.sys_evt, SYS_EVENT_MINER_BLOCK_HIT | SYS_EVENT_MINER_HIGH_DIFF_ACHIEVED | SYS_EVENT_SCREEN_SAVER_TRIGGERED | SYS_EVENT_FIND_NEIGHBOR_TRIGGERED);
            b->status.ui.last_active_ms = millis();
            if(no_page_switch) return; // Dismiss overlay only; do not switch page
            
            ui_switch_prev_page_cb();
        };

        auto long_press_wrapper = [](void *param){
            board_sal_t *b = static_cast<board_sal_t*>(param);
            xSemaphoreGive(b->status.force_config_xsem);
        };

        boot_btn->attachClick(click_wrapper, board); 
        boot_btn->attachDoubleClick(double_click_wrapper, board);
        boot_btn->attachLongPressStart(long_press_wrapper, board);
        boot_btn->attachLongPressStop(NULL);
        boot_btn->attachDuringLongPress(NULL);
    }
    // link the user button functions.
    if(user_btn != nullptr){
        auto click_wrapper = [](void *param){
            board_sal_t *b = static_cast<board_sal_t*>(param);
            // Suppress page switch when screensaver OR find-neighbor is active — both require
            // a single interaction to dismiss the overlay before navigation is permitted.
            bool no_page_switch = (xEventGroupGetBits(b->status.sys_evt) & (SYS_EVENT_SCREEN_SAVER_TRIGGERED | SYS_EVENT_FIND_NEIGHBOR_TRIGGERED)) != 0;
            xEventGroupWaitBits(b->status.init_evt, INIT_EVENT_MINER_READY, pdFALSE, pdTRUE, portMAX_DELAY);// ensure miner is ready before allowing page switch
            xEventGroupClearBits(b->status.sys_evt, SYS_EVENT_MINER_BLOCK_HIT | SYS_EVENT_MINER_HIGH_DIFF_ACHIEVED | SYS_EVENT_SCREEN_SAVER_TRIGGERED | SYS_EVENT_FIND_NEIGHBOR_TRIGGERED);
            b->status.ui.last_active_ms = millis();
        };
        auto double_click_wrapper = [](void *param){
            board_sal_t *b = static_cast<board_sal_t*>(param);
            bool no_page_switch = (xEventGroupGetBits(b->status.sys_evt) & (SYS_EVENT_SCREEN_SAVER_TRIGGERED | SYS_EVENT_FIND_NEIGHBOR_TRIGGERED)) != 0;
            xEventGroupWaitBits(b->status.init_evt, INIT_EVENT_MINER_READY, pdFALSE, pdTRUE, portMAX_DELAY);// ensure miner is ready before allowing page switch
            xEventGroupClearBits(b->status.sys_evt, SYS_EVENT_MINER_BLOCK_HIT | SYS_EVENT_MINER_HIGH_DIFF_ACHIEVED | SYS_EVENT_SCREEN_SAVER_TRIGGERED | SYS_EVENT_FIND_NEIGHBOR_TRIGGERED);
            b->status.ui.last_active_ms = millis();
        };
        
        auto long_press_wrapper = [](void *param){
            board_sal_t *b = static_cast<board_sal_t*>(param);
            xSemaphoreGive(b->status.recover_factory_xsem);
        };

        user_btn->attachClick(click_wrapper, board);
        user_btn->attachDoubleClick(double_click_wrapper, board);
        user_btn->attachLongPressStart(long_press_wrapper, board);
        user_btn->attachLongPressStop(NULL);
        user_btn->attachDuringLongPress(NULL);
    }

    while (true){
        delay(20);
        if(board->status.ota.running) continue;
        if(boot_btn != nullptr) boot_btn->tick();
        if(user_btn != nullptr) user_btn->tick();
    }
}

void swarm_thread_entry(void *args){
    board_sal_t *board = (board_sal_t*)args;
    auto& ctx = board->status.swarm;
    auto& nbr = board->status.neighbor;

    while (true) {
        delay(30 * 1000);

        if(WiFi.status() != WL_CONNECTED) continue; // skip if WiFi is down, will check again in 1s in the next loop iteration
        if(board->status.ota.running)     continue; // skip while OTA is running to avoid resource contention
        if(xEventGroupGetBits(board->status.sys_evt) & SYS_EVENT_SCREEN_SAVER_TRIGGERED) {
            LOG_D("(swarm) screensaver active, skipping swarm probe to yield resources");
            continue; // yield network resources during screensaver
        }
        
        // ── Read current scan generation and alive list (hold lock for minimum time) ──────
        std::vector<String> alive;
        uint32_t cur_gen = 0;
        if (xSemaphoreTake(nbr.mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
            alive   = nbr.alive_ips;
            cur_gen = nbr.scan_generation;
            xSemaphoreGive(nbr.mutex);
        } else {
            LOG_W("(swarm) WARNING: failed to acquire nbr.mutex in 200ms");
        }

        // ── Detect whether a new ICMP scan generation completed ──────────────────────────
        // On generation change: only clear the blacklist (give non-NM devices a chance to be re-evaluated).
        // confirmed_ips and gossip_union are kept across generations to prevent a UI worker-count cliff.
        // A confirmed_ip is removed only after MAX_PROBE_FAIL consecutive probe failures (see below).
        const uint8_t MAX_PROBE_FAIL = 3;
        if (cur_gen != ctx.last_scan_gen) {
            ctx.last_scan_gen = cur_gen;
            if (xSemaphoreTake(ctx.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                ctx.probe_blacklist.clear();
                xSemaphoreGive(ctx.mutex);
            }
            LOG_W("(swarm) new scan gen=%u, blacklist reset; confirmed=%u gossip=%u (kept)",
                  cur_gen, (uint32_t)ctx.confirmed_ips.size(), (uint32_t)ctx.gossip_union.size());
        }

        // ── Build probe targets: local_alive ∪ confirmed_ips ∪ gossip_union ─────────────
        // All three sources participate in probing; identity is determined by the /probe result this round.
        const String selfIP = WiFi.localIP().toString();
        std::vector<String> targets;
        std::set<String>    seen;   // dedupe
        auto add_target = [&](const String& ip) {
            if (ip == selfIP) return;
            if (seen.count(ip)) return;
            bool bl = false;
            if (xSemaphoreTake(ctx.mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                bl = ctx.probe_blacklist.count(ip) > 0;
                xSemaphoreGive(ctx.mutex);
            }
            if (bl) return;
            seen.insert(ip);
            targets.push_back(ip);
        };
        // confirmed_ips first to keep worker count stable
        for (const auto& ip : ctx.confirmed_ips) add_target(ip);
        // IPs discovered by local ICMP scan
        for (const auto& ip : alive)             add_target(ip);
        // supplemental IPs collected via gossip
        for (const auto& ip : ctx.gossip_union)  add_target(ip);

        // ── Probe target list ────────────────────────────────────────────────────────
        uint32_t workers         = 0;
        double   total_hr        = 0.0;
        double   best_session_bd = 0.0;
        double   best_ever_bd    = 0.0;

        LOG_D("(swarm) targets(%u): confirmed=%u alive=%u gossip=%u",
              (uint32_t)targets.size(),
              (uint32_t)ctx.confirmed_ips.size(),
              (uint32_t)alive.size(),
              (uint32_t)ctx.gossip_union.size());

        for (const auto& ip : targets) {
            uint32_t t0 = millis();
            LOG_D("(swarm) >>> probing %s", ip.c_str());

            // Use WiFiClient directly to control connection and reads, avoiding HTTPClient's Stream timedRead()
            // which resets the timer on every received byte, causing "slow response" devices to stall for tens of seconds
            WiFiClient wclient;
            int code = -1;
            String body;
            if (wclient.connect(ip.c_str(), 80, 1000)) {   // 1000ms TCP connect timeout — keeps per-IP total < TWDT 5s
                wclient.print("GET /probe HTTP/1.0\r\nHost: " + ip + "\r\nConnection: close\r\n\r\n");
                String resp;
                resp.reserve(512);
                uint32_t read_dl = millis() + 1500;   // 1.5s hard deadline for reading the full response
                while ((wclient.connected() || wclient.available()) && millis() < read_dl) {
                    while (wclient.available()) {
                        resp += (char)wclient.read();
                        if (resp.length() > 1024) goto probe_read_done;   // response too long, truncate
                    }
                    delay(1);
                }
                probe_read_done:
                wclient.stop();
                // Parse HTTP status code "HTTP/x.x NNN ..."
                int sp = resp.indexOf(' ');
                if (sp >= 0 && resp.startsWith("HTTP/") && (int)resp.length() > sp + 3) {
                    code = resp.substring(sp + 1, sp + 4).toInt();
                }
                // Extract body (after double CRLF)
                int bi = resp.indexOf("\r\n\r\n");
                if (bi >= 0) body = resp.substring(bi + 4);
            }

            LOG_D("(swarm) <<< probe %s => code=%d (%lums)", ip.c_str(), code, (unsigned long)(millis() - t0));

            if (code == 200) {
                StaticJsonDocument<256> doc;
                LOG_D("(swarm) %s: %s", ip.c_str(), body.c_str());
                if (!deserializeJson(doc, body)) {
                    // NM device identification: /probe must have "hr" (hashrate) and "ver" (firmware version)
                    // Other fields are optional per model; only these two are checked, no changes needed here for future extensions
                    bool isNM = doc.containsKey("hr") && doc.containsKey("ver");
                    if (isNM) {
                        workers++;
                        double hr  = doc["hr"].as<double>();
                        double sbd = doc["sbd"].as<double>();
                        double ebd = doc["ebd"].as<double>();
                        // Sanity check: remote /probe may return a torn-read value (double is non-atomic on 32-bit ESP32).
                        // A single NMAxe device cannot exceed 100 TH/s; discard obviously corrupt readings.
                        if (!isfinite(hr) || hr < 0.0 || hr > 1e14) {
                            LOG_W("(swarm) %s INVALID hr=%.0f, skipped (torn read?)", ip.c_str(), hr);
                        } else {
                            total_hr += hr;
                        }
                        if (!isfinite(sbd) || sbd < 0.0) sbd = 0.0;
                        if (!isfinite(ebd) || ebd < 0.0) ebd = 0.0;
                        if (sbd > best_session_bd) best_session_bd = sbd;
                        if (ebd > best_ever_bd)    best_ever_bd    = ebd;
                        ctx.confirmed_ips.insert(ip);
                        ctx.probe_fail_cnt[ip] = 0; // reset on success
                        LOG_D("(swarm) %s NM device hr=%.0f sbd=%.0f ebd=%.0f",
                              ip.c_str(), hr, sbd, ebd);
                    } else {
                        // Non-NM device: blacklist for current generation; if it was confirmed before
                        // (e.g. firmware downgraded / role changed), demote it.
                        if (xSemaphoreTake(ctx.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                            ctx.probe_blacklist.insert(ip);
                            xSemaphoreGive(ctx.mutex);
                        }
                        ctx.confirmed_ips.erase(ip);
                        ctx.probe_fail_cnt.erase(ip);
                        LOG_D("(swarm) %s not NM device, blacklisted", ip.c_str());
                    }
                }
            } else {
                // Connection failed / non-200:
                //   - confirmed NM miner    : ++fail_cnt; remove only after MAX_PROBE_FAIL consecutive failures (≈90s)
                //   - unconfirmed (alive/gossip) : blacklist immediately to avoid repeated timeout waits
                bool is_confirmed = ctx.confirmed_ips.count(ip) > 0;
                if (is_confirmed) {
                    uint8_t &fc = ctx.probe_fail_cnt[ip];
                    if (fc < 0xFF) fc++;
                    if (fc >= MAX_PROBE_FAIL) {
                        ctx.confirmed_ips.erase(ip);
                        ctx.probe_fail_cnt.erase(ip);
                        LOG_W("(swarm) %s removed from confirmed after %u failures", ip.c_str(), MAX_PROBE_FAIL);
                    } else {
                        LOG_D("(swarm) probe %s => %d, fail_cnt=%u (kept)", ip.c_str(), code, fc);
                    }
                } else {
                    if (xSemaphoreTake(ctx.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                        ctx.probe_blacklist.insert(ip);
                        xSemaphoreGive(ctx.mutex);
                    }
                    LOG_D("(swarm) probe %s => %d, blacklisted", ip.c_str(), code);
                }
            }
            delay(50);
        }

        // ── Gossip: ask each confirmed NM peer for its /alive list ──────────────────────
        // IPs seen by a neighbor but not scanned locally are added to gossip_union;
        // they will naturally enter the probe targets next round.
        // They are NOT probed immediately this round to avoid making a single round too long.
        size_t gossip_added = 0;
        for (const auto& ip : ctx.confirmed_ips) {
            if (board->status.ota.running) break;
            if (xEventGroupGetBits(board->status.sys_evt) & SYS_EVENT_SCREEN_SAVER_TRIGGERED) break;

            WiFiClient wclient;
            String body;
            if (!wclient.connect(ip.c_str(), 80, 1000)) continue;
            wclient.print("GET /alive HTTP/1.0\r\nHost: " + ip + "\r\nConnection: close\r\n\r\n");
            String resp;
            resp.reserve(1024);
            uint32_t read_dl = millis() + 1500;
            while ((wclient.connected() || wclient.available()) && millis() < read_dl) {
                while (wclient.available()) {
                    resp += (char)wclient.read();
                    if (resp.length() > 4096) goto gossip_read_done;
                }
                delay(1);
            }
            gossip_read_done:
            wclient.stop();
            int bi = resp.indexOf("\r\n\r\n");
            if (bi < 0) continue;
            String gbody = resp.substring(bi + 4);
            // /alive returns: {"self":"x","ips":["x","y",...]}
            DynamicJsonDocument gdoc(2048);
            if (deserializeJson(gdoc, gbody)) continue;
            JsonArray arr = gdoc["ips"].as<JsonArray>();
            for (JsonVariant v : arr) {
                String x = v.as<String>();
                if (x == selfIP) continue;
                if (ctx.confirmed_ips.count(x)) continue;
                bool bl = false;
                if (xSemaphoreTake(ctx.mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                    bl = ctx.probe_blacklist.count(x) > 0;
                    xSemaphoreGive(ctx.mutex);
                }
                if (bl) continue;
                if (ctx.gossip_union.insert(x).second) gossip_added++;
            }
            delay(50);
        }
        if (gossip_added) {
            LOG_W("(swarm) gossip added %u new IPs from %u peers, union=%u",
                  (uint32_t)gossip_added, (uint32_t)ctx.confirmed_ips.size(), (uint32_t)ctx.gossip_union.size());
        }

        // ── Write aggregated results ──────────────────────────────────────────────────
        // Snapshot self-hashrate once: double is non-atomic on 32-bit ESP32,
        // reading it twice could yield different torn values.
        double self_hr = board->status.miner.hashrate._3m;
        if (!isfinite(self_hr) || self_hr < 0.0 || self_hr > 1e14) {
            LOG_W("(swarm) INVALID self hr=%.0f, using 0 (torn read?)", self_hr);
            self_hr = 0.0;
        }
        // Use confirmed_ips.size() (not workers) for the displayed count: an already-confirmed NM
        // peer that misses a single probe stays in confirmed_ips (kept until MAX_PROBE_FAIL),
        // but is NOT counted in `workers` (only successful probes this round increment it).
        // Reporting `workers` directly causes the UI count to flicker -1 on transient probe misses.
        if (xSemaphoreTake(ctx.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            ctx.total_workers   = (uint32_t)ctx.confirmed_ips.size() + 1;      // include self
            ctx.total_hr        = total_hr + self_hr;                          // include self (slight dip on miss is OK; hr is continuous)
            ctx.best_session_bd = (best_session_bd > board->status.miner.diff.best_session) ? best_session_bd : board->status.miner.diff.best_session;
            ctx.best_ever_bd    = (best_ever_bd > board->status.miner.diff.best_ever) ? best_ever_bd : board->status.miner.diff.best_ever;
            xSemaphoreGive(ctx.mutex);
        }
        LOG_D("(swarm) gen=%u probed=%u(+self) workers=%u hr=%sH/s sbd=%s ebd=%s", 
            ctx.last_scan_gen, (uint32_t)targets.size(), ctx.total_workers, 
            formatNumber(ctx.total_hr, 3).c_str(), 
            formatNumber(ctx.best_session_bd, 3).c_str(), 
            formatNumber(ctx.best_ever_bd, 3).c_str());
    }
}

void alive_ip_scan_thread_entry(void* args) {
    board_sal_t *board = (board_sal_t*)args;

    constexpr uint32_t PING_TIMEOUT_MS = 500; // wait 500ms per IP

    // ICMP packet layout: 8-byte header + 16-byte payload
    struct PingPkt {
        struct icmp_echo_hdr hdr;
        uint8_t              data[32];
    };

    // avoid lwip resorce contention with the main thread during startup; wait until miner is ready before starting the scan
    xEventGroupWaitBits(g_board.status.init_evt, INIT_EVENT_MINER_READY, pdFALSE, pdTRUE, portMAX_DELAY);
    // additional delay to allow the system to stabilize after miner is ready
    delay(2000); 

    // ── ARP pre-resolve: send ARP request and wait for resolution or timeout ───
    // This avoids the lwIP assert(q==NULL) crash by ensuring the ARP entry is
    // either STABLE (resolved) or fully timed-out BEFORE we send the ICMP packet.
    // When sendto() is called with no ARP entry, lwIP creates a PENDING entry and
    // queues the packet. If the ARP table is full, lwIP tries to recycle the oldest
    // PENDING entry but asserts that its packet queue is empty — which it isn't.
    // By pre-resolving ARP, the ICMP sendto() either hits a STABLE entry (no queue)
    // or we skip the IP entirely (no PENDING entry created).
    auto arp_resolve = [](ip4_addr_t *ipaddr, uint32_t timeout_ms) -> bool {
        struct netif *nif = netif_default;
        if (!nif) return false;

        // Send ARP request via tcpip_thread (thread-safe)
        LOCK_TCPIP_CORE();
        etharp_request(nif, ipaddr);
        UNLOCK_TCPIP_CORE();

        // Poll for ARP resolution
        uint32_t t0 = millis();
        while ((millis() - t0) < timeout_ms) {
            struct eth_addr *eth_ret = nullptr;
            const ip4_addr_t *ip_ret = nullptr;
            LOCK_TCPIP_CORE();
            int8_t idx = etharp_find_addr(nif, ipaddr, &eth_ret, &ip_ret);
            UNLOCK_TCPIP_CORE();
            if (idx >= 0) return true;  // ARP resolved → MAC known
            delay(20);
        }
        return false; // ARP timeout → host likely offline
    };

    // ── Ping a single IP; returns true if an ICMP Echo Reply is received ────────
    // sock:      RAW ICMP socket already created and configured with SO_RCVTIMEO
    // o0~o2:     first three octets of the subnet
    // last:      last octet of the target IP
    // seq:       ICMP sequence number (maintained by caller, incremented by 1 each call)
    // returns true  = Echo Reply received (host alive)
    // returns false = timeout / send failure / non-Echo-Reply response
    auto ping_one = [&](int sock, uint8_t o0, uint8_t o1, uint8_t o2, int last, uint16_t seq) -> bool {
        // ── ARP pre-resolution: resolve MAC before sending ICMP ──────────
        // This prevents lwIP from creating PENDING ARP entries with queued
        // packets, which cause assert(q==NULL) crash on ARP table recycle.
        ip4_addr_t target_ip;
        IP4_ADDR(&target_ip, o0, o1, o2, last);
        if (!arp_resolve(&target_ip, PING_TIMEOUT_MS)) {
            LOG_D("(scan) %u.%u.%u.%u ARP timeout, skipping", o0, o1, o2, last);
            return false; // host didn't respond to ARP → definitely offline
        }

        PingPkt pkt = {};
        ICMPH_TYPE_SET(&pkt.hdr, ICMP_ECHO);
        ICMPH_CODE_SET(&pkt.hdr, 0);
        pkt.hdr.id     = htons(0xBEEF);
        pkt.hdr.seqno  = htons(seq);
        pkt.hdr.chksum = inet_chksum(&pkt, sizeof(pkt));

        struct sockaddr_in dest = {};
        dest.sin_family      = AF_INET;
        dest.sin_addr.s_addr = htonl(
            ((uint32_t)o0 << 24) | ((uint32_t)o1 << 16) |
            ((uint32_t)o2 <<  8) |  (uint32_t)last);

        uint32_t t0 = millis();
        if (::sendto(sock, &pkt, sizeof(pkt), 0,
                     (struct sockaddr*)&dest, sizeof(dest)) < 0) {
            LOG_D("(scan) %u.%u.%u.%u sendto failed: errno=%d (%s)", o0, o1, o2, last, errno, strerror(errno));
            return false;
        }

        uint8_t rbuf[64];
        struct sockaddr_in from = {};
        socklen_t fromlen = sizeof(from);
        int n = ::recvfrom(sock, rbuf, sizeof(rbuf), 0,
                           (struct sockaddr*)&from, &fromlen);
        if (n > 0) {
            struct icmp_echo_hdr* reply = (struct icmp_echo_hdr*)(rbuf + 20);
            if (ICMPH_TYPE(reply) == ICMP_ER) {
                LOG_D("(scan) %u.%u.%u.%u alive, ping %lums", o0, o1, o2, last, (unsigned long)(millis() - t0));
                return true;
            }
            LOG_D("(scan) %u.%u.%u.%u unexpected ICMP type=%u code=%u", o0, o1, o2, last, ICMPH_TYPE(reply), ICMPH_CODE(reply));
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                LOG_D("(scan) %u.%u.%u.%u no reply (timeout %ums)", o0, o1, o2, last, PING_TIMEOUT_MS);
            } else {
                LOG_D("(scan) %u.%u.%u.%u recvfrom error: errno=%d (%s)",o0, o1, o2, last, errno, strerror(errno));
            }
        }
        return false;
    };
    
    while (true) {
        delay(1000);
        if(WiFi.status() != WL_CONNECTED) continue; // skip if WiFi is down, will check again in 1s in the next loop iteration
        if(board->status.ota.running)     continue; // skip while OTA is running to avoid resource contention
        if(xEventGroupGetBits(board->status.sys_evt) & SYS_EVENT_SCREEN_SAVER_TRIGGERED) {
            LOG_D("(scan) screensaver active, skipping alive IP scan to yield resources");
            continue; // yield network resources during screensaver
        }

        // Create a single RAW ICMP socket shared for the entire /24 scan
        int sock = ::socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
        if (sock < 0) {
            LOG_E("(scan) raw socket create failed: %d", errno);
             delay(1000*10); // creation failure is usually a permission issue; retry after 10s to avoid spamming the log
            continue;
        }
        struct timeval tv = { 0, (long)(PING_TIMEOUT_MS * 1000L) };
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        uint32_t scan_start = millis();

        IPAddress local = WiFi.localIP();
        uint8_t o0 = local[0], o1 = local[1], o2 = local[2], self_last = local[3];
        // LOG_W("(scan) start ping %u.%u.%u.0/24, generation=%u", o0, o1, o2, ctx->scan_generation);
        LOG_D("(scan) start ping %u.%u.%u.0/24, generation=%u", o0, o1, o2, board->status.neighbor.scan_generation);

        std::vector<String> found;
        found.reserve(16); // pre-allocate to avoid frequent reallocs; actual count is usually far less than 256

        // Mark scan as active
        board->status.neighbor.is_scanning  = true;
        board->status.neighbor.scan_progress = 0;

        uint16_t seq = 0, MAX_SCAN = 254;
        for (int last = 1; last <= MAX_SCAN; last++) {
            if (last == self_last) continue; // skip self

            // Abort scan immediately if OTA starts or screensaver activates mid-scan.
            // Raw ICMP socket + lwIP ARP delays compete directly with TCP transfer.
            if (board->status.ota.running) {
                LOG_D("(scan) OTA started mid-scan, aborting at %u.%u.%u.%u", o0, o1, o2, last);
                break;
            }
            if (xEventGroupGetBits(board->status.sys_evt) & SYS_EVENT_SCREEN_SAVER_TRIGGERED) {
                LOG_D("(scan) screensaver activated mid-scan, aborting at %u.%u.%u.%u", o0, o1, o2, last);
                break;
            }

            // Update scan progress for frontend polling
            board->status.neighbor.scan_progress = (uint16_t)last;

            // Page was refreshed — reset scan progress
            if (xSemaphoreTake(board->status.neighbor.scan_required, 0) == pdTRUE) {
                // Scan interrupt triggered: usually by a page refresh, requiring an immediate reset and full re-scan.
                // NOTE: Do NOT increment scan_generation here. If we did, swarm would immediately clear confirmed_ips
                // while alive_ips is also empty (just cleared), causing a long blind period where swarm can't find
                // any miners. Generation is only incremented once, at scan completion, so swarm always resets against
                // a complete alive list.
                if (xSemaphoreTake(board->status.neighbor.mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
                    board->status.neighbor.alive_ips.clear(); // clear stale IPs immediately so /alive returns fresh data
                    xSemaphoreGive(board->status.neighbor.mutex);
                    LOG_D("(scan) page refresh: reset scan progress, alive_ips cleared (gen unchanged)");
                }
                last = 1;      
                seq  = 0;
                found.clear();
                board->status.neighbor.scan_progress = 0;
            }

            if (ping_one(sock, o0, o1, o2, last, ++seq)) {
                char ip_str[16] = {0,};
                snprintf(ip_str, sizeof(ip_str), "%u.%u.%u.%u", o0, o1, o2, last);
                found.push_back(String(ip_str));
            }

            // Batch-update scan results every 10 IPs to avoid holding the mutex too long and blocking frontend requests
            // Intermediate batches use copy-assignment to keep accumulating in found; the final batch uses move to avoid a deep copy
            if ((seq % 10 == 0) || (last == MAX_SCAN)) {
                bool is_last = (last == MAX_SCAN);
                if (xSemaphoreTake(board->status.neighbor.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    if (is_last) board->status.neighbor.alive_ips = std::move(found);
                    else         board->status.neighbor.alive_ips = found;
                    board->status.neighbor.last_scan_ms = millis();
                    xSemaphoreGive(board->status.neighbor.mutex);
                }
                LOG_D("(scan) progress to %u.%u.%u.%u, found %u alive hosts", o0, o1, o2, last, (unsigned)board->status.neighbor.alive_ips.size());
                // Yield >1000ms after every 10 IPs so lwIP's ARP timer (fires every 1000ms) has time to evict
                // timed-out PENDING ARP entries. 50ms was insufficient — the ARP table (10 slots by default) fills
                // up with PENDING entries for dead hosts, and lwIP asserts(q==NULL) when trying to evict a slot
                // that still holds a queued packet.
                delay(1100); 
            } 
            delay(5); // give lwIP a processing window between pings within a batch
        }
        ::close(sock);

        uint32_t elapsed = (millis() - scan_start) / 1000;
        LOG_D("(scan) done in %lus, %u alive on %u.%u.%u.0/24", (uint32_t)elapsed, (uint16_t)board->status.neighbor.alive_ips.size(), o0, o1, o2);
        // Scan complete: mark done and increment generation
        board->status.neighbor.is_scanning   = false;
        board->status.neighbor.scan_progress = 254;
        if (xSemaphoreTake(board->status.neighbor.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            board->status.neighbor.scan_generation++;
            board->status.neighbor.last_scan_ms = millis();
            xSemaphoreGive(board->status.neighbor.mutex);
        }
        // Wait for the next scan: triggered immediately by a page refresh, or automatically after 5 minutes
        xSemaphoreTake(board->status.neighbor.scan_required, 5 * 60 * 1000);
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
        //update power, wifi, hashrate and ui distribution every second
        if(board->status.miner.uptime_session % 1 == 0){
            board->status.power.vbus       = board->power->get_vbus();
            board->status.power.ibus       = board->power->get_ibus();
            board->status.power.vcore      = board->power->get_vcore();
            board->status.wifi.rssi        = WiFi.RSSI();
            if (board->status.miner.hashrate._3m > 0)
                board->status.miner.efficiency = (board->status.power.vbus * board->status.power.ibus / 1e6) / (board->status.miner.hashrate._3m / 1e12); //J/TH
            //recalculate hashrate every second; drives natural decay to 0 when ASIC is idle
            board->miner->calculate_hashrate(&board->status.miner.hashrate);
            //give miner update signal
            xSemaphoreGive(board->status.miner.update_xsem);

            //sample the hashrate for hashrate distribution chart on ui
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
        
        //status check — completely skipped in benchmark mode
        if(board->status.miner.uptime_session % 2 == 0){
            if(board->status.bm_mode == 0) {
            //avoid restart when ota running
            if(!board->status.ota.running) {
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
                        reboot_intent_set(REBOOT_INTENT_OVERHEAT_VCORE,
                                        "Vcore=%.1fC > limit=%dC for >10s",
                                        board->status.temp.vcore,
                                        board->info.spec.pwr.temp_limit.high);
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
                        reboot_intent_set(REBOOT_INTENT_OVERHEAT_ASIC,
                                        "ASIC=%.1fC > limit=%dC for >10s",
                                        board->status.temp.asic,
                                        board->info.spec.asic.temp_limit.high);
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
                                reboot_intent_set(REBOOT_INTENT_FAN_STALL,
                                                "fan#%d rpm=%d < danger=%d for >20s",
                                                fan.id, fan.rpm,
                                                board->info.spec.fans[fan.id].init.danger_rpm_thr);
                                xSemaphoreGive(board->status.reboot_xsem);
                            }
                        }
                        else fan_err_cnt = 0;
                    }
                }

                //check power status
                static uint16_t pwr_err_cnt = 0;
                if((board->status.power.vbus * board->status.power.ibus / 1000.0 / 1000.0) < board->info.spec.pwr.power_low_threshold){
                    LOG_W("Power %0.1fW is too low...", board->status.power.vbus * board->status.power.ibus / 1000.0 / 1000.0);
                    if(++pwr_err_cnt > 120){//120s
                        LOG_W("Power is too low, restart miner...");
                        reboot_intent_set(REBOOT_INTENT_POWER_LOW,
                                          "input %.1fW < threshold=%.1fW for >120s",
                                          board->status.power.vbus * board->status.power.ibus / 1000.0 / 1000.0,
                                          (double)board->info.spec.pwr.power_low_threshold);
                        xSemaphoreGive(board->status.reboot_xsem);
                    }
                }else pwr_err_cnt = 0;

                //check hashrate
                static uint16_t hr_err_cnt = 0;
                if(board->status.miner.hashrate._3m <= 1){
                    if(++hr_err_cnt > 60){//1min
                        LOG_W("Hashrate is too low, restart miner...");
                        reboot_intent_set(REBOOT_INTENT_LOW_HASHRATE,
                                        "3m hashrate %.2f <= 1 for >60s",
                                        (double)board->status.miner.hashrate._3m);
                        xSemaphoreGive(board->status.reboot_xsem);
                    }
                }else hr_err_cnt = 0;
            } // end !ota.running
            } // end bm_mode == 0 (Normal mode only)
        }

        // auto screen page scrolling
        if((board->status.miner.uptime_session % 10 == 0) && (true == board->status.preference.screen.auto_rolling)){
            ui_switch_next_page_cb();
        }

        //save status to NVS
        if(board->status.miner.uptime_session - last_nvs_save_time > BOARD_NVS_SAVE_INTERVAL){
            xSemaphoreGive(board->status.nvs_save_xsem);
        }
        
        //update miner status history queue
        if(board->status.miner.uptime_session % MINER_HISTORY_SAMPLE_INTERVAL == 0){
            history_node_t node;
            node.hashrate     = (float)(board->status.miner.hashrate._3m / 1e9);  // GH/s
            node.asic_temp    = board->status.temp.asic;
            node.vcore_temp   = board->status.temp.vcore;
            node.pbus         = (board->status.power.vbus * board->status.power.ibus / 1000.0f / 1000.0f); //W
            node.vbus         = (board->status.power.vbus / 1000.0f); //V
            node.ibus         = (board->status.power.ibus / 1000.0f); //A
            node.vcore        = board->status.power.vcore;//mV
            node.fanspeed     = board->status.fan.list[0].speed; //%
            node.fanrpm       = board->status.fan.list[0].rpm;
            node.wifi_rssi    = board->status.wifi.rssi;
            node.free_ram     = ESP.getFreeHeap() / 1024;  //free sram in Kbytes
            node.free_psram   = ESP.getFreePsram() / 1024; //free psram in Kbytes
            node.epoch        = board->status.time.utc * 1000ULL; // Convert UTC seconds to milliseconds
            node.latency      = board->status.miner.latency; // ms

            // add node to history queue and protect concurrent access
            if (xSemaphoreTake(board->status.miner.status_history.mutex, portMAX_DELAY) == pdTRUE) {
                board->status.miner.status_history.deque.push_back(node);
                // Hard cap on element count to prevent PSRAM exhaustion
                while (board->status.miner.status_history.deque.size() > MINER_HISTORY_MAX_SIZE) {
                    board->status.miner.status_history.deque.pop_front();
                }
                //remove old history by time window
                uint64_t current_time_ms = board->status.time.utc * 1000ULL; // Convert to milliseconds
                while (!board->status.miner.status_history.deque.empty()) {
                    uint64_t oldest_time_ms = board->status.miner.status_history.deque.front().epoch; // Already in milliseconds
                    if(current_time_ms - oldest_time_ms > MINER_HISTORY_SAMPLE_DEEPTH){ 
                        board->status.miner.status_history.deque.pop_front();
                        LOG_D("Remove old history, current size: %d, removed timestamp: %llu", board->status.miner.status_history.deque.size(), oldest_time_ms);
                    } else {
                        break;
                    }
                }
                xSemaphoreGive(board->status.miner.status_history.mutex);
            }
        }

        // force config if user long press boot button
        if(xSemaphoreTake(board->status.force_config_xsem, 0) == pdTRUE){
            LOG_W("Force configuration triggered, starting wifi in AP mode when next reboot...");
            nvs_config_set_u8(NVS_CONFIG_FORCE_CONFIG, true);
            reboot_intent_set(REBOOT_INTENT_FORCE_CONFIG,
                              "long-press boot button → AP mode on next boot");
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
    // Mirror uptime / minimum free heap into RTC so a sudden crash still
    // leaves "ran for X seconds, min heap Y" behind for the next boot.
    reboot_log_tick();

    //check ota status and reboot
    if(xSemaphoreTake(board->status.reboot_xsem, 0) == pdTRUE){
        delay(1500); // keep running=true so display can render 100% before reboot
        g_board.status.ota.running  = false;
        delay(300);  // brief pause for any final rendering
        // Fallback: if no caller stamped a real intent, mark the restart as
        // a generic daemon-driven event so the UI doesn't show "unknown_sw".
        reboot_intent_set_if_unset(REBOOT_INTENT_DAEMON_GENERIC,
                                   "reboot_xsem fired without explicit intent");
        ESP.restart();
    }

    //avoid restart when ota running
    if(board->status.ota.running) continue;

    // recover factory if user long press user button
    if(xSemaphoreTake(board->status.recover_factory_xsem, 0) == pdTRUE){
        LOG_W("Factory reset triggered, erasing config and restart...");
        if(erase_all_nvs()){
            reboot_intent_set(REBOOT_INTENT_FACTORY_RESET, "user-triggered factory reset");
            xSemaphoreGive(board->status.reboot_xsem);
        }else{
            LOG_E("Factory reset failed!");
        }
    }

    //WiFi daemon
    if(xSemaphoreTake(board->status.wifi.reconnect_xsem, 0) == pdTRUE){
      WiFi.begin(board->info.connection.wifi.sta.ssid.c_str(), board->info.connection.wifi.sta.pwd.c_str());
    }

    // skip further checks if wifi not connected
    if(board->status.wifi.status != WL_CONNECTED) continue;

    //Stratum daemon
    if(millis() - board->status.miner.stratum_update > MINER_STRATUM_ALIVE_TIMEOUT){
      LOG_W("Stratum connection seems frozen, restarting...");
      reboot_intent_set(REBOOT_INTENT_POOL_TIMEOUT,
                        "no stratum traffic for %lums", (unsigned long)MINER_STRATUM_ALIVE_TIMEOUT);
      xSemaphoreGive(board->status.reboot_xsem);
    }
    //ASIC daemon — skipped in benchmark mode (benchmark_thread handles zero-HR abort itself)
    if(board->status.bm_mode == 0) {
      if(millis() - board->status.miner.asic_update > MINER_ASIC_ALIVE_TIMEOUT){
        LOG_W("ASIC seems frozen, restarting...");
        reboot_intent_set(REBOOT_INTENT_ASIC_FROZEN,
                          "no ASIC reply for %lums", (unsigned long)MINER_ASIC_ALIVE_TIMEOUT);
        xSemaphoreGive(board->status.reboot_xsem);
      }
    }
  }
  LOG_W("Daemon thread exiting...");
  delay(1000);       // Give some time for logging
  vTaskDelete(NULL); // This line is not strictly necessary, but it's good practice to clean up the task when done.
}

void fan_thread_entry(void *args){
    board_sal_t *board = (board_sal_t*)args;

    int16_t now_count[board->info.spec.fans.size()] = {0}, last_count[board->info.spec.fans.size()] = {0}, temp_cnt = 0;
    uint32_t start_ms[board->info.spec.fans.size()] = {0};
    delay(100);

    // Initialize TMP102 temperature sensor
    tmp102_init();

    // TMP102 self-test: retry until both sensors pass (3-sample average each attempt)
    while(true){
        float vcore_sum = 0, asic_sum = 0;
        int   vcore_cnt = 0, asic_cnt = 0;
        for(int i = 0; i < 3; i++){
            float t;
            t = get_vcore_temperature();
            if(!isnan(t)){ vcore_sum += t; vcore_cnt++; }
            t = get_asic_temperature();
            if(!isnan(t)){ asic_sum += t; asic_cnt++; }
            delay(50);
        }
        float vcore_avg = (vcore_cnt > 0) ? vcore_sum / vcore_cnt : NAN;
        float asic_avg  = (asic_cnt  > 0) ? asic_sum  / asic_cnt  : NAN;
        bool vcore_ok = !isnan(vcore_avg);
        bool asic_ok  = !isnan(asic_avg);
        if(vcore_ok) LOG_W("TMP102 VRM : OK %.1fC",  vcore_avg);
        else         LOG_W("TMP102 VRM : FAIL (no response)");
        if(asic_ok)  LOG_W("TMP102 ASIC: OK %.1fC",  asic_avg);
        else         LOG_W("TMP102 ASIC: FAIL (no response)");
        if(vcore_ok && asic_ok) break;
        LOG_W("TMP102 self test failed, retrying...");
        delay(500);
    }
    xEventGroupSetBits(board->status.init_evt, INIT_EVENT_TMP_READY);

    //fan init
    for(auto &fan : board->info.spec.fans){
        // fan initialize with defined parameters
        fan_drv_init(fan.init);
        LOG_D("Fan[%d] initialized with torch pin %d, pwm pin %d", fan.id, fan.init.torch.pulse_gpio_num, fan.init.pwm.pin);
    }

    // Local helper types for parallel fan init — local structs + captureless lambdas decay to TaskFunction_t
    struct PolarityArg {
        fan_init_t        init_param;   // copied — each fan owns independent LEDC ch + PCNT unit
        bool             *out_polarity;
        SemaphoreHandle_t done;
    };
    struct SelfTestArg {
        fan_init_t        init_param;
        bool              fan_invert;
        fan_status_t     *fan_status;
        uint16_t          rpm_thr;
        SemaphoreHandle_t done;
    };

    // polarity detection — all fans probed in parallel
    {
        auto polarity_task = [](void *pv) {
            auto *a = static_cast<PolarityArg*>(pv);
            *a->out_polarity = guess_fan_polarity(a->init_param);
            xSemaphoreGive(a->done);
            vTaskDelete(NULL);
        };
        size_t n = board->info.spec.fans.size();
        SemaphoreHandle_t pol_done = xSemaphoreCreateCounting(n, 0);
        std::vector<PolarityArg> pol_args(n);
        size_t i = 0;
        for (auto &fan : board->info.spec.fans) {
            pol_args[i].init_param   = fan.init;
            pol_args[i].out_polarity = &fan.polarity;
            pol_args[i].done         = pol_done;
            xTaskCreate(polarity_task, "fan_pol", 4096, &pol_args[i], uxTaskPriorityGet(NULL), NULL);
            i++;
        }
        for (size_t j = 0; j < n; j++) xSemaphoreTake(pol_done, portMAX_DELAY);
        vSemaphoreDelete(pol_done);
        for (auto &fan : board->info.spec.fans)
            LOG_I("Guess fan[%d] polarity :[%s]", fan.id, fan.polarity ? "inverted" : "normal");
    }
    xEventGroupSetBits(board->status.init_evt, INIT_EVENT_FAN_POLARITY_DETECT);

    // Helper function to find fan config by id
    auto get_fan_config = [&](uint8_t fan_id) -> fan_config_t* {
        for(auto &fan_cfg : board->info.spec.fans){
            if(fan_cfg.id == fan_id){
                return &fan_cfg;
            }
        }
        return nullptr;
    };

    auto get_fan_status = [&](uint8_t fan_id) -> fan_status_t* {
        for(auto &fan_status : board->status.fan.list){
            if(fan_status.id == fan_id){
                return &fan_status;
            }
        }
        return nullptr;
    };

    // fan self test — all fans tested in parallel
    while(true){
        auto selftest_task = [](void *pv) {
            auto *a = static_cast<SelfTestArg*>(pv);
            uint16_t rpm = 0;
            for (uint8_t i = 0; i < 3; i++)
                rpm = measure_fan_rpm_for_duration(a->init_param, 1.0f, 1000, a->fan_invert);
            a->fan_status->rpm       = rpm;
            a->fan_status->self_test = (rpm > a->rpm_thr);
            LOG_W("Fan[%d] self test result: %s, measured rpm: %d, threshold rpm: %d",
                  a->fan_status->id, a->fan_status->self_test ? "OK" : "FAIL", rpm, a->rpm_thr);
            xSemaphoreGive(a->done);
            vTaskDelete(NULL);
        };
        size_t n = board->status.fan.list.size();
        SemaphoreHandle_t st_done = xSemaphoreCreateCounting(n, 0);
        std::vector<SelfTestArg> st_args(n);
        size_t i = 0;
        for (auto &fan : board->status.fan.list) {
            fan_config_t* fan_cfg = get_fan_config(fan.id);
            if (fan_cfg == nullptr) { xSemaphoreGive(st_done); i++; continue; }
            st_args[i].init_param = fan_cfg->init;
            st_args[i].fan_invert = fan_cfg->polarity;
            st_args[i].fan_status = &fan;
            st_args[i].rpm_thr    = fan_cfg->init.self_test_rpm_thr;
            st_args[i].done       = st_done;
            xTaskCreate(selftest_task, "fan_st", 4096, &st_args[i], uxTaskPriorityGet(NULL), NULL);
            i++;
        }
        for (size_t j = 0; j < n; j++) xSemaphoreTake(st_done, portMAX_DELAY);
        vSemaphoreDelete(st_done);

        bool all_fan_ok = true;
        for (auto &fan : board->status.fan.list) all_fan_ok = all_fan_ok && fan.self_test;

        if(all_fan_ok) {
            LOG_W("All fans passed self test.");
            break;
        }
        else LOG_W("Some fans failed self test, retrying...");
    }
    xEventGroupSetBits(board->status.init_evt, INIT_EVENT_FAN_READY);

    // fan control loop
    while(true){
        uint8_t fan_id = 0; // default to fan 0 for event waiting; can be extended to support multiple fans with separate events if needed
        EventBits_t bits = xEventGroupWaitBits(board->status.sys_evt, SYS_EVENT_MINER_VCORE_TEMP_UPDATE | SYS_EVENT_MINER_ASIC_TEMP_UPDATE, pdFALSE, pdFALSE, portMAX_DELAY);
        if((bits & SYS_EVENT_MINER_ASIC_TEMP_UPDATE) == SYS_EVENT_MINER_ASIC_TEMP_UPDATE){
            fan_id = 0; // for example, fan 1 responds to ASIC temp updates
            xEventGroupClearBits(board->status.sys_evt, SYS_EVENT_MINER_ASIC_TEMP_UPDATE);
        }
        if((bits & SYS_EVENT_MINER_VCORE_TEMP_UPDATE) == SYS_EVENT_MINER_VCORE_TEMP_UPDATE){
            fan_id = 1; // for example, fan 0 responds to ASIC temp updates
            xEventGroupClearBits(board->status.sys_evt, SYS_EVENT_MINER_VCORE_TEMP_UPDATE);
        }

        fan_config_t* fan_cfg    = get_fan_config(fan_id);
        fan_status_t* fan_status = get_fan_status(fan_id);
        if(fan_cfg == nullptr || fan_status == nullptr) continue; // skip if fan config or status not found

        bool fan_invert = fan_cfg->polarity;  // find fan polarity by id from config
        fan_init_t init_param = fan_cfg->init;// find fan init config by id from config
        // Calculate fan RPM
        if(millis() - start_ms[fan_id] >= 1000){
            uint32_t delta_time = millis() - start_ms[fan_id];
            pcnt_get_counter_value(init_param.torch.unit, &now_count[fan_id]);
            uint16_t delta_pcnt = 0;
            if (now_count[fan_id] < last_count[fan_id]) delta_pcnt = (init_param.torch.counter_h_lim - last_count[fan_id]) + now_count[fan_id];
            else delta_pcnt = now_count[fan_id] - last_count[fan_id];
            fan_status->rpm = calculate_rpm(delta_pcnt, delta_time / 1000.0);
            last_count[fan_id] = now_count[fan_id];
            start_ms[fan_id] = millis();
        }

        // Adjust fan speed     
        if(fan_cfg->auto_speed && fan_status->self_test){
            static uint32_t pid_start[2] = {0};
            float dt = (millis() - pid_start[fan_id]) / 1000.0f; // Convert to seconds

            float target   = fan_cfg->target_temp;
            float measured = (fan_id == 0) ? board->status.temp.asic : board->status.temp.vcore; // fan 0 measures ASIC temp, fan 1 measures Vcore temp
            
            fan_status->speed = (uint16_t)pid_compute(&fan_cfg->pid, target, measured, dt);
            pid_start[fan_id] = millis();
        }
        fan_set_speed(init_param, fan_status->speed / 100.0, fan_invert);
        
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
    const uint32_t MARKET_RETRY_DELAY_MS = 1000*10;

    while(true){
        delay(1000);
        // Skip market update if OTA is running to avoid potential instability during critical updates.
        if(board->status.ota.running) {
            LOG_D("Market update skipped: OTA in progress.");
            continue;
        }
        // Skip market update during screensaver to reduce network activity.
        if(xEventGroupGetBits(board->status.sys_evt) & SYS_EVENT_SCREEN_SAVER_TRIGGERED) {
            LOG_D("Market update skipped: screensaver active.");
            continue;
        }

        // Only attempt to fetch market data if WiFi is connected. If not, log and skip this cycle.
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

            // Fetch watchlist pairs, then sort by price descending for display
            board->market->refresh_watchlist(board->info.base.coin_watchlist);
            board->market->sort_watchlist_by_price();
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
            board->miner->reset_hashrate();           // clear stale samples
            board->status.miner.hashrate = {0.0, 0.0, 0.0};
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
            if(!board->miner->mining(&board->miner->pool_job_now)){
                delay(10); // avoid tight spin loop triggering interrupt WDT when mining() fails
                continue;
            }
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

                //submit solution — fetch job context first so dedup can run before hashrate is counted
                uint32_t version_submit = version ^ (*(uint32_t*)job.version);
                String   pool_id_submit = board->miner->get_pool_job_id_by_asic_job_id(result.asic.job_id);
                String   extra2_submit  = board->miner->get_extranonce2_by_asic_job_id(result.asic.job_id);
                if(pool_id_submit.length() == 0 || extra2_submit.length() == 0) continue; // slot evicted/cleared

                // Deduplicate nonces within the same (pool_job_id, extranonce2) scope.
                // BM1370 at high frequency occasionally emits the same result frame twice;
                // both pass local SHA256 but the second submission is rejected by the pool as Duplicate.
                // Must run BEFORE record_nonce() so duplicates are not counted toward hashrate.
                static std::unordered_set<uint32_t> _submitted_nonces;
                static String _dedup_job_key = "";
                String _cur_job_key = pool_id_submit + extra2_submit;
                if(_cur_job_key != _dedup_job_key){
                    _submitted_nonces.clear();
                    _dedup_job_key = _cur_job_key;
                }
                if(_submitted_nonces.count(result.asic.nonce)){
                    LOG_W("Dup nonce 0x%08x skipped (pool_job=%s)", result.asic.nonce, pool_id_submit.c_str());
                    continue;
                }
                _submitted_nonces.insert(result.asic.nonce);

                //record nonce into hashrate ring; calculation is driven by monitor thread every second
                board->miner->record_nonce();

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

                bool res = board->miner->submit_job_share(pool_id_submit, extra2_submit, result.asic.nonce, *(uint32_t*)job.ntime, version_submit);
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
                    xEventGroupSetBits(board->status.sys_evt, SYS_EVENT_MINER_BLOCK_HIT);
                }

                //update miner status
                board->status.miner.diff.last            = diff;
                board->status.miner.diff.best_session    = (diff > board->status.miner.diff.best_session) ? diff : board->status.miner.diff.best_session;
                board->status.miner.diff.best_ever       = (diff > board->status.miner.diff.best_ever) ? diff : board->status.miner.diff.best_ever;

                // update best ever diff to NVS if best ever diff updated, avoid write to NVS too frequently
                // notify ui 
                if(diff == board->status.miner.diff.best_ever){
                    xSemaphoreGive(board->status.nvs_save_xsem);
                    if(diff > 100.0f*1000.0f*1000.0f){ // only trigger high diff event when diff is larger than 100M
                        xEventGroupSetBits(board->status.sys_evt, SYS_EVENT_MINER_HIGH_DIFF_ACHIEVED);
                    }
                }
                
                //add share to History of block proximity
                if(xSemaphoreTake(board->status.miner.proximity_history.mutex, portMAX_DELAY) == pdTRUE){
                    proximity_node_t node;
                    node.block_proximity = diff / board->status.miner.diff.network;
                    node.share_diff      = diff;
                    node.net_diff        = board->status.miner.diff.network;
                    node.epoch           = board->status.time.utc * 1000ULL;
                    add_share_diff_history(board->status.miner.proximity_history.deque, node, 36);
                    xSemaphoreGive(board->status.miner.proximity_history.mutex);
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

    // Parse a Stratum JSON-RPC error response into a human-readable string.
    // Stratum error array format: [code, message, traceback]
    // Standard error codes (de-facto industry standard, originated from Slush Pool):
    //   20 = Other/Unknown, 21 = Stale share, 22 = Duplicate share,
    //   23 = Low difficulty,  24 = Unauthorized worker, 25 = Not subscribed
    auto parse_stratum_error = [](const String& raw) -> String {
        StaticJsonDocument<256> err_doc;
        if (deserializeJson(err_doc, raw) != DeserializationError::Ok) return "Unknown error";
        if (!err_doc.containsKey("error") || err_doc["error"].isNull())  return "Unknown error";
        int code = err_doc["error"][0] | 0;
        switch (code) {
            case 20: return "Other/Unknown";
            case 21: return "Stale share";
            case 22: return "Duplicate share";
            case 23: return "Low difficulty";
            case 24: return "Unauthorized worker";
            case 25: return "Not subscribed";
            default: return String("Error code ") + code;
        }
    };

    while(true){
        static int w_retry = 0, w_maxRetries = 24;
        if(board->status.wifi.status != WL_CONNECTED){
            w_retry++;
            LOG_W("WiFi reconnecting %d/%d...", w_retry, w_maxRetries);
            if(w_retry >= w_maxRetries) {
                reboot_intent_set(REBOOT_INTENT_WIFI_RECONNECT_FAIL,
                                  "stratum loop: %d/%d retries exhausted",
                                  w_retry, w_maxRetries);
                ESP.restart();
            }

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
                LOG_W("Primary pool [%s] is not available, keep using fallback pool [%s]...", board->info.connection.pool.primary.url.c_str(), board->info.connection.pool.fallback.url.c_str());
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
                    board->status.miner.stratum_update = millis();//pool alive timestamp (server-pushed)
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
                    board->status.miner.stratum_update = millis();//pool alive timestamp (server-pushed)
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
                        board->status.miner.stratum_update = millis();//pool alive timestamp (server-pushed)
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
                            board->status.miner.stratum_update = millis();//pool alive timestamp
                            if (rsp.status == true){
                                board->status.miner.share_accepted++;
                                LOG_L("#%d share accepted, %ldms", board->status.miner.share_accepted + board->status.miner.share_rejected, board->status.miner.latency);      
                            }
                            else {
                                board->status.miner.share_rejected++;
                                String err_msg = parse_stratum_error(method.raw);
                                LOG_E("#%d share rejected, %ldms, %s", board->status.miner.share_accepted + board->status.miner.share_rejected, board->status.miner.latency, err_msg.c_str());
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
                    // Pool replied with an error (e.g. share rejected, auth failed). The TCP
                    // connection is clearly alive — refresh watchdog so we don't wrongly
                    // reboot when a window is full of stale/low-diff rejections.
                    board->status.miner.stratum_update = millis();
                    if(method.id != -1){
                        board->stratum->set_msg_rsp_map(method.id, true);
                        stratum_rsp rsp = board->stratum->get_method_rsp_by_id(method.id);
                        if(rsp.method == "mining.submit"){
                            board->status.miner.latency = millis() - rsp.stamp;
                            board->status.miner.share_rejected++;
                            String err_msg = parse_stratum_error(method.raw);
                            LOG_E("#%d share rejected, %ldms, %s", board->status.miner.share_accepted + board->status.miner.share_rejected, board->status.miner.latency, err_msg.c_str());
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
  uint16_t tick_interval = 1;

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
      lv_timer_handler(); /* let the GUI do its work */
      xSemaphoreGive(board->status.ui.lvgl.drv_xMutex); 
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
        {UI_PAGE_SETTING_SWARM,   ui_setting_or_swarm_page_update}   // NMAxe and Gamma is swarm page, NMQAxe++ is setting page
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
            // screen saver update must ALWAYS run regardless of event state:
            // it is responsible for both detecting the inactivity timeout (setting the event)
            // and managing the active overlay / fade-out once the event is set.
            ui_screen_saver_page_update((void*)board);
            // find-me overlay: must run alongside screen-saver so it works even
            // when the screensaver is active (device hidden behind another overlay).
            ui_find_me_page_update((void*)board);

            // When screen saver overlay is active skip all underlying page updates so the
            // mutex is released quickly and lvgl_tick_thread_entry can advance GIF frames.
            if((xEventGroupGetBits(board->status.sys_evt) & SYS_EVENT_SCREEN_SAVER_TRIGGERED) == 0){
                // find the update function based on current page and call it, if not found, just skip page update
                auto it = ui_page_update_cbs.find(board->status.ui.page.current);
                if(it != ui_page_update_cbs.end()){
                    it->second((void*)board);  
                }
                // countdown page update, if running, cover current page
                ui_countdown_page_update((void*)board);
                // achievement page update, if running, cover current page
                ui_achieve_page_update((void*)board);
                // block hits page popup, if hit, cover current page
                ui_hits_page_update((void*)board);
                // OTA page update, if running, cover current page
                ui_ota_page_update((void*)board);
                // benchmark mode overlay — covers active page during sweep
                ui_benchmark_overlay_update((void*)board);
            }
            // Power OC alert — highest priority overlay, runs even during screen saver
            ui_power_oc_alert_update((void*)board);
            //release mutex
            xSemaphoreGive(board->status.ui.lvgl.drv_xMutex); 
        }
    }
}

void display_thread_entry(void *args){
  board_sal_t *board = (board_sal_t*)args;

  // Use const char* arrays instead of String to avoid 44 heap allocations at task startup.
  // String objects on the stack (12 bytes each × 44 = 528 bytes) plus their heap-allocated
  // contents caused internal-heap corruption on boards with tighter memory layout.
  static const char* vbus_chk_str[]         = {"Vbus check   ","Vbus check.  ","Vbus check.. ","Vbus check..."};
  static const char* vcore_chk_str[]        = {"Vcore check   ","Vcore check.  ","Vcore check.. ","Vcore check..."};
  static const char* asci_init_str[]        = {"ASIC init  ","ASIC init.  ","ASIC init.. ","ASIC init..."};
  static const char* wifi_con_str[]         = {"Wifi connect   ","Wifi connect.  ","Wifi connect.. ","Wifi connect..."};
  static const char* fan_polarity_str[]     = {"Fan polarity check   ","Fan polarity check.  ","Fan polarity check.. ","Fan polarity check..."};
  static const char* fan_self_test_str[]    = {"Fan test   ","Fan test.  ","Fan test.. ","Fan test..."};
  static const char* tmp_chk_str[]          = {"temp sensor check   ","temp sensor check.  ","temp sensor check.. ","temp sensor check..."};
  static const char* market_con_str[]       = {"Market connect   ","Market connect.  ","Market connect.. ","Market connect..."};
  static const char* pool_con_str[]         = {"Pool connect   ","Pool connect.  ","Pool connect.. ","Pool connect..."};
  static const char* pool_auth_str[]        = {"Pool auth   ","Pool auth.  ","Pool auth.. ","Pool auth..."};
  static const char* wait_job_str[]         = {"Waiting pool job   ","Waiting pool job.  ","Waiting pool job.. ","Waiting pool job..."};
  static const char* config_str[]           = {"Config   ","Config.  ","Config.. ","Config..."};

  // tft hardware init
  tft_init(board);
  // touch hardware init, even if touch not detected, still need to init to avoid some screen with touch panel have no display if touch init fail.
  touch_init(board);
  // notify screen ready
  xEventGroupSetBits(board->status.init_evt, INIT_EVENT_SCREEN_READY);  
  // wait lvgl and ui thread ready
  xEventGroupWaitBits(board->status.init_evt, INIT_EVENT_UI_READY | INIT_EVENT_LVGL_READY, pdFALSE, pdTRUE, portMAX_DELAY);

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
  /******************************************wait TMP102 self test ***************************************/
  cnt = 0;
  board->status.ui.page.loading.percent = 0.5;
  // Blocks here until both TMP102 sensors pass (fan_thread retries until all good)
  // Realtime temps shown inline, e.g. "temp sensor check.  43.2C/34.1C"
  while(true){
    float t_vrm  = get_vcore_temperature();
    float t_asic = get_asic_temperature();
    String vrm_str  = isnan(t_vrm)  ? "NAN" : (String(t_vrm,  1) + "C");
    String asic_str = isnan(t_asic) ? "NAN" : (String(t_asic, 1) + "C");
    board->status.ui.page.loading.details.color = 0xFFFFFF;
    board->status.ui.page.loading.details.msg   = String(tmp_chk_str[cnt++ % 4]) + " " + vrm_str + "/" + asic_str;
    if((xEventGroupWaitBits(board->status.init_evt, INIT_EVENT_TMP_READY, pdFALSE, pdTRUE, 100) & INIT_EVENT_TMP_READY) == INIT_EVENT_TMP_READY) break;
  }
  // TMP_READY means both sensors passed — show final green confirmation
  {
    float t_vrm  = get_vcore_temperature();
    float t_asic = get_asic_temperature();
    String vrm_str  = isnan(t_vrm)  ? "NAN" : (String(t_vrm,  1) + "C");
    String asic_str = isnan(t_asic) ? "NAN" : (String(t_asic, 1) + "C");
    board->status.ui.page.loading.details.color = 0x00FF00;
    board->status.ui.page.loading.details.msg   = String("Temp Pass  ") + vrm_str + "/" + asic_str;
    delay(700);
  }
 /*********************************wait fan porality detect and fan self test *********************************/
  cnt = 0;
  board->status.ui.page.loading.percent = 0.5;
  for(uint8_t i = 0; i < board->info.spec.fans.size(); i++){
    while(true){
      board->status.ui.page.loading.details.color = 0xFFFFFF;
      board->status.ui.page.loading.details.msg   = String(fan_polarity_str[cnt++ % 4]);
      if((xEventGroupWaitBits(board->status.init_evt, INIT_EVENT_FAN_POLARITY_DETECT, pdFALSE, pdTRUE, 100) & INIT_EVENT_FAN_POLARITY_DETECT)  == INIT_EVENT_FAN_POLARITY_DETECT) break;
    }
    String polarity_str = (board->info.spec.fans[i].polarity) ? " Inverted" : " Normal";
    board->status.ui.page.loading.details.color = 0x00FF00;
    board->status.ui.page.loading.details.msg   = String("Fan ") + ((board->info.spec.fans.size() > 1) ? String(i + 1) : " ") + polarity_str;
    delay(1000);
  }
  xEventGroupWaitBits(g_board.status.init_evt, INIT_EVENT_FAN_POLARITY_DETECT, pdFALSE, pdTRUE, portMAX_DELAY);
  /********************************************wait fan self test ****************************************/
  cnt = 0;
  board->status.ui.page.loading.percent = 0.5;
  for(uint8_t i = 0; i < board->info.spec.fans.size(); i++){
    while(true){
      board->status.ui.page.loading.details.color = 0xFFFFFF;
      board->status.ui.page.loading.details.msg   = String(fan_self_test_str[cnt++ % 4]) + String(board->status.fan.list[i].rpm) + "/ " + String(board->info.spec.fans[i].init.self_test_rpm_thr) + "rpm";
      if((xEventGroupWaitBits(board->status.init_evt, INIT_EVENT_FAN_READY, pdFALSE, pdTRUE, 100) & INIT_EVENT_FAN_READY)  == INIT_EVENT_FAN_READY) break;
    }
    board->status.ui.page.loading.details.color = 0x00FF00;
    board->status.ui.page.loading.details.msg   = "Fan" + ((board->info.spec.fans.size() > 1) ? String(i + 1) : "") + " Pass! [" + String(board->status.fan.list[i].rpm) + "/ " + String(board->info.spec.fans[i].init.self_test_rpm_thr) + " rpm]";
    delay(1000);
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
  /****************************************wait for pool connected**************************************/
  cnt = 0;
  board->status.ui.page.loading.percent = 0.75;
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
  board->status.ui.page.loading.percent = 0.85;
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

void aphorism_thread_entry(void *args){
    board_sal_t *board = (board_sal_t*)args;
    auto &aphorism = board->status.aphorism;
    auto &pool     = aphorism.pool; // producer appends here; consumer pops from front; pool is never cleared wholesale

    // Theme: solo mining requires patience — be a friend of time, good luck can arrive at any moment
    static const char* const KEYWORDS[] = {
        // // Patience & Waiting
        // "patience", "patient", "wait", "waiting", "endure", "endurance", "persist",
        // "persevere", "perseverance", "hold", "sustain", "steady", "calm", "breathe",
        // Persistence & Effort
        "keep", "continue", "never", "quit", "grind", "work", "effort", "dedication",
        "commitment", "resilience", "fortitude", "tenacity", "grit", "strength",
        "courage", "discipline", "focus", "purpose",
        // Time & Opportunity
        "time", "moment", "soon", "next", "tomorrow", "chance", "opportunity",
        "happen", "closer", "near", "almost", "sudden", "overnight", "surprise",
        // Luck & Miracles
        "luck", "lucky", "fortune", "miracle", "destiny", "fate", "blessing",
        "unexpected", "breakthrough",
        // Hope & Faith
        "hope", "faith", "believe", "trust", "wish", "dream", "vision", "goal",
        // Victory & Reward
        "reward", "win", "success", "achieve", "triumph", "glory", "victory",
        "celebrate", "worthy", "prize",
        // Bitcoin & Crypto Mining
        "bitcoin", "btc", "crypto", "cryptocurrency", "blockchain", "satoshi",
        "halving", "hash", "block", "miner", "hashrate", "nonce", "coinbase",
        "hodl", "hodler", "solo", "difficulty", "proof", "decentralized",
        "freedom", "scarce", "digital", "gold", "immutable", "trustless",
        "accumulate", "stack"
    };
    static const uint8_t  KEYWORD_COUNT  = sizeof(KEYWORDS) / sizeof(KEYWORDS[0]);
    static const uint16_t MAX_CHARS        = 80;           // max quote length in chars (adjust to change filter threshold)
    static const uint8_t  POOL_MAX         = 30;           // producer stops appending when pool reaches this size
    static const uint8_t  POOL_LOW_THR     = (uint8_t)(POOL_MAX * 0.3f); // refetch when pool drops below 30% (≤ 15 quotes)
    static const uint32_t POOL_POLL_MS     = 1000 * 60;    // interval between pool-size checks when pool is healthy
    static const uint8_t  BATCHES          = 2;            // 1 batch per round; ~50 quotes per fetch
    static const uint32_t BATCH_DELAY_MS   = 1000 * 30;    // short inter-batch pause (http already closed before this delay)
    // Max random jitter applied at startup and between rounds.
    // Multiple LAN devices share the same public IP and the same 100 req/hr limit (zenquotes.io).
    // Spreading requests across this window prevents simultaneous 429s.
    static const uint32_t STARTUP_JITTER_MS = 1000UL * 60 * 5; // up to 5 minutes random startup offset

    // Random startup jitter: stagger initial fetch across all LAN devices sharing the same public IP
    uint32_t init_jitter_ms = esp_random() % STARTUP_JITTER_MS;
    LOG_W("[Aphorism] Startup jitter: ~%lu s (desync LAN devices)", init_jitter_ms / 1000);
    delay(init_jitter_ms);

    // Print current config for debugging
    LOG_W("[Aphorism] Config => MAX_CHARS:%d  POOL_MAX:%d  POOL_LOW_THR:%d  BATCHES:%d  KEYWORDS:%d",
          (int)MAX_CHARS, (int)POOL_MAX, (int)POOL_LOW_THR, (int)BATCHES, (int)KEYWORD_COUNT);

    uint32_t batch_no = 0;
    while(true) {
        delay(10); 
        if(WiFi.status() != WL_CONNECTED) continue; // skip if WiFi is down, will check again in 1s in the next loop iteration
        if(board->status.ota.running)     continue; // skip while OTA is running to avoid resource contention
        
        // Check current pool capacity before deciding whether to fetch
        xSemaphoreTake(aphorism.mutex, portMAX_DELAY);
        size_t cur_pool_size = pool.size();
        xSemaphoreGive(aphorism.mutex);

        // Pool is above the 30% low-water mark — keep waiting, no fetch needed yet
        if(cur_pool_size >= POOL_LOW_THR) {
            delay(POOL_POLL_MS);
            continue;
        }

        // Pool is low — start a new fetch round
        batch_no++;
        LOG_I("[Aphorism] Round %lu: pool low (%d quotes), fetching...", batch_no, (int)cur_pool_size);

        // Build dedup set from current pool to avoid re-appending already-queued quotes
        std::set<String> seen;
        xSemaphoreTake(aphorism.mutex, portMAX_DELAY);
        for(auto &qt : pool) seen.insert(qt.quote);
        xSemaphoreGive(aphorism.mutex);

        // zenquotes.io HTTPS batch API — returns ~50 quotes per call, fetch BATCHES rounds
        for(uint8_t b = 0; b < BATCHES; b++) {
            { // scope: HTTPClient & WiFiClientSecure are destroyed HERE before inter-batch delay, freeing TLS RAM immediately
            HTTPClient http;
            WiFiClientSecure client;
            client.setInsecure(); // skip certificate verification to save CA storage overhead
            http.begin(client, "https://zenquotes.io/api/quotes");
            http.setTimeout(30000);
            http.setConnectTimeout(15000);
            http.addHeader("Connection", "close");

            int code = http.GET();
            if(code == HTTP_CODE_OK) {
                String body = http.getString();
                http.end();

                // skip if server returns a non-array response (e.g. rate-limit error object)
                if(!body.startsWith("[")) {
                    LOG_E("[Aphorism] Response is not a JSON array, skipping. Content: %.100s", body.c_str());
                    delay(BATCH_DELAY_MS);
                    continue;
                }

                // Use PSRAM-allocated JsonDocument to parse the array
                // Each entry includes an h(HTML) field; 50 entries are ~25-35 KB, allocate 64 KB to be safe
                BasicJsonDocument<PsramJsonAllocator> doc(1024 * 64);
                DeserializationError err = deserializeJson(doc, body);
                if(!err) {
                    int cnt_total = 0, cnt_too_long = 0, cnt_no_kw = 0, cnt_hit = 0;
                    for(JsonObject item : doc.as<JsonArray>()) {
                        String q = item["q"].as<String>();
                        String a = item["a"].as<String>();
                        if(q.isEmpty() || seen.count(q)) continue;
                        seen.insert(q);
                        cnt_total++;
                        if((uint16_t)q.length() > MAX_CHARS) { cnt_too_long++; continue; }
                        String ql = q;
                        ql.toLowerCase();
                        bool matched = false;
                        String matched_kw;
                        for(uint8_t k = 0; k < KEYWORD_COUNT && !matched; k++) {
                            if(ql.indexOf(KEYWORDS[k]) >= 0) { matched = true; matched_kw = KEYWORDS[k]; }
                        }
                        if(matched) {
                            xSemaphoreTake(aphorism.mutex, portMAX_DELAY);
                            if(pool.size() < POOL_MAX) {
                                pool.push_back({q, a, matched_kw});
                                cnt_hit++;
                            }
                            xSemaphoreGive(aphorism.mutex);
                        }
                        else cnt_no_kw++;
                    }
                    LOG_I("[Aphorism] Batch %d: total=%d too_long=%d no_kw=%d hit=%d",
                          (int)b+1, cnt_total, cnt_too_long, cnt_no_kw, cnt_hit);
                } else {
                    LOG_E("[Aphorism] JSON parse error: %s | body: %.100s", err.c_str(), body.c_str());
                }
            } else {
                String body = http.getString();
                http.end();
                LOG_E("[Aphorism] HTTP error: %d | body: %.100s", code, body.c_str());
            }
            } // end HTTP scope: HTTPClient & WiFiClientSecure freed here

            if(b < BATCHES - 1) delay(BATCH_DELAY_MS);
        }

        xSemaphoreTake(aphorism.mutex, portMAX_DELAY);
        int pool_snap_size = (int)pool.size();
        xSemaphoreGive(aphorism.mutex);
        LOG_I("[Aphorism] Round %lu complete, pool now has %d quotes", batch_no, pool_snap_size);

        // Random inter-round jitter to desync LAN devices (min 1 min, max STARTUP_JITTER_MS)
        static const uint32_t ROUND_JITTER_MIN_MS = 1000UL * 60 * 2;
        uint32_t round_jitter_ms = ROUND_JITTER_MIN_MS + esp_random() % (STARTUP_JITTER_MS - ROUND_JITTER_MIN_MS + 1);
        LOG_I("[Aphorism] Next check in ~%lu s", round_jitter_ms / 1000);
        delay(round_jitter_ms);
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// benchmark_thread_entry
//
// Implements on-device hashrate benchmarking equivalent to the Python tool.
// Workflow (one reboot per round):
//   1. At boot, board.cpp reads bm_cur_freq/bm_cur_vcore from NVS and uses them
//      instead of the user-configured values (benchmark mode only).
//   2. This thread waits for MINER_READY, then waits stabilize_time seconds.
//   3. Samples hashrate/power/temp for benchmark_time seconds.
//   4. Evaluates stability (avg >= 98% expected HR).
//   5. Writes result to bm_result JSON string in NVS, advances the sweep state,
//      then triggers a reboot via reboot_xsem.
// In Normal mode (bm_mode == 0) the thread exits immediately.
// ──────────────────────────────────────────────────────────────────────────────
void benchmark_thread_entry(void *args) {
    board_sal_t *board = (board_sal_t*)args;

    // Exit immediately in Normal mode — use cached bm_mode, zero overhead on normal boots
    if (board->status.bm_mode != 1) {
        LOG_D("[BM] Normal mode, benchmark thread exiting.");
        vTaskDelete(NULL);
        return;
    }

    // Load benchmark parameters from NVS
    uint16_t freq_min    = nvs_config_get_u16(NVS_CONFIG_BM_FREQ_MIN,   400);
    uint16_t freq_max    = nvs_config_get_u16(NVS_CONFIG_BM_FREQ_MAX,   625);
    uint16_t freq_step   = nvs_config_get_u16(NVS_CONFIG_BM_FREQ_STEP,  50);
    uint16_t vcore_min   = nvs_config_get_u16(NVS_CONFIG_BM_VCORE_MIN,  1000);
    uint16_t vcore_max   = nvs_config_get_u16(NVS_CONFIG_BM_VCORE_MAX,  1300);
    uint16_t vcore_step  = nvs_config_get_u16(NVS_CONFIG_BM_VCORE_STEP, 25);
    uint8_t  smp_intv    = nvs_config_get_u8 (NVS_CONFIG_BM_SAMPLE_INTV, 10);  // seconds
    uint16_t bm_time     = nvs_config_get_u16(NVS_CONFIG_BM_TIME,        180); // seconds
    uint16_t stab_time   = nvs_config_get_u16(NVS_CONFIG_BM_STAB_TIME,   120); // seconds

    uint16_t cur_freq    = nvs_config_get_u16(NVS_CONFIG_BM_CUR_FREQ,  freq_min);
    uint16_t cur_vcore   = nvs_config_get_u16(NVS_CONFIG_BM_CUR_VCORE, vcore_min);

    LOG_W("[BM] === Benchmark started: freq=%d vcore=%d ===", cur_freq, cur_vcore);
    LOG_W("[BM] Range freq %d~%dMHz step=%d, vcore %d~%dmV step=%d",
          freq_min, freq_max, freq_step, vcore_min, vcore_max, vcore_step);
    LOG_W("[BM] stab=%ds bm=%ds smp=%ds", stab_time, bm_time, smp_intv);

    // Wait for full system init (WiFi + ASIC + miner ready)
    xEventGroupWaitBits(board->status.init_evt, INIT_EVENT_MINER_READY, pdFALSE, pdTRUE, portMAX_DELAY);

    // ── Populate benchmark status for the UI overlay ──────────────────────────
    board->status.bm.cur_freq    = cur_freq;
    board->status.bm.cur_vcore   = cur_vcore;
    board->status.bm.freq_min    = freq_min;
    board->status.bm.freq_max    = freq_max;
    board->status.bm.freq_step   = freq_step;
    board->status.bm.vcore_min   = vcore_min;
    board->status.bm.vcore_max   = vcore_max;
    board->status.bm.vcore_step  = vcore_step;
    board->status.bm.phase_total = stab_time;
    board->status.bm.phase_elapsed = 0;
    board->status.bm.in_stab     = true;
    board->status.bm.avg_hr_ghs  = 0.0f;
    board->status.bm.asic_temp   = 0.0f;
    board->status.bm.vcore_temp  = 0.0f;
    board->status.bm.stab_total  = stab_time;
    board->status.bm.bm_total    = (uint16_t)bm_time;
    // Calculate and store expected hashrate so overlay can show it from the start
    {
        uint32_t sc = board->miner ? board->miner->get_asic_small_cores() : 0;
        uint8_t  ac = board->miner ? board->miner->get_asic_count()       : 0;
        board->status.bm.exp_hr_ghs = (float)((double)cur_freq * ((double)(sc * ac) / 1000.0));
    }
    board->status.bm.active      = true;  // show overlay

    // ── Pre-compute ETA invariants (fixed for this boot) ─────────────────────
    uint16_t bm_freq_total = (freq_step > 0) ? ((freq_max - freq_min) / freq_step + 1) : 1;
    uint16_t bm_freq_idx   = (freq_step > 0) ? ((cur_freq  - freq_min) / freq_step + 1) : 1;
    uint32_t bm_per_round  = (uint32_t)stab_time + (uint32_t)bm_time;
    // Worst-case ETA:
    //   Current freq  : tries ALL remaining vcore steps from cur_vcore to vcore_max.
    //   Future freqs  : each starts one step below the current stable point (optimizer),
    //                   so worst-case rounds = vc_rounds_this + 1.
    //   ETA may increase slightly between rounds when the observed stable vcore is high
    //   (signals future freqs will also need higher voltage) — this is intentional and
    //   informative, similar to a nav app updating ETA when it detects slower conditions.
    uint32_t bm_vc_rounds_this   = (vcore_step > 0 && vcore_max >= cur_vcore)
                                    ? ((vcore_max - cur_vcore) / vcore_step + 1) : 1;
    uint32_t bm_vc_rounds_future = bm_vc_rounds_this + 1; // next freq starts at cur_vcore-step
    uint32_t bm_freq_remaining = (bm_freq_total > bm_freq_idx) ? (bm_freq_total - bm_freq_idx) : 0;
    // bm_future_rounds: seconds for everything AFTER the current round (worst case)
    uint32_t bm_future_rounds  = (bm_vc_rounds_this > 1 ? bm_vc_rounds_this - 1 : 0) * bm_per_round
                                  + bm_freq_remaining * bm_vc_rounds_future * bm_per_round;
    board->status.bm.eta_sec = (uint32_t)stab_time + (uint32_t)bm_time + bm_future_rounds;

    // ── Stabilization wait ────────────────────────────────────────────────────
    LOG_W("[BM] Stabilizing for %ds at freq=%dMHz vcore=%dmV ...", stab_time, cur_freq, cur_vcore);
    for (uint16_t s = 0; s < stab_time; s++) {
        board->status.bm.phase_elapsed = s;
        board->status.bm.asic_temp     = board->status.temp.asic;
        board->status.bm.vcore_temp    = board->status.temp.vcore;
        uint32_t stab_rem = (stab_time > s) ? (stab_time - s) : 0;
        board->status.bm.eta_sec = stab_rem + (uint32_t)bm_time + bm_future_rounds;
        delay(1000);
    }

    // ── Sampling loop ─────────────────────────────────────────────────────────
    uint32_t total_samples = bm_time / smp_intv;
    if (total_samples == 0) total_samples = 1;

    double hr_sum = 0, eff_sum = 0, pwr_sum = 0, at_sum = 0, vt_sum = 0;
    uint32_t sample_cnt = 0;
    uint8_t  zero_hr_cnt = 0;

    // Update overlay: switch to sampling phase
    board->status.bm.in_stab      = false;
    board->status.bm.phase_total   = bm_time;
    board->status.bm.phase_elapsed = 0;
    board->status.bm.eta_sec       = (uint32_t)bm_time + bm_future_rounds;

    // Calculate expected hashrate based on ASIC spec
    uint32_t small_cores = board->miner ? board->miner->get_asic_small_cores() : 0;
    uint8_t  asic_count  = board->miner ? board->miner->get_asic_count()       : 0;
    double   exp_hr_ghs  = (double)cur_freq * ((double)(small_cores * asic_count) / 1000.0); // GH/s
    // (exp_hr_ghs already written at init; keep local var for stable/eff checks below)

    LOG_W("[BM] Expected HR: %.1f GH/s (freq=%d small_cores=%d asic_cnt=%d)",
          exp_hr_ghs, cur_freq, small_cores, asic_count);

    for (uint32_t i = 0; i < total_samples; i++) {
        // Wait one sample interval
        for (uint8_t s = 0; s < smp_intv; s++) delay(1000);

        double hr_ghs = board->status.miner.hashrate._3m / 1e9;
        double vbus   = board->status.power.vbus / 1000.0;  // V
        double ibus   = board->status.power.ibus / 1000.0;  // A
        double pwr_w  = vbus * ibus;
        double at     = board->status.temp.asic;

        // Update overlay data
        board->status.bm.phase_elapsed = (i + 1) * smp_intv;
        board->status.bm.asic_temp     = (float)at;
        board->status.bm.vcore_temp    = board->status.temp.vcore;
        board->status.bm.avg_hr_ghs    = (sample_cnt > 0) ? (float)((hr_sum + hr_ghs) / (sample_cnt + 1)) : (float)hr_ghs;
        {
            uint32_t samp_elapsed = (i + 1) * (uint32_t)smp_intv;
            uint32_t samp_rem = (samp_elapsed < (uint32_t)bm_time) ? ((uint32_t)bm_time - samp_elapsed) : 0;
            board->status.bm.eta_sec = samp_rem + bm_future_rounds;
        }

        if (hr_ghs == 0.0) {
            zero_hr_cnt++;
        } else {
            zero_hr_cnt = 0;
        }

        // Abort if zero hashrate persists for 5 consecutive samples
        if (zero_hr_cnt >= 5) {
            LOG_W("[BM] Zero hashrate for 5 samples, aborting round.");
            break;
        }

        hr_sum  += hr_ghs;
        eff_sum += (hr_ghs > 0 && pwr_w > 0) ? (pwr_w / (hr_ghs / 1000.0)) : 0; // J/TH
        pwr_sum += pwr_w;
        at_sum  += at;
        vt_sum  += board->status.temp.vcore;
        sample_cnt++;

        double hr_avg = hr_sum / sample_cnt;
        uint32_t remaining = (total_samples - i - 1) * smp_intv;
        LOG_W("[BM] [%3ds] %2.0f%% | HR:%.1fGH/s EXP:%.0fGH/s | AT:%.1fC VT:%.1fC | Vcore:%dmV | Pwr:%.1fW",
              remaining, 100.0 * (i + 1) / total_samples,
              hr_ghs, exp_hr_ghs, at, board->status.temp.vcore,
              board->status.power.vcore, pwr_w);

        // Early exit if halfway and avg < 50% expected
        if (sample_cnt >= total_samples / 2 && exp_hr_ghs > 0 && hr_avg < exp_hr_ghs * 0.5) {
            LOG_W("[BM] Avg HR too low (%.1f < 50%% of %.1f), aborting round early.", hr_avg, exp_hr_ghs);
            break;
        }
    }

    // ── Evaluate stability ────────────────────────────────────────────────────
    double hr_avg  = (sample_cnt > 0) ? (hr_sum  / sample_cnt) : 0;
    double eff_avg = (sample_cnt > 0) ? (eff_sum / sample_cnt) : 0;
    double pwr_avg = (sample_cnt > 0) ? (pwr_sum / sample_cnt) : 0;
    double at_avg  = (sample_cnt > 0) ? (at_sum  / sample_cnt) : 0;
    double vt_avg  = (sample_cnt > 0) ? (vt_sum  / sample_cnt) : 0;

    bool stable = (exp_hr_ghs > 0) && (hr_avg >= exp_hr_ghs * 0.98);
    LOG_W("[BM] Round %s | avg HR:%.1fGH/s exp:%.1fGH/s | eff:%.3fJ/TH | pwr:%.2fW | asicT:%.1fC vcoreT:%.1fC",
          stable ? "STABLE" : "UNSTABLE", hr_avg, exp_hr_ghs, eff_avg, pwr_avg, at_avg, vt_avg);

    // ── Build result entry and append to NVS JSON string ─────────────────────
    if (stable) {
        // Read existing results
        char *existing = nvs_config_get_string(NVS_CONFIG_BM_RESULT, "[]");
        String results(existing);
        free(existing);

        // Remove trailing ']'
        results.trim();
        if (results.endsWith("]")) results.remove(results.length() - 1);
        if (!results.endsWith("[")) results += ",";

        // Append new entry
        char entry[300];
        snprintf(entry, sizeof(entry),
            "{\"freq\":%d,\"vcore\":%d,\"expHR\":%.1f,\"avgHR\":%.1f,\"avgAsicTemp\":%.1f,\"avgVcoreTemp\":%.1f,\"effJTH\":%.3f,\"avgPwr\":%.2f,\"ts\":%ld}",
            cur_freq, cur_vcore, exp_hr_ghs, hr_avg, at_avg, vt_avg, eff_avg, pwr_avg, (long)time(nullptr));
        results += entry;
        results += "]";

        nvs_config_set_string(NVS_CONFIG_BM_RESULT, results.c_str());
        LOG_W("[BM] Result saved: %s", entry);

        // Advance to next frequency.
        // Start next freq's vcore one step below the last stable point:
        // higher freq needs at least as much vcore, so vcore_min is wasteful.
        // One step down gives a small exploratory margin in case the new freq
        // can actually run slightly lower.
        cur_freq  += freq_step;
        cur_vcore  = (cur_vcore > vcore_min + vcore_step) ? (cur_vcore - vcore_step) : vcore_min;

        if (cur_freq > freq_max) {
            LOG_W("[BM] All frequencies tested — benchmark complete, applying best result.");
            board->status.bm.active = false;
            nvs_config_set_u8(NVS_CONFIG_BM_MODE, 0);
            nvs_config_set_u16(NVS_CONFIG_BM_CUR_FREQ,  freq_min);
            nvs_config_set_u16(NVS_CONFIG_BM_CUR_VCORE, vcore_min);
            // Select best result: highest avgHR entry in saved results
            {
                char *res_str = nvs_config_get_string(NVS_CONFIG_BM_RESULT, "[]");
                DynamicJsonDocument rdoc(4096);
                DeserializationError rerr = deserializeJson(rdoc, res_str);
                free(res_str);
                uint16_t best_freq = 0, best_vcore = 0;
                float    best_avgHR = -1.0f;
                if (!rerr && rdoc.is<JsonArray>()) {
                    for (JsonObject e : rdoc.as<JsonArray>()) {
                        float avgHR = e["avgHR"] | 0.0f;
                        if (avgHR > best_avgHR) {
                            best_avgHR  = avgHR;
                            best_freq   = e["freq"]  | (uint16_t)0;
                            best_vcore  = e["vcore"] | (uint16_t)0;
                        }
                    }
                }
                if (best_freq > 0 && best_vcore > 0) {
                    LOG_W("[BM] Best result: freq=%dMHz vcore=%dmV avgHR=%.1fGH/s — applying to Normal mode.",
                          best_freq, best_vcore, best_avgHR);
                    nvs_config_set_u16(NVS_CONFIG_ASIC_FREQ,    best_freq);
                    nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, best_vcore);
                } else {
                    LOG_W("[BM] No stable results found, keeping current Normal mode settings.");
                }
            }
            {
                uint32_t _start = nvs_config_get_u32(NVS_CONFIG_BM_START_TS, 0);
                if (_start > 0) nvs_config_set_u32(NVS_CONFIG_BM_TOTAL_SEC, (uint32_t)time(nullptr) - _start);
            }
            reboot_intent_set(REBOOT_INTENT_DAEMON_GENERIC, "benchmark complete, switching to Normal mode with best params");
            xSemaphoreGive(board->status.reboot_xsem);
            vTaskDelete(NULL);
            return;
        }
    } else {
        // Unstable: increase vcore for next round
        cur_vcore += vcore_step;
        if (cur_vcore > vcore_max) {
            LOG_W("[BM] freq=%dMHz unstable at all vcores, skipping to next freq.", cur_freq);
            cur_freq  += freq_step;
            // Even vcore_max wasn't enough for this freq; higher freq will need
            // at least as much, so skip straight to vcore_max to avoid pointless rounds.
            cur_vcore  = vcore_max;
            if (cur_freq > freq_max) {
                LOG_W("[BM] All frequencies exhausted — benchmark complete, applying best result.");
                board->status.bm.active = false;
                nvs_config_set_u8(NVS_CONFIG_BM_MODE, 0);
                nvs_config_set_u16(NVS_CONFIG_BM_CUR_FREQ,  freq_min);
                nvs_config_set_u16(NVS_CONFIG_BM_CUR_VCORE, vcore_min);
                // Select best result: highest avgHR entry in saved results
                {
                    char *res_str = nvs_config_get_string(NVS_CONFIG_BM_RESULT, "[]");
                    DynamicJsonDocument rdoc(4096);
                    DeserializationError rerr = deserializeJson(rdoc, res_str);
                    free(res_str);
                    uint16_t best_freq = 0, best_vcore = 0;
                    float    best_avgHR = -1.0f;
                    if (!rerr && rdoc.is<JsonArray>()) {
                        for (JsonObject e : rdoc.as<JsonArray>()) {
                            float avgHR = e["avgHR"] | 0.0f;
                            if (avgHR > best_avgHR) {
                                best_avgHR  = avgHR;
                                best_freq   = e["freq"]  | (uint16_t)0;
                                best_vcore  = e["vcore"] | (uint16_t)0;
                            }
                        }
                    }
                    if (best_freq > 0 && best_vcore > 0) {
                        LOG_W("[BM] Best result: freq=%dMHz vcore=%dmV avgHR=%.1fGH/s — applying to Normal mode.",
                              best_freq, best_vcore, best_avgHR);
                        nvs_config_set_u16(NVS_CONFIG_ASIC_FREQ,    best_freq);
                        nvs_config_set_u16(NVS_CONFIG_ASIC_VOLTAGE, best_vcore);
                    } else {
                        LOG_W("[BM] No stable results found, keeping current Normal mode settings.");
                    }
                }
                {
                    uint32_t _start = nvs_config_get_u32(NVS_CONFIG_BM_START_TS, 0);
                    if (_start > 0) nvs_config_set_u32(NVS_CONFIG_BM_TOTAL_SEC, (uint32_t)time(nullptr) - _start);
                }
                reboot_intent_set(REBOOT_INTENT_DAEMON_GENERIC, "benchmark complete (all unstable), switching to Normal mode");
                xSemaphoreGive(board->status.reboot_xsem);
                vTaskDelete(NULL);
                return;
            }
        }
    }

    // ── Write next round state and reboot ─────────────────────────────────────
    nvs_config_set_u16(NVS_CONFIG_BM_CUR_FREQ,  cur_freq);
    nvs_config_set_u16(NVS_CONFIG_BM_CUR_VCORE, cur_vcore);
    LOG_W("[BM] Next round: freq=%dMHz vcore=%dmV, rebooting...", cur_freq, cur_vcore);
    delay(500);
    reboot_intent_set(REBOOT_INTENT_DAEMON_GENERIC, "benchmark advancing to freq=%d vcore=%d", cur_freq, cur_vcore);
    xSemaphoreGive(board->status.reboot_xsem);
    vTaskDelete(NULL);
}
