#include "ui/ui_root.h"

#include <Arduino.h>

#include "product/ui_profiles/ui_profile_registry.h"
#include "ui/port/ports.h"

namespace nm::ui {

void boot(const bsp::Board& board) {
    const auto& profile = product::active_ui_profile(board);

    port::bind_display(*board.drivers().display);
    port::bind_input(board.drivers().touch);

    Serial.printf("[ui] profile=%s layout=%u variant=%u input=%u\n",
                  profile.profile_name,
                  static_cast<unsigned>(profile.layout_id),
                  static_cast<unsigned>(profile.variant_id),
                  static_cast<unsigned>(profile.input_mode));
}

}  // namespace nm::ui
