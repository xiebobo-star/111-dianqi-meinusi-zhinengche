# TC264 测距模块（VL53LXX-V2 / VL53LXX 系列）

> AURIX Development Studio 1.10.40 / 逐飞 TC264 开源库 / 工程：`Ranging Module VL53LXX-V2`
> 传感器：淘宝常见的 **VL53LXX-V2** 模块（丝印 VL53L0X V2 / VL53LXX V2，6 个脚：VIN GND SCL SDA XSHUT GPIO1）
> 也兼容逐飞 DL1A / DL1B 模块（同一条 IIC 总线、同样的软件 IIC 引脚）
> 显示：IPS200（240×320）或 TFT180（128×160），在 `menu_config.h` 一行切换

这是从 `Ranging Module` 复制出来的独立工程，专门给 VL53LXX-V2 模块用。和原来那份的区别：

| | `Ranging Module`（老） | `Ranging Module VL53LXX-V2`（本工程） |
|---|---|---|
| 芯片型号 | 编译时写死 DL1A（VL53L0X） | **上电自动识别** VL53L0X / VL53L1X，也可以强制指定 |
| 量程 | 固定 2000 mm | 跟着识别结果走：L0X 2000 mm、L1X 4000 mm |
| XSHUT | 由驱动内部复位 | 初始化前先拉高并等 20 ms，再读型号（裸模块必须这样） |
| 初始化失败 | 直接报 DL1A self check error | 先重试 1 次，再打印总线探测日志（读到的型号原始值） |
| 型号显示 | 固定 "DL1A" | 测距统计页显示实际认出来的 "VL53L0X" / "VL53L1X" |

---

## 一、这个模块做了什么

| 功能 | 说明 |
|---|---|
| 自动识别芯片 | 读 VL53L0X 的 `0xC0`（0xEE）和 VL53L1X 的 `0x010F`（0xEA），自动选驱动和量程，串口会打一行 `[tof] detect: ...` |
| 测距 | 每秒约 30 帧连续测量（由 TOF 自己的时序预算决定），单位 mm |
| 滤波 | 三级可调：`0` 原始 / `1` 中值（5 点）/ `2` 中值 + 平滑（EMA 1/4） |
| 有效性判断 | 按量程过滤：< 20 mm、超量程、`8190~8192` 哨兵值都算无效帧，不参与滤波/统计/报警 |
| 零点偏移 | 模块装在车壳里、比车头凹进去多少就填多少（mm） |
| 报警 | 距离 ≤ 阈值并连续 3 帧确认才报警，要大于「阈值 + 30 mm」才解除（回差） |
| 掉线判断 | 超过 250 ms 没有新帧 → 状态「掉线」，距离显示 `--` |
| 统计 / 曲线 | 帧数 / 帧率 / 有效 / 无效 / 最小 / 最大 / 平均 / 波动；2 寸屏画最近 64 个采样的面积图 |
| 参数掉电保存 | 报警距离 / 零点偏移 / 滤波等级 / 单位 / 报警开关，写在 TC264 片内 Flash 第 10/11 页 |

---

## 二、接线（VL53LXX-V2 → TC264）

| 模块脚 | TC264 引脚 | 说明 |
|---|---|---|
| VIN | **5V** | 模块自带稳压，5V 最稳（3.3V 也能亮，但激光瞬时电流大，容易掉压导致读数乱跳） |
| GND | GND | 必须共地 |
| SCL | `P33_4` | 软件 IIC 时钟 |
| SDA | `P33_5` | 软件 IIC 数据 |
| XSHUT | `P20_10` | **必须接！** 初始化时拉低再拉高让模块复位；不接的话 MCU 一复位模块还停在上次配置里，就会报 `DL1A self check error` |
| GPIO1 | 不接 | 测距完成中断输出；默认轮询，不用它。真要中断就接 `P14_3` 并把 `TOF_USE_INT` 改 1 |

其它外设（和菜单工程一样）：

| 功能 | 引脚 |
|---|---|
| 屏幕 SPI2 | SCK=`P15_3` MOSI=`P15_5` CS=`P15_2` RST=`P15_1` DC=`P15_0` BLK=`P15_4` |
| 按键 1/2/3/4 | `P20_6` / `P20_7` / `P11_2` / `P11_3` |
| 核心板 LED | `P20_9` / `P20_8` |
| 调试串口 | TX=`P14_0` RX=`P14_1` 115200 |

> **为什么必须接 XSHUT**：TOF 芯片不复位就一直保持上一次的测量状态（可能已经进了连续测量 / 改了 IIC 模式）。
> 这时再跑一遍初始化，寄存器读到的东西对不上，`dl1a_init()` 就会卡在 SPAD 自检，报你现在看到的那句
> `zf_device_dl1a.c 603 check DL1A self error`。接上 XSHUT，每次上电/复位都让芯片从干净状态开始，就不会了。
>
> 如果模块是插在主板的 IIC 座上，而那个座的脚不是 `P33_4/P33_5`，改
> `libraries/zf_device/zf_device_dl1a.h` 里的 `DL1A_SCL_PIN` / `DL1A_SDA_PIN` 两行（库文件里唯一需要改的地方）。

---

## 三、配置（`user/tof_config.h`）

| 宏 | 默认 | 说明 |
|---|---|---|
| `TOF_MODULE_ENABLE` | `1` | 0 = 上电不初始化（没接模块、只想调菜单时用，不碰 IIC、不报错） |
| `TOF_SENSOR_TYPE` | `TOF_SENSOR_AUTO` | 自动识别；也可强制 `TOF_SENSOR_DL1A` / `TOF_SENSOR_DL1B` |
| `TOF_USE_INT` | `0` | 0 = 10 ms 轮询（不接 GPIO1）；1 = 中断读数（GPIO1 接 `P14_3`） |
| `TOF_VALID_MIN_MM` | 20 | 小于它算无效 |
| `TOF_RANGE_L0X_MM` / `L1X_MM` | 2000 / 4000 | 两种芯片的量程，识别后自动用对应的 |
| `TOF_FILTER_WINDOW` | 5 | 中值窗口，只能 3 或 5 |
| `TOF_TIMEOUT_MS` | 250 | 多久没数据算掉线 |
| `TOF_INIT_RETRY` | 1 | 初始化失败重试次数 |
| `TOF_USE_MENU_PARAM` | 1 | 参数从菜单参数表来 |
| `TOF_SERIAL_DEBUG` | 1 | 初始化 / 识别结果打到调试串口 |

---

## 四、菜单怎么用

```
主菜单
 ├─ 测距模块
 │    ├─ 实时测距   ← 距离大数字 + 状态 + 帧率 + 量程条 + 曲线（2 寸屏）
 │    └─ 测距统计   ← 型号 / 滤波 / 采样 / 帧率 / 有效 / 无效 / 最小 / 最大 / 平均 / 波动 / 阈值 / 状态 / 报警
 ├─ 参数设置（第 4 组「测距参数」）
 ├─ 状态查看
 └─ 系统信息
```

| 页面 | 按键 | 作用 |
|---|---|---|
| 实时测距 | 1 | 单位 mm ↔ cm（会存 Flash） |
| | 2 | 清统计 |
| | 3 | 重新初始化模块（约 0.5 s，热插拔后用它救） |
| | 4 | 返回 |
| 测距统计 | 1 / 2 | 上翻 / 下翻（小屏 6 行，共 13 行） |
| | 3 | 清统计 |
| | 4 | 返回 |
| 任意页面 | 长按 4 | 直接回主菜单 |

右上角状态颜色：绿=正常、黄=弱信号、红=掉线、蓝=待机、灰=未初始化。

---

## 五、参数（参数设置 → 测距参数）

| 参数 | 默认 | 范围 | 步进 | 说明 |
|---|---|---|---|---|
| 报警距离 | 300 | 50 ~ 1500 mm | 10 mm | 小于等于它就报警 |
| 零点偏移 | 0 | -500 ~ 500 mm | 1 mm | 模块比车头凹进去 20 mm 就填 -20 |
| 滤波等级 | 1 | 0 ~ 2 | 1 | 0 原始 / 1 中值 / 2 中值+平滑 |
| 单位cm | 关 | 关 / 开 | — | 开 = 屏上按 cm 显示 |
| 报警开关 | 开 | 关 / 开 | — | 关 = 只测距不报警 |

改完立刻生效（`tof_ranging.c` 每 10 ms 同步一次），退出修改自动存 Flash。
参数项是 17 项，Flash 里旧格式会因「项数对不上」自动作废，上电恢复默认值。

---

## 六、工程结构（新增文件都在 `user/`，逐飞库和 iLLD 一行没改）

| 文件 | 作用 |
|---|---|
| `user/tof_config.h` | **唯一配置文件**：型号、采样方式、量程、滤波、报警、曲线点数、串口日志 |
| `user/tof_ranging.c/.h` | 测距核心：芯片自动识别、驱动封装、有效性判断、零点偏移、中值/平滑滤波、统计、报警、掉线判断 |
| `user/menu_tof.c/.h` | 两个菜单页面（实时测距 / 测距统计） |
| `user/cpu0_main.c` | 菜单初始化 → **TOF 初始化** → 上电延时 → 开 10 ms 中断 |
| `user/isr.c` | 10 ms 中断里调 `tof_module_tick_10ms()` |
| `user/menu_config.h` / `menu_param.c` | 5 个测距参数下标 + 第 4 组参数 |
| `user/menu_ui.c` | 主菜单「测距模块」、`MENU_UI_STATE_TOF`、10 Hz 局部刷新 |
| `user/menu_font.c` | 汉字点阵（已用 `tools/gen_font.py` 重新生成，229 个汉字） |

数据流：

```
TOF 模块测完一帧
  → 每 10 ms 读一次寄存器（或 GPIO1 中断触发），数据进 dl1a_distance_mm / dl1b_distance_mm
  → tof_module_tick_10ms()：发现有新帧 → tof_process()
  → 判有效 → 减零点偏移 → 中值/平滑滤波 → 统计 → 报警 → 曲线
  → 菜单页面（100 ms 刷新）读 tof_module_distance_mm() 显示
```

---

## 七、给整车 / 避障用的接口（`tof_ranging.h`）

```c
tof_module_init();                         // 上电初始化一次（已在 cpu0_main.c 里）
tof_module_tick_10ms();                    // 10ms 中断里调（已在 isr.c 里）

uint16 mm   = tof_module_distance_mm();    // 滤波后的距离，0 = 当前没有有效读数
uint8  ok   = (TOF_STATE_OK == tof_module_state());
uint8  near = tof_module_alarm();          // 1 = 距离已小于报警阈值（带确认 + 回差）
uint16 rng  = tof_module_max_range_mm();   // 当前量程（识别出 L0X 2000 / L1X 4000）
```

---

## 八、常见问题

| 现象 | 原因 / 处理 |
|---|---|
| 串口 `zf_device_dl1a.c 603 check DL1A self error` | 模块没被正常识别。先看紧跟着的 `[tof] probe:` 那行；最常见是 **XSHUT 没接**、VIN 不是 5V、SCL/SDA 接反 |
| `probe: L0X 0xC0=0xEE` | 通信正常，是 VL53L0X。自检还失败就查 XSHUT/5V/模块好坏 |
| `probe: ... L1X 0x010F=0xEA` | 是 VL53L1X，工程会自动按 L1X 驱动，量程 4 m |
| `probe: 总线上没人应答`（两个 0xFF） | 供电或接线：量 VIN、换一根线、确认模块插紧 |
| `detect: 没认出来，按 VL53L0X 试` | 模块不是 VL53LXX 系列，或者 IIC 线上有干扰；先按上一行查线 |
| 一直显示「掉线」 | 模块没数据；或者 `TOF_USE_INT = 1` 但 GPIO1 没接 |
| 距离一直 `--`，状态「弱信号」 | 前面没东西（超过量程）、目标太黑/太亮/玻璃、镜头有膜 |
| 数值跳 | 滤波等级改 2；检查 5V 供电和共地；目标换成白纸/纸箱 |
| 近了测不到 | TOF 盲区约 20~30 mm，贴脸检测要用微动开关/红外 |
| 屏上汉字空白 | 改过中文文案没重新生成字库：工程根目录跑 `python tools/gen_font.py` |
| 参数改了不生效 | 参数页按 3 进入修改、按 1/2 加减，退出修改才生效并存 Flash |

---

## 九、编译 / 烧录

1. ADS：`File → Import → General → Existing Projects into Workspace` → 选 `Ranging Module VL53LXX-V2` 目录 → Finish。
2. `Project → Build Project`（Debug）：**0 error 0 warning**（本工程已用 AURIX Development Studio 的 TASKING 工具链验证：ROM 约 89.6 KB，RAM 约 30.6 KB）。
3. 接下载器 → `Debug`（第一次停在 `core0_main`，按 F8 继续）。
4. 每次烧录前先把上一次调试会话 **Terminate**。
