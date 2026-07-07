#include "ui/port/ports.h"

#include <Arduino.h>

namespace nm::ui::port {

void bind_display(const drivers::Display& display) {
    const auto size = display.size();
    Serial.printf("[ui.port] bind display=%s %ux%u\n",
                  display.name(),
                  size.width,
                  size.height);
}

}  // namespace nm::ui::port
