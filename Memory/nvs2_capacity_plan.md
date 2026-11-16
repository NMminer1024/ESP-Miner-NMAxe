# nvs2 扩容方案（benchmark 结果存储与主 NVS 分离）

状态：**已放弃（2026-11-16）**。改为在 24 KB 内瘦身，见
`Memory/nvs24_slimming_plan.md`。

放弃原因：OTA 不写分区表，纯网页 OTA 的客户拿不到 nvs2，收益只覆盖
愿意插一次 USB 的设备；同样的容量可以通过压缩现有占用获得，且 OTA 即生效。
下面的内容仅作为技术记录保留。

## 背景：24 KB 主 NVS 已到临界

真机 124.2 实测（`GET /api/system/nvs`）：

| 项目 | 值 |
|---|---|
| 分区 | 24 576 B = 6 页 × 126 entries = **756** |
| 已用 | 341–354（纯配置，0 条结果行） |
| 空闲 | 402–415 |
| 一条结果行 | 12 entries（~339 B），带 soak 判决 **14** |

NVS 回收要整页搬移，必须常留一个空闲页，因此实际可用上限是 `756 − 126 = 630`。
按 14 entries/行，剩余余量 `(402 − 126) / 14 ≈ 19 行`。

客户 overnight sweep 19 行就会把分区压满，随后开始驱逐。这不是 bug，是物理容量。

## 可用的空闲 flash

`partitions.csv` 末尾未分配区域：

```
coredump   0x0F92000  64K   end 0x0FA2000
（未分配）  0x0FA2000  0x5E000 = 376 KB   end 0x1000000  ← 正好到 16 MB
```

**注意 `factory` 不是空闲区**：`platformio.ini` 的 `upload_command` 把
`firmware.bin` 写到 `0x10000`，即 factory 分区；`ota_data_initial.bin` 全 0xFF，
bootloader 默认从 factory 启动。USB 刷写和变砖恢复都依赖它，不可回收。
（app 当前 2.95 MB，factory 给了 4 MB，内部约 1 MB 未用，但属于 app 分区内部，
不能分给 NVS，且每次 USB 刷写整体擦除。）

## 关键约束：OTA 不写分区表

`Update.begin()/Update.write()` 只写 `ota_0`/`ota_1`。分区表固定在 `0x8000`，
只有 bootloader 读取，**没有任何标准 OTA 路径能更新它**。

后果：

| 刷写方式 | 能否获得 nvs2 |
|---|---|
| 网页 / HTTP OTA（客户日常） | ❌ |
| `pio run -t upload`、串口/USB 全量刷写 | ✅ |

所以 nvs2 是「新出厂 + 愿意用一次 USB 的老设备」受益，纯 OTA 设备拿不到。

## 如果只做分区表而不做降级，会发生什么

必须避免的灾难路径：

1. `nvs_flash_init_partition("nvs2")` 在表里找不到 → `ESP_ERR_NOT_FOUND`(0x105)；
   `nvs_open_from_partition("nvs2", …)` → `ESP_ERR_NVS_PART_NOT_FOUND`(0x110F)。
2. 如果代码不判断返回值，所有 `bm_r_*` 读取失败 → `bm_result_read_all()` 返回空数组
   → 网页和执行路径都认为**没有任何结果**；
3. 所有写入失败 → 每轮 sweep 的 `Result saved` 都变成 NVS 错误，**结果静默丢失**；
4. 更糟：`bm_finish()` 找不到行 → 不 apply → 用户以为 benchmark 坏了。

结论：**只要引入 nvs2，就必须先有完整降级路径，否则等于把所有 OTA 用户的
benchmark 功能一次性打掉。**

## 必须遵守的三条设计约束

1. **降级必须是默认行为，不是异常处理**
   `nvs2` 不可用 → 完全退回当前主 NVS 行为，功能等价于 v3.2.02o。不允许
   「nvs2 缺失就禁用 benchmark」。
2. **分区表改动必须是纯追加**
   nvs2 放在 `coredump` 之后的尾部，**不移动任何已有分区偏移**。
   这样即使客户只刷了 `partitions.bin`、app 还是旧固件，设备照常启动，
   旧固件不认识 nvs2 就忽略它。这是整个方案安全的前提。
3. **绝不因为 nvs2 缺失而擦除或重建主 NVS**
   `nvs_flash_erase()` 会清掉 WiFi、pool、benchmark 结果。

## 一次性刷写的最小动作（重要）

**不需要全量刷机。** 由于分区表是纯追加、其他偏移不变，最小操作是：

```powershell
esptool.py --chip esp32s3 --port COM<x> write_flash 0x8000 partitions.bin
```

只写 12 KB 分区表区域，不碰：

- `nvs`（WireShift 配置 + 已有结果）→ 保留
- `factory` / `ota_0` / `ota_1`（当前运行镜像）→ 保留
- `www`（网页资源）→ 保留

风险提示：如果 `flash.nmminer.com` 之类的网页工具是**合并镜像从 0x0 整体烧写**，
它可能用 0xFF 填满 `0x9000–0xF000` 的 NVS 空档，从而**擦掉 WiFi 配置和已有
benchmark 结果**。必须确认该工具：
- 要么只写 `0x8000` 的分区表；
- 要么整体烧写时跳过后面的 NVS 区域。
这个外部依赖需要单独验证，不能假设。

## 提醒方案（分层，不打扰）

| 层 | 位置 | 内容 | 触发条件 |
|---|---|---|---|
| L1 启动日志 | 串口/WS | `[storage] nvs2 not found — benchmark results limited to ~19 rows; one USB flash at flash.nmminer.com adds ~200` | 每次开机一次（不是每次写） |
| L2 API | `/api/system/info` 增加 `storage` 块 | `{"resultStore":"nvs"\|"nvs2","layoutVersion":"v1"\|"v2","usbFlashRecommended":true}` | 常驻，供前端判断 |
| L3 API | `/api/system/nvs` 增加 nvs2 段 | 两个分区各自的 entries/rows 余量 | 常驻 |
| L4 网页 | Benchmark 页顶部提示条 | 「结果存储仅支持约 N 行。用 USB 在 flash.nmminer.com 刷一次可扩展到约 200 行」+ 链接 | 仅 `layoutVersion == v1` **且** 已有 ≥8 行 或 发生过驱逐；否则不显示，避免打扰 |
| L5 帮助 | Help「Benchmark」/「Update」 | 新增「为什么需要插一次 USB」条目，解释 OTA 无法改分区表 | 常驻 |
| L6 一次性 toast | Benchmark 首次点 Start | 若 v1 且行数接近上限，弹一次 | 用 NVS 标志去重，只弹一次 |

设计要点：
- 只在**真的有数据风险**时提醒（行数接近上限），新装用户不该看到警告；
- 文案必须给「不刷也能用」的信息，避免恐慌；
- 绝不阻塞挖矿或 benchmark 启动。

## 实现清单（若动手）

1. `partitions/partitions.csv` 追加（纯追加）：
   ```
   nvs2,  data, nvs,  0xFA2000,  0x18000   # 96 K
   ```
   96 K = 24 页 → 可用 `(24−1)×126 = 2898` entries → **约 207 行**（14/行）。
   128 K 则约 279 行；376 K 全用约 837 行。建议 96 K，剩余留给未来。

2. `nvs_config.cpp` 新增结果分区封装（**不要**改那 33 处 `nvs_open`）：
   - `bm_store_init()`：`esp_partition_find_first(DATA, NVS, "nvs2")`
     → 存在则 `nvs_flash_init_partition("nvs2")`
     → 首次（主 NVS 标志 `nvs2formatted` 未置）遇 `NO_FREE_PAGES`/`NEW_VERSION_FOUND`
       才 `nvs_flash_erase_partition("nvs2")` 再 init，置标志；
       已置标志后遇到同样的错**不擦**（否则会静默清空结果），改为标记不可用并告警
   - `bm_r_get_string/set_string/delete_key/get_u16/set_u16` 内部按 `s_bm_store_ok`
     选择 `nvs_open_from_partition("nvs2",…)` 或 `nvs_open(NVS_CONFIG_NAMESPACE,…)`
   - 只迁 benchmark 结果环 + 元数据（`bm_r_*`），配置仍留主 NVS

3. 首次迁移（`nvs2` 可用且 `storagemig` 未置）：
   逐条「从主 NVS 读 → 写 nvs2」，**先全部复制、置标志，再删除主 NVS 旧键**。
   删除中断只会留下副本，下次开机重试清理；反之若先删后写，中断就真丢数据。

4. `nvs_log_headroom()` 改成同时报两个分区，并把余量算式修正为
   `(free − 126) / 14`（当前是 `free / 14`，高估约 3 倍）。

5. `/api/system/nvs`、`/api/system/info` 增加字段；前端加 L4 提示条。

6. 文档：`docs/API.md`、`api-doc.html`、帮助页、`readme.md` 说明
   「需要一次 USB 刷写」。

## 容量对照

| nvs2 大小 | 页 | 可用 entries | 可存行（14/行） |
|---|---|---|---|
| 64 K | 16 | 1890 | ~135 |
| **96 K** | 24 | 2898 | **~207** |
| 128 K | 32 | 3906 | ~279 |
| 376 K 全用 | 94 | 11718 | ~837 |

## 风险与取舍

1. **纯 OTA 用户拿不到收益**。想让 OTA 也受益，只有两条路：
   - 行瘦身（~420 B → ~250 B，14 → 9 entries），20 行从 280 降到 180 entries。
     **OTA 即生效，无需分区表**，应优先做。
   - 自管裸 flash：用 `esp_flash_read/write()` 直接操作 `0xFA2000` 起的尾部区域，
     不依赖分区表，因此 OTA 也能用。代价是自己实现擦除/CRC/掉电安全/磨损，
     风险高于 NVS，属于第二阶段。
2. **新旧固件共存**。降级到不认 nvs2 的旧固件时，结果「不可见但不丢失」。
   需要文档说明。
3. **`flash.nmminer.com` 必须验证只写分区表**，否则会清掉用户配置与结果。

## 推荐顺序

1. **行瘦身**（OTA 生效，所有客户立即受益，风险最低）
2. **修正余量日志高估**（`free/14` → `(free−126)/14`）
3. **nvs2 纯追加 + 降级 + L1–L6 提醒**（新出厂 + 愿意插一次 USB 的客户）
4. （可选）自管裸 flash 尾部区域，让纯 OTA 客户也能扩容
