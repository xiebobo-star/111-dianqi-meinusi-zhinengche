/*********************************************************************************************************************
* 文件名            car_tune.h
* 说明              运行期参数桥：菜单参数表里的值 -> 控制算法用的量
*
*   原来的 car_config.h 把这些都写成编译期宏（改一次要重新编译烧录）。
*   这个工程里它们改从菜单参数读，所以「菜单里改完 -> 立刻生效 -> 掉电保存」。
*
*   速度在菜单里是 mm/s（人看着直观），控制算法内部是「编码器脉冲 / 10ms」，
*   两者的换算靠「每圈脉冲」和「轮径」两个参数，换算函数也在这里。
*********************************************************************************************************************/
#ifndef CAR_TUNE_H
#define CAR_TUNE_H

#include "zf_common_headfile.h"

// ---- 舵机 ----
uint16 car_tune_servo_mid_us     (void);        // 中位脉宽 us
uint16 car_tune_servo_min_us     (void);        // 左限（打满一侧）
uint16 car_tune_servo_max_us     (void);        // 右限
float  car_tune_steer_kp         (void);        // 转向 PD：us / 归一化误差
float  car_tune_steer_kd         (void);
float  car_tune_turn_ratio       (void);        // 差速比例 0~0.6

// ---- 速度 / 电机 ----
float  car_tune_speed_straight   (void);        // 直道目标，单位：脉冲 / 10ms
float  car_tune_speed_bend       (void);
float  car_tune_speed_cautious   (void);
float  car_tune_speed_kp         (void);
float  car_tune_speed_ki         (void);
float  car_tune_pwm_limit        (void);        // PWM 上限（10000 = 100%）

// ---- 圆环 ----
uint8  car_tune_ring_enable      (void);
int16  car_tune_ring_dir         (void);        // 0 自动 / -1 左 / +1 右
float  car_tune_ring_speed       (void);        // 环内限速，单位：脉冲 / 10ms
uint32 car_tune_ring_timeout_ms  (void);

// ---- 电机 / 编码器方向 ----
uint8  car_tune_motor_inv_l      (void);
uint8  car_tune_motor_inv_r      (void);
int8   car_tune_enc_sign_l       (void);
int8   car_tune_enc_sign_r       (void);

// ---- 单位换算 ----
float  car_tune_mm_s_to_counts   (float mm_s);     // mm/s -> 脉冲/10ms
float  car_tune_counts_to_mm_s   (float counts);   // 脉冲/10ms -> mm/s（带 x100，只能给速度用）
float  car_tune_counts_to_mm     (float counts);   // 脉冲 -> mm（里程用，不带 x100）
float  car_tune_wheel_mm         (void);        // 轮径 mm
float  car_tune_cpr              (void);        // 车轮转一圈的脉冲数

#endif
