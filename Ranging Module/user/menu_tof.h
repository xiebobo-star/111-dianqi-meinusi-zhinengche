/*********************************************************************************************************************
* 文件名            menu_tof.h
* 说明              菜单里的两个测距页面（实时刷新，由 menu_ui.c 分派）
*                     MENU_TOF_PAGE_LIVE     实时测距：距离大数字 + 量程条 + 曲线 + 阈值线
*                     MENU_TOF_PAGE_STATS    测距统计：型号 / 滤波 / 帧数 / 帧率 / 最小最大平均 / 状态
*
* 用法              menu_ui.c 里只调 4 个函数：enter / draw / live / key
*                   enter 进入页面整屏画一次；live 主循环每 100ms 调一次（只重画数字，不整屏清屏）
********************************************************************************************************************/
#ifndef _menu_tof_h_
#define _menu_tof_h_

#include "zf_common_headfile.h"

#define MENU_TOF_PAGE_LIVE          (0)                                            // 实时测距
#define MENU_TOF_PAGE_STATS         (1)                                            // 测距统计
#define MENU_TOF_PAGE_NUM           (2)

void  menu_tof_enter (uint8 page);                                                 // 进入页面：复位页面状态并整屏画一次
void  menu_tof_draw  (uint8 page);                                                 // 整屏重画
void  menu_tof_live  (uint8 page);                                                 // 周期局部刷新（每 100ms 由主循环调用）
uint8 menu_tof_key   (uint8 page, uint8 k_up, uint8 k_down, uint8 k_ok, uint8 k_back);
                                                                                   // 返回 0 = 留在本页，1 = 退回菜单

#endif
