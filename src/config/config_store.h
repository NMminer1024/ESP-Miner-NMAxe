// What: Configuration storage abstraction for the new application layer.
// Why: The main flow should ask a config service for normalized values instead
// of reading board defaults or persistence details directly.
// Role: Defines the load/save contract plus a board-default implementation used
// during the current bring-up phase.
// Benefit: Lets the architecture adopt NVS or remote config later without
// reshaping application or service code.
#pragma once

#include "bsp/board.h"
#include "config/app_config.h"

namespace nm::config {

class ConfigStore {
public:
    virtual ~ConfigStore() = default;
    virtual bool init() = 0;
    virtual bool load(const bsp::Board& board, AppConfig& config) = 0;
    virtual bool save(const AppConfig& config) = 0;
};

class BoardDefaultConfigStore final : public ConfigStore {
public:
    bool init() override;
    bool load(const bsp::Board& board, AppConfig& config) override;
    bool save(const AppConfig& config) override;
};

}  // namespace nm::config
