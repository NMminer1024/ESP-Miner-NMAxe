# NMAxe BSP 主导框架草案

## 1. 这次接手时已存在的内容

前一个 agent 已经做了两件事：

1. 新建了这份本地草稿 `NMAxe-bsp-framework-draft.md`
2. 在 `.gitignore` 中加入了该文件，避免误提交

我这次继续推进的重点不是再起一个新方向，而是把草稿收敛成一份更贴近你当前 `NMAxe` 代码现状、并且能直接指导后续重构拆分的文档。

---

## 2. 你的目标，用工程化语言重新表述

你希望 `NMAxe` 参考 `NMMiner` 的 `bsp` 组织方式，核心目标是：

1. 板型在编译期间唯一确定
2. 顶层业务不再直接碰板级宏、GPIO、屏幕/电源/风扇等硬件细节
3. 业务层只依赖驱动抽象接口和少量板级能力/策略接口
4. 板级差异收口在 `bsp` 层，而不是散落在 `app` / `thread` / `ui` / `web`
5. UI 不再把“板型”“分辨率”“页面差异”混成一层判断

这个方向是对的，而且和你当前 `platformio.ini` 的编译期选板方式是连续的，不是推倒重来。

---

## 3. 我对 NMMiner 框架的归纳

`NMMiner` 当前的 `src/bsp` 不是“大一统配置结构体”，而是：

1. 顶层统一入口是 `src/bsp/board.h`
2. 每个板型一个独立目录，例如 `src/bsp/esp32_c3_042_oled/`
3. 每个板型目录里通常至少有：
   - `config.h`
   - `<board>.cpp`
4. 板型实现类直接对外暴露稳定抽象：
   - `get_display()`
   - `get_touch()`
   - `get_boot_button()`
   - `get_user_button()`
   - `get_led()`
   - `get_uart()`
   - `get_profile()`
5. 业务层主要依赖 `Board::GetInstance()` 和 `BoardProfile`，而不是到处判断具体板型

这套模式的关键点不是“目录长得像 `bsp/xxx`”，而是：

1. 板型选择发生在编译期
2. 硬件组装在板型实现内部完成
3. 上层只看抽象能力，不看 wiring

---

## 4. NMAxe 当前现状

`NMAxe` 现在已经有“编译期选板”的基础，但还没有形成真正的 `bsp` 边界。

### 4.1 已经具备的部分

1. `platformio.ini` 中每个环境只定义一个 `BOARD_*` 宏
2. `src/board/nmaxe.h`
3. `src/board/nmaxegamma.h`
4. `src/board/nmqaxepp.h`

这些头文件已经在做一部分“板型专属工厂”工作，例如：

1. `create_asic_instance`
2. `create_power_instance`
3. `setup_temp_hal`

这说明当前项目已经不是完全无序状态，已经有一半 `bsp` 雏形。

### 4.2 现在真正的问题

当前板级信息仍然集中在：

1. `src/board/board.h`
2. `src/board/board.cpp`

其中 `BoardSpecConfig` 同时承担了过多职责：

1. 硬件 wiring
2. 运行时默认频压
3. UI 展示量程
4. 风扇控制参数
5. 板型命名与展示信息
6. 驱动工厂函数
7. 偏好默认值

这导致 `board.cpp` 已经不是“板描述”，而是“全系统板级配置总线”。

---

## 5. 当前强耦合点，后续拆分时必须正视

我检索了 `NMAxe` 当前对 `BoardSpecConfig` 和 `BOARD_*` 的依赖，主要耦合点如下。

### 5.1 配置结构体横向穿透太多模块

当前直接依赖 `BoardSpecConfig` 的模块包括：

1. `src/app/application.h`
2. `src/mining/mining_types.h`
3. `src/app/button_ctx.h`
4. `src/app/led_ctx.h`
5. `src/drivers/fan/fan_ctx.h`
6. `src/drivers/power/power_ctx.h`
7. `src/drivers/display/display_hal.h`
8. `src/web/web_ctx.h`
9. `src/ui/overlay_manager.h`

这说明板级配置不只是“被 app 用一下”，而是已经穿透到多个子域。

### 5.2 业务和驱动里还存在按板型宏分支

除了 `board.cpp` 之外，目前还有不少地方直接写：

1. `#if defined(BOARD_NMAXE) ...`
2. `#elif defined(BOARD_NMQAXE_PP...) ...`

重点位置包括：

1. `src/drivers/display/display_hal.cpp`
2. `src/app/application.cpp`
3. `src/mining/miner.cpp`
4. `src/thread/thread_entry.cpp`
5. `src/ui/overlay_manager.cpp`
6. `src/ui/pages/page_clock.cpp`
7. `src/ui/pages/page_hr_health.cpp`
8. `src/ui/pages/page_miner.cpp`
9. `src/web/http_server.cpp`

这类分支不全部消掉，`bsp` 边界就立不住。

### 5.3 display / touch 层已经暴露出“抽象不够”的问题

当前 `src/drivers/display/display_hal.cpp` 实际上混了四层职责：

1. `TFT_eSPI` 面板构造
2. 板型背光极性与默认旋转
3. LVGL display flush 注册
4. FT6206 touch 初始化与 LVGL indev 注册

再加上当前 `src/drivers/touch/ft6206.cpp` 直接绑定全局 `i2c_master_*`，说明现在的问题不只是“有几个板型判断”，而是下面几层混在了一起：

1. 芯片级驱动
2. 板型装配
3. LVGL 适配

最终目标应改成：

1. `src/drivers/display/*` 只保留公共显示抽象和可复用的控制器直驱实现
2. `src/drivers/touch/*` 只保留公共触摸抽象和可复用的控制器驱动实现
3. `src/bsp/<board>/board.cpp` 像 `NMMiner` 一样直接实例化并缓存 `Display` / `Touch`
4. LVGL 侧只吃 `Display` / `Touch` 抽象接口，不再知道 `BOARD_*`、`TFT_eSPI`、`FT6206`

---

## 6. 你提的两个扩展场景，反过来约束框架

你提的两个场景很关键，它们说明框架不能只有“BSP 一层”，还必须把 UI 再拆开。

### 6.1 场景一：新增一个板子

例如后面新增：

1. 单颗 `BM1373`
2. `240x135` TFT 触摸屏
3. 单风扇

这类变化的本质是：

1. 板型变了
2. ASIC family 和 ASIC count 变了
3. 输入模式从 button-only 变成了 touch 或 hybrid
4. 分辨率可能和旧板相同，但 UI 行为不一定相同

所以框架不能把：

1. `BM1373`
2. `240x135`
3. `有触摸`
4. `单 ASIC`

混成一个隐式板型判断。

### 6.2 场景二：新增 `480x320` 分辨率，某一页内容不同

这类变化的本质不是“新增板型”，而是：

1. 新增了一个 UI layout 维度
2. 某一页内容和其他分辨率不一样
3. 甚至未来可能同样是 `480x320`，不同产品的某一页还会不一样

所以 UI 不能只有：

1. `board -> page`

还必须拆成：

1. `layout -> resolution/layout choice`
2. `variant/profile -> page content difference`

### 6.3 结论

只做 `BSP` 层还不够，必须明确成三层：

1. `BSP`
2. `UI Layout`
3. `UI Profile / Variant`

---

## 7. 修正后的总体模型

新的模型建议明确成下面三层。

### 7.1 BSP 层

负责：

1. 编译期选板
2. 硬件 wiring
3. 驱动装配
4. 板级静态 traits
5. 板级默认行为 policy
6. 板型实例直接暴露 `Display` / `Touch` / `Power` / `ASIC` 等抽象对象

它回答的问题是：

1. 这是什么板
2. 这块板有哪些硬件
3. 这些硬件怎么连
4. 该实例化哪些驱动
5. 上层应该拿哪个 display / touch 实例去接 LVGL

### 7.2 UI Layout 层

负责：

1. 按分辨率和输入密度决定页面布局
2. 决定这个屏幕尺寸下每一页怎么排版
3. 决定哪些页面适合该分辨率

它回答的问题是：

1. `240x135` 怎么排
2. `320x240` 怎么排
3. `480x320` 怎么排

### 7.3 UI Profile / Variant 层

负责：

1. 同分辨率下不同产品的页面差异
2. 同 layout 下某一页的特化内容
3. 输入模式差异，例如 button / touch / hybrid
4. 某些页面默认开关和能力暴露

它回答的问题是：

1. 同样是 `240x135`，单 ASIC 触摸板和普通按键板是不是同一套页面
2. 同样是 `480x320`，某一页是不是要换展示内容
3. 某产品是否需要“触摸优先”的页面交互

### 7.4 公共芯片驱动层

这层不属于产品逻辑，但在工程落地时必须明确存在。

它负责：

1. 可复用的显示控制器驱动，例如 `ILI9341`、`ST7789`、`SSD1306`
2. 可复用的触摸控制器驱动，例如 `FT6206/FT6x36`、`XPT2046`、`GT911`
3. 总线级实现，例如 `SPI`、`I2C`、并口、软 `I2C`
4. 控制器初始化命令表、寄存器写序列、旋转寄存器规则

它不回答的问题是：

1. 这是什么板
2. 这个触摸控制器接在哪个 GPIO
3. 这个板默认旋转是多少
4. 这个板有没有触摸

这些依然属于 `BSP` 装配层。

---

## 8. 面向 NMAxe 的修正后目录建议

这是更贴近你当前仓库、同时能覆盖上面两个场景的建议结构。

```text
src/
  app/
  bsp/
    board_select.h
    board_registry.h
    board_compat.h
    shared/
      board_caps.h
      board_traits.h
      board_context.h
      board_interfaces.h
      board_policies.h
      board_init.h
      display_profile.h
      thermal_profile.h
      mining_profile.h
      input_profile.h
    nmaxe/
      board.h
      board.cpp
      wiring.h
      traits.h
      policy.h
      policy.cpp
      assembly.h
      assembly.cpp
      compat.cpp
    nmaxe_gamma/
      board.h
      board.cpp
      wiring.h
      traits.h
      policy.h
      policy.cpp
      assembly.h
      assembly.cpp
      compat.cpp
    nmqaxepp/
      rev60/
        board.h
        board.cpp
        wiring.h
        traits.h
        policy.h
        policy.cpp
        assembly.h
        assembly.cpp
        compat.cpp
      rev61/
        board.h
        board.cpp
        wiring.h
        traits.h
        policy.h
        policy.cpp
        assembly.h
        assembly.cpp
        compat.cpp
      rev81/
        board.h
        board.cpp
        wiring.h
        traits.h
        policy.h
        policy.cpp
        assembly.h
        assembly.cpp
        compat.cpp
    new_bm1373_touch_240x135/
      board.h
      board.cpp
      wiring.h
      traits.h
      policy.h
      policy.cpp
      assembly.h
      assembly.cpp
      compat.cpp
  product/
    ui_profiles/
      ui_profile.h
      ui_profile_registry.h
      page_variant.h
      profile_default.cpp
      profile_touch_single_asic.cpp
      profile_480x320_rich.cpp
  board/
    board.h
    board.cpp
    nmaxe.h
    nmaxegamma.h
    nmqaxepp.h
  drivers/
    asic/
    display/
      display.h
      spi_port.cpp
      spi_port.h
      iic_port.cpp
      iic_port.h
      vendors/
        ili9341_init.h
        st7789_init.h
        ssd1306_init.h
    touch/
      touch.h
      ft62xx_iic.cpp
      ft62xx_iic.h
      xpt2046_spi.cpp
      xpt2046_spi.h
      gt911_iic.cpp
      gt911_iic.h
    temp/
    power/
    fan/
    extio/
    iic/
  market/
  mining/
  net/
  nvs/
  stratum/
  thread/
  ui/
    pages/
    port/
      lvgl_display_port.cpp
      lvgl_input_port.cpp
    layouts/
      layout_240x135/
      layout_320x240/
      layout_480x320/
    page_variants/
      default/
      touch_single_asic/
      rich_dashboard/
  utils/
  web/
```

注意这里我仍然故意保留了 `src/board/`。

原因不是它应该长期存在，而是第一阶段需要一个兼容层，避免一上来改爆所有依赖点。

---

## 9. 每层职责定义

### 9.1 `bsp/board_select.h`

唯一编译期入口。

职责只做三件事：

1. 校验只选中了一个 BSP
2. 选择当前 active board
3. 导出统一工厂入口

示意：

```cpp
#if defined(BOARD_NMAXE)
  #include "bsp/nmaxe/board.h"
#elif defined(BOARD_NMAXE_GAMMA)
  #include "bsp/nmaxe_gamma/board.h"
#elif defined(BOARD_NMQAXE_PP)
  #include "bsp/nmqaxepp/rev60/board.h"
#elif defined(BOARD_NMQAXE_PP_REV61)
  #include "bsp/nmqaxepp/rev61/board.h"
#elif defined(BOARD_NMQAXE_PP_REV81)
  #include "bsp/nmqaxepp/rev81/board.h"
#elif defined(BOARD_NEW_BM1373_TOUCH_240X135)
  #include "bsp/new_bm1373_touch_240x135/board.h"
#else
  #error "No BSP selected"
#endif
```

### 9.2 `bsp/shared/board_interfaces.h`

定义上层允许依赖的驱动接口集合。

这里必须把 `touch/input` 提升为一等公民，而不是 UI 自己再猜。

示意：

```cpp
struct BoardDrivers {
    BMxxx* asic;
    AxePowerHal* power;
    ITempHal* temp;
    Display* display;
    Touch* touch;
    IButtonHal* buttons;
    ILedHal* leds;
    std::vector<IFanHal*> fans;
};
```

这里只暴露“能力对象”，不暴露 pin。

### 9.2.1 `board.cpp` 中显示 / 触摸的最终形式

这里应明确参考 `NMMiner` 的做法，而不是继续走“一个全局 `display_hal.cpp` 自己判断板型”的路。

目标形态是：

1. 某个具体 `bsp/<board>/board.cpp` 里直接 new 具体显示驱动
2. 显示驱动本身是寄存器直驱或明确的控制器级实现，不依赖 `TFT_eSPI` 这种板外大封装
3. 板型实例缓存 `Display*`
4. 板型实例按需缓存 `Touch*`
5. LVGL 只通过 `Display` / `Touch` 抽象做 flush 和 read

示意：

```cpp
class BSPBoard {
public:
    Display* get_display() {
        if (_display) return _display;
        _display = new SPIDisplay(&_spi,
                                  TFT_CS_PIN,
                                  TFT_DC_PIN,
                                  TFT_RST_PIN,
                                  TFT_BL_PIN,
                                  320, 240);
        _display->load_vendor_config(ili9341_vendor_cfg);
        _display->reset();
        _display->init();
        return _display;
    }

    Touch* get_touch() {
        if (!BOARD_HAS_TOUCH) return nullptr;
        if (_touch) return _touch;
        _touch = new FT62xxTouch(&_wire,
                                 TOUCH_I2C_ADDR,
                                 TOUCH_SDA_PIN,
                                 TOUCH_SCL_PIN,
                                 TOUCH_IRQ_PIN,
                                 TOUCH_RST_PIN,
                                 320, 240);
        _touch->load_config(ft62xx_touch_cfg);
        _touch->reset();
        _touch->init();
        return _touch;
    }
};
```

也就是说，后面真正给 LVGL 推显示的，不该再是“`BoardSpecConfig + display_hal.cpp`”，而应该是“当前板型实例暴露出来的 `Display` 对象”。

### 9.3 `bsp/shared/board_traits.h`

放静态元信息，不放 wiring，不放可变状态。

适合放：

1. board name
2. display name
3. `asic_family`
4. `asic_count`
5. `fan_count`
6. `has_touch`
7. `has_led`
8. `screen_width`
9. `screen_height`
10. capability mask
11. board revision

注意这里必须把：

1. `asic_family`
2. `asic_count`

拆开，不能再让 `BM1373` 隐式等于 “4 颗芯片 QAxe++ Rev8.1”。

### 9.4 `bsp/shared/board_policies.h`

放真正的板级默认行为策略。

适合承载：

1. 默认频率/电压
2. 允许的 OC/VC 范围
3. 温控阈值
4. 电源阈值
5. 默认背光极性
6. 默认旋转
7. 默认偏好值

这里不要再塞“某一页内容怎么画”这种产品/UI 差异。

### 9.4.1 `bsp/shared/display_profile.h`

这一层是为了解决“同分辨率不代表同控制器”的问题。

至少建议包含：

1. `width`
2. `height`
3. `controller_id`
4. `bus_type`
5. `color_invert`
6. `rgb_order`
7. `swap_bytes`
8. `default_rotation`
9. `backlight_active_high`
10. `shared_bus_with_touch`

这样：

1. `320x240 + ST7789`
2. `320x240 + ILI9341`

就不会再被错误地当成“同一种屏”。

### 9.4.2 `bsp/shared/thermal_profile.h`

这一层是为了解决“温度来源会变，但上层不应跟着改”的问题。

至少建议包含：

1. `vcore_sensor_id`
2. `asic_sensor_id`
3. `sensor_bus_type`
4. `sample_policy`
5. `aggregation_policy`
6. `fault_value_policy`

这样后面无论是：

1. 板载 power 芯片内置温度
2. `TMP102`
3. `DS1802` 或后续替换的其他温度器件

都由 BSP 负责绑定到统一温度入口，上层继续只调 `temp_hal_get_vcore()` / `temp_hal_get_asic()`。

### 9.4.3 `bsp/shared/mining_profile.h`

这一层是为了解决“ASIC 家族变化”和“链路规模变化”。

至少建议包含：

1. `asic_family`
2. `asic_count`
3. `chain_topology`
4. `default_freq`
5. `default_vcore`
6. `job_interval_ms`
7. `expected_hashrate_range`

这样：

1. `1 x BM1373`
2. `4 x BM1373`
3. `8 x BM1378`

都不会再被混成一个板型宏判断。

### 9.5 `product/ui_profiles/*`

这是这次修正后新增的关键层。

它负责：

1. 为每个产品或板型绑定一个 `UiProfileId`
2. 定义 layout 选择
3. 定义 page variant 选择
4. 定义输入模式，例如 button / touch / hybrid
5. 定义某些页面是否显示精简版或增强版

示意：

```cpp
enum class UiLayoutId : uint8_t {
    Layout240x135,
    Layout320x240,
    Layout480x320,
};

enum class UiVariantId : uint8_t {
    Default,
    TouchSingleAsic,
    RichDashboard,
};

enum class UiInputMode : uint8_t {
    ButtonOnly,
    TouchOnly,
    Hybrid,
};

struct UiProfile {
    UiLayoutId layout_id;
    UiVariantId variant_id;
    UiInputMode input_mode;
};
```

### 9.6 `ui/layouts/*`

这里只按分辨率组织布局。

例如：

1. `layout_240x135`
2. `layout_320x240`
3. `layout_480x320`

它解决的是“同一页在不同分辨率怎么排版”，而不是“不同产品的页面内容有什么差异”。

### 9.7 `ui/page_variants/*`

这里只放页面差异版本。

例如：

1. `default`
2. `touch_single_asic`
3. `rich_dashboard`

它解决的是：

1. 同样是 `240x135`，单 ASIC 触摸板的 dashboard 是否和老板型不同
2. 同样是 `480x320`，某一页内容是否和其他产品不同

### 9.8 `bsp/<board>/wiring.h`

严格只放硬件连接信息：

1. GPIO
2. I2C/SPI/UART 绑定
3. 背光 pin
4. 电源 rail enable pin
5. ADC pin
6. 风扇 PWM/tach pin
7. 触摸中断 pin
8. 扩展 IO reset pin

禁止上层直接 include 这个文件。

### 9.9 `bsp/<board>/assembly.*`

这里是每个板型真正的装配中心。

它负责：

1. 选择具体 ASIC 驱动
2. 选择具体电源驱动
3. 选择具体 display / touch / fan 实现
4. 组装 `BoardContext`
5. 绑定默认 `UiProfileId`
6. 执行板级 pre-init

它本质上是对当前这些逻辑的拆分承接：

1. `create_asic_instance`
2. `create_power_instance`
3. `setup_temp_hal`
4. `hardware_pre_init`

### 9.10 `bsp/<board>/compat.cpp`

这是第一阶段很重要但容易被忽略的层。

它的作用是：

1. 从新的 `traits + policy + wiring + assembly + ui_profile_binding` 重新组装出旧 `BoardSpecConfig`
2. 让旧代码先继续跑
3. 反过来阻止再往旧结构里塞新字段

也就是说，第一阶段不是立即删除 `BoardSpecConfig`，而是把它降级为“兼容输出”。

---

## 10. 选择关系应该是什么样

后续正确的选择链路应该是：

```text
platformio env
    ->
BOARD_* macro
    ->
active BSP
    ->
BoardTraits + BoardPolicies + BoardDrivers
    ->
UiProfileId
    ->
UiLayoutId + UiVariantId + UiInputMode
    ->
具体页面与交互实现
```

这个链路里：

1. `BSP` 决定硬件
2. `UiProfile` 决定产品体验
3. `Layout` 决定分辨率布局
4. `Variant` 决定页面差异

不要再直接从页面里反向判断 `BOARD_*`。

---

## 11. `BoardSpecConfig` 应该怎么拆

这里直接按你当前字段来拆，不空谈。

### 11.1 留在 BSP 私有 wiring 层

这些字段应该下沉到 `wiring.h` 或装配内部：

1. `tft.dc_pin`
2. `tft.rst_pin`
3. `tft.pwr_pin`
4. `tft.bl.*`
5. `spi.*`
6. `iic.*`
7. `btn.*`
8. `led.*`
9. `pwr.en_pins`
10. `pwr.adc_pins`
11. `pwr.vcore_regulator_pin`
12. `pwr.pgood_pin`
13. `pwr.dc_plug_pin`
14. `asic.rx_pin`
15. `asic.tx_pin`
16. `asic.rst_pin`
17. `asic.com_port`
18. `fans[].init.*`

### 11.2 迁入 traits / info provider

这些是板信息，不是 wiring：

1. `name`
2. `display_name`
3. `asic.name`
4. `asic.num_req`
5. `tft.width`
6. `tft.height`

另外建议补这些 traits：

1. `asic_family`
2. `asic_count`
3. `fan_count`
4. `has_touch`
5. `has_led`
6. `input_capability`

### 11.3 迁入 board policy

这些属于板级行为策略：

1. `asic.default_frq`
2. `asic.default_vcore`
3. `asic.min_vcore`
4. `asic.max_vcore`
5. `asic.temp_limit.*`
6. `pwr.temp_limit.*`
7. `pwr.vbus_min_required`
8. `pwr.power_low_threshold`
9. `preference.screen.*`
10. `preference.led.*`

### 11.4 迁入 UI Profile / Variant

这些不该再放进板级 policy，而应该迁入 `UiProfile` 体系：

1. `ui.hashrate_dist_page.*`
2. `ui.dashboard_page.*`
3. `ui.setting_page.oc`
4. `ui.setting_page.vc`

原因是：

1. 它们更多描述“产品页面怎么展示”
2. 不只是硬件参数
3. 同分辨率、同板型未来也可能分裂出不同页面版本

### 11.5 留在 assembly / factory

这些不是配置数据，而是装配逻辑：

1. `create_asic_instance`
2. `create_power_instance`
3. `setup_temp_hal`

它们应该从结构体字段变成 board assembly 的实现细节。

---

## 12. 三个场景在新框架里如何落地

### 12.1 新增板子：单颗 `BM1373` + `240x135` 触摸屏 + 单风扇

在新框架里应这么落：

1. 新建 `src/bsp/new_bm1373_touch_240x135/`
2. `traits.h` 里写：
   - `asic_family = BM1373`
   - `asic_count = 1`
   - `fan_count = 1`
   - `has_touch = true`
   - `screen_width = 240`
   - `screen_height = 135`
3. `assembly.cpp` 里复用：
   - `bm1373`
   - 对应的 power 驱动
   - 对应 display 驱动
   - 对应 touch 驱动
4. `policy.cpp` 里写默认频压和温控
5. 绑定 `UiProfileId::TouchSingleAsic240x135`

注意这里 `240x135` 只是 layout 信息，不等于“复用 NMAxe 的整套 UI 页面定义”。

### 12.2 新增 `480x320` 分辨率，某一页内容与其他分辨率不同

在新框架里应这么落：

1. 新建 `src/ui/layouts/layout_480x320/`
2. 针对该分辨率实现对应 page 布局
3. 如果只是分辨率差异，停在 layout 层就够了
4. 如果同样 `480x320` 下某产品该页还要特化，再在 `ui/page_variants/` 放差异实现
5. 最终由 `UiProfileId` 选择：
   - 用哪个 layout
   - 用哪个 page variant

这就避免了把“分辨率差异”和“产品差异”混成一个判断。

### 12.3 新增板子：`8 x BM1378` + `320x240` + `ILI9341` + 无触摸 + 新温度传感器

这个场景正好能检验框架是否真的可扩展。

在新框架里应这么落：

1. 新建 `src/bsp/new_bm1378_8x_320x240_ili9341/`
2. `traits.h` 里写：
   - `asic_family = BM1378`
   - `asic_count = 8`
   - `fan_count = ...`
   - `has_touch = false`
   - `screen_width = 320`
   - `screen_height = 240`
3. `display_profile.h` 绑定：
   - `controller_id = ILI9341`
   - `bus_type = SPI`
   - `default_rotation = ...`
   - `backlight_active_high = ...`
4. `thermal_profile.h` 绑定新的温度来源：
   - `vcore_sensor_id = DS1802`
   - `asic_sensor_id = DS1802`
5. `assembly.cpp` 中：
   - new `BM1378` 驱动实例
   - new `ILI9341` 显示实例
   - 不创建 touch，`get_touch()` 直接返回 `nullptr`
   - 绑定新的 power/temp 适配
6. UI 层继续复用 `layout_320x240`
7. 如果这个 `320x240` 产品某一页还和旧产品不同，再额外绑定新的 `UiVariantId`

这个例子说明：

1. “同为 `320x240`” 不代表屏幕驱动相同
2. “无触摸” 不应该让 UI/线程层到处加 `#if BOARD_*`
3. “8 x BM1378” 只应该影响 `MiningProfile/BSP`，不应污染页面逻辑
4. “温度传感器变化” 应由 `ThermalProfile + setup_temp_hal` 收口

---

## 13. 第一阶段目录方案

第一阶段目标不是“重构完成”，而是“把板级边界和 UI 选择链先立起来，业务行为尽量不变”。

### 13.1 第一阶段应该完成的事情

1. 新建 `src/bsp/shared`
2. 新建每个板型目录
3. 新建 `product/ui_profiles/`
4. 新建 `ui/layouts/layout_480x320/` 作为后续扩展位
5. 把 `src/board/board.cpp` 按板型拆成多个 `bsp/<board>/compat.cpp`
6. 新建统一编译期入口 `bsp/board_select.h`
7. 给每个 BSP 增加 `UiProfileId` 绑定
8. 把当前 `display_hal.cpp` 拆成：
   - 公共 `Display/Touch` 抽象与控制器驱动
   - `ui/port/lvgl_*` 桥接层
   - `bsp/<board>/board.cpp` 实例装配

### 13.2 第一阶段不要急着做的事情

1. 不要立刻删除 `BoardSpecConfig`
2. 不要立刻重写所有线程上下文
3. 不要一开始就把所有 `BOARD_*` 宏判断扫干净
4. 不要先碰所有 UI 页面逻辑细节

第一阶段真正重点是：

1. 让板级代码从单一巨型文件变成每板独立目录
2. 让 UI 选择从“直接看板型”过渡到“看 `UiProfileId`”
3. 让新增板型以后不需要继续往一个 `board.cpp` 里塞 `#if`

---

## 14. 第二阶段接口收口方案

第二阶段才开始让上层脱离 `BoardSpecConfig`。

建议按下面顺序收。

### 14.1 先收 display / touch

最先改：

1. 把当前 `display_hal.cpp` 里的面板构造逻辑移出，放进具体 `bsp/<board>/board.cpp`
2. 把当前 `display_hal.cpp` 里的背光极性判断移到 `DisplayProfile`
3. 把当前 `display_hal.cpp` 里的默认旋转判断移到 `DisplayProfile`
4. 把当前 `touch_drv_register()` 的控制器创建逻辑移到具体 BSP
5. 把当前 `src/drivers/touch/ft6206.cpp` 改成总线注入式公共驱动，不再直接绑全局 `i2c_master`

改完后的边界应该是：

1. `drivers/display/*` 只负责“这个控制器怎么写寄存器”
2. `drivers/touch/*` 只负责“这个控制器怎么读坐标”
3. `bsp/<board>/board.cpp` 负责“这块板到底有没有这个器件，以及它接在哪”
4. `ui/port/lvgl_*` 只负责把 `Display/Touch` 抽象接到 LVGL

### 14.2 再收 button / led / fan / power ctx

当前这些 ctx 直接挂 `BoardSpecConfig*`：

1. `ButtonCtx`
2. `LedCtx`
3. `FanCtx`
4. `PowerCtx`

第二阶段应改为只注入它们真正需要的更小对象，比如：

1. `ButtonPins`
2. `LedProfile`
3. `FanProfileList`
4. `PowerPolicy`

### 14.3 再收 mining / web / ui

最后才处理：

1. `MinerCtx`
2. `WebCtx`
3. `UI` 页面逻辑

因为这些模块读取字段更散，适合在板级目录和 `UiProfile` 体系稳定后再做。

---

## 15. 第三阶段目标状态

第三阶段完成后，项目结构应该接近这种依赖关系：

```text
app / thread / mining / ui / web
        |
        v
platform context / board traits / board policies / ui profiles
        |
        v
bsp shared interfaces
        |
        v
board-specific bsp assembly
        |
        v
drivers/*
        |
        v
esp-idf / arduino / soc
```

禁止反向依赖：

1. `ui -> bsp/<board>/wiring.h`
2. `ui/page -> BOARD_*`
3. `thread -> BOARD_*`
4. `drivers/display -> BOARD_*`
5. `drivers/touch -> BOARD_*`
6. `web -> board.config.xxx`

---

## 16. 我认为最适合 NMAxe 的实际落地版本

### 16.1 屏幕驱动和触摸驱动到底该放哪里

这个问题要定规则，否则后面目录会再次长乱。

推荐规则如下：

1. 可复用的“芯片级驱动”统一放公共 `src/drivers/display/`、`src/drivers/touch/`
2. 板级 wiring、GPIO、IRQ/RST、I2C 地址、SPI 片选、默认旋转、触摸坐标映射、校准值，放 `src/bsp/<board>/`
3. 如果某个驱动实现明显只服务一个板，而且里面混了该板独有的扩展 IO/复位时序/电源联动，可以先放 `src/bsp/<board>/private/`
4. 但一旦第二块板也要复用它，就应立即提炼回公共 `drivers`

对应到你现在的项目：

1. `touch/ft6206` 这类文件更适合放公共 `src/drivers/touch/`
2. 但它不该继续直接依赖全局 `i2c_master_*`
3. 它应改成像 `NMMiner` 的 `IICTouch` 一样，由 BSP 注入总线对象、地址、IRQ、RST、坐标映射
4. 屏幕驱动同理：`ILI9341` / `ST7789` 这类控制器驱动应放公共 `src/drivers/display/`
5. 真正决定“这个板用 ILI9341 还是 ST7789、是否有触摸、默认转向是多少”的，是 `bsp/<board>/board.cpp`

### 16.2 关于你刚提的“显示驱动要像 NMMiner 一样由 BSP 直接暴露”

这一点我认同，而且我认为应该直接写进框架约束里：

1. 顶层 UI/LVGL 不应该自己 new 显示驱动
2. 具体 `bsp.cpp` 应该直接暴露 `get_display()`
3. 如果有触摸，具体 `bsp.cpp` 也直接暴露 `get_touch()`
4. `lvgl_display_port.cpp` 只向 `Display` 抽象写像素
5. `lvgl_input_port.cpp` 只向 `Touch` 抽象读点位

也就是说，后面 `NMAxe` 的演进方向应当是：

`board selected at compile time -> bsp instance owns display/touch -> lvgl port consumes abstract display/touch`

而不是：

`ui thread -> display_hal.cpp -> 根据 BoardSpecConfig 和 BOARD_* 自己拼显示/触摸逻辑`

结合你当前代码，我建议不是直接照抄 `NMMiner` 的 `Board` 单例类模式，而是走“过渡式 BSP 主导 + UI 双层选择”：

1. 保留你现有 `AxePowerHal`、`BMxxx`、`fan_config_t` 等驱动体系
2. 不强行把所有东西改成 `NMMiner` 那种 `Board` 单例类
3. 借鉴的是它的目录边界和编译期选板方式
4. `NMAxe` 继续以 `Application + ctx 注入` 为主
5. 但 ctx 不再直接吃巨型 `BoardSpecConfig`
6. UI 选择不再直接绑定 board，而是走 `UiProfileId -> Layout + Variant`

这是我认为成本最低、且和你现有代码连续性最好的方式。

---

## 17. 一句话结论

`NMAxe` 的新框架不应只是把 `src/board` 改名为 `src/bsp`，而应改成：

“每个板型一个独立 `bsp` 装配目录，板级 wiring 只存在于 `bsp` 私有层；UI 再拆成 `Layout` 与 `Profile/Variant` 两层；上层模块只依赖驱动抽象、能力描述、板级策略和 UI profile，旧 `BoardSpecConfig` 仅作为过渡兼容层短期存在。”

---

## 18. 下一步最值得做的具体工作

如果按这份文档继续往下推进，最合理的下一步不是全量重构，而是先做下面这个最小骨架：

1. 新建 `src/bsp/shared/`
2. 新建：
   - `src/bsp/nmaxe/`
   - `src/bsp/nmaxe_gamma/`
   - `src/bsp/nmqaxepp/rev60/`
   - `src/bsp/nmqaxepp/rev61/`
   - `src/bsp/nmqaxepp/rev81/`
3. 新建 `product/ui_profiles/`
4. 新建 `ui/layouts/layout_480x320/`
5. 把 `src/board/board.cpp` 里每块 `#if defined(BOARD_...)` 拆到这些目录中
6. 先保留 `BoardSpecConfig`，但改为由新 `bsp` 目录反向生成
7. 增加 `UiProfileId` 绑定机制
8. 第二步再把 `display_hal.cpp` 和后续 UI 页面里的 `BOARD_*` 判断清掉

如果你要，我下一轮可以直接继续帮你做“第一阶段骨架落地”：

1. 建 `src/bsp` 目录骨架
2. 建 `product/ui_profiles` 骨架
3. 建 `board_select.h`
4. 先把 `board.cpp` 拆成过渡版多文件结构
5. 预留 `layout_480x320`
6. 保持当前编译行为不变
