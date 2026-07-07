// What: LVGL input adapter placeholder for board-provided input devices.
// Why: Input wiring will eventually need its own indev registration path, even
// though the current gamma bring-up still runs without touch or button binding.
// Role: Serves as the dedicated integration point between BSP input drivers and LVGL.
// Benefit: Preserves the intended architecture now, so real input support can be
// added later without reshaping application or page-layer code.
#include "ui/port/ports.h"

#include <Arduino.h>

#include "utils/logger/logger.h"

namespace nm::ui::port {

void bind_input(drivers::Touch* touch) {
    // Temporary BSP-first input stub.
    // TODO(agent): replace this with a real LVGL indev binding once touch/button input is implemented.
    if (touch == nullptr) {
        LOG_I("[ui.port] no touch bound");
        return;
    }

    LOG_I("[ui.port] bind touch=%s", touch->name());
}

}  // namespace nm::ui::port
