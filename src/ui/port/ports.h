#pragma once

#include "drivers/display/display.h"
#include "drivers/touch/touch.h"

namespace nm::ui::port {

void bind_display(const drivers::Display& display);
void bind_input(const drivers::Touch* touch);

}  // namespace nm::ui::port
