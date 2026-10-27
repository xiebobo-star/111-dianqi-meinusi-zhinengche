/*********************************************************************************************************************
* 文件名            menu_display.c
* 说明              屏幕封装实现。IPS200 与 TFT180 的函数名一一对应，所以这里用宏套了一层统一接口
*                   汉字显示：点阵表在 menu_font.c，格式与逐飞库 *_show_chinese 一致（逐行取模、高位在左）
********************************************************************************************************************/
#include "menu_config.h"
#include "menu_display.h"
#include "menu_font.h"

static uint16 menu_display_pen_color = MENU_COLOR_PEN;      // 当前画笔颜色（汉字显示要单独传颜色）

// ============================================ 快速填充（一整块灌同一个颜色） ============================================
// 逐飞库对外只有 ips200_draw_point / ips200_draw_line，画一个填充矩形就只能一个点一个点地画：
// 一条 240x18 的高亮条 = 4000 多次「设显示区域 + 写一个像素」，屏上看着就是「刷得慢」。
// 库里真正快的那套（ips200_set_region + 连续写显存）是 static 函数，文件外面调不到，
// 所以这里按库里的命令序列自己发一遍：0x2A 设列范围 / 0x2B 设行范围 / 0x2C 开始写显存，
// 然后一次灌一大块颜色。整块区域只设一次，比逐点画快十几倍。
// 注意：本工程用的是 IPS200_TYPE_SPI，所以这里直接按 SPI 时序发命令。
#if (MENU_DISPLAY_USE_IPS200)
#define MENU_FILL_CHUNK         (64)                        // 一次灌多少个像素（64 个 = 128 字节）
static uint16 menu_fill_color_buf[MENU_FILL_CHUNK];

static void menu_display_set_region (uint16 x1, uint16 y1, uint16 x2, uint16 y2)
{
    IPS200_DC(0); spi_write_8bit(IPS200_SPI, 0x2A); IPS200_DC(1);       // 列地址范围
    spi_write_16bit(IPS200_SPI, x1);
    spi_write_16bit(IPS200_SPI, x2);

    IPS200_DC(0); spi_write_8bit(IPS200_SPI, 0x2B); IPS200_DC(1);       // 行地址范围
    spi_write_16bit(IPS200_SPI, y1);
    spi_write_16bit(IPS200_SPI, y2);

    IPS200_DC(0); spi_write_8bit(IPS200_SPI, 0x2C); IPS200_DC(1);       // 开始写显存
}
#endif

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     屏幕初始化
// 备注信息     屏幕初始化之后，逐飞库的 zf_assert / zf_log 也会直接显示到屏幕上（不开串口也能看到出错的文件行号）
//-------------------------------------------------------------------------------------------------------------------
void menu_display_init (void)
{
#if (MENU_DISPLAY_USE_IPS200)
    ips200_init(IPS200_TYPE_SPI);                           // 2 寸 IPS200，SPI 驱动
    ips200_set_dir(IPS200_PORTAIT);                         // 竖屏 240 x 320
    ips200_set_font(IPS200_8X16_FONT);                      // 8x16 ASCII 字体（汉字另外用 16x16 点阵）
    ips200_set_color(MENU_COLOR_PEN, MENU_COLOR_BG);
#else
    tft180_init();                                          // 1.8 寸 TFT180
    tft180_set_dir(TFT180_PORTAIT);                         // 竖屏 128 x 160
    tft180_set_font(TFT180_8X16_FONT);
    tft180_set_color(MENU_COLOR_PEN, MENU_COLOR_BG);
#endif
    menu_display_pen_color = MENU_COLOR_PEN;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     清屏
//-------------------------------------------------------------------------------------------------------------------
void menu_display_clear (void)
{
#if (MENU_DISPLAY_USE_IPS200)
    ips200_clear();
#else
    tft180_clear();
#endif
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     设置画笔颜色与背景色
// 参数说明     bg_color    背景色一定要写，否则屏幕上原来这一块的字盖不掉
//-------------------------------------------------------------------------------------------------------------------
void menu_display_set_color (const uint16 pen_color, const uint16 bg_color)
{
    menu_display_pen_color = pen_color;
#if (MENU_DISPLAY_USE_IPS200)
    ips200_set_color(pen_color, bg_color);
#else
    tft180_set_color(pen_color, bg_color);
#endif
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     显示一个 ASCII 字符
//-------------------------------------------------------------------------------------------------------------------
void menu_display_char (uint16 x, uint16 y, const char dat)
{
#if (MENU_DISPLAY_USE_IPS200)
    ips200_show_char(x, y, dat);
#else
    tft180_show_char(x, y, dat);
#endif
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     显示一串 ASCII 字符
//-------------------------------------------------------------------------------------------------------------------
void menu_display_string (uint16 x, uint16 y, const char *str)
{
    uint16 px = x;
    uint8  i  = 0;

    // 一个字符一个字符地画：右边画不下就停下
    // ★ 直接调 ips200_show_string 的话，字符串一长就超过屏幕宽度，
    //   库里的 ips200_set_region 有 zf_assert(x2 < width)，一超就停在断言语（屏幕定格 Assert error）
    while ('\0' != str[i])
    {
        if ((uint16)(px + 8) > menu_display_width()) { break; }
        menu_display_char(px, y, str[i]);
        px = (uint16)(px + 8);
        i ++;
    }
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     显示中英混排字符串
// 参数说明     str         字符串（本工程所有 .c 都是 GBK 编码，所以一个汉字就是两个字节）
// 备注信息     汉字 16x16 占 16 像素宽，ASCII 字符 8x16 占 8 像素宽；字库里没有的汉字直接跳过（留空）
//-------------------------------------------------------------------------------------------------------------------
void menu_display_cn (uint16 x, uint16 y, const char *str)
{
    uint16 px = x;
    uint8 i = 0;

    while ('\0' != str[i])
    {
        if ((0x80 > (uint8)str[i]) || ('\0' == str[i + 1]))      // ASCII 字符（'\0' 判断防止把结尾半个汉字当双字节）
        {
            if ((uint16)(px + 8) > menu_display_width()) { break; }
            menu_display_char(px, y, str[i]);
            px += 8;
            i += 1;
        }
        else                                                    // GBK 汉字
        {
            const uint8 *p_data = menu_font_get((uint8)str[i], (uint8)str[i + 1]);

            if ((uint16)(px + 16) > menu_display_width()) { break; }
            if (NULL != p_data)                                 // 字库里有才画
            {
#if (MENU_DISPLAY_USE_IPS200)
                ips200_show_chinese(px, y, 16, p_data, 1, menu_display_pen_color);
#else
                tft180_show_chinese(px, y, 16, p_data, 1, menu_display_pen_color);
#endif
            }
            px += 16;
            i += 2;
        }
    }
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     显示有符号整数
//-------------------------------------------------------------------------------------------------------------------
void menu_display_int (uint16 x, uint16 y, const int32 dat, uint8 num)
{
#if (MENU_DISPLAY_USE_IPS200)
    ips200_show_int(x, y, dat, num);
#else
    tft180_show_int(x, y, dat, num);
#endif
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     填充矩形（画选中项的高亮条、弹窗底色都用它）
// 参数说明     x,y         左上角坐标
// 参数说明     w,h         宽度、高度（像素）
// 备注信息     逐飞库只有画线没有填充矩形，这里用横线拼出来；越界部分自动裁掉
//-------------------------------------------------------------------------------------------------------------------
void menu_display_fill (uint16 x, uint16 y, uint16 w, uint16 h, const uint16 color)
{
    uint16 x_end;
    uint16 y_end;
    uint16 i;

    if (0 == w || 0 == h) { return; }

    x_end = (uint16)(x + w) - 1;
    y_end = (uint16)(y + h) - 1;
    if (x_end >= menu_display_width() ) { x_end = menu_display_width()  - 1; }
    if (y_end >= menu_display_height()) { y_end = menu_display_height() - 1; }

#if (MENU_DISPLAY_USE_IPS200)
    {
        uint32 total = (uint32)(x_end - x + 1) * (uint32)(y_end - y + 1);

        for (i = 0; i < MENU_FILL_CHUNK; i ++) { menu_fill_color_buf[i] = color; }

        IPS200_CS(0);
        menu_display_set_region(x, y, x_end, y_end);                    // 区域只设一次
        while (total >= MENU_FILL_CHUNK)
        {
            spi_write_16bit_array(IPS200_SPI, menu_fill_color_buf, MENU_FILL_CHUNK);
            total -= MENU_FILL_CHUNK;
        }
        if (0 != total) { spi_write_16bit_array(IPS200_SPI, menu_fill_color_buf, total); }
        IPS200_CS(1);
    }
#else
    {                                                                   // TFT180 没有这套快路径，仍按行画
        for (i = y; i <= y_end; i ++)
        {
            menu_display_line(x, i, x_end, i, color);
        }
    }
#endif
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     画矩形边框（弹窗用）
//-------------------------------------------------------------------------------------------------------------------
void menu_display_frame (uint16 x, uint16 y, uint16 w, uint16 h, const uint16 color)
{
    uint16 x_end = (uint16)(x + w) - 1;
    uint16 y_end = (uint16)(y + h) - 1;

    menu_display_line(x,     y,     x_end, y,     color);
    menu_display_line(x,     y_end, x_end, y_end, color);
    menu_display_line(x,     y,     x,     y_end, color);
    menu_display_line(x_end, y,     x_end, y_end, color);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     画线（横线 / 竖线走整块灌色的快路径）
//-------------------------------------------------------------------------------------------------------------------
static void menu_display_line_raw (uint16 x_start, uint16 y_start, uint16 x_end, uint16 y_end, const uint16 color);     // 前向声明

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     画线（横线 / 竖线走整块灌色的快路径）
//-------------------------------------------------------------------------------------------------------------------
void menu_display_line (uint16 x_start, uint16 y_start, uint16 x_end, uint16 y_end, const uint16 color)
{
#if (MENU_DISPLAY_USE_IPS200)
    // 横线 / 竖线直接走「整块灌色」，比库里的逐点画快得多（标题下面那条分隔线、弹窗边框都用得上）
    if (y_start == y_end)
    {
        if (x_start > x_end) { uint16 tmp = x_start; x_start = x_end; x_end = tmp; }
        menu_display_fill(x_start, y_start, (uint16)(x_end - x_start + 1), 1, color);
        return;
    }
    if (x_start == x_end)
    {
        if (y_start > y_end) { uint16 tmp = y_start; y_start = y_end; y_end = tmp; }
        menu_display_fill(x_start, y_start, 1, (uint16)(y_end - y_start + 1), color);
        return;
    }
#endif
    menu_display_line_raw(x_start, y_start, x_end, y_end, color);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     画任意方向的斜线（直接用库函数逐点画，很少用到）
//-------------------------------------------------------------------------------------------------------------------
static void menu_display_line_raw (uint16 x_start, uint16 y_start, uint16 x_end, uint16 y_end, const uint16 color)
{
#if (MENU_DISPLAY_USE_IPS200)
    ips200_draw_line(x_start, y_start, x_end, y_end, color);
#else
    tft180_draw_line(x_start, y_start, x_end, y_end, color);
#endif
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     屏幕宽度（像素）
//-------------------------------------------------------------------------------------------------------------------
uint16 menu_display_width (void)
{
#if (MENU_DISPLAY_USE_IPS200)
    return ips200_width_max;
#else
    return tft180_width_max;
#endif
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     屏幕高度（像素）
//-------------------------------------------------------------------------------------------------------------------
uint16 menu_display_height (void)
{
#if (MENU_DISPLAY_USE_IPS200)
    return ips200_height_max;
#else
    return tft180_height_max;
#endif
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     计算中英混排字符串的像素宽度
// 备注信息     界面靠它做居中（弹窗、标题右侧的状态文字）
//-------------------------------------------------------------------------------------------------------------------
uint16 menu_display_text_width (const char *str)
{
    uint16 width = 0;
    uint8 i = 0;

    while ('\0' != str[i])
    {
        if ((0x80 > (uint8)str[i]) || ('\0' == str[i + 1]))
        {
            width += 8;
            i += 1;
        }
        else
        {
            width += 16;
            i += 2;
        }
    }
    return width;
}
