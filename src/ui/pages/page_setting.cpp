// What: Settings/swarm page base implementation for the new UI framework.
// Why: High-level system settings should remain visible through page
// abstractions while the exact old settings visuals are still pending.
// Role: Shows system/UI settings currently available in config and BSP traits.
// Benefit: Keeps the settings page boundary stable for later migration work.
#include "ui/pages/page_setting.h"

#include <stdio.h>

namespace nm::ui {

namespace {

const char* input_mode_text(bsp::UiInputMode mode) {
    switch (mode) {
        case bsp::UiInputMode::ButtonOnly:
            return "button";
        case bsp::UiInputMode::TouchOnly:
            return "touch";
        case bsp::UiInputMode::Hybrid:
            return "hybrid";
    }

    return "unknown";
}

}  // namespace

void PageSettingBase::create_setting_page(lv_obj_t* parent, const PageScaffoldMetrics& metrics) {
    create_scaffold(parent, metrics);
    set_title("Setting");
    set_subtitle("System / swarm");
    set_footer("TODO(agent): replace with legacy settings page");
}

void PageSettingBase::render(const PageContext& context) {
    if (!is_created()) {
        return;
    }

    char line[128] = {};
    set_title("Setting");
    set_subtitle("System / swarm");

    snprintf(line, sizeof(line), "startup page %u", static_cast<unsigned>(context.config.ui.startup_page));
    set_line(0, line);

    snprintf(line, sizeof(line), "auto cycle %s", context.config.screen.auto_cycle_pages ? "on" : "off");
    set_line(1, line);

    snprintf(line, sizeof(line), "indicator %s", context.config.led.indicator_enabled ? "on" : "off");
    set_line(2, line);

    snprintf(line, sizeof(line), "input %s", input_mode_text(context.board.input_profile().mode));
    set_line(3, line);

    snprintf(
        line,
        sizeof(line),
        "touch:%s button:%s",
        context.board.input_profile().has_touch ? "yes" : "no",
        context.board.input_profile().has_button ? "yes" : "no");
    set_line(4, line);

    set_footer(context.board.traits().board_revision);
}

}  // namespace nm::ui
