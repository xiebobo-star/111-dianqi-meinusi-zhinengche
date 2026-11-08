/*********************************************************************************************************************
* 文件名            enc_pd1503.h
* 说明              编码器模块（PD-1503 磁编 / 通用增量编码器）：把原始脉冲换成真实单位
*
*   硬件   编码器1  A=P02_8  B=P00_9  (TIM4)        编码器2  A=P10_3  B=P10_1  (TIM5)
*
*   两种接法，菜单「编码模式」切换，不用改代码：
*     0 = 脉冲 + 方向（PD-1503-SDI / 智能车常见的「1024 线带方向」mini 编码器）
*     1 = 正交 AB（PD-1503-ENC 双脉冲型号，4 倍频由逐飞库做）
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

#endif
