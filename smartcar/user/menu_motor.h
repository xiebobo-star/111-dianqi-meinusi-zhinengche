/*********************************************************************************************************************
* 文件名            menu_motor.h
* 说明              菜单里的两个电机 / 编码器页面（由 menu_ui.c 分派）
*                     MENU_MOTOR_PAGE_MOTOR   电机输出：两轮 / 单轮占空比加减，同时看两轮编码器速度
*                     MENU_MOTOR_PAGE_ENC     编码器：本周期脉冲 / 累计脉冲 / 速度 mm/s / 里程，可清零
*
* 用法              menu_ui.c 里只调 4 个函数：enter / draw / live / key
*                   懒初始化：第一次进页面才 pwm_init，没进就不出波（电机绝不自己转）
*
* 硬件              主板「电机信号1」 左 = P21_2(PWM) + P21_3(DIR)   右 = P21_4(PWM) + P21_5(DIR)
*                   驱动板 DRV8701E（PH/EN）：PWM 进 EN 调速、DIR 进 PH 定方向；板上还有个使能拨动开关
*                   编码器1 A=P02_8 B=P00_9 (TIM4)   编码器2 A=P10_3 B=P10_1 (TIM5)
*
* 按键              按键1 = 占空比 +     按键2 = 占空比 -
*                   按键3 = 切换 两轮 / 左轮 / 右轮     编码器页：清零
*                   按键4 = 返回菜单（退出前一定先把电机停掉）
*********************************************************************************************************************/
#ifndef _menu_motor_h_
#define _menu_motor_h_

#include "zf_common_headfile.h"

#define MENU_MOTOR_PAGE_MOTOR       (0)                                         // 电机输出
#define MENU_MOTOR_PAGE_ENC         (1)                                         // 编码器
#define MENU_MOTOR_PAGE_NUM         (2)

void  menu_motor_enter (uint8 page);                                            // 进入页面：初始化 PWM 并整屏画一次
void  menu_motor_draw  (uint8 page);                                            // 整屏重画
void  menu_motor_live  (uint8 page);                                            // 周期刷新数字（主循环 200ms 一次）
uint8 menu_motor_key   (uint8 page, uint8 k_up, uint8 k_down, uint8 k_ok, uint8 k_back);   // 返回 0 = 留在本页，1 = 退回菜单
void  menu_motor_stop_all (void);                                               // 立刻停两个轮子（长按 4 回主菜单时 menu_ui 会调它）

#endif
