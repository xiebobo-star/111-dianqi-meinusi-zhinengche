/*********************************************************************************************************************
* 文件名            menu_display.h
* 说明              屏幕封装（主板「屏幕1」接口，SPI2：SCK=P15_3 MOSI=P15_5 CS=P15_2 RST=P15_1 DC=P15_0 BLK=P15_4）
*                   支持两种屏：2 寸 IPS200 / 1.8 寸 TFT180，在 menu_config.h 里用 MENU_DISPLAY_USE_IPS200 选
*                   中文字库在 menu_font.c 里，汉字用 menu_display_cn() 自动中英混排
* 用法              menu_display_init();
*                   menu_display_cn(4, 38, "电机参数");
*                   menu_display_fill(0, 36, 128, 18, MENU_COLOR_SEL);     // 画高亮条
********************************************************************************************************************/
#ifndef _menu_display_h_
#define _menu_display_h_

#include "zf_common_headfile.h"

void    menu_display_init       (void);
void    menu_display_clear      (void);
void    menu_display_set_color  (const uint16 pen_color, const uint16 bg_color);
void    menu_display_char       (uint16 x, uint16 y, const char dat);
void    menu_display_string     (uint16 x, uint16 y, const char *str);
void    menu_display_cn         (uint16 x, uint16 y, const char *str);       // 中英混排（汉字 16 像素宽）
void    menu_display_int        (uint16 x, uint16 y, const int32 dat, uint8 num);
void    menu_display_fill       (uint16 x, uint16 y, uint16 w, uint16 h, const uint16 color);   // 填充矩形
void    menu_display_frame      (uint16 x, uint16 y, uint16 w, uint16 h, const uint16 color);   // 矩形边框
void    menu_display_line       (uint16 x_start, uint16 y_start, uint16 x_end, uint16 y_end, const uint16 color);
uint16  menu_display_width      (void);
uint16  menu_display_height     (void);
uint16  menu_display_text_width (const char *str);                          // 中英混排文本的像素宽度

#endif
