# Reflect 重构迁移进度

目标：把 `src/` 的 god 结构 `board_sal_t g_board` 拆成 NMMiner 风格的「每线程上下文 (void* args 注入)」，
逻辑/时序严格不变；驱动按更优雅的结构重组。每个阶段必须编译通过后才进入下一步。

构建命令：`pio run -e reflect`

## 阶段规划（自底向上）
- [x] P0 基线：reflect 骨架可编译（board_probe + app/thread/mining 占位）
- [ ] P1 基础层：utils/ + nvs/ 接入构建，从 global.h 解耦
- [ ] P2 驱动 HAL：asic/power/temp/iic/touch/fan/extio 迁移并解耦 g_board
- [ ] P3 板级 spec：BoardSpecConfig 运行时配置（按 board_probe 结果产出）
- [ ] P4 显示驱动 + 把 UIManager 接入真实分辨率
- [ ] P5+ 线程逐个上下文化：wifi → stratum → miner → power/fan/temp → market → web

## 上下文拆分设计（替代 g_board）
g_board.status 里的字段按「谁写谁拥有」原则拆成各线程 Ctx：
- MiningSharedCtx（已建）：stratum<->miner 共享
- WifiCtx / SwarmCtx（已建于 application.h）
- 后续：PowerCtx / FanCtx / DisplayCtx / MarketCtx / WebCtx / NeighborCtx ...
- 纯展示字段 → AppState Observable（已建）

## 变更日志

### P1 完成 (utils + nvs)
- 新增 reflect_src/version.h，reboot_log.cpp 从 global.h 解耦到 version.h
- 复制 src/nvs → reflect_src/nvs
- platformio.ini: build_src_filter 加 utils/ nvs/；加 `-iquote ./reflect_src`
  解决 PlatformIO 隐式 -Isrc 抢占（src/logger.h 误拉 AsyncWebSocket.h）
- 构建通过：Flash 15.4%

### P2 完成 (驱动 HAL: iic/asic/temp/touch/power/extio)
- 复制以上驱动到 reflect_src/drivers/，root-relative include 在 -iquote/-I reflect_src 下正确解析
- power_hal.cpp / tca9554.cpp 删除了无用的 `#include "global.h"`（实际未用 g_board）
- build_src_filter 逐个加入；构建通过 Flash 15.6%
- fan / displays 依赖 board spec(fan_config_t / tft 配置)，留到 P3 之后
