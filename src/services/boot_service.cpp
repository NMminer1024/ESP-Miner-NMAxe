// What: Concrete boot service for the current minimal startup path.
// Why: The new architecture needs an explicit boot-policy layer before mining
// and networking are migrated.
// Role: Drives config load, board init, power defaults, fan self-test, and
// initial UI-related runtime state.
// Benefit: Establishes the future startup skeleton without leaking hardware
// details into `Application`.
#include "services/boot_service.h"

#include <Arduino.h>

namespace nm::services {

namespace {

bool fail(state::RuntimeState& runtime, const char* message) {
    runtime.boot.phase = state::BootPhase::Fault;
    runtime.boot.message = message;
    runtime.boot.ready = false;
    return false;
}

}  // namespace

bool BootService::start(
    bsp::Board& board,
    config::ConfigStore& config_store,
    config::AppConfig& config,
    state::RuntimeState& runtime,
    state::UiState& ui_state,
    system::EventFlags& events) {
    runtime.boot.phase = state::BootPhase::LoadConfig;
    runtime.boot.message = "load config";

    if (!config_store.init()) {
        return fail(runtime, "config store init failed");
    }
    if (!config_store.load(board, config)) {
        return fail(runtime, "config load failed");
    }

    ui_state.current_page = config.ui.startup_page == 0 ? state::UiPageId::Summary : state::UiPageId::Detail;
    ui_state.last_activity_ms = millis();
    ui_state.dirty = true;

    runtime.boot.phase = state::BootPhase::InitBoard;
    runtime.boot.message = "init board";
    board.init();
    runtime.boot.board_ready = true;
    events.set(system::Event::BoardReady);

    if (board.drivers().power != nullptr) {
        runtime.boot.phase = state::BootPhase::InitPower;
        runtime.boot.message = "apply power defaults";
        board.drivers().power->set_vcore_limits(board.policies().min_vcore_mv, board.policies().max_vcore_mv);
        board.drivers().power->set_vcore_mv(config.mining.target_vcore_mv);
        board.drivers().power->set_rail_enabled(drivers::PowerRail::Pll0v8, true);
        board.drivers().power->set_rail_enabled(drivers::PowerRail::Vdd1v8, true);
        board.drivers().power->set_rail_enabled(drivers::PowerRail::Vcore, true);
    }

    runtime.fan_count = 0;
    if (!board.drivers().fans.empty()) {
        runtime.boot.phase = state::BootPhase::InitCooling;
        runtime.boot.message = "fan self-test";
        for (size_t i = 0; i < board.drivers().fans.size() && i < state::kMaxFans; ++i) {
            auto* fan = board.drivers().fans[i];
            if (fan == nullptr) {
                continue;
            }
            fan->set_speed_percent(100);
            const auto self_test = fan->run_self_test();
            runtime.fans[i].present = true;
            runtime.fans[i].self_test_passed = self_test.passed;
            runtime.fans[i].speed_percent = fan->speed_percent();
            runtime.fans[i].rpm = self_test.rpm;
            runtime.fan_count++;
        }
    }

    runtime.button_count = 0;
    for (size_t i = 0; i < board.drivers().buttons.size() && i < state::kMaxButtons; ++i) {
        if (board.drivers().buttons[i] != nullptr) {
            runtime.buttons[i].present = true;
            runtime.button_count++;
        }
    }

    if (board.drivers().display != nullptr) {
        board.drivers().display->set_flip(config.screen.flip);
        board.drivers().display->set_brightness_percent(config.screen.brightness_percent);
    }

    runtime.boot.phase = state::BootPhase::InitUi;
    runtime.boot.message = "ui bind pending";
    return true;
}

}  // namespace nm::services
