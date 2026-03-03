#pragma once
#include "global.h"
#include "drivers/displays/display.h"
#include "drivers/board/board.h"
#include "thread/thread_entry.h"

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
    TaskHandle_t _swarmTask    = NULL;
    TaskHandle_t _marketTask   = NULL;
    TaskHandle_t _daemonTask   = NULL;
    TaskHandle_t _stratumTask  = NULL;
    TaskHandle_t _minerTxTask  = NULL;
    TaskHandle_t _minerRxTask  = NULL;
    TaskHandle_t _powerTask    = NULL;
    TaskHandle_t _lvglTask     = NULL;
    TaskHandle_t _uiTask       = NULL;
};
