// What: AxeOS-compatible HTTP/API service for the BSP-first runtime.
// Why: The Angular frontend expects the legacy route table and JSON field names,
// while the new firmware stores state in Board/AppConfig/RuntimeState.
// Role: Serves gzipped SPIFFS assets and maps legacy API endpoints onto the new
// runtime/config model.
#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "bsp/board.h"
#include "config/app_config.h"
#include "config/config_store.h"
#include "state/runtime_state.h"
#include "system/events.h"

class AsyncWebServerRequest;

namespace nm::web {

class WebService {
public:
    bool start(
        const bsp::Board& board,
        config::ConfigStore& config_store,
        config::AppConfig& config,
        state::RuntimeState& runtime,
        system::EventFlags& events,
        SemaphoreHandle_t state_mutex);

    bool started() const { return _started; }

public:
    // Route handlers in web_service.cpp are file-local to keep the legacy route
    // table compact, so they read this shared runtime context directly.
    friend bool file_system_init();
    friend void lock_state();
    friend void unlock_state();
    friend void save_config_locked();
    friend void handle_system_info(AsyncWebServerRequest* request);
    friend void handle_setting_network(AsyncWebServerRequest* request);
    friend void patch_setting_network(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t, size_t);
    friend void handle_setting_time(AsyncWebServerRequest* request);
    friend void patch_setting_time(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t, size_t);
    friend void handle_setting_mining(AsyncWebServerRequest* request);
    friend void patch_setting_mining(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t, size_t);
    friend void handle_setting_market(AsyncWebServerRequest* request);
    friend void patch_setting_market(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t, size_t);
    friend void handle_setting_preference(AsyncWebServerRequest* request);
    friend void patch_setting_preference(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t, size_t);
    friend void handle_chart(AsyncWebServerRequest* request, bool history);
    friend void handle_lucky_history(AsyncWebServerRequest* request);
    friend void handle_gauge_limits(AsyncWebServerRequest* request);
    friend void handle_hr_dist(AsyncWebServerRequest* request);
    friend void handle_theme(AsyncWebServerRequest* request);
    friend void post_theme(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t, size_t);
    friend void handle_benchmark(AsyncWebServerRequest* request);
    friend void patch_benchmark(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t, size_t);
    friend void handle_probe(AsyncWebServerRequest* request);
    friend void handle_alive(AsyncWebServerRequest* request);
    friend void handle_mining_state_patch(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t, size_t);
    friend void register_common_routes(bool fs_ready);

    const bsp::Board* _board = nullptr;
    config::ConfigStore* _config_store = nullptr;
    config::AppConfig* _config = nullptr;
    state::RuntimeState* _runtime = nullptr;
    system::EventFlags* _events = nullptr;
    SemaphoreHandle_t _state_mutex = nullptr;
    bool _started = false;
    bool _fs_ready = false;
};

}  // namespace nm::web
