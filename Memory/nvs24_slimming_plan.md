# 24 KB NVS 瘦身方案（不扩容）

状态：**已实施并真机验证（2026-11-16，v3.2.02p，app sha 46bc4153d64637bd）**。

真机实测（Nexus 124.2）：

| 项目 | 变化 |
|---|---|
| `themecolors` 727 B | 已删除，改由预设名派生（省 24 entries） |
| `reboot` 命名空间 | 13 键/1286 B → 9 键/647 B（省 25 entries） |
| benchmark 结果行 | 261 B / 10 entries → **231 B / 9 entries** |
| 新增 `bm_expf` | u16，1 entry（一次性，用于派生 expHR） |

侧记：**只有 soak 后的行才是真实占用**。未 soak 行 231→，soak 加 25 B 后约 256 B，正好仍在 9 entries 内（边界紧，数值更长时可能回到 10）。


## 真机对账（124.2，v3.2.02o，0 条结果行）

`GET /api/system/nvs`：partition 24 576 B = 6 页 × 126 = **756 entries**，
`used_entries = 354`，`free_entries = 402`。

NVS entry 公式（已用真机数据验证）：

- 字符串/Blob 占 `N` 字节 → **1 个 header + ceil(N/32) 个 chunk**
- 回收需要整页搬移，**必须常留 1 个空闲页（126 entries）**
- 因此结果行实际可用上限 = `756 − 126 − 配置占用`，不是 756

逐项对账（与 354 完全吻合）：

| 项目 | 大小 | entries |
|---|---|---|
| `phy/cal_data` | 1 904 B blob | **61** |
| `phy/cal_mac` + `cal_version` | 6 + 4 B | 4 |
| `nvs.net80211/sta.apinfo` | 700 B blob | **23** |
| `nvs.net80211/ap.pmk_info` | 132 B | 6 |
| `ap.passwd` / `sta.pswd` | 65 B ×2 | 8 |
| `ap.ssid` / `sta.ssid` | 36 B ×2 | 6 |
| `reboot/r0..r9` | 128 B ×10 | **50** |
| `reboot/{head,count,bootidx}` | — | 3 |
| `main/topdiffsnap` | 750 B str | **25** |
| `main/themecolors` | 727 B str | **24** |
| `main/topdiffnet` | 134 B str | 6 |
| `main/topdiffs` + `topdiffts` | 127 + 110 B | 10 |
| 其余小键（含 bm_* 配置 13 个） | — | 128 |
| **合计** | | **354** |

## 当前容量结论

```
可用 630 − 配置 354 = 276 → 276 / 14 ≈ 19 行（含 soak 判决）
```

客户 19 行 sweep 正好压满。upsert 还要「旧+新」同时在，峰值再需 10–14，
实际没有余量 → 开始驱逐。与现场现象一致。

## 瘦身清单（按 ROI 排序，均可 OTA 生效）

### 1. 重启日志 ring 10 → 5　**−25 entries**

`REBOOT_LOG_RING_SIZE` 10 → 5。`r5..r9` 五个 128 B blob 正好 25 entries。

迁移要点（不能只改宏）：

- 改 `REBOOT_LOG_RING_SIZE` 会改变 `head % RING` 的映射，旧 `head/count`
  直接套用会把记录读错位。
- 必须先**全量读出**（最多 10 × 128 = 1 280 B，可放栈/临时堆），
  按 `boot_index` 降序取最新 5 条，写入 `r0..r4`，
  最后一次性写 `head=0, count=5`，再删 `r5..r9`。
- 顺序必须「先写记录 → 再写 head/count → 最后删旧槽」。
  中途掉电最坏留下重复记录，UI 按 `boot_index` 排序可自愈；
  反过来先删就会真丢历史。

可选进一步：`RebootRecord` 128 B → 64 B（`detail` 64→32、`fw_version` 24→12），
每条 5 → 3 entries。5 条时 28 → 18 entries（再省 10）。
但会破坏与旧记录的字节兼容，需要版本位（`reserved1` 可作版本号），
收益不大，建议只在需要极限瘦身时做。

### 2. `themecolors` 727 B → 删除　**−24 entries**

前端只有 **4 个命名预设**（`theme-config.component.ts`：Red / Blue / Green / Purple）
加 dark/light 两套 scheme，配色表完全由
`(themescheme, themename)` 决定，而这两个键**已经单独存储**
（5 B + 6 B，共 2 entries）。

所以 727 B 的 `"accentColors"` JSON 是**纯冗余**：

- `POST /api/theme` 不再写 `NVS_CONFIG_THEME_COLORS`
- `GET /api/theme` 不再从 NVS 读，返回预设名即可；前端用本地预设表渲染
- 启动时 `nvs_config_delete_key(NVS_CONFIG_THEME_COLORS)` 一次性回收
- 需前端小改：`app.layout.service.ts` 已能从 `accentColors` 应用 CSS 变量，
  改成按 `themename` 查本地预设即可

附带收益：该键在每次改主题时被整体重写，727 B → 24 entries 的**写放大**也一起消失。

### 3. `topdiffsnap` 750 B → SPIFFS 或只留 rank#1　**−20~24 entries**

`system_threads.cpp` 里，当 10 个 rank 的 pool/user 完全相同时会折叠成一对
（约 34 B），但多 pool 用户会展开到 750 B。124.2 配了两个 stratum，
所以是 750 B / 25 entries。

选项：

- **A（推荐）** 只持久化 rank#1 的 pool/user，其余 rank 的显示用「未知」，
  Top-10 榜单的难度值本身仍在 `topdiffs`（127 B）里，功能基本不损失。
- **B** 移到 SPIFFS（`www` 分区 3.5 MB，实际只用几百 KB）。
  但 SPIFFS 与 OTA 共用，需注意 `spiffs.bin` 上传会整体覆盖。
- **C** 保持现状，接受 25 entries。

### 4. benchmark 结果行瘦身　**−80 entries @20 行**

当前行格式（`benchmark_threads.cpp` 的 `snprintf`）：

```c
{"freq":325,"vcore":990,"expHR":2246.4,"avgHR":2240.6,"avgAsicTemp":45.2,
 "avgVcoreTemp":52.1,"effJTH":44.347,"avgPwr":99.35,"maxIout":8.35,
 "maxPwr":99.87,"maxAsicTemp":47.1,"maxVcoreTemp":54.2,"maxPhaseIout":2.78,
 "phases":3,"maxIbus":8.42,"maxPwrIn":100.12,"vFail":980,"vFailPct":96.42,
 "q":1,"fail":0,"fanAsic":68,"fanVcore":46,"fw":"v3.2.02o","ts":1794796400}
```

约 370 B → 13 entries；加 soak 三字段约 415 B → **14 entries**（与实测一致）。

可省的部分：

| 改动 | 省 |
|---|---|
| `expHR` 删（= `freq × smallCores × chips / 1000`，可推导） | ~12 B |
| `effJTH` 删（= `avgPwr / (avgHR/1000)`，可推导） | ~16 B |
| `phases` 删（板级常量，放全局/首次探测） | ~11 B |
| `fw` → 2 字节构建号（而非 `"v3.2.02o"`） | ~16 B |
| `q`/`fail`/`soak` 合并成一个位掩码字段 | ~18 B |
| 键名统一缩短（`freq`→`f`,`vcore`→`v`,`avgHR`→`h`…） | ~70 B |

合计约 143 B → 新行 ~230 B / 加 soak ~270 B
→ `1 + ceil(270/32) = 10 entries`（vs 14）。

20 行：280 → 200 entries，**−80**。这是最大的单项收益。

注意：键名是 API 契约，须同步改
`docs/API.md`、`api-doc.html`、前端 `benchmark.component.ts`、
以及 `bm_result_*` 的所有读取点（`nvs_config.cpp` 内多处 `strstr`）。
建议保留一个短期兼容读路径（读到旧长键也能显示）。

### 5. 顺带修正余量日志（bug，不是瘦身）　0 entries

`system_threads.cpp`：

```cpp
(unsigned)(s.free_entries / NVS_ENTRIES_PER_RESULT_ROW)
```

必须扣除 GC 保留页：

```cpp
const uint32_t usable = s.free_entries > 126 ? s.free_entries - 126 : 0;
(unsigned)(usable / NVS_ENTRIES_PER_RESULT_ROW)
```

否则客户看到「free 182 → 还能存 13 行」，实际只能存 4 行。

## 收益汇总

| 步骤 | 配置占用 | 14/行容量 | 10/行容量 |
|---|---|---|---|
| 现状 | 354 | **19 行** | 27 行 |
| +1 reboot 5 条 | 329 | 21 | 30 |
| +2 删 themecolors | 305 | 23 | 32 |
| +3 topdiffsnap 只留 #1 | 281 | 24 | 34 |
| **+4 行瘦身** | 281 | 24 | **34** |

即：配置侧省 73 entries（+27%），行侧省 4 entries/行（再 +42%）。
**合计 19 → 34 行（+79%）**，客户 19 行 sweep 可留 15 行余量。

## 不建议动的

| 项目 | entries | 原因 |
|---|---|---|
| `phy/cal_data` 1 904 B | 61 | ESP-IDF PHY 校准缓存。可设 `CONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGE=n` 关掉，省 61 entries，但每次开机重新校准（启动变慢、功耗略增），且属于 sdkconfig 改动，收益/风险不划算。仅作备选。 |
| `nvs.net80211/sta.apinfo` 700 B | 23 | WiFi 协议栈自己维护的连接缓存，不可控。 |
| `ap.passwd`/`sta.pswd`/`ap.pmk_info` | 18 | 安全相关，不碰。 |
| `main` 里的 bm_* 配置 13 键 | 13 | benchmark 参数，功能必需；可考虑并入一个 blob，收益 <10，暂不做。 |
| `factory` 分区 | — | **不是空闲区**：`upload_command` 把 `firmware.bin` 写到 `0x10000`（factory），`otadata` 全 0xFF 时设备默认从 factory 启动，USB 刷写与变砖恢复都依赖它。绝不可回收。 |

## 建议实施顺序

1. **修正余量日志公式**（零风险，先让诊断数字可信）
2. **删 `themecolors` 冗余**（省 24，纯收益，需前端小改）
3. **reboot ring 10 → 5**（省 25，需要一次性迁移逻辑 + 边界测试）
4. **`topdiffsnap` 只留 rank#1**（省 20，需确认 UI 影响）
5. **benchmark 行瘦身**（省 80，改动最大，但收益最高，须同步 API/前端/文档）

1–3 完成后即可从 19 行提升到 23 行，已经能让客户的 20 频点窗口 sweep 不再驱逐；
第 5 步做完才有真正的宽裕（34 行）。
