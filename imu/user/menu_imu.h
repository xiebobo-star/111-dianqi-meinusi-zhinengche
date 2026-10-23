/*********************************************************************************************************************
* 文件名            menu_imu.h
* 说明              菜单里的 4 个陀螺仪页面（实时刷新，由 menu_ui.c 分派）
*                     MENU_IMU_PAGE_ATT     姿态显示：roll / pitch / yaw + 静止状态
*                     MENU_IMU_PAGE_CALIB   陀螺校准：算零漂、存 Flash、清零漂
*                     MENU_IMU_PAGE_ODOM    里程定位：X / Y / 里程 + 轨迹小地图（宽屏）
*                     MENU_IMU_PAGE_WP      航点记录：列表 / 记录 / 删除 / 清空
* 用法              menu_ui.c 里只调 3 个函数：draw / live / key
********************************************************************************************************************/
#ifndef _menu_imu_h_
#define _menu_imu_h_

#include "zf_common_headfile.h"

#define MENU_IMU_PAGE_ATT           (0)
#define MENU_IMU_PAGE_CALIB         (1)
#define MENU_IMU_PAGE_ODOM          (2)
#define MENU_IMU_PAGE_WP            (3)
#define MENU_IMU_PAGE_NUM           (4)

void  menu_imu_enter   (uint8 page);                                            // 进入页面：复位页面内部状态并整屏画一次
void  menu_imu_draw    (uint8 page);                                            // 整屏重画
void  menu_imu_live    (uint8 page);                                            // 周期局部刷新（每 100ms 由主循环调用）
uint8 menu_imu_key     (uint8 page, uint8 k_up, uint8 k_down, uint8 k_ok, uint8 k_ok_long, uint8 k_back);
                                                                                // 返回 0=留在本页 1=退回菜单 2=跳到校准页

#endif
