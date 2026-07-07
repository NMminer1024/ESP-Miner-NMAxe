// What: Configuration storage abstraction for the new application layer.
// Why: The main flow should ask one service for normalized settings instead of
// reading board defaults or persistence details directly.
// Role: Defines the load/save contract plus the NVS-backed implementation used
// by the current firmware image.
// Benefit: Board defaults remain the fallback source, while runtime settings can
// now persist without leaking NVS access into services or UI code.
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

class NvsConfigStore final : public ConfigStore {
public:
    bool init() override;
    bool load(const bsp::Board& board, AppConfig& config) override;
    bool save(const AppConfig& config) override;
};

}  // namespace nm::config
