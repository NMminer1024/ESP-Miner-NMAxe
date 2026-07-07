#include "app/application.h"

#include <Arduino.h>

#include "bsp/board.h"
#include "ui/ui_root.h"

namespace nm {

Application& Application::instance() {
    static Application app;
    return app;
}

void Application::setup() {
    if (_initialized) {
        return;
    }

    Serial.begin(115200);
    delay(50);
    Serial.println();
    Serial.println("[app] BSP-first skeleton boot");

    _board = &bsp::board();
    _board->init();
    ui::boot(*_board);

    _initialized = true;
}

void Application::loop() {
    if (!_initialized || _board == nullptr) {
        return;
    }

    static uint32_t last_log_ms = 0;
    const uint32_t now = millis();
    if (now - last_log_ms < 1000) {
        return;
    }

    last_log_ms = now;
    Serial.printf("[app] alive board=%s display=%s %ux%u\n",
                  _board->traits().board_name,
                  _board->display_profile().display_name,
                  _board->display_profile().width,
                  _board->display_profile().height);
}

const bsp::Board& Application::board() const {
    return *_board;
}

}  // namespace nm
