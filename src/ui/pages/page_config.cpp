// What: Config-page base implementation for the new UI framework.
// Why: The service layer already owns normalized config data, so the page layer
// can start consuming it before the old visuals are migrated.
// Role: Shows the key config domains currently available in `AppConfig`.
// Benefit: Keeps config rendering behind a page boundary instead of expanding
// `ui_root` into another monolithic switch statement.
#include "ui/pages/page_config.h"

#include <stdio.h>

namespace nm::ui {

void PageConfigBase::create_config_page(lv_obj_t* parent, const PageScaffoldMetrics& metrics) {
    create_scaffold(parent, metrics);
    set_title("Config");
    set_subtitle("NVS / defaults");
    set_footer("TODO(agent): replace with legacy config page");
}

void PageConfigBase::render(const PageContext& context) {
    if (!is_created()) {
        return;
    }

    char line[128] = {};
    set_title("Config");
    set_subtitle("NVS / defaults");

    snprintf(line, sizeof(line), "wifi %s", context.config.network.sta_ssid.c_str());
    set_line(0, line);

    snprintf(
        line,
        sizeof(line),
        "host %s",
        context.config.network.hostname.isEmpty() ? "<auto>" : context.config.network.hostname.c_str());
    set_line(1, line);

    snprintf(line, sizeof(line), "pool %s", context.config.stratum.primary.url.c_str());
    set_line(2, line);

    snprintf(
        line,
        sizeof(line),
        "screen flip:%s br:%u%%",
        context.config.screen.flip ? "on" : "off",
        static_cast<unsigned>(context.config.screen.brightness_percent));
    set_line(3, line);

    snprintf(
        line,
        sizeof(line),
        "saver %s %lus",
        context.config.screen.screensaver_enabled ? "on" : "off",
        static_cast<unsigned long>(context.config.screen.screensaver_timeout_s));
    set_line(4, line);

    set_footer(context.config.network.force_config ? "force_config=yes" : "force_config=no");
}

}  // namespace nm::ui
