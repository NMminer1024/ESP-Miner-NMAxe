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

### P3 完成 (板级 spec BoardSpecConfig)
- 复制 board.{h,cpp} + nmaxe/nmaxegamma/nmqaxepp.h；fan 目录(仅 tmp102.cpp 入构建) + fan.h 类型
- tmp102.cpp 去掉无用 global.h
- application 改用真实板层：get_board_model() → get_board_config() → hardware_pre_init()
  MinerApp 持有 `BoardModelType _model` + `BoardSpecConfig _spec`（取代 g_board.info.spec）
- 删除占位 board_probe.{h,cpp}/board_re_export.h，检测路径单一化（保留原始去抖时序）
- init() 补回原始 Serial.begin(115200) 时序
- 注意：board.cpp::get_board_model() 顶部仍保留原 src 的 `return REV81` 测试强制项（逻辑与 src 一致，未改）
- 构建通过 Flash 17.6%

### P5 完成 (挖矿域类: market/stratum/miner，依赖注入解耦)
- market：零 g_board，直接迁移（去 global.h）。reflect 环境补齐全套 lib_deps
- stratum：StratumClass 新增 `String _client_id` + `set_client_id()`，
  subscribe() 用注入的 client_id 取代 g_board.info.spec.display_name+fw_version（逻辑不变）
- miner：AsicMinerClass 新增 `_stratum` + `_asic_name` 成员与 set_stratum()/set_asic_name()，
  9 处 g_board 引用全部改为注入成员（asic 名称比较/日志、stratum 调用，逻辑/时序不变）
- 构建通过 Flash 17.9%
- 注入点将在后续矿工线程上下文阶段由 application 调用（set_stratum/set_asic_name/set_client_id）

### 整理: tmp102 归位 (drivers/fan → drivers/temp)
- TMP102 是温度传感器，注册进 temp HAL（temp_hal_register_vcore/asic），放 fan/ 是历史遗留
- git mv tmp102.{cpp,h} → drivers/temp/；board.cpp include 改 drivers/temp/tmp102.h
- platformio.ini 去掉显式 fan/tmp102.cpp（已被 drivers/temp/ glob 覆盖）
- 构建通过 Flash 17.9%

### P6 完成 (power 域线程上下文化)
- 新增 reflect_src/app/system_events.h：INIT_EVENT_* / SYS_EVENT_* 真实事件位（与 global.h 字面一致）
- mining_types.h：新增 MinerRuntimeState 枚举 + runtime_state/user_paused/pause_started_ms/
  resume_grace_until_ms 字段 + is_controlled_idle()/in_resume_grace()/suppress_activity_checks() 辅助
- 新增 reflect_src/drivers/power/power_ctx.h：PowerCtx（前置声明，零重头依赖）
  字段=power/spec/init_evt/sys_evt/ota_running/mining，全部由 application 注入（替代 board_sal_t*）
- thread_entry.cpp：power_init/power_loop 从 src 完整迁移，board-> 全部改读 PowerCtx，逻辑/时序不变
- application：init() 用 spec.create_power_instance + setup_temp_hal 创建 _power（替代 g_board.power）
  新增 _begin_power() 注入 PowerCtx 并启动 (pwr_init)/(pwr_loop)；boot 阶段数 6→7
  占位事件位改为真实位（wifi 占位 set WIFI_STA_CONNECTED）
- 临时项：fan 未迁移，_begin_power 里临时 set INIT_EVENT_FAN_READY 让 power_init 过 gate（带 TODO）
  PowerCtx.mining 暂为 nullptr（miner 域未迁移，idle 判断退化为“非 idle”，与启动期行为一致）
- 构建通过 Flash 18.0%

### 待办（下一步：线程上下文化）
g_board.status god 结构按「写者拥有」拆分为各线程 Ctx：
  Power/Fan/Display/Wifi/Web/Neighbor/Swarm/Market/Benchmark ...
仍耦合 g_board 的源：display.cpp(5249行,新UI已重写,不整体迁移)、thread_entry.cpp(3981行)、fan.cpp
建议顺序：先定义各 Ctx struct（取代 board_status_t 字段），再逐线程把 thread_entry.cpp 的逻辑搬入并改读 ctx。
