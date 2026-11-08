/*********************************************************************************************************************
* 文件名            tof_ranging.h
* 说明              VL53LXX 系列测距模块的对外接口：初始化（自动识别芯片）、取距离、取状态、统计、报警
*
* 用法（三步）：
*   1) 上电初始化一次：        tof_module_init();           // 里面自动识别 VL53L0X / VL53L1X
*   2) 10ms 定时中断里调一次：  tof_module_tick_10ms();       // 计时 + 收数据 + 滤波 + 掉线判断
*   3) 要距离的时候随时取：     tof_module_distance_mm()      // 滤波后的距离，0 = 当前没有有效读数
*
* 中断接线（TOF_USE_INT = 1 时）：
*   模块 GPIO1 -> P14_3，isr.c 里那句 tof_module_exti_handler() 是库里留的回调函数指针，
*   本模块在 tof_module_init() 里用 set_tof_type() 把它接到对应芯片的中断处理函数，
*   所以 isr.c 一个字都不用改
*
* 单位：内部一律用 mm 存、用 mm 算；显示要 cm 就用 tof_module_display_str()，别自己做除法
********************************************************************************************************************/
#ifndef _tof_ranging_h_
#define _tof_ranging_h_

#include "zf_common_headfile.h"

// ============================================ 模块状态 ============================================
typedef enum
{
    TOF_STATE_OFF = 0,                              // 没初始化 / 初始化失败（模块没接、线不对）
    TOF_STATE_IDLE,                                 // 初始化成功，还没收到第一帧
    TOF_STATE_OK,                                   // 正常测距（最近一帧有效）
    TOF_STATE_WEAK,                                 // 最近一帧无效（太近 / 超量程 / 没回波），显示沿用上一次
    TOF_STATE_LOST,                                 // 超过 TOF_TIMEOUT_MS 没有新数据（线掉了 / 模块没供电）
}tof_state_enum;

// ============================================ 初始化 / 周期任务 ============================================
uint8       tof_module_init            (void);     // 初始化模块 1 = 成功，0 = 失败（失败也能继续跑，只是没数据）
uint8       tof_module_reinit          (void);     // 重新初始化（测距统计页按 3 用；会阻塞约 0.5 s）
void        tof_module_bus_probe       (void);     // 打印总线上读到的型号寄存器（初始化失败时自动调用）
void        tof_module_tick_10ms       (void);     // 10 ms 定时中断里调用（放 isr.c 的 CCU60_CH0）

// ============================================ 状态查询 ============================================
uint8       tof_module_ok              (void);     // 1 = 初始化过了
uint8       tof_module_state           (void);     // tof_state_enum
const char *tof_module_state_str       (void);     // "正常" / "弱信号" / "掉线" / "待机" / "未初始化"
const char *tof_module_chip_str        (void);     // "VL53L0X" / "VL53L1X" / "未识别"
const char *tof_module_type_str        (void);     // 同上（老名字，保留兼容）
uint16      tof_module_max_range_mm    (void);     // 当前量程（识别出 L0X 是 2000，L1X 是 4000）

// ============================================ 距离 ============================================
uint16      tof_module_distance_mm     (void);     // 滤波后的距离（mm），0 = 当前没有有效读数
uint16      tof_module_raw_mm          (void);     // 最近一帧的原始距离（mm），调试用
const char *tof_module_display_str     (void);     // 按「单位cm」参数格式化好的字符串，如 "312 mm" / "31 cm"
uint8       tof_module_unit_cm         (void);     // 1 = 当前按 cm 显示
uint8       tof_module_filter_level    (void);     // 0 原始 / 1 中值 / 2 中值+平滑

// ============================================ 统计（测距统计页用） ============================================
uint32      tof_module_sample_cnt      (void);     // 一共收到多少帧
uint32      tof_module_valid_cnt       (void);     // 其中有效多少帧
uint32      tof_module_invalid_cnt     (void);     // 其中无效多少帧
uint16      tof_module_rate_hz         (void);     // 帧率（每秒新数据个数）
uint16      tof_module_min_mm          (void);     // 复位统计以来的最小值（0 = 还没有有效值）
uint16      tof_module_max_mm          (void);     // 复位统计以来的最大值
uint16      tof_module_avg_mm          (void);     // 复位统计以来的平均值
uint16      tof_module_span_mm         (void);     // 波动 = 最大值 - 最小值
void        tof_module_reset_stats     (void);     // 清统计（不清报警和参数）

// ============================================ 报警 ============================================
uint8       tof_module_alarm           (void);     // 1 = 距离小于阈值（带连续确认 + 回差，不会抖）
uint16      tof_module_alarm_threshold_mm (void);  // 当前阈值（mm）

// ============================================ 参数（不接菜单时也能直接改） ============================================
void        tof_module_set_alarm_mm    (uint16 mm);        // 设报警距离（mm）
void        tof_module_set_unit_cm     (uint8 on);         // 1 = 按 cm 显示
void        tof_module_set_filter_level(uint8 level);      // 0~2，超过按 2 算

// ============================================ 曲线（实时测距页的画图用） ============================================
uint8       tof_module_history_num     (void);     // 现在存了几个点（最多 TOF_HISTORY_NUM）
uint16      tof_module_history_get     (uint8 index);      // index 0 = 最旧的点；越界返回 0

#endif
