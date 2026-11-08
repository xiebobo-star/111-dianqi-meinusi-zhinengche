/*********************************************************************************************************************
* 文件名            menu_camera.h
* 说明              菜单里的两个摄像头页面（由 menu_ui.c 分派）
*                     MENU_CAM_PAGE_IMAGE   图像显示：原图 / 二值图 + 曝光 / 阈值 / 帧率 / 亮度均值
*                     MENU_CAM_PAGE_LINE    循迹识别：二值图 + 每行中线叠加 + 偏差 / 状态 / 找到几行
*
* 用法              menu_ui.c 里只调 4 个函数：enter / draw / live / key
*                   enter 进去时初始化摄像头（懒初始化：没进这个页面就不占 P00/P02 口）
*                   live  主循环每圈调一次，来一帧画一帧
*
* 按键              按键1 = 曝光 +      按键2 = 曝光 -
*                   按键3 = 图像页：灰度 / 二值切换   循迹页：阈值 +10（加到 240 之后回自动）
*                   长按按键3 = 冻结 / 恢复画面（调曝光、阈值时把画面定住看）
*                   按键4 = 返回菜单
*********************************************************************************************************************/
#ifndef _menu_camera_h_
#define _menu_camera_h_

#include "zf_common_headfile.h"

#define MENU_CAM_PAGE_IMAGE         (0)                                         // 图像显示
#define MENU_CAM_PAGE_LINE          (1)                                         // 循迹识别
#define MENU_CAM_PAGE_NUM           (2)

void  menu_cam_enter (uint8 page);                                              // 进入页面：初始化摄像头并整屏画一次
void  menu_cam_draw  (uint8 page);                                              // 整屏重画
void  menu_cam_live  (uint8 page);                                              // 有新图就识别 + 重画（主循环每圈调）
uint8 menu_cam_key   (uint8 page, uint8 k_up, uint8 k_down, uint8 k_ok, uint8 k_ok_long, uint8 k_back);
                                                                                // 返回 0 = 留在本页，1 = 退回菜单
                                                                                // k_ok_long = 长按按键3：冻结 / 恢复画面

#endif
