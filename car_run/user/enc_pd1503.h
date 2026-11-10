/*********************************************************************************************************************
* 文件名            enc_pd1503.h
* 说明              编码器模块（PD-1503 磁编 / 通用增量编码器）：把原始脉冲换成真实单位
*
*   硬件   编码器1  A=P02_8  B=P00_9  (TIM4)        编码器2  A=P10_3  B=P10_1  (TIM5)
*
*   ★ 接法只有一种：A 相（脉冲）接计数脚，B 相 / DIR 接方向脚，用逐飞库的「脉冲 + 方向」模式读。
*     - PD-1503-SDI（带方向）：DIR 就是方向脚
*     - PD-1503-ENC（正交 AB）：B 相在 A 相上升沿的电平本身就代表方向，接方向脚上一样能读，
*       只是按 1 倍频计数（不享 4 倍频）。每圈脉冲靠下面的实测标定，倍频不影响准确性。
*     （逐飞库 encoder_quad_init() 里有 zf_assert(encoder_n <= TIM4_ENCODER)，TIM5 进不去会直接卡死，
*       所以右轮不可能走正交模式 —— 这也是统一用「脉冲 + 方向」的原因）
*
*   真实单位靠两个菜单参数换算：
*     「每圈脉冲」= 车轮转一整圈有多少个脉冲（★标定方法：把车架起来，看屏上「编码总脉冲」，
*                   手推轮子正好转一圈，读数差多少就填多少；齿轮比、倍频都已经算在这个数里）
*     「轮径」    = 车轮直径 mm
*   所以换轮子 / 换齿轮 / 换接法，只改参数，不用改代码。
*
* 用法            enc_init();                  // 上电一次（参数已经读好之后）
*                 enc_update();                // 10ms 定时中断里调用（读脉冲 -> 清零 -> 换算）
*                 int32 c = enc_counts(ENC_L);          // 本周期脉冲（带符号）
*                 float v = enc_speed_mm_s(ENC_L);      // 滤波后的速度 mm/s
*                 float d = enc_dist_mm(ENC_L);         // 累计里程 mm
*********************************************************************************************************************/
#ifndef ENC_PD1503_H
#define ENC_PD1503_H

#include "zf_common_headfile.h"

#define ENC_L                   (0)
#define ENC_R                   (1)
#define ENC_NUM                 (2)

void   enc_init          (void);                    // 上电初始化（按菜单参数选方向/正交模式）
void   enc_update        (void);                    // 10ms 中断：读脉冲、清零、滤波、累计
void   enc_clear         (void);                    // 计数和累计里程清零
int32  enc_counts        (uint8 idx);               // 本周期脉冲数（已按参数取反）
float  enc_speed_cpt     (uint8 idx);               // 滤波后的速度：脉冲 / 10ms（控制算法用的单位）
float  enc_speed_mm_s    (uint8 idx);               // 滤波后的速度 mm/s
int32  enc_total         (uint8 idx);               // 累计脉冲（标定「每圈脉冲」时看它）
float  enc_dist_mm       (uint8 idx);               // 累计里程 mm
uint8  enc_abnormal      (void);                    // 1 = 最近一次读数异常大（线序错/干扰）

// ---- 编码自检用（菜单「系统信息 -> 编码自检」显示）----
// 在 10ms 中断里顺带采 4 个脚的电平，1 秒统计一次占空比：
//   转轮子时 A/B 相是方波 -> 占空比 ≈ 50%      停着不动 -> 0% 或 100%
//   所以「一直是 0% 或 100%」= 这个脚上没有信号（没供电 / 线没接 / 磁铁没装 / 接错脚）
#define ENC_DBG_A_L         (0)
#define ENC_DBG_B_L         (1)
#define ENC_DBG_A_R         (2)
#define ENC_DBG_B_R         (3)
#define ENC_DBG_NUM         (4)
uint8  enc_pin_duty      (uint8 which);             // 最近 1 秒该脚为高的比例 0~100
uint8  enc_pin_level     (uint8 which);             // 该脚当前电平 0/1

#endif
