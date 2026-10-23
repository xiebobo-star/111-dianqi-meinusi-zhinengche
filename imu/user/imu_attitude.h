/*********************************************************************************************************************
* 文件名            imu_attitude.h
* 说明              IMU660RA 姿态解算（互补滤波）对外接口
* 用法              imu_attitude_init();                   // 上电一次（内部 imu660ra_init + 角度清零）
*                   imu_attitude_update(moving);           // ★ 10ms 定时中断里调用，不要放主循环
*                   float r = imu_attitude_roll();         // 读横滚角（度）
* 坐标系            X = 车头，Y = 车左，Z = 向上；yaw 逆时针（左转）为正
********************************************************************************************************************/
#ifndef _imu_attitude_h_
#define _imu_attitude_h_

#include "zf_common_headfile.h"

void   imu_attitude_init        (void);                             // 初始化 IMU（失败也能跑，只是角度一直是 0）
void   imu_attitude_update      (uint8 moving);                     // 10ms 周期调用：moving = 1 表示轮子在转

uint8  imu_attitude_ok          (void);                             // 1 = IMU 通信正常
float  imu_attitude_roll        (void);                             // 横滚角 度（左倾为正）
float  imu_attitude_pitch       (void);                             // 俯仰角 度（抬头为正）
float  imu_attitude_yaw         (void);                             // 航向角 度（-180 ~ 180，逆时针为正）
float  imu_attitude_yaw_rad     (void);                             // 航向角 弧度
void   imu_attitude_yaw_set     (float deg);                        // 把当前航向改成 deg（通常传 0，即「现在这个方向当正前方」）

float  imu_attitude_gyro        (uint8 axis);                       // 去零漂后的角速度 dps，axis 0=X 1=Y 2=Z
float  imu_attitude_acc         (uint8 axis);                       // 加速度 g，axis 0=X 1=Y 2=Z
uint8  imu_attitude_still       (void);                             // 1 = 判定为静止

uint8  imu_attitude_calibrate   (void);                             // 采 1 秒求零漂：0=成功 1=零漂超范围 2=采样时在动
void   imu_attitude_bias_get    (float *x, float *y, float *z);     // 读当前零漂 dps
void   imu_attitude_bias_set    (float x, float y, float z);        // 写零漂（Flash 载入时用）
void   imu_attitude_bias_clear  (void);                             // 零漂清零（等于没校准）
uint8  imu_attitude_calib_done  (void);                             // 1 = 本次上电的零漂是校准过的
uint32 imu_attitude_sample_cnt  (void);                             // 已经从解算了多少次（判断中断有没有在跑）

#endif
