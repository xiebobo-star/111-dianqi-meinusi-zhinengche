/*********************************************************************************************************************
* 文件名            dt_display.h
* 说明              屏幕封装：把 IPS200 / TFT180 两种屏合成一套接口，是哪种屏在 dt_config.h 里选
*                   自己带中文字库（dt_cn_font.c），汉字 16 像素宽、ASCII 8 像素宽
* 用法              dt_display_init();
*                   dt_display_clear();
*                   dt_display_cn(0, 22, "电机左: 3000");
*                   dt_display_int(72, 22, 3000, 5);
********************************************************************************************************************/
#ifndef _dt_display_h_
#define _dt_display_h_

#include "zf_common_headfile.h"
#include "dt_config.h"

#define DT_COLOR_BG                 (RGB565_WHITE)
#define DT_COLOR_TEXT               (RGB565_BLACK)
#define DT_COLOR_SEL                (RGB565_BLUE)
#define DT_COLOR_LINE               (RGB565_GRAY)
#define DT_COLOR_OK                 (RGB565_GREEN)
#define DT_COLOR_ERR                (RGB565_RED)

void   dt_display_init       (void);
void   dt_display_clear      (void);
void   dt_display_full       (const uint16 color);
void   dt_display_color      (const uint16 pen_color, const uint16 bg_color);
void   dt_display_char       (uint16 x, uint16 y, const char dat);
void   dt_display_string     (uint16 x, uint16 y, const char *str);
void   dt_display_cn         (uint16 x, uint16 y, const char *str);        // 汉字 + ASCII 混排
void   dt_display_int        (uint16 x, uint16 y, const int32 dat, uint8 num);
void   dt_display_uint       (uint16 x, uint16 y, const uint32 dat, uint8 num);
void   dt_display_float      (uint16 x, uint16 y, const double dat, uint8 num, uint8 pointnum);
void   dt_display_line       (uint16 x_start, uint16 y_start, uint16 x_end, uint16 y_end, const uint16 color);
void   dt_display_image      (const uint8 *image);                        // 摄像头灰度图 128x82 铺在左上角
uint16 dt_display_width      (void);
uint16 dt_display_height     (void);

#endif
