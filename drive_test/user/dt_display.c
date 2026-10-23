/*********************************************************************************************************************
* 文件名            dt_display.c
* 说明              屏幕封装（IPS200 / TFT180 二选一），带中英混排显示
********************************************************************************************************************/
#include "zf_common_headfile.h"
#include "dt_config.h"
#include "dt_display.h"
#include "dt_cn_font.h"

static uint16 dt_pen_color = DT_COLOR_TEXT;
static uint16 dt_bg_color  = DT_COLOR_BG;
static uint16 dt_x_offset  = 0;             // 屏比 128 宽时（IPS200 = 240）把内容整体居中，排版按 128 宽写就够了

//-------------------------------------------------------------------------------------------------------------------
// 函数简介      屏幕初始化（按 dt_config.h 里的选择调用对应的库函数）
// 使用示例      dt_display_init();
//-------------------------------------------------------------------------------------------------------------------
void dt_display_init (void)
{
#if DT_DISPLAY_USE_IPS200
    ips200_init(IPS200_TYPE_SPI);
    ips200_set_font(IPS200_8X16_FONT);
    dt_pen_color = DT_COLOR_TEXT;
    dt_bg_color  = DT_COLOR_BG;
    ips200_set_color(dt_pen_color, dt_bg_color);
    ips200_clear();
    dt_x_offset = (ips200_width_max > 128) ? (uint16)((ips200_width_max - 128) / 2) : 0;
#else
    tft180_init();
    tft180_set_font(TFT180_8X16_FONT);
    dt_pen_color = DT_COLOR_TEXT;
    dt_bg_color  = DT_COLOR_BG;
    tft180_set_color(dt_pen_color, dt_bg_color);
    tft180_clear();
    dt_x_offset = (tft180_width_max > 128) ? (uint16)((tft180_width_max - 128) / 2) : 0;
#endif
}

void dt_display_clear (void)
{
#if DT_DISPLAY_USE_IPS200
    ips200_clear();
#else
    tft180_clear();
#endif
}

void dt_display_full (const uint16 color)
{
#if DT_DISPLAY_USE_IPS200
    ips200_full(color);
#else
    tft180_full(color);
#endif
}

void dt_display_color (const uint16 pen_color, const uint16 bg_color)
{
    dt_pen_color = pen_color;
    dt_bg_color  = bg_color;
#if DT_DISPLAY_USE_IPS200
    ips200_set_color(pen_color, bg_color);
#else
    tft180_set_color(pen_color, bg_color);
#endif
}

void dt_display_char (uint16 x, uint16 y, const char dat)
{
    x = (uint16)(x + dt_x_offset);
    if((x + 8 > dt_display_width()) || (y + 16 > dt_display_height()))     // 越界会让库里的断言卡死，先自己挡掉
    {
        return;
    }
#if DT_DISPLAY_USE_IPS200
    ips200_show_char(x, y, dat);
#else
    tft180_show_char(x, y, dat);
#endif
}

void dt_display_string (uint16 x, uint16 y, const char *str)
{
    uint16 px = x;
    uint8  i  = 0;
    while('\0' != str[i])
    {
        dt_display_char(px, y, str[i]);
        px += 8;
        i  += 1;
    }
}

void dt_display_cn (uint16 x, uint16 y, const char *str)
{
    uint16      px  = x;
    uint8       i   = 0;
    const uint8 *dot = NULL;

    while('\0' != str[i])
    {
        if((0x80 > (uint8)str[i]) || ('\0' == str[i + 1]))                  // ASCII：'\0' 判断防止把结尾半个汉字当双字节
        {
            dt_display_char(px, y, str[i]);
            px += 8;
            i  += 1;
        }
        else
        {
            if((uint16)(px + dt_x_offset + 16) > dt_display_width())         // 这一行写满了就不画了，防止断言
            {
                return;
            }
            dot = dt_cn_font_get((uint8)str[i], (uint8)str[i + 1]);
            if(NULL != dot)
            {
#if DT_DISPLAY_USE_IPS200
                ips200_show_chinese((uint16)(px + dt_x_offset), y, 16, dot, 1, dt_pen_color);
#else
                tft180_show_chinese((uint16)(px + dt_x_offset), y, 16, dot, 1, dt_pen_color);
#endif
            }
            px += 16;
            i  += 2;
        }
    }
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介      数字位数按屏宽收一下，防止超出右边缘（超了库里的断言会卡死）
//-------------------------------------------------------------------------------------------------------------------
static uint8 dt_digits_fit (uint16 x, uint8 num)
{
    uint16 px = (uint16)(x + dt_x_offset);
    uint8  n  = num;

    while((n > 1) && ((uint16)(px + (uint16)n * 8) > dt_display_width()))
    {
        n--;
    }
    return n;
}

void dt_display_int (uint16 x, uint16 y, const int32 dat, uint8 num)
{
    num = dt_digits_fit(x, num);
    x   = (uint16)(x + dt_x_offset);
#if DT_DISPLAY_USE_IPS200
    ips200_show_int(x, y, dat, num);
#else
    tft180_show_int(x, y, dat, num);
#endif
}

void dt_display_uint (uint16 x, uint16 y, const uint32 dat, uint8 num)
{
    num = dt_digits_fit(x, num);
    x   = (uint16)(x + dt_x_offset);
#if DT_DISPLAY_USE_IPS200
    ips200_show_uint(x, y, dat, num);
#else
    tft180_show_uint(x, y, dat, num);
#endif
}

void dt_display_float (uint16 x, uint16 y, const double dat, uint8 num, uint8 pointnum)
{
    num = dt_digits_fit(x, num);
    x   = (uint16)(x + dt_x_offset);
#if DT_DISPLAY_USE_IPS200
    ips200_show_float(x, y, dat, num, pointnum);
#else
    tft180_show_float(x, y, dat, num, pointnum);
#endif
}

void dt_display_line (uint16 x_start, uint16 y_start, uint16 x_end, uint16 y_end, const uint16 color)
{
#if DT_DISPLAY_USE_IPS200
    ips200_draw_line(x_start, y_start, x_end, y_end, color);
#else
    tft180_draw_line(x_start, y_start, x_end, y_end, color);
#endif
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介      显示一帧摄像头灰度图（188x120 缩到 128x82 放在左上角）
// 使用示例      dt_display_image(mt9v03x_image[0]);
//-------------------------------------------------------------------------------------------------------------------
void dt_display_image (const uint8 *image)
{
#if DT_DISPLAY_USE_IPS200
    ips200_show_gray_image(dt_x_offset, 0, image, MT9V03X_W, MT9V03X_H, 128, 82, 0);
#else
    tft180_show_gray_image(dt_x_offset, 0, image, MT9V03X_W, MT9V03X_H, 128, 82, 0);
#endif
}

uint16 dt_display_width (void)
{
#if DT_DISPLAY_USE_IPS200
    return ips200_width_max;
#else
    return tft180_width_max;
#endif
}

uint16 dt_display_height (void)
{
#if DT_DISPLAY_USE_IPS200
    return ips200_height_max;
#else
    return tft180_height_max;
#endif
}
