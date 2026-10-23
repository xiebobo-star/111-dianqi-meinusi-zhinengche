/*********************************************************************************************************************
* 文件名            imu_store.h
* 说明              陀螺仪零漂 + 航点的掉电保存（TC264 片内 DFlash 第 8 / 9 页，双页备份）
*                   和菜单参数（menu_flash.c，用的是第 10 / 11 页）互不干扰
* 用法              imu_store_load();     // 上电读一次：有数据就覆盖零漂和航点
*                   imu_store_save();     // 校准完 / 记录航点后调用，0 = 成功
* 注意              擦写一次是 ms 级阻塞 + Flash 寿命约 10 万次，只在用户按键时写，绝不能在中断/循环里写
********************************************************************************************************************/
#ifndef _imu_store_h_
#define _imu_store_h_

#include "zf_common_headfile.h"

uint8   imu_store_load        (void);       // 1 = 从 Flash 载入了零漂和航点，0 = Flash 里没有有效数据
uint8   imu_store_save        (void);       // 0 = 保存成功，1 = 保存失败（回读校验没过）
uint8   imu_store_valid       (void);       // 1 = 本次上电已经从 Flash 读到过数据
uint32  imu_store_seq         (void);       // 当前数据是第几次保存（越大越新）
uint8   imu_store_save_count  (void);       // 本次上电保存成功的次数
uint8   imu_store_last_result (void);       // 0 = 还没保存过，1 = 上次成功，2 = 上次失败

#endif
