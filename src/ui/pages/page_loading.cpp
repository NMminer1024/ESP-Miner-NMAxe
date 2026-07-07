// What: Loading-page base implementation for the new UI framework.
// Why: Boot progress has to be shown through page abstractions before the exact
// legacy Gamma loading page is migrated.
// Role: Fills the shared scaffold with boot-centric placeholder text.
// Benefit: Keeps the future loading migration isolated to one page family.
#include "ui/pages/page_loading.h"

#include <stdio.h>

namespace nm::ui {

namespace {

const char* boot_phase_text(state::BootPhase phase) {
    switch (phase) {
        case state::BootPhase::ColdBoot:
            return "cold-boot";
        case state::BootPhase::LoadConfig:
            return "load-config";
        case state::BootPhase::InitBoard:
            return "init-board";
        case state::BootPhase::InitPower:
            return "init-power";
        case state::BootPhase::InitCooling:
            return "init-cooling";
        case state::BootPhase::InitUi:
            return "init-ui";
        case state::BootPhase::Ready:
            return "ready";
        case state::BootPhase::Fault:
            return "fault";
    }

    return "unknown";
}

}  // namespace

void PageLoadingBase::create_loading_page(lv_obj_t* parent, const PageScaffoldMetrics& metrics) {
    create_scaffold(parent, metrics);
    set_title("Loading");
    set_subtitle("Boot sequence");
    set_footer("TODO(agent): replace with legacy loading page");
}

void PageLoadingBase::render(const PageContext& context) {
    if (!is_created()) {
        return;
    }

    char line[128] = {};
    set_title(context.board.traits().board_name);
    set_subtitle("Loading");

    snprintf(
        line,
        sizeof(line),
        "progress %u%%  phase %s",
        static_cast<unsigned>(context.runtime.boot.progress_percent),
        boot_phase_text(context.runtime.boot.phase));
    set_line(0, line);

    snprintf(
        line,
        sizeof(line),
        "display %s %ux%u",
        context.board.display_profile().display_name,
        static_cast<unsigned>(context.board.display_profile().width),
        static_cast<unsigned>(context.board.display_profile().height));
    set_line(1, line);

    snprintf(
        line,
        sizeof(line),
        "boot %s",
        context.runtime.boot.message);
    set_line(2, line);

    snprintf(
        line,
        sizeof(line),
        "miner %s",
        context.runtime.mining.message);
    set_line(3, line);

    snprintf(
        line,
        sizeof(line),
        "input buttons:%u fans:%u",
        static_cast<unsigned>(context.runtime.button_count),
        static_cast<unsigned>(context.runtime.fan_count));
    set_line(4, line);

    set_footer("TODO(agent): replace with exact legacy loading visuals");
}

}  // namespace nm::ui
