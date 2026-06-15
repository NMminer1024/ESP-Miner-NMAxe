#pragma once

#include <Arduino.h>
#include <vector>

#include "app_state.h"
#include "task_config.h"
#include "../mining/mining_types.h"

class MinerApp {
public:
    static constexpr int WIFI_DISCONNECTED = 0;
    static constexpr int WIFI_CONNECTED = 3;

    static MinerApp& instance();

    bool init();
    void begin();

    void print_stack_hwm() const;

private:
    MinerApp() = default;
    MinerApp(const MinerApp&) = delete;
    MinerApp& operator=(const MinerApp&) = delete;

    struct SystemSync {
        EventGroupHandle_t init_evt = nullptr;
        EventGroupHandle_t sys_evt = nullptr;
        SemaphoreHandle_t reboot_xsem = nullptr;
    };

    struct WifiCtx {
        volatile int status = WIFI_DISCONNECTED;
        char ip[16] = {0};
        volatile int rssi = 0;
        SemaphoreHandle_t reconnect_xsem = nullptr;
        volatile bool client_connected = false;
    };

    struct SwarmCtx {
        SemaphoreHandle_t mutex = nullptr;
        volatile uint32_t total_workers = 0;
        volatile float total_hr = 0.0f;
        volatile float best_ever_bd = 0.0f;
    };

    struct TaskEntry {
        TaskHandle_t handle;
        const char* name;
        uint32_t stack_bytes;
        UBaseType_t priority;
    };

    struct BootProgress {
        int step = 0;
        int step_inc;
        explicit BootProgress(int total_stages);
        void post(const char* msg, uint32_t c = 0xFFFFFF);
        void next(const char* msg, uint32_t c = 0xFFFFFF);
        void finish(const char* msg, uint32_t c = 0x00FF00);
    };

    SystemSync* _sys = nullptr;
    WifiCtx* _wifi = nullptr;
    SwarmCtx* _swarm = nullptr;
    MiningSharedCtx* _miningShared = nullptr;
    std::vector<TaskEntry> _tasks;

    BaseType_t _create_task(TaskFunction_t fn, const char* name,
                            uint32_t stack_bytes, void* param,
                            UBaseType_t prio, BaseType_t core);

    void _begin_board_init(BootProgress& boot);
    void _begin_wifi_connect(BootProgress& boot);
    void _begin_display(BootProgress& boot);
    void _begin_infra(BootProgress& boot);
    void _begin_market(BootProgress& boot);
    void _begin_miners(BootProgress& boot);

    static void _tick_thread_entry(void* args);
    static void _lvgl_thread_entry(void* args);

    bool _ui_init();
};
