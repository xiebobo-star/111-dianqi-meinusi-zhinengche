/*********************************************************************************************************************
* 文件名            imu_odom.h
* 说明              里程计：编码器路程 + 陀螺仪航向 -> 相对于「原点」的 (x, y) 坐标
*                   同时维护一条轨迹（间隔采样，供屏幕画路径）和最多 16 个航点（可存 Flash）
* 用法              imu_odom_init();                       // 上电一次
*                   // imu_odom_update() 由 10ms 定时中断调用
*                   imu_odom_reset();                      // 当前点设为原点：位置清零 + yaw 归零 + 清轨迹
*                   imu_odom_mark();                       // 把当前位置记成一个航点，返回航点个数
*                   imu_odom_x_mm(); imu_odom_y_mm();      // 读相对坐标（mm）
********************************************************************************************************************/
#ifndef _imu_odom_h_
#define _imu_odom_h_

#include "zf_common_headfile.h"

void   imu_odom_init        (void);
void   imu_odom_update      (void);                                 // 10ms 定时中断里调用
void   imu_odom_reset       (void);                                 // 原点归零（位置 + 航向 + 轨迹）
void   imu_odom_trail_clear (void);                                 // 只清轨迹，不动坐标

float  imu_odom_x_mm        (void);                                 // 当前 X（mm，向右为正）
float  imu_odom_y_mm        (void);                                 // 当前 Y（mm，向前为正）
float  imu_odom_dist_mm     (void);                                 // 上电以来走过的总路程（mm）
float  imu_odom_speed_mm_s  (void);                                 // 当前车速（mm/s）

uint16 imu_odom_trail_num   (void);                                 // 现在有几个轨迹点
void   imu_odom_trail_get   (uint16 index, int16 *x, int16 *y);      // index 0 = 最老的点

uint8  imu_odom_wp_mark     (void);                                 // 记录当前位置，返回记录后的航点总数（0 = 已满）
uint8  imu_odom_wp_num      (void);                                 // 已经记录了几个航点
void   imu_odom_wp_get      (uint8 index, int16 *x, int16 *y, int16 *yaw10);  // yaw10 = 航向 x10（度）
void   imu_odom_wp_clear    (void);                                 // 清空所有航点
void   imu_odom_wp_set      (uint8 index, int16 x, int16 y, int16 yaw10);     // Flash 载入时逐个填
void   imu_odom_wp_num_set  (uint8 num);

#endif
