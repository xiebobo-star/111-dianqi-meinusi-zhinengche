/*********************************************************************************************************************
* 文件名            menu_run.h
* 说明              菜单里的两个「干活」页面（由 menu_ui.c 分派）
*                     MENU_RUN_PAGE_RACE   循迹跑车：图像 + 中线 + 状态 / 速度 / PWM / 舵机，按键3 启动停车
*                     MENU_RUN_PAGE_CAM    摄像头调试：只看图，调曝光 / 阈值 / 灰度二值
*
* 用法              menu_ui.c 只调 5 个函数：enter / draw / live / key / stop_all
*                   live 由主循环每圈调用：有新帧就识别（跑车页还会把结果送进控制），屏上 5Hz 刷新
*
* 按键              跑车页：1/2 = 停车时调曝光   3 = 启动 / 停车   长按3 = 里程清零   4 = 停车返回
*                   摄像头页：1/2 = 曝光   3 = 灰度/二值   长按3 = 冻结画面   4 = 返回
*                   （任何页长按 4 都能直接回主菜单，menu_ui 会先停车）
*********************************************************************************************************************/
#ifndef _menu_run_h_
#define _menu_run_h_

#include "zf_common_headfile.h"

#define MENU_RUN_PAGE_RACE          (0)                     // 循迹跑车
#define MENU_RUN_PAGE_CAM           (1)                     // 摄像头调试
#define MENU_RUN_PAGE_NUM           (2)

void  menu_run_enter    (uint8 page);                       // 进页面：初始化摄像头并整屏画一次
void  menu_run_draw     (uint8 page);                       // 整屏重画
void  menu_run_live     (uint8 page);                       // 主循环每圈调（收帧 / 识别 / 控制 / 5Hz 刷屏）
uint8 menu_run_key      (uint8 page, uint8 k_up, uint8 k_down, uint8 k_ok, uint8 k_ok_long, uint8 k_back);
void  menu_run_stop_all (void);                             // 立刻停车（长按 4 回主菜单 / 离开页面都走这里）

#endif
