/*********************************************************************************************************************
* 文件名            menu_tof.c
* 说明              菜单里的两个测距页面：实时测距 / 测距统计
*
*                   实时测距页（1.8 寸小屏只有上面一半，2 寸屏多一条曲线）：
*                     +----------------------------+
*                     | 实时测距            正常   |  标题 + 右上角状态（绿=正常 黄=弱信号 红=掉线）
*                     | +------------------------+ |
*                     | |        312 mm          | |  距离大数字（报警时变红）
*                     | +------------------------+ |
*                     | 状态 正常  30 Hz           |  状态 + 帧率
*                     | 最小 250  最大 480 mm      |  这一轮统计
*                     | 报警 否  阈值 300 mm       |  报警 + 阈值
*                     | #####################......|  量程条（黄竖线 = 阈值刻度）
*                     |      曲线（只有 2 寸屏画） |  最近 64 个采样，绿=距离，黄=阈值
*                     +----------------------------+
*
*                   刷新方式：整屏清屏太慢，live 只重画「数字 / 状态 / 量程条」那几处；
*                             曲线 500ms 才重画一次（每 100ms 重画一遍太费 SPI，屏会拖影）
*
* 注意              本文件是 GBK 编码；汉字由 menu_font.c 提供，改完汉字记得跑 tools/gen_font.py
********************************************************************************************************************/
#include "zf_common_headfile.h"

#include "menu_config.h"
#include "menu_display.h"
#include "menu_param.h"
#include "tof_config.h"
#include "tof_ranging.h"
#include "menu_tof.h"

#define TOF_X               (4)                             // 正文左边距
#define TOF_LABEL_W         (68)                            // 标题右上角状态字的擦除宽度（够放 4 个汉字）
#define TOF_STATS_LINES     (13)                            // 测距统计页一共几行（小屏放不下，按 1/2 翻）

static uint8  tof_small;                                    // 1 = 窄屏（TFT180 128x160）
static uint16 tof_box_y;                                    // 距离大数字那个框
static uint16 tof_box_h;
static uint16 tof_row_y;                                    // 三行文字的第一行 y
static uint16 tof_bar_y;                                    // 量程条
static uint16 tof_bar_h;
static uint16 tof_curve_y;                                  // 曲线区（小屏为 0）
static uint16 tof_curve_h;
static uint8  tof_stats_top;                                // 统计页从第几行开始显示
static uint8  tof_live_tick;                                // live 被调了几次（用来给曲线 / 统计降频）
static char   tof_buf[48];                                  // 格式化缓冲

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     按屏幕尺寸算版面（两块屏都用同一套代码，只是数字不一样）
//-------------------------------------------------------------------------------------------------------------------
static uint16 tof_line_h (void)
{
    return (0 == tof_small) ? (MENU_UI_LINE_H) : (18);
}

static uint16 tof_hint_y (void)
{
    return (uint16)(menu_display_height() - 18);
}

static void tof_layout (void)
{
    uint16 h = menu_display_height();

    tof_small = (h >= 240) ? (0) : (1);

    tof_box_y  = (0 == tof_small) ? (26) : (20);
    tof_box_h  = (0 == tof_small) ? (46) : (32);
    tof_row_y  = (uint16)(tof_box_y + tof_box_h + 4);
    tof_bar_y  = (uint16)(tof_row_y + ((0 == tof_small) ? (3) : (2)) * tof_line_h() + 4);   // 小屏少一行统计
    tof_bar_h  = (0 == tof_small) ? (12) : (8);
    tof_curve_y = (uint16)(tof_bar_y + tof_bar_h + 12);
    tof_curve_h = (0 == tof_small) ? (uint16)(h - 20 - tof_curve_y) : (0);

    if (tof_curve_h > 200) { tof_curve_h = 200; }
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     一行中英混排文字（用空格补到行尾，把上一次的内容盖掉，不用整屏清屏）
//-------------------------------------------------------------------------------------------------------------------
static void tof_row_cn (uint16 y, const char *text)
{
    char   tmp[64];
    uint16 maxw = (uint16)(menu_display_width() - TOF_X - 2);
    uint16 w    = menu_display_text_width(text);
    uint8  n    = 0;

    while (('\0' != text[n]) && (n < (uint8)(sizeof(tmp) - 1))) { tmp[n] = text[n]; n ++; }

    while ((w < maxw) && (n < (uint8)(sizeof(tmp) - 1)))
    {
        tmp[n] = ' ';
        n ++;
        w = (uint16)(w + 8);
    }
    tmp[n] = '\0';

    menu_display_set_color(MENU_COLOR_PEN, MENU_COLOR_BG);
    menu_display_cn(TOF_X, y, tmp);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     标题
//-------------------------------------------------------------------------------------------------------------------
static void tof_title (const char *title)
{
    menu_display_set_color(MENU_COLOR_TITLE, MENU_COLOR_BG);
    menu_display_cn(TOF_X, MENU_UI_TITLE_Y, title);
    menu_display_set_color(MENU_COLOR_PEN, MENU_COLOR_BG);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     标题右上角的小字（状态 / 页码），固定宽度擦除，字变短也不会留下残影
//-------------------------------------------------------------------------------------------------------------------
static void tof_top_right (const char *text, uint16 color)
{
    uint16 w     = menu_display_text_width(text);
    uint16 right = (uint16)(menu_display_width() - 2);
    uint16 x0    = (uint16)(right - TOF_LABEL_W);

    menu_display_fill(x0, MENU_UI_TITLE_Y, TOF_LABEL_W, 16, MENU_COLOR_BG);

    menu_display_set_color(color, MENU_COLOR_BG);
    menu_display_cn((uint16)(right - w), MENU_UI_TITLE_Y, text);
    menu_display_set_color(MENU_COLOR_PEN, MENU_COLOR_BG);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     底部操作提示
//-------------------------------------------------------------------------------------------------------------------
static void tof_hint (const char *wide, const char *narrow)
{
    const char *s = (menu_display_width() >= 200) ? (wide) : (narrow);

    menu_display_fill(0, tof_hint_y(), menu_display_width(), 18, MENU_COLOR_BG);
    menu_display_set_color(RGB565_BLUE, MENU_COLOR_BG);
    menu_display_cn(TOF_X, tof_hint_y(), s);
    menu_display_set_color(MENU_COLOR_PEN, MENU_COLOR_BG);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     标题文字：128 宽的小屏用短标题，免得压到右上角的状态字
//-------------------------------------------------------------------------------------------------------------------
static const char *tof_title_text (const char *wide, const char *narrow)
{
    return (0 == tof_small) ? (wide) : (narrow);
}
//-------------------------------------------------------------------------------------------------------------------
// 函数简介     状态对应的颜色：绿 = 正常，黄 = 弱信号，红 = 掉线，蓝 = 待机，灰 = 没初始化
//-------------------------------------------------------------------------------------------------------------------
static uint16 tof_state_color (void)
{
    switch (tof_module_state())
    {
        case TOF_STATE_OK:      return RGB565_GREEN;
        case TOF_STATE_WEAK:    return RGB565_YELLOW;
        case TOF_STATE_LOST:    return RGB565_RED;
        case TOF_STATE_IDLE:    return RGB565_BLUE;
        default:                return RGB565_GRAY;
    }
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     切换显示单位（mm <-> cm），改的是菜单参数，掉电也能记住
//-------------------------------------------------------------------------------------------------------------------
static void tof_toggle_unit (void)
{
#if (1 == TOF_USE_MENU_PARAM)
    int32 v = menu_param_get(MENU_PARAM_IDX_TOF_UNIT);

    menu_param_set(MENU_PARAM_IDX_TOF_UNIT, (0 == v) ? (1) : (0));
#else
    tof_module_set_unit_cm((0 == tof_module_unit_cm()) ? (1) : (0));
#endif
}

// ===================================================================================================================
//                                              页面 1：实时测距
// ===================================================================================================================
//-------------------------------------------------------------------------------------------------------------------
// 函数简介     距离大数字（固定在框里居中，右补空格把上一次的位数盖掉）
//-------------------------------------------------------------------------------------------------------------------
static void live_draw_value (void)
{
    const char *s = tof_module_display_str();
    char        field[16];
    uint8       n = 0;
    uint16      fw = (uint16)(9 * 8);                           // 固定 9 个 ASCII 字符宽，"1780 mm" 刚好放得下
    uint16      x;
    uint16      y     = (uint16)(tof_box_y + (tof_box_h - 16) / 2);
    uint16      color = (0 != tof_module_alarm()) ? (RGB565_RED) : (MENU_COLOR_PEN);

    while (('\0' != s[n]) && (n < (uint8)(sizeof(field) - 1))) { field[n] = s[n]; n ++; }
    while (n < 9) { field[n] = ' '; n ++; }
    field[n] = '\0';

    x = (uint16)((menu_display_width() - fw) / 2);

    menu_display_set_color(color, MENU_COLOR_BG);
    menu_display_string(x, y, field);
    menu_display_set_color(MENU_COLOR_PEN, MENU_COLOR_BG);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     三行文字：状态 + 帧率 / 最小最大 / 报警 + 阈值
//-------------------------------------------------------------------------------------------------------------------
static void live_draw_rows (void)
{
    uint16 y = tof_row_y;

    if (0 == tof_small)
    {
        snprintf(tof_buf, sizeof(tof_buf), "状态 %s  %u Hz",
                 tof_module_state_str(), (unsigned)tof_module_rate_hz());
        tof_row_cn(y, tof_buf);
        y = (uint16)(y + tof_line_h());

        snprintf(tof_buf, sizeof(tof_buf), "最小 %u  最大 %u mm",
                 (unsigned)tof_module_min_mm(), (unsigned)tof_module_max_mm());
        tof_row_cn(y, tof_buf);
        y = (uint16)(y + tof_line_h());

        snprintf(tof_buf, sizeof(tof_buf), "报警 %s  阈值 %u mm",
                 (0 != tof_module_alarm()) ? ("是") : ("否"), (unsigned)tof_module_alarm_threshold_mm());
        tof_row_cn(y, tof_buf);
    }
    else
    {
        // 128 宽的小屏放不下三行，压成两行：状态 + 帧率 / 报警 + 阈值（最小最大去统计页看）
        snprintf(tof_buf, sizeof(tof_buf), "状态%s %uHz",
                 tof_module_state_str(), (unsigned)tof_module_rate_hz());
        tof_row_cn(y, tof_buf);
        y = (uint16)(y + tof_line_h());

        snprintf(tof_buf, sizeof(tof_buf), "报警%s 阈值%u",
                 (0 != tof_module_alarm()) ? ("是") : ("否"), (unsigned)tof_module_alarm_threshold_mm());
        tof_row_cn(y, tof_buf);
    }
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     量程条：绿色填充 = 当前距离占满量程的比例，黄竖线 = 报警阈值刻度
//-------------------------------------------------------------------------------------------------------------------
static void live_draw_bar (void)
{
    uint16 x       = 4;
    uint16 w       = (uint16)(menu_display_width() - 8);
    uint16 inner_w = (uint16)(w - 2);
    uint16 mm      = tof_module_distance_mm();
    uint16 fill_w;
    uint16 th_x;

    fill_w = (uint16)(((uint32)mm * inner_w) / tof_module_max_range_mm());
    if (fill_w > inner_w) { fill_w = inner_w; }

    th_x = (uint16)(((uint32)tof_module_alarm_threshold_mm() * inner_w) / tof_module_max_range_mm());
    if (th_x >= inner_w) { th_x = (uint16)(inner_w - 1); }

    menu_display_fill(x, tof_bar_y, w, tof_bar_h, MENU_COLOR_BG);
    menu_display_frame(x, tof_bar_y, w, tof_bar_h, RGB565_GRAY);

    if (0 != fill_w)
    {
        menu_display_fill((uint16)(x + 1), (uint16)(tof_bar_y + 1), fill_w, (uint16)(tof_bar_h - 2),
                          (0 != tof_module_alarm()) ? (RGB565_RED) : (RGB565_GREEN));
    }

    menu_display_line((uint16)(x + 1 + th_x), (uint16)(tof_bar_y + 1),
                      (uint16)(x + 1 + th_x), (uint16)(tof_bar_y + tof_bar_h - 2), RGB565_YELLOW);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     距离曲线（填充式面积图，只有 2 寸屏画）
// 备注信息     X 轴 = 最近 TOF_HISTORY_NUM 个采样，Y 轴 = 0 ~ 满量程；黄线是报警阈值
//              主循环里 500ms 才调一次：每 100ms 重画整块曲线太费 SPI，屏上会看着发拖
//-------------------------------------------------------------------------------------------------------------------
static void live_draw_curve (void)
{
    uint16 x0     = 4;
    uint16 w      = (uint16)(menu_display_width() - 8);
    uint16 bottom = (uint16)(tof_curve_y + tof_curve_h - 1);
    uint16 th_y;
    uint16 colw   = (uint16)(w / TOF_HISTORY_NUM);
    uint8  n      = tof_module_history_num();
    uint8  i;

    if (0 == tof_curve_h) { return; }
    if (0 == colw)        { colw = 1; }

    menu_display_fill(x0, tof_curve_y, w, tof_curve_h, MENU_COLOR_BG);
    menu_display_frame(x0, tof_curve_y, w, tof_curve_h, RGB565_GRAY);

    // 阈值线：阈值占满量程的比例，换算成屏幕上的高度
    {
        uint32 th_h = ((uint32)tof_module_alarm_threshold_mm() * (uint32)(tof_curve_h - 4)) / tof_module_max_range_mm();

        th_y = (uint16)(bottom - (uint16)th_h);
    }
    if (th_y > (uint16)(tof_curve_y + 1)) { menu_display_line(x0, th_y, (uint16)(x0 + w - 1), th_y, RGB565_YELLOW); }

    // 每个采样一列
    for (i = 0; i < n; i ++)
    {
        uint16 mm = tof_module_history_get(i);
        uint16 x  = (uint16)(x0 + (uint16)(i * colw));
        uint16 hh = (uint16)(((uint32)mm * (tof_curve_h - 4)) / tof_module_max_range_mm());
        uint16 y;

        if (hh > (uint16)(tof_curve_h - 4)) { hh = (uint16)(tof_curve_h - 4); }
        y = (uint16)(bottom - hh);
        if (y < (uint16)(tof_curve_y + 2)) { y = (uint16)(tof_curve_y + 2); }

        menu_display_fill(x, y, colw, (uint16)(bottom - y), RGB565_GREEN);
    }
}

static void live_draw_all (void)
{
    menu_display_clear();
    tof_title(tof_title_text("实时测距", "测距"));
    tof_top_right(tof_module_state_str(), tof_state_color());

    menu_display_frame(2, tof_box_y, (uint16)(menu_display_width() - 4), tof_box_h, RGB565_BLUE);

    live_draw_value();
    live_draw_rows();
    live_draw_bar();
    if (0 == tof_small) { live_draw_curve(); }

    tof_hint("1=单位 2=清统计 3=重来 4=返回", "1单 2清 3重 4返");
}

// ===================================================================================================================
//                                              页面 2：测距统计
// ===================================================================================================================
static const char *tof_filter_str (void)
{
    switch (tof_module_filter_level())
    {
        case 0:  return "原始";
        case 1:  return "中值";
        default: return "平滑";
    }
}


//-------------------------------------------------------------------------------------------------------------------
// 函数简介     组第 index 行的文字，返回 0 = 没有这一行
//-------------------------------------------------------------------------------------------------------------------
static uint8 stats_line (uint8 index, char *out, uint8 size)
{
    switch (index)
    {
        case 0:  snprintf(out, size, "型号 %s", tof_module_type_str());                 break;
        case 1:  snprintf(out, size, "滤波 %s", tof_filter_str());                      break;
        case 2:  snprintf(out, size, "采样 %lu 帧", (unsigned long)tof_module_sample_cnt());   break;
        case 3:  snprintf(out, size, "帧率 %u Hz", (unsigned)tof_module_rate_hz());     break;
        case 4:  snprintf(out, size, "有效 %lu 帧", (unsigned long)tof_module_valid_cnt());    break;
        case 5:  snprintf(out, size, "无效 %lu 帧", (unsigned long)tof_module_invalid_cnt());  break;
        case 6:  snprintf(out, size, "最小 %u mm", (unsigned)tof_module_min_mm());      break;
        case 7:  snprintf(out, size, "最大 %u mm", (unsigned)tof_module_max_mm());      break;
        case 8:  snprintf(out, size, "平均 %u mm", (unsigned)tof_module_avg_mm());      break;
        case 9:  snprintf(out, size, "波动 %u mm", (unsigned)tof_module_span_mm());     break;
        case 10: snprintf(out, size, "阈值 %u mm", (unsigned)tof_module_alarm_threshold_mm()); break;
        case 11: snprintf(out, size, "状态 %s", tof_module_state_str());                break;
        case 12: snprintf(out, size, "报警 %s", (0 != tof_module_alarm()) ? ("是") : ("否"));  break;
        default: out[0] = '\0'; return 0;
    }
    return 1;
}

static uint8 stats_rows_visible (void)
{
    uint16 y0 = (uint16)(MENU_UI_ITEM_Y0 - 4);
    uint16 y1 = (uint16)(tof_hint_y() - 2);
    uint8  n;

    if (y1 <= y0) { return 1; }

    n = (uint8)((y1 - y0) / tof_line_h());
    return (0 == n) ? (1) : (n);
}

static void stats_draw_values (void)
{
    uint8 vis = stats_rows_visible();
    uint8 i;

    for (i = 0; i < vis; i ++)
    {
        uint8 idx = (uint8)(tof_stats_top + i);

        if (idx >= TOF_STATS_LINES) { break; }
        if (0 == stats_line(idx, tof_buf, (uint8)sizeof(tof_buf))) { break; }
        tof_row_cn((uint16)((MENU_UI_ITEM_Y0 - 4) + (uint16)i * tof_line_h()), tof_buf);
    }
}

static void stats_draw_all (void)
{
    uint8 vis = stats_rows_visible();

    if (vis < TOF_STATS_LINES)
    {
        if (tof_stats_top > (uint8)(TOF_STATS_LINES - vis)) { tof_stats_top = (uint8)(TOF_STATS_LINES - vis); }
    }
    else
    {
        tof_stats_top = 0;                                          // 屏够高，一屏全放得下
    }

    menu_display_clear();
    tof_title(tof_title_text("测距统计", "统计"));

    snprintf(tof_buf, sizeof(tof_buf), "%u/%u", (unsigned)(tof_stats_top + 1), (unsigned)TOF_STATS_LINES);
    tof_top_right(tof_buf, MENU_COLOR_TITLE);

    stats_draw_values();

    tof_hint("1=上翻 2=下翻 3=清统计 4=返回", "1上 2下 3清 4返");
}

// ===================================================================================================================
//                                              对外接口
// ===================================================================================================================
void menu_tof_enter (uint8 page)
{
    tof_layout();

    if (MENU_TOF_PAGE_STATS == page) { tof_stats_top = 0; }

    tof_live_tick = 0;
    menu_tof_draw(page);
}

void menu_tof_draw (uint8 page)
{
    tof_layout();

    switch (page)
    {
        case MENU_TOF_PAGE_LIVE:    live_draw_all();    break;
        case MENU_TOF_PAGE_STATS:   stats_draw_all();   break;
        default:                                        break;
    }
}

void menu_tof_live (uint8 page)
{
    tof_live_tick ++;

    switch (page)
    {
        case MENU_TOF_PAGE_LIVE:
            tof_top_right(tof_module_state_str(), tof_state_color());
            live_draw_value();
            live_draw_rows();
            live_draw_bar();

            // 曲线 500ms 重画一次（每 100ms 一次太费 SPI，屏幕会看着发拖）
            if ((0 == tof_small) && (0 == (tof_live_tick % 5))) { live_draw_curve(); }
            break;

        case MENU_TOF_PAGE_STATS:
            if (0 == (tof_live_tick % 5)) { stats_draw_values(); }
            break;

        default:
            break;
    }
}

uint8 menu_tof_key (uint8 page, uint8 k_up, uint8 k_down, uint8 k_ok, uint8 k_back)
{
    uint8 ret = 0;

    switch (page)
    {
        // ------------------------------------------------ 实时测距 ------------------------------------------------
        case MENU_TOF_PAGE_LIVE:
            if (0 != k_up)                                      // 1 = 单位 mm / cm 切换
            {
                tof_toggle_unit();
                menu_tof_draw(MENU_TOF_PAGE_LIVE);
            }
            if (0 != k_down)                                    // 2 = 清统计（最小 / 最大 / 平均 / 曲线）
            {
                tof_module_reset_stats();
                menu_tof_draw(MENU_TOF_PAGE_LIVE);
            }
            if (0 != k_ok)                                      // 3 = 重新初始化模块（约 0.5 s）
            {
                tof_module_reinit();
                menu_tof_draw(MENU_TOF_PAGE_LIVE);
            }
            if (0 != k_back) { ret = 1; }
            break;

        // ------------------------------------------------ 测距统计 ------------------------------------------------
        case MENU_TOF_PAGE_STATS:
        {
            uint8 vis = stats_rows_visible();

            if (0 != k_up)                                      // 1 = 往上翻一行
            {
                if (0 != tof_stats_top) { tof_stats_top --; stats_draw_all(); }
            }
            if (0 != k_down)                                    // 2 = 往下翻一行
            {
                if ((uint8)(tof_stats_top + vis) < TOF_STATS_LINES) { tof_stats_top ++; stats_draw_all(); }
            }
            if (0 != k_ok)                                      // 3 = 清统计
            {
                tof_module_reset_stats();
                stats_draw_all();
            }
            if (0 != k_back) { ret = 1; }
            break;
        }

        default:
            ret = 1;
            break;
    }

    return ret;
}
