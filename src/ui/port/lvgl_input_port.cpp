#include "ui/port/ports.h"

#include <Arduino.h>

namespace nm::ui::port {

void bind_input(const drivers::Touch* touch) {
    if (touch == nullptr) {
        Serial.println("[ui.port] no touch bound");
        return;
    }

    Serial.printf("[ui.port] bind touch=%s\n", touch->name());
}

}  // namespace nm::ui::port
