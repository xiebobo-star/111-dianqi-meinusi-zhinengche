/*********************************************************************************************************************
* 文件名            imu_encoder.h
* 说明              两路编码器（主板「编码器1 / 编码器2」接口：P02_8/P00_9 与 P10_3/P10_1）
*                   方向解码模式：A 脚数脉冲、B 脚判方向，所以倒车读出来是负数
* 用法              imu_encoder_init();                        // 上电一次
*                   // imu_encoder_update() 由 10ms 定时中断自动调用，主循环不用管
*                   int32 total = imu_encoder_total(IMU_ENC_LEFT);   // 累计脉冲（里程计用它）
*                   uint8 m = imu_encoder_moving();                  // 1 = 至少一个轮子在转
********************************************************************************************************************/
#ifndef _imu_encoder_h_
#define _imu_encoder_h_

#include "zf_common_headfile.h"

typedef enum
{
    IMU_ENC_LEFT  = 0,
    IMU_ENC_RIGHT = 1,
    IMU_ENC_NUM,
}imu_enc_id_enum;

void  imu_encoder_init       (void);
void  imu_encoder_update     (void);                                    // 10ms 定时中断里调用
void  imu_encoder_clear      (void);                                    // 计数 / 累计全清零（原点归零时用）
int16 imu_encoder_count      (imu_enc_id_enum enc_n);                   // 最近一个周期的脉冲数
int32 imu_encoder_total      (imu_enc_id_enum enc_n);                   // 累计脉冲
int32 imu_encoder_speed      (imu_enc_id_enum enc_n);                   // 脉冲/秒
float imu_encoder_speed_mm_s (imu_enc_id_enum enc_n);                   // mm/s
uint8 imu_encoder_moving     (void);                                    // 1 = 有轮子在转

// 编码自检：A/B 两相引脚的电平与「上 1 秒里读到高的比例%」
// 只用来判断「信号到底有没有到芯片」，不参与控制
typedef enum
{
    IMU_ENC_PIN_A_LEFT  = 0,
    IMU_ENC_PIN_B_LEFT  = 1,
    IMU_ENC_PIN_A_RIGHT = 2,
    IMU_ENC_PIN_B_RIGHT = 3,
    IMU_ENC_PIN_NUM,
}imu_enc_pin_enum;

uint8 imu_encoder_pin_duty  (imu_enc_pin_enum pin_n);                   // 占空比 0~100（%）
uint8 imu_encoder_pin_level (imu_enc_pin_enum pin_n);                   // 最近一次读到的电平

#endif
