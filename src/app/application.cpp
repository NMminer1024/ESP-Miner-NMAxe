// What: Concrete application bootstrap and main-loop implementation.
// Why: This is where the selected BSP and UI framework are actually stitched
// together at runtime after the firmware image starts.
// Role: Initializes serial logging, boots the active board, and pumps UI polling.
// Benefit: Keeps the execution order explicit and makes framework bring-up easy
// to inspect or adjust without touching board-specific code.
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

    ui::poll();
}

const bsp::Board& Application::board() const {
    return *_board;
}

}  // namespace nm
