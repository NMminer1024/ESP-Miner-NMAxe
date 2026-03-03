#include "global.h"
#include "drivers/displays/display.h"
#include "app/application.h"

void setup() {
    MinerApp::instance().init();
    MinerApp::instance().begin();
}

void loop() {
    uint32_t last = millis();
    while (millis() - last < 1000 * 1) {
        static uint16_t brightness = g_board.status.preference.screen.brightness;
        static float    x = 0;
        if (g_board.status.miner.last_hits != g_board.status.miner.hits) { // blink on block hit
            brightness = 100 * (1 + sin(x)) / 2;
            x += 0.1;
            tft_bl_ctrl(brightness);
        } else {
            brightness = g_board.status.preference.screen.brightness;
        }
        delay(10);
    }
    delay(100);
}
