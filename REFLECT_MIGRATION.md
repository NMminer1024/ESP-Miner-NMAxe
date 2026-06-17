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

### P7 完成 (fan + temp 域线程上下文化)
- 新增 reflect_src/drivers/temp/temp_ctx.h：TempState { volatile float asic/vcore }（取代 g_board.status.temp）
- 新增 reflect_src/drivers/fan/fan_ctx.h：FanCtx { spec(可变, 极性)/init_evt/sys_evt/status_list/temp }
- fan.cpp 去掉 board/global 依赖，成为纯驱动；thread_entry.cpp 迁移 fan 线程读 FanCtx
- application：_begin_fan 注入 FanCtx；tick 线程按 125ms 采样温度写 TempState

### P8 完成 (挖矿上下文按真实类模型重定义)
- mining_types.h 重写：MinerStatus（diff/hashrate/efficiency/hits/shares/uptime/latency/
  asic_update/stratum_update/runtime_state/控制信号量/asic_rsp_counter/status_history+proximity_history）
  + ConnInfo（pool/stratum use/primary/fallback）+ MinerCtx（全依赖注入）

### P9 完成 (asic/miner/stratum 实例 + ConnInfo 依赖注入)
- application::_init_mining_instances()：解析 NVS 池 URL/用户，spec 工厂建 asic，建 AsicMinerClass/StratumClass
  set_stratum/set_asic_name/set_client_id 注入

### P10-P13 完成 (挖矿线程全管线迁移)
- miner_count / miner_init / stratum / miner_tx / miner_rx 全部从 src 迁移读 MinerCtx，逻辑/时序不变

### P14 完成 (wifi 域)
- 新增 reflect_src/net/wifi_ctx.h：WifiState（status/ip/rssi/...）+ WifiConnConfig（sta/ap/hostname）+ WifiCtx
- 迁移 wifi_connect + config_monitor 线程（STA 连接 + AP 配置门户 + 超时看门狗）
- application 读 NVS 装载 wifi 配置，_begin_wifi_connect 启动 (wifi) 线程；tick 用 ip.toString()

### P15 完成 (market 域)
- 新增 reflect_src/market/market_ctx.h：MarketCtx（market/wifi_status/ota/sys_evt/coin_price/coin_watchlist）
- 迁移 market 线程；application 建 MarketClass + 读 NVS coin 配置，_begin_market 启动 (market)

### P16 完成 (daemon 域)
- 新增 reflect_src/app/daemon_ctx.h：DaemonCtx（reboot/factory/wifi_reconnect 信号量 + ota/wifi/bm_mode/status/wifi_cfg）
- 迁移 daemon 线程（重启编排 + 工厂复位 + stratum/ASIC 冻结看门狗）
- application：新增 _recover_factory_xsem/_force_config_xsem/_bm_mode，_begin_infra 启动 (daemon)

### P17 完成 (swarm + scan 邻居发现域)
- 新增 reflect_src/net/swarm_ctx.h：neighbor_ip_* 别名 + SwarmState + NeighborState + SwarmCtx
- 迁移 swarm（/probe + /alive gossip 聚合）与 scan（/24 ICMP 存活扫描）线程
- application：用 SwarmState/NeighborState 替换原嵌套 SwarmCtx；_begin_infra 启动 (swarm)/(neighbor)

### P18 完成 (monitor 域)
- 新增 reflect_src/app/monitor_ctx.h：MonitorCtx（power/spec/miner/status/pwr/temp/fan/wifi/utc/tz/...）
- 新增 PowerTelemetry（power_ctx.h）；mining_types.h 补 BOARD_NVS_SAVE_INTERVAL/MINER_HISTORY_* 常量
- 迁移 monitor 线程：NTP/时间同步、每秒遥测+算力重算+效率、安全看门狗(温度降压/风扇/功率/算力→重启)、
  历史队列、NVS 持久化、force_config。UI 翻页/亮度由新 UI 框架负责，故略去
- application：新增 _tz/_pwr_tele，_begin_miners 启动 (monitor)

### P19 完成 (button 域)
- 新增 reflect_src/app/button_ctx.h：ButtonCtx（spec/init_evt/sys_evt/控制信号量/ota + UI 导航 hook 函数指针）
- 迁移 button 线程：长按 boot→force_config，长按 user→factory_reset；翻页经注入 hook（与 UI 框架解耦）

### P20 完成 (led 域)
- 新增 reflect_src/app/runtime_state.h：PreferenceState + OtaState；reflect_src/app/led_ctx.h：LedCtx
- 迁移 led 线程（NMAXE/Gamma 状态灯 + NMQAXE++ NeoPixel 灯效 + OTA 进度条）
- application：新增 _ota_progress/_pref，读 NVS 装载 preference，_begin_infra 启动 (led)

### P21 完成 (benchmark 域)
- runtime_state.h 补 BenchmarkState；新增 reflect_src/app/benchmark_ctx.h：BenchmarkCtx
- 迁移 benchmark 线程（freq/vcore 自动扫描，NVS 存稳定结果，逐轮重启，完成时择优写回 Normal 模式）
- application：新增 _bm，_begin_miners 启动 (benchmark)

### P22 完成 (webserver 域)
- 新增 reflect_src/web/web_ctx.h：WebCtx（miner/stratum/market/power 实例 + spec/status/conn/wifi/
  wifi_cfg/pwr/temp/time/ota/pref/neighbor/fan_status 状态结构 + bm_mode/utc/tz/coin_price/
  coin_watchlist/fw_version + sys_evt/init_evt/reboot_xsem/brightness_update_xsem）；模块级 g_web 单例
- 迁移 reflect_src/web/http_server.{h,cpp}（约 2000 行）：全部 g_board.X → g_web->...（系统化替换，
  保持逻辑/超时/PSRAM 分配/OTA 流程逐字一致）；recovery_page.h 逐字复制
- webserver 线程：set g_web、file_system_init、等 STA/AP 就绪、注册全部路由（含 recovery 模式分支、
  /probe、/alive、swarm/find、swarm/scan、wakeup、benchmark、setting、dashboard、ota、theme、
  coredump、reboot），webSocket 事件 + cleanupClients + OTA 失速看门狗；UI 活动（last_active_ms）
  改由新 UI 框架负责，仅保留 sys_evt 屏保唤醒清位
- platformio.ini：build_src_filter 增加 +<../reflect_src/web/>
- application：新增 _web_ctx，_begin_infra 启动 (webserver)（stack 1024*5，TASK_PRIORITY_WS，core 0）
- application 预备：合并 _ota_running/_ota_progress → OtaState _ota；新增 TimeState _time +
  _brightness_update_xsem；init() 读 NVS_CONFIG_TIME_FORMAT/DATE_FORMAT；mining_types.h 补
  miner_runtime_state_to_string()

### P23 完成 (显示 HAL + UI 框架接线)
- 新增 reflect_src/drivers/display/display_hal.{h,cpp}：从 legacy display.cpp 面板层移植（DI 化，
  无 g_board）：tft_init（上电/背光 PWM/TFT_eSPI begin 运行时引脚/rotation）、tft_bl_ctrl、
  ui_drv_register（PSRAM LVGL draw buffer + flush_cb）、tft_screen_width/height
- 编译 reflect_src/ui/（UIManager tileview + 8 页面 ×2 分辨率 + AppState 观察者模式）
  - 修复 codegen 残留：_W/_H 初始化行的 '.Replace(...)' 伪代码（12 个 layout 头）
  - 修复 base page .cpp include 路径；统一页面类名为 PascalCase（PageConfig/Dashboard/
    Hr_health/Clock/Market/Setting）以匹配 ui_manager.cpp
  - app_state.h：ObsLabel 增加 subscribe/unsubscribe 便捷重载
- application：_ui_init 改为真实 tft_init + ui_drv_register + UIManager::init + 背光；
  _lvgl_thread 调 UIManager::render_update()；栈 4K→8K
- platformio.ini：build_src_filter 增加 reflect_src/ui/ + reflect_src/drivers/display/
- AppState 已由 _tick_thread 以 1Hz 喂数：miner 页算力/diff/shares/IP/RSSI/swarm；loading 页进度

### P24 完成 (UI 导航接线)
- display_hal：touch_drv_register()——FT6206 触摸初始化（共享 I2C）+ LVGL 指针 indev，
  去抖 + flip 感知坐标映射（移植 legacy touchpad_read_cb），无控制器时优雅禁用
- application：_ui_init 调 touch_drv_register(&_pref)；button 导航 hook 接到 UIManager
  request_next_page/request_prev_page/wake_activity（无捕获 lambda→函数指针，跨线程安全）

构建均通过；Flash 随阶段增长至约 40.4%。

## 迁移结论
**God-object（board_sal_t g_board）→ 每线程 DI 上下文的迁移已完成。** reflect_src 中不再存在
g_board；所有后台线程（stratum/miner×4/wifi/config/power×2/fan/monitor/daemon/button/led/
market/scan/swarm/benchmark/webserver）均通过各自的 *Ctx 注入依赖。UI 框架已在真实 TFT 上点亮，
loading/miner 页有实时数据，触摸滑动 + 按键翻页可用。

## UI 页面数据接线（P25–P29，迁移完成后的功能补全）
全部 8 个页面已从静态占位补全为 AppState 观察者驱动的实时页面：
- **P25 clock**：本地时间 + 日期（按 _time.format 12/24h 与日期格式；NTP 同步后）
- **P26 market**：主币种 符号/价格/24h 涨跌（涨绿跌红）
- **P27 config**：AP 配网 SSID/IP + 配网倒计时（仅 force_config 时显示秒数）
- **P28 dashboard**：功率/Vbus/Ibus/ASIC 温/VRM 温/频率/Vcore 文本行
- **P28 hr_health**：3 分钟算力大字 + 效率(J/TH) + shares + best diff
- **P29 setting/swarm**：集群 workers/总算力/best diff + 邻居数 + 本机 IP
统一模式：AppState 增 *PageState（ObsLabel）；page base（ui/pages/*.{h,cpp}）订阅/退订；
layout（240x135 + 320x240）建 widget 并 _finish_create()；_tick_thread 以 1Hz 喂数。
lv_conf.h 启用 Montserrat 28/48 大字。构建通过，Flash 约 44.7%。

## Overlay / 屏保 / 亮度（P30–P33）
- **P30 亮度联动**：tick 线程消费 _brightness_update_xsem → tft_bl_ctrl(_pref.brightness)，
  web/UI 改亮度即时生效。**OverlayManager**（lv_layer_top 顶层面板，DI via OverlayCtx，
  自限频 4Hz）：按优先级显示 OC/OT 告警 > benchmark 扫描进度 > 挖矿暂停。
- **P31 屏保**：tick 线程自带状态机，独占背光——LVGL 空闲（触摸）超过 saver_timeout 熄屏，
  触摸/外部清位（web/按键）唤醒；wake_activity() 改为在 LVGL 线程 lv_disp_trig_activity()。
- **P32 find-me**：web /api/swarm/find 触发全屏黑白闪烁 ~6s 定位设备，自动清位。
- **P33 庆祝 overlay**：爆块 BLOCK FOUND!（金）/ 新高难度 NEW BEST!（青），按键消除。

最终构建通过，Flash≈44.8%，RAM≈21.1%。

## 总结：reflect 架构迁移 + UI 重建已达可用完整度
- god-object → DI 全后台域 + webserver 已迁移；reflect_src 无 g_board。
- UI：真实 TFT 点亮（实机验证）；8 页全部实时数据；触摸滑动 + 按键翻页；
  6 类 overlay（find-me/OC/OT/爆块/新高/benchmark/暂停）；屏保 + 亮度联动。

## 全量对比 src/ 后的补完（P34–P41）
对 `src/` 逐目录、逐线程、逐 NVS 键、逐事件位审计，确认无后台逻辑遗漏，并补完 UI 行为：
- **P34** OTA 升级进度 overlay（升级中保持亮屏）。
- **P35** aphorism 格言抓取线程（zenquotes.io，逐字移植，DI 化）。
- **P36** GIF 屏保（lv_gif + SPIFFS 'S' LVGL 文件系统驱动）+ 格言叠加；mode 1 仍熄屏。
- **P37** 自动翻页（screenAutoRoll，每 10s）。
- **P38** 触摸长按恢复出厂倒计时（NMQAxe++ 无实体 user 键，触摸长按是唯一路径）。
- **P39** 启动后 LOADING→MINER 自动切换（MINER_READY）。
- **P40** AP 配网模式自动切到 CONFIG 页 + loading 页「Connecting WiFi[ssid]」动画。
- **P41** miner 页数据补全（功率/温度/网络难度/版本/运行时长/时间/币价）。

### 审计结论（无遗漏）
- 线程：src 全部 *_thread_entry 在 reflect 均有对应（display/lvgl_tick/ui 三者合并为
  UIManager 渲染循环；config_monitor 由 wifi 线程动态拉起，已核对）。
- 数据模型：NVS 键、SYS/INIT 事件位 src→reflect 全覆盖（diff 为空）。
- 共享模块（board/miner/stratum/market/power_hal/fan/nvs/asic 等）行数与逻辑一致。
- OC/OT 故障位在 reflect power_loop 已置位，overlay 可正常触发。

### 仅剩纯视觉润色（非逻辑、需美术资源）
- 开机 logo 图、爆块/成就整屏位图、弧形仪表/直方图（dashboard/hr_health 现为文本行，
  数据已就绪）。这些不影响功能，依赖 src/image 位图资源，按需再做。

## UI 视觉还原（P42–P46，对照 src 真实坐标/底图/字体）
- **P42 资源管线**：从 src/image 复制全部底图位图（loading/config/mining/status/black/blockhit/
  achievement/logo），images.{h,cpp}（lv_img_dsc 描述符 + images_init 按分辨率设维度）；从
  src/drivers/displays 复制 ds_digib_font_*/Inconsolata_*/symbol_* 字体（fonts.h 声明）。UIManager
  瓦片网格与滑动方向改为与 legacy 完全一致的 2D 布局。
- **P43 loading 页**：底图 + version/details/进度条/slogan 精确 legacy 坐标（240x135 & 320x240）。
- **P44 miner 页**：mining 底图 + worker logo + ~21 元素精确坐标/字体/颜色/符号（ds_digib 数字字体、
  Inconsolata、symbol 图标）；MinerPageState 重定义为 legacy 字段集；NMQAxe++ 含 swarm 行 + UTC。
- **P45 底图铺设**：dashboard/hr_health 用 status 底图，config 用 config 底图；clock/market/setting 底图
  为黑。修正所有页面由竖屏 → 横屏实际帧（240x135 / 320x240）。
- **P46 clock 页**：黑底 + ds_digib 大字时间（135=ds_56，240=ds_120）+ 算力/爆块/币价精确坐标。

构建通过，Flash≈89.7%（大数字字体 ds_120 较大；legacy 同样内置全部字体，体量相当）。

### UI 还原状态
- **loading / miner / clock：逐像素对齐 legacy**（坐标/字体/颜色/符号/底图）。
- config / dashboard / hr_health / market / setting：**底图与帧方向已对齐**，元素为可用文本布局。
  这几页的仪表/图表/币种列表在 legacy 中大多**烘焙进底图位图**、动态覆盖元素较少；其精确覆盖坐标
  可对照各自底图继续细化（后续）。

## Latest Status - 2026-06-17

Build baseline saved for next power-on test.

What was completed in this round:
- loading page progress bar now animates smoothly in reflect, with moving percent text restored
- loading page `details`, IP, and pool labels were adjusted to behave closer to legacy scroll/layout timing
- loading page IP/pool live updates were moved onto the fast UI tick path so they no longer wait for the 1 Hz refresh
- tileview swipe destination logic was corrected to match legacy page selection more closely
- power OC/OT overlay actions were restored so the overlay is no longer display-only
- miner bring-up was corrected back to a 2-stage flow:
  1. `miner_count_thread_entry()` still counts ASICs after `INIT_EVENT_VDD_VPLL_READY`
  2. `miner_init_thread_entry()` now waits for `INIT_EVENT_ASIC_COUNTED | INIT_EVENT_VCORE_READY | INIT_EVENT_FAN_READY | INIT_EVENT_WIFI_STA_CONNECTED`

Why this ASIC change matters:
- this preserves the old USB-only detection window
- with DC/Vcore not yet enabled, the board should still be able to identify ASIC presence before full miner init begins

Build verification:
- `pio run -e reflect`
- result: `SUCCESS`

Hardware test focus for next session:
- USB-only power, no DC: verify ASIC count can still be detected before Vcore comes up
- confirm loading page progress, details, IP, and pool text all update/scroll as expected during boot
- verify page swipe behavior and overlay dismissal behavior on real hardware

Known limitation:
- no hardware validation was possible in this session; ASIC power-path behavior is restored by code audit and build verification only
