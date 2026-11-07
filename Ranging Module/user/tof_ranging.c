/*********************************************************************************************************************
* 文件名            tof_ranging.c
* 说明              VL53LXX 系列测距模块实现：芯片选型 + 驱动封装 + 中值/平滑滤波 + 统计 + 报警 + 掉线判断
*
* 当前配置（tof_config.h 的 TOF_SENSOR_TYPE）：
*   TOF_SENSOR_DL1B（默认）-> 强制 VL53L1X（逐飞 DL1B），量程 4 m，不读型号寄存器、上电最快
*   TOF_SENSOR_DL1A        -> 强制 VL53L0X（逐飞 DL1A），量程 2 m
*   TOF_SENSOR_AUTO        -> 先读型号寄存器再选：0xC0==0xEE -> L0X，0x010F==0xEA -> L1X
*                             （VL53LXX-V2 那种两代芯混卖的模块才需要）
* 初始化失败时 tof_module_selfcheck() 仍会把两个型号寄存器和 IIC 扫描结果读出来，屏上直接显示
*
* 数据流（一帧一帧走）：
*   TOF 模块测完 -> INT 中断（或 10ms 轮询）-> 驱动把原始距离放进 dl1a_distance_mm / dl1b_distance_mm
*   -> tof_module_tick_10ms() 发现「有新帧」标志 -> tof_process()
*   -> 判有效 -> 减零点偏移 -> 中值滤波 -> 平滑滤波 -> 统计 / 报警 / 曲线
*   -> 菜单页面读 tof_module_distance_mm() 显示
*
* 为什么有效/无效要自己判：
*   驱动只给一个「距离 + 有没有新帧」，不给距离状态码。TOF 在「没回波 / 太近 / 超量程」时
*   给的是 8190~8192 这种哨兵值，或者干脆一个小到不合理的数，所以这里按量程再过滤一遍，
*   无效帧不参与滤波、不参与统计、不会触发报警，屏上会显示「弱信号」并沿用上一次的有效值
*
* 注意              本文件是 GBK 编码；汉字由 menu_font.c 提供，改完汉字记得跑 tools/gen_font.py
********************************************************************************************************************/
#include "zf_common_headfile.h"

#include "tof_config.h"
#include "tof_ranging.h"

#if (1 == TOF_USE_MENU_PARAM)
#include "menu_config.h"                            // 参数下标（MENU_PARAM_IDX_TOF_xxx）
#include "menu_param.h"                             // 参数值
#endif

// ===================================================================================================================
//                                            1. 芯片类型（运行期识别）
// ===================================================================================================================
#define TOF_CHIP_NONE               (0)
#define TOF_CHIP_L0X                (1)             // VL53L0X（逐飞 DL1A 驱动）
#define TOF_CHIP_L1X                (2)             // VL53L1X（逐飞 DL1B 驱动）

#if (TOF_SENSOR_AUTO != TOF_SENSOR_TYPE) && (TOF_SENSOR_DL1A != TOF_SENSOR_TYPE) && (TOF_SENSOR_DL1B != TOF_SENSOR_TYPE)
    #error "TOF_SENSOR_TYPE 只能是 TOF_SENSOR_AUTO / TOF_SENSOR_DL1A / TOF_SENSOR_DL1B（见 tof_config.h）"
#endif

#if (3 != TOF_FILTER_WINDOW) && (5 != TOF_FILTER_WINDOW)
    #error "TOF_FILTER_WINDOW 只能是 3 或 5（中值滤波要奇数个点，见 tof_config.h）"
#endif

// ===================================================================================================================
//                                            2. 内部变量
// ===================================================================================================================
// 参数（可以来自菜单参数，也可以是 tof_config.h 里的默认值）
typedef struct
{
    uint16 alarm_mm;                                // 报警距离
    int16  offset_mm;                               // 零点偏移（加到原始距离上）
    uint8  filter;                                  // 0 原始 / 1 中值 / 2 中值 + 平滑
    uint8  unit_cm;                                 // 0 = mm，1 = cm
    uint8  alarm_en;                                // 1 = 打开报警
}tof_setting_struct;

static tof_setting_struct tof_cfg =
{
    TOF_DEFAULT_ALARM_MM, 0, 1, 0, 1
};

static uint8  tof_ok;                               // 1 = 初始化过了
static uint8  tof_state;                            // tof_state_enum
static uint8  tof_chip;                             // TOF_CHIP_xxx（识别出来的芯片）
static uint16 tof_valid_max = TOF_VALID_MAX_MM;     // 当前量程（跟着芯片走，L0X 2 m / L1X 4 m）
static uint8  tof_probe_l0x;                        // 识别时读到的 0xC0（日志用）
static uint8  tof_probe_l1x;                        // 识别时读到的 0x010F（日志用）
static uint8  tof_scan_cnt;                         // 上次 IIC 扫描应答了几个器件
static uint8  tof_scan_addr = 0xFF;                 // 上次扫描应答的第一个地址（0xFF = 没有）

static uint16 tof_raw_mm;                           // 最近一帧原始距离
static uint16 tof_dist_mm;                          // 滤波后的距离（0 = 没有有效值）

static uint16 tof_window[TOF_FILTER_WINDOW];        // 中值滤波窗口
static uint8  tof_window_idx;
static uint8  tof_window_num;

static int32  tof_ema;                              // 平滑滤波的中间值
static uint8  tof_ema_valid;                        // 1 = 平滑滤波里已经有初值了

static uint32 tof_ms;                               // 10ms 累加的毫秒时钟
static uint32 tof_sample_ms;                        // 最近一帧的时刻
static uint32 tof_valid_ms;                         // 最近一帧有效的时刻

static uint32 tof_sample_cnt;                       // 总帧数
static uint32 tof_valid_cnt;                        // 有效帧数
static uint32 tof_invalid_cnt;                      // 无效帧数
static uint16 tof_rate_hz;                          // 帧率
static uint16 tof_rate_cnt;                         // 这一秒里收到几帧

static uint16 tof_min_mm;                           // 统计：最小
static uint16 tof_max_mm;                           // 统计：最大
static uint32 tof_sum_mm;                           // 统计：累加（算平均）

static uint8  tof_alarm;                            // 1 = 正在报警
static uint8  tof_alarm_cnt;                        // 连续多少帧小于阈值

static uint16 tof_history[TOF_HISTORY_NUM];         // 曲线缓存（环形）
static uint8  tof_history_idx;                      // 下一个写哪
static uint8  tof_history_num;                      // 现在有几个点

static char   tof_str[24];                          // 格式化字符串的静态缓冲

// ===================================================================================================================
//                                            3. 参数读取 / 限幅
// ===================================================================================================================
//-------------------------------------------------------------------------------------------------------------------
// 函数简介     把参数从菜单参数表同步进来（TOF_USE_MENU_PARAM = 1 时）
// 备注信息     10ms 同步一次（在 tof_module_tick_10ms 里）：菜单里改完「滤波等级 / 报警距离 / 单位」
//              当场生效，不用重启。菜单参数可能被 Flash 里的坏数据弄成越界值，这里再限幅一次，
//              后面算的时候就不用担心了
//-------------------------------------------------------------------------------------------------------------------
static void tof_cfg_sync (void)
{
#if (1 == TOF_USE_MENU_PARAM)
    int32 v;

    v = menu_param_get(MENU_PARAM_IDX_TOF_ALARM);
    if (v < TOF_VALID_MIN_MM) { v = TOF_VALID_MIN_MM; }
    if (v > (int32)tof_valid_max) { v = (int32)tof_valid_max; }
    tof_cfg.alarm_mm = (uint16)v;

    tof_cfg.offset_mm = (int16)menu_param_get(MENU_PARAM_IDX_TOF_OFFSET);

    v = menu_param_get(MENU_PARAM_IDX_TOF_FILTER);
    tof_cfg.filter = (uint8)((v < 0) ? (0) : ((v > TOF_FILTER_LEVEL_MAX) ? (TOF_FILTER_LEVEL_MAX) : (v)));

    tof_cfg.unit_cm = (0 != menu_param_get(MENU_PARAM_IDX_TOF_UNIT)) ? (1) : (0);
    tof_cfg.alarm_en = (0 != menu_param_get(MENU_PARAM_IDX_TOF_ENABLE)) ? (1) : (0);
#endif
}

// ===================================================================================================================
//                                            4. 滤波
// ===================================================================================================================
//-------------------------------------------------------------------------------------------------------------------
// 函数简介     中值滤波：把新值塞进窗口，返回窗口里的中位数
// 参数说明     value           新采样值
// 返回参数     中位数（窗口还没填满时按已有的点数取中间那个）
// 备注信息     中值滤波专门治「偶尔一帧跳到 1800 mm」这种毛刺：排序后取中间，毛刺自动被扔掉
//              窗口只有 3~5 个点，用插入排序足够了（不进中断、不动态分配）
//-------------------------------------------------------------------------------------------------------------------
static uint16 tof_median_push (uint16 value)
{
    uint16 tmp[TOF_FILTER_WINDOW];
    uint8  i;
    uint8  j;
    uint8  n;

    tof_window[tof_window_idx] = value;
    tof_window_idx = (uint8)((tof_window_idx + 1) % TOF_FILTER_WINDOW);
    if (tof_window_num < TOF_FILTER_WINDOW) { tof_window_num ++; }

    n = tof_window_num;
    for (i = 0; i < n; i ++) { tmp[i] = tof_window[i]; }

    for (i = 1; i < n; i ++)
    {
        uint16 key = tmp[i];

        j = i;
        while ((j > 0) && (tmp[j - 1] > key))
        {
            tmp[j] = tmp[j - 1];
            j --;
        }
        tmp[j] = key;
    }

    return tmp[n / 2];
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     清空滤波状态（重新初始化时用）
//-------------------------------------------------------------------------------------------------------------------
static void tof_filter_reset (void)
{
    uint8 i;

    for (i = 0; i < TOF_FILTER_WINDOW; i ++) { tof_window[i] = 0; }
    tof_window_idx = 0;
    tof_window_num = 0;
    tof_ema        = 0;
    tof_ema_valid  = 0;
}

// ===================================================================================================================
//                                            5. 曲线缓存
// ===================================================================================================================
static void tof_history_push (uint16 mm)
{
    tof_history[tof_history_idx] = mm;
    tof_history_idx = (uint8)((tof_history_idx + 1) % TOF_HISTORY_NUM);
    if (tof_history_num < TOF_HISTORY_NUM) { tof_history_num ++; }
}

// ===================================================================================================================
//                                            6. 一帧数据处理
// ===================================================================================================================
//-------------------------------------------------------------------------------------------------------------------
// 函数简介     处理一帧新数据：判有效 -> 减偏移 -> 滤波 -> 统计 -> 报警 -> 曲线
// 参数说明     raw             驱动给的原始距离（mm）
// 备注信息     在 10ms 中断里被调用，里面全是整数运算，没有 printf / 没有浮点 / 没有延时
//-------------------------------------------------------------------------------------------------------------------
static void tof_process (uint16 raw)
{
    uint16 dist;
    uint8  valid;

    tof_raw_mm    = raw;
    tof_sample_ms = tof_ms;
    tof_sample_cnt ++;
    if (255 > tof_rate_cnt) { tof_rate_cnt ++; }

    // ------------------------- 1. 判这一帧有没有效 -------------------------
    valid = ((raw >= TOF_VALID_MIN_MM) && (raw <= tof_valid_max) && (raw < TOF_RAW_INVALID_MM)) ? (1) : (0);

    if (0 == valid)
    {
        tof_invalid_cnt ++;
        tof_state = TOF_STATE_WEAK;                 // 有帧但是无效：模块还活着，只是这一帧没测到

        // 一直测不到东西（目标太远 / 前面是吸光材料）：显示值清 0，别让人以为还停在老距离
        if ((uint32)(tof_ms - tof_valid_ms) >= TOF_HOLD_MS)
        {
            tof_dist_mm   = 0;
            tof_alarm     = 0;
            tof_alarm_cnt = 0;
        }
        return;
    }

    // ------------------------- 2. 减零点偏移（模块装在车壳里比车头凹进去多少就填多少） -------------------------
    {
        int32 v = (int32)raw + (int32)tof_cfg.offset_mm;

        if (v < 0) { v = 0; }
        if (v > (int32)tof_valid_max) { v = (int32)tof_valid_max; }
        dist = (uint16)v;
    }

    // ------------------------- 3. 滤波 -------------------------
    //  0 = 原始值：这里什么都不做，直接用上面算出来的 dist
    if (1 == tof_cfg.filter)                                                // 1 = 中值
    {
        dist = tof_median_push(dist);
    }
    else if (2 <= tof_cfg.filter)                                           // 2 = 中值 + 平滑
    {
        dist = tof_median_push(dist);

        if (0 == tof_ema_valid)
        {
            tof_ema       = (int32)dist;                                    // 第一帧直接当初值，避免从 0 慢慢爬上来
            tof_ema_valid = 1;
        }
        else
        {
            tof_ema = tof_ema + (((int32)dist - tof_ema) / (1 << TOF_EMA_SHIFT));
        }

        if (tof_ema < 0) { tof_ema = 0; }
        if (tof_ema > (int32)tof_valid_max) { tof_ema = (int32)tof_valid_max; }
        dist = (uint16)tof_ema;
    }

    // ------------------------- 4. 统计 -------------------------
    tof_valid_cnt ++;
    tof_valid_ms = tof_ms;
    tof_dist_mm  = dist;

    if (0 == tof_min_mm)                                                    // 第一个有效值同时当最小值和最大值
    {
        tof_min_mm = dist;
        tof_max_mm = dist;
    }
    if (dist < tof_min_mm) { tof_min_mm = dist; }
    if (dist > tof_max_mm) { tof_max_mm = dist; }
    tof_sum_mm += (uint32)dist;

    // ------------------------- 5. 报警（连续确认 + 回差） -------------------------
    if ((0 != tof_cfg.alarm_en) && (dist <= tof_cfg.alarm_mm))
    {
        if (TOF_ALARM_CONFIRM > tof_alarm_cnt) { tof_alarm_cnt ++; }
        if (tof_alarm_cnt >= TOF_ALARM_CONFIRM) { tof_alarm = 1; }
    }
    else
    {
        tof_alarm_cnt = 0;
        // 只有走得比「阈值 + 回差」还远才解除报警，不然在阈值附近会一闪一闪
        if ((0 == tof_cfg.alarm_en) || (dist > (uint16)(tof_cfg.alarm_mm + TOF_ALARM_HYST_MM)))
        {
            tof_alarm = 0;
        }
    }

    // ------------------------- 6. 曲线 -------------------------
    tof_history_push(dist);

    tof_state = TOF_STATE_OK;
}

// ===================================================================================================================
//                                            7. 对外接口
// ===================================================================================================================
//-------------------------------------------------------------------------------------------------------------------
// 函数简介     读型号寄存器，判断总线上是 VL53L0X 还是 VL53L1X
// 返回参数     TOF_CHIP_L0X / TOF_CHIP_L1X / TOF_CHIP_NONE
// 备注信息     结果同时存进 tof_probe_l0x / tof_probe_l1x，串口日志直接打原始值
//              VL53L0X 用 8 位寄存器地址读 0xC0；VL53L1X 用 16 位寄存器地址读 0x010F
//-------------------------------------------------------------------------------------------------------------------
static uint8 tof_detect_chip (void)
{
    soft_iic_info_struct bus;
    uint8  tx[2];
    uint8  rx[2];

    soft_iic_init(&bus, DL1A_DEV_ADDR, DL1A_SOFT_IIC_DELAY, DL1A_SCL_PIN, DL1A_SDA_PIN);

    tof_probe_l0x = soft_iic_read_8bit_register(&bus, DL1A_IDENTIFICATION_MODEL_ID);       // 0xC0

    tx[0] = (uint8)(0x010F >> 8);
    tx[1] = (uint8)(0x010F & 0xFF);
    rx[0] = 0xFF;
    soft_iic_transfer_8bit_array(&bus, tx, 2, rx, 1);
    tof_probe_l1x = rx[0];

    if (0xEE == tof_probe_l0x) { return TOF_CHIP_L0X; }
    if (0xEA == tof_probe_l1x) { return TOF_CHIP_L1X; }

    return TOF_CHIP_NONE;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     直接探一下 IIC 总线上挂的是什么（初始化失败时自动调用，平时不用管）
// 备注信息     两条型号寄存器都读一遍，串口打印原始值，用来分清「芯片认错」和「线/供电有问题」：
//               0xEE -> VL53L0X    0xEA -> VL53L1X    都是 0xFF -> 总线上没人应答
//-------------------------------------------------------------------------------------------------------------------
void tof_module_bus_probe (void)
{
#if (1 == TOF_SERIAL_DEBUG)
    uint8 chip;

    chip = tof_detect_chip();

    printf("[tof] probe: L0X 0xC0=0x%02X | L1X 0x010F=0x%02X\r\n", tof_probe_l0x, tof_probe_l1x);

    if (TOF_CHIP_L0X == chip)
    {
        printf("[tof] probe: 通信正常，认出来是 VL53L0X -> 自检失败多半是 XSHUT(P20_10) 没接 / 供电不是 5V / 模块坏了\r\n");
    }
    else if (TOF_CHIP_L1X == chip)
    {
        printf("[tof] probe: 通信正常，认出来是 VL53L1X -> 本工程会自动按 L1X 驱动，若仍失败请把模块重新上电再试\r\n");
    }
    else if ((0xFF == tof_probe_l0x) && (0xFF == tof_probe_l1x))
    {
        printf("[tof] probe: 总线上没人应答 -> 量模块 VIN 是不是 5V / SCL(P33_4) SDA(P33_5) 有没有接反 / 模块插好没\r\n");
    }
    else
    {
        printf("[tof] probe: 读到的不是已知型号 -> 可能是别的 TOF 模块（串口版或其它芯片），把模块丝印告诉我\r\n");
    }
#else
    (void)tof_detect_chip();
#endif
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     初始化测距模块
// 返回参数     1 = 成功，0 = 失败
// 使用示例     tof_module_init();
// 备注信息     1) 先拉高 XSHUT、等模块起来，再读型号寄存器识别芯片（TOF_SENSOR_AUTO 时）
//              2) 识别出 L0X 用 dl1a 驱动、L1X 用 dl1b 驱动，量程跟着换（2 m / 4 m）
//              3) 初始化失败会重试 TOF_INIT_RETRY 次，最后打印总线探测结果，状态置「未初始化」
//              4) 菜单里按「重来」也会走到这里（tof_module_reinit），所以必须可重入
//-------------------------------------------------------------------------------------------------------------------
uint8 tof_module_init (void)
{
#if (0 == TOF_MODULE_ENABLE)
    tof_ok    = 0;
    tof_state = TOF_STATE_OFF;
    return 0;
#else
    uint8 ret;
    uint8 attempt;
    uint8 chip;

    tof_ok         = 0;
    tof_state      = TOF_STATE_OFF;
    tof_raw_mm     = 0;
    tof_dist_mm    = 0;
    tof_sample_ms  = tof_ms;
    tof_valid_ms   = tof_ms;
    tof_alarm      = 0;
    tof_alarm_cnt  = 0;
    tof_min_mm     = 0;
    tof_max_mm     = 0;
    tof_chip       = TOF_CHIP_NONE;
    tof_valid_max  = TOF_VALID_MAX_MM;

    tof_filter_reset();
    tof_module_reset_stats();
    tof_cfg_sync();

    // ------------------------- 1. 先摘掉中断回调 -------------------------
    // 初始化过程要独占 IIC 总线（拉 XSHUT 复位、写一堆寄存器），这时候如果 INT 脚正好来中断，
    // 中断里又去读 IIC，两边就抢总线了。所以初始化期间先把回调挂空，等模块配好再挂回去
    set_tof_type(TOF_DL1A, NULL);

    // ------------------------- 2. XSHUT 拉高、等模块起来，再识别芯片 -------------------------
    // VL53LXX 上电后 XSHUT 为低会一直关着，IIC 不应答；先给它一个高电平再读型号
    gpio_init(DL1A_XS_PIN, GPO, GPIO_HIGH, GPO_PUSH_PULL);
    gpio_high(DL1A_XS_PIN);
    system_delay_ms(20);

#if (TOF_SENSOR_DL1A == TOF_SENSOR_TYPE)
    chip = TOF_CHIP_L0X;
#elif (TOF_SENSOR_DL1B == TOF_SENSOR_TYPE)
    chip = TOF_CHIP_L1X;
#else
    chip = tof_detect_chip();

#if (1 == TOF_SERIAL_DEBUG)
    printf("[tof] detect: L0X 0xC0=0x%02X | L1X 0x010F=0x%02X -> %s\r\n",
           tof_probe_l0x, tof_probe_l1x,
           (TOF_CHIP_L0X == chip) ? ("VL53L0X") : ((TOF_CHIP_L1X == chip) ? ("VL53L1X") : ("没认出来，按 VL53L0X 试")));
#endif
    if (TOF_CHIP_NONE == chip) { chip = TOF_CHIP_L0X; }     // 认不出来也按 L0X 试一次，失败有 probe 日志
#endif

    tof_chip      = chip;
    tof_valid_max = (TOF_CHIP_L1X == chip) ? (TOF_RANGE_L1X_MM) : (TOF_RANGE_L0X_MM);

    // ------------------------- 3. 初始化模块（失败重试几次） -------------------------
    for (attempt = 0; ; attempt ++)
    {
        ret = (TOF_CHIP_L1X == tof_chip) ? (dl1b_init()) : (dl1a_init());

        if (0 == ret) { break; }
        if (attempt >= TOF_INIT_RETRY) { break; }

#if (1 == TOF_SERIAL_DEBUG)
        printf("[tof] init failed, retry %d/%d ...\r\n", (int)(attempt + 1), (int)TOF_INIT_RETRY);
#endif
        system_delay_ms(200);                       // 等模块电源稳一稳再复位重来
    }

    // ------------------------- 4. 挂回中断回调（只有中断方式才需要） -------------------------
    // isr.c 里通道1 那句 tof_module_exti_handler(); 是库里的回调指针，
    // 不 set_tof_type() 的话它指向默认空函数，永远收不到数据；
    // 轮询方式下故意挂空：GPIO1 悬空时就算被干扰触发，中断里也不会去碰 IIC
#if (TOF_SENSOR_AUTO == TOF_SENSOR_TYPE)
    // 型号寄存器没认出来、按 L0X 也初始化失败时，再拿 L1X 驱动试一次
    // （有些翻新模块的型号寄存器读法跟手册不一样，多试一次没坏处）
    if ((0 != ret) && (TOF_CHIP_L0X == tof_chip))
    {
#if (1 == TOF_SERIAL_DEBUG)
        printf("[tof] L0X driver failed, try L1X driver ...\r\n");
#endif
        ret = dl1b_init();
        if (0 == ret)
        {
            tof_chip      = TOF_CHIP_L1X;
            tof_valid_max = TOF_RANGE_L1X_MM;
        }
    }
#endif

#if (0 != TOF_USE_INT)
    if (0 == ret)
    {
        if (TOF_CHIP_L1X == tof_chip) { set_tof_type(TOF_DL1B, dl1b_int_handler); }
        else                          { set_tof_type(TOF_DL1A, dl1a_int_handler); }
    }
#endif

    if (0 != ret)
    {
        tof_state = TOF_STATE_OFF;

#if (1 == TOF_SERIAL_DEBUG)
        printf("[tof] init FAILED (check VIN 5V / SCL P33_4 / SDA P33_5 / XSHUT P20_10)\r\n");
#endif
        tof_module_selfcheck();                     // 扫一遍 IIC + 重读型号寄存器（屏幕自检行用的就是这几个数）
        tof_module_bus_probe();                     // 打印总线上到底有没有东西，帮着分清「型号错」还是「线错」
        return 0;
    }

    tof_ok        = 1;
    tof_state     = TOF_STATE_IDLE;
    tof_sample_ms = tof_ms;

#if (1 == TOF_SERIAL_DEBUG)
    printf("[tof] init %s ok  int:%d  filter:%d  alarm:%d mm  range:%d-%d mm\r\n",
           tof_module_chip_str(), (int)TOF_USE_INT, (int)tof_cfg.filter,
           (int)tof_cfg.alarm_mm, (int)TOF_VALID_MIN_MM, (int)tof_valid_max);
#endif

    return 1;
#endif      // TOF_MODULE_ENABLE
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     重新初始化（模块拔了再插、或者初始化时没插好，用这个救一下）
// 返回参数     1 = 成功，0 = 失败
// 备注信息     会阻塞约 0.5 s，只能在主程序里调（菜单按键），绝不能放中断
//-------------------------------------------------------------------------------------------------------------------
uint8 tof_module_reinit (void)
{
    return tof_module_init();
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     10ms 周期任务：计时、收数据、滤波、掉线判断、帧率统计
// 备注信息     放 isr.c 的 CCU60_CH0 中断里（和按键扫描同一个中断，周期 10ms）
//              里面只做整数运算；轮询方式下会读一次 IIC（约 50 us），中断方式下几乎不花时间
//-------------------------------------------------------------------------------------------------------------------
void tof_module_tick_10ms (void)
{
    tof_ms += 10;

    if (0 == tof_ok) { return; }

    tof_cfg_sync();                                 // 菜单里改了参数，这里 10ms 就跟着变（常量几字节的读取，很便宜）

#if (0 == TOF_USE_INT)
    // 轮询方式：主动问模块要一帧。没测完时驱动自己会很快返回，不会卡住
    if (TOF_CHIP_L1X == tof_chip) { dl1b_get_distance(); }
    else                          { dl1a_get_distance(); }
#endif

    // ------------------------- 有新帧就处理（按识别出来的芯片取对应驱动的数据） -------------------------
    if (TOF_CHIP_L1X == tof_chip)
    {
        if (0 != dl1b_finsh_flag)
        {
            dl1b_finsh_flag = 0;
            tof_process((uint16)dl1b_distance_mm);
        }
    }
    else
    {
        if (0 != dl1a_finsh_flag)
        {
            dl1a_finsh_flag = 0;
            tof_process((uint16)dl1a_distance_mm);
        }
    }

    // ------------------------- 掉线判断 -------------------------
    // 超过 TOF_TIMEOUT_MS 一帧都没来：线掉了 / 模块没供电 / INT 没接却选了中断方式
    if ((uint32)(tof_ms - tof_sample_ms) > TOF_TIMEOUT_MS)
    {
        tof_state = TOF_STATE_LOST;

        // 掉线久一点就把显示值清掉，屏上显示「--」，别一直挂着最后一帧骗人
        if ((uint32)(tof_ms - tof_valid_ms) >= TOF_HOLD_MS)
        {
            tof_dist_mm   = 0;
            tof_alarm     = 0;
            tof_alarm_cnt = 0;
        }
    }

    // ------------------------- 每秒算一次帧率 -------------------------
    {
        static uint32 rate_ms = 0;

        if ((uint32)(tof_ms - rate_ms) >= 1000u)
        {
            rate_ms     = tof_ms;
            tof_rate_hz = tof_rate_cnt;
            tof_rate_cnt = 0;
        }
    }
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     清统计（最小 / 最大 / 平均 / 帧数 / 曲线），参数和报警状态不动
//-------------------------------------------------------------------------------------------------------------------
void tof_module_reset_stats (void)
{
    tof_sample_cnt  = 0;
    tof_valid_cnt   = 0;
    tof_invalid_cnt = 0;
    tof_rate_hz     = 0;
    tof_rate_cnt    = 0;
    tof_min_mm      = 0;
    tof_max_mm      = 0;
    tof_sum_mm      = 0;
    tof_history_idx = 0;
    tof_history_num = 0;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     状态 / 型号字符串（屏上显示用）
//-------------------------------------------------------------------------------------------------------------------
uint8 tof_module_ok (void)
{
    return tof_ok;
}

uint8 tof_module_state (void)
{
    return tof_state;
}

const char *tof_module_state_str (void)
{
    switch (tof_state)
    {
        case TOF_STATE_OK:      return "正常";
        case TOF_STATE_WEAK:    return "弱信号";
        case TOF_STATE_LOST:    return "掉线";
        case TOF_STATE_IDLE:    return "待机";
        default:                return "未初始化";
    }
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     芯片型号 / 量程（屏上显示用）
// 返回参数     "VL53L0X" / "VL53L1X" / "未识别"
//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------
// 函数简介     扫一遍 IIC 总线（0x08~0x77）+ 重读两个型号寄存器，这是「不用示波器判线」的核心
// 返回参数     应答的器件个数（0 = 总线上一个都没应答 -> 供电 / 接线 / XSHUT 的问题）
// 备注信息     只读不写，不会改坏别人寄存器；耗时约 20 ms，只在初始化失败或用户按自检时调用
//-------------------------------------------------------------------------------------------------------------------
uint8 tof_module_selfcheck (void)
{
    soft_iic_info_struct bus;
    uint8  cnt = 0;
    uint8  addr;
    uint8  v;

    soft_iic_init(&bus, DL1A_DEV_ADDR, DL1A_SOFT_IIC_DELAY, DL1A_SCL_PIN, DL1A_SDA_PIN);
    tof_scan_cnt  = 0;
    tof_scan_addr = 0xFF;

    for (addr = 0x08; 0x77 >= addr; addr ++)
    {
        bus.addr = addr;
        v = soft_iic_read_8bit_register(&bus, 0x00);       // 有人应答时读回来一般不是 0xFF
        if (0xFF != v)
        {
            cnt ++;
            if (0xFF == tof_scan_addr) { tof_scan_addr = addr; }
        }
    }

    tof_scan_cnt = cnt;
    (void)tof_detect_chip();                                // 顺便把型号寄存器再读一次（屏幕自检行用）

    return cnt;
}

uint8 tof_module_probe_l0x (void) { return tof_probe_l0x; }
uint8 tof_module_probe_l1x (void) { return tof_probe_l1x; }
uint8 tof_module_scan_cnt  (void) { return tof_scan_cnt;  }
uint8 tof_module_scan_addr (void) { return tof_scan_addr; }

const char *tof_module_chip_str (void)
{
    switch (tof_chip)
    {
        case TOF_CHIP_L0X:  return "VL53L0X";
        case TOF_CHIP_L1X:  return "VL53L1X";
        default:            return "未识别";
    }
}

const char *tof_module_type_str (void)
{
    return tof_module_chip_str();
}

uint16 tof_module_max_range_mm (void)
{
    return tof_valid_max;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     取距离
// 返回参数     滤波后的距离（mm），0 表示当前没有有效读数
//-------------------------------------------------------------------------------------------------------------------
uint16 tof_module_distance_mm (void)
{
    return tof_dist_mm;
}

uint16 tof_module_raw_mm (void)
{
    return tof_raw_mm;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     格式化好的显示字符串（按「单位cm」参数自动换算）
// 返回参数     "312 mm" / "31 cm" / "--"
// 备注信息     用静态缓冲，取完立刻显示；不要存这个指针
//-------------------------------------------------------------------------------------------------------------------
const char *tof_module_display_str (void)
{
    uint16 mm = tof_dist_mm;

    if (0 == mm)
    {
        return "--";
    }

    if (0 != tof_cfg.unit_cm)
    {
        snprintf(tof_str, sizeof(tof_str), "%ld cm", (long)((mm + 5) / 10));    // 四舍五入到 cm
    }
    else
    {
        snprintf(tof_str, sizeof(tof_str), "%ld mm", (long)mm);
    }

    return tof_str;
}

uint8 tof_module_unit_cm (void)
{
    return tof_cfg.unit_cm;
}

uint8 tof_module_filter_level (void)
{
    return tof_cfg.filter;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     统计查询
//-------------------------------------------------------------------------------------------------------------------
uint32 tof_module_sample_cnt (void)
{
    return tof_sample_cnt;
}

uint32 tof_module_valid_cnt (void)
{
    return tof_valid_cnt;
}

uint32 tof_module_invalid_cnt (void)
{
    return tof_invalid_cnt;
}

uint16 tof_module_rate_hz (void)
{
    return tof_rate_hz;
}

uint16 tof_module_min_mm (void)
{
    return tof_min_mm;
}

uint16 tof_module_max_mm (void)
{
    return tof_max_mm;
}

uint16 tof_module_avg_mm (void)
{
    if (0 == tof_valid_cnt) { return 0; }
    return (uint16)(tof_sum_mm / tof_valid_cnt);
}

uint16 tof_module_span_mm (void)
{
    return (uint16)(tof_max_mm - tof_min_mm);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     报警状态 / 阈值
//-------------------------------------------------------------------------------------------------------------------
uint8 tof_module_alarm (void)
{
    return tof_alarm;
}

uint16 tof_module_alarm_threshold_mm (void)
{
    return tof_cfg.alarm_mm;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     直接改参数（TOF_USE_MENU_PARAM = 1 时，下一帧会被菜单参数覆盖，改菜单参数才有用）
//-------------------------------------------------------------------------------------------------------------------
void tof_module_set_alarm_mm (uint16 mm)
{
    if (mm < TOF_VALID_MIN_MM) { mm = TOF_VALID_MIN_MM; }
    if (mm > tof_valid_max) { mm = tof_valid_max; }
    tof_cfg.alarm_mm = mm;
}

void tof_module_set_unit_cm (uint8 on)
{
    tof_cfg.unit_cm = (0 != on) ? (1) : (0);
}

void tof_module_set_filter_level (uint8 level)
{
    tof_cfg.filter = (level > TOF_FILTER_LEVEL_MAX) ? (TOF_FILTER_LEVEL_MAX) : (level);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     曲线数据（实时测距页画图用）
//-------------------------------------------------------------------------------------------------------------------
uint8 tof_module_history_num (void)
{
    return tof_history_num;
}

uint16 tof_module_history_get (uint8 index)
{
    uint8 pos;

    if (index >= tof_history_num) { return 0; }

    // 环形缓冲里「最旧」的那个点：从写指针往前退 num 个
    pos = (uint8)((tof_history_idx + (TOF_HISTORY_NUM - tof_history_num) + index) % TOF_HISTORY_NUM);
    return tof_history[pos];
}
