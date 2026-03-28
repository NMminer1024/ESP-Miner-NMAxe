#pragma once
#include "global.h"
#include "drivers/displays/display.h"
#include "board/board.h"
#include "thread/thread_entry.h"
#include <vector>

class MinerApp {
public:
    static MinerApp& instance();

    /**
     * @brief Initialize hardware and board configuration.
     *        Should be retried until it returns true.
     * @return true on success
     */
    bool init();

    /**
     * @brief Create and launch all application threads.
     *        Call only after init() succeeds.
     */
    void begin();

    /**
     * @brief Print FreeRTOS stack high-water mark for every managed thread.
     *        Call periodically from loop() for stack tuning.
     *        Wrap with #if 0 / #endif to disable in production.
     */
    void print_stack_hwm() const;

private:
    MinerApp()                          = default;
    MinerApp(const MinerApp&)           = delete;
    MinerApp& operator=(const MinerApp&)= delete;

    bool _board_init(const BoardSpecConfig& config);

    // Task handles - owned by this class
    TaskHandle_t _fanTask      = NULL;
    TaskHandle_t _ledTask      = NULL;
    TaskHandle_t _btnTask      = NULL;
    TaskHandle_t _touchTask    = NULL;
    TaskHandle_t _wsTask       = NULL;
    TaskHandle_t _monitorTask  = NULL;
    TaskHandle_t _neighborTask = NULL;
    TaskHandle_t _swarmTask    = NULL;
    TaskHandle_t _marketTask   = NULL;
    TaskHandle_t _daemonTask   = NULL;
    TaskHandle_t _stratumTask  = NULL;
    TaskHandle_t _minerTxTask  = NULL;
    TaskHandle_t _minerRxTask  = NULL;
    TaskHandle_t _aphorismTask = NULL;
    TaskHandle_t _powerTask    = NULL;
    TaskHandle_t _lvglTask     = NULL;
    TaskHandle_t _uiTask       = NULL;

    // Thread pool config kept after begin() for debug introspection
    std::vector<thread_config_t> _thread_pool;

    // MinerApp-private tick thread
    TaskHandle_t _tickTask = NULL;
    static void  _tick_thread_entry(void* args);
};
