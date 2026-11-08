/*********************************************************************************************************************
* 文件名            menu_imu.c
* 说明              菜单里的 4 个陀螺仪页面（姿态 / 校准 / 里程 / 航点）
*
*                   页面刷新方式：
*                     menu_imu_enter()  进入页面时整屏画一次（清屏 + 标题 + 所有行 + 提示）
*                     menu_imu_live()   主循环每 100ms 调一次，只重画「数字」那几行，不整屏清屏
*                                       （整屏清一次要几十 ms，10Hz 刷会把屏幕刷出闪烁）
*                     menu_imu_key()    按键：返回 0 = 留在本页，1 = 退回菜单，2 = 跳到校准页
*
*                   地图：里程定位页下半屏画轨迹小地图
*                     X = 车头方向 -> 屏幕向上     Y = 车左侧 -> 屏幕向左
*                     位置超出地图范围时自动把范围放大一倍重画
*
* 注意              本文件是 GBK 编码；汉字由 menu_font.c 提供，改完汉字记得跑 tools/gen_font.py
********************************************************************************************************************/
#include "imu_config.h"
#include "menu_display.h"
#include "menu_imu.h"
#include "imu_attitude.h"
#include "imu_encoder.h"
#include "imu_odom.h"
#include "imu_store.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

#define IMU_X               (4)                         // 正文左边距
#define IMU_MSG_ROW         (5)                         // 提示消息固定在第 6 行
#define IMU_MAP_ROW         (6)                         // 地图从小地图页第 7 行开始
#define IMU_MAP_MIN_H       (44)                        // 地图区太矮就不画（1.8 寸屏没有地图）

static uint8  imu_msg_row = IMU_MSG_ROW;                // 提示消息画在第几行（姿态页有加速度行时往下挪）
static uint8  imu_page_now;                             // 当前页面
static char   imu_buf[48];                              // 格式化缓冲
static char   imu_msg[40];                              // 底部提示消息（按键操作的结果）

// ------------------------- 地图状态（里程定位页） -------------------------
static uint8  map_ready;                                // 1 = 地图已经画好，可以增量刷新
static int32  map_range_mm;                             // 地图半幅对应的实际距离（mm）
static int16  map_cx, map_cy;                           // 地图中心（屏幕坐标）
static int16  map_half;                                 // 地图半幅（像素）
static int16  map_x0, map_y0, map_x1, map_y1;           // 地图外框（屏幕坐标，画点前用来限幅）
static uint16 map_drawn;                                // 已经画到第几个轨迹点
static uint8  map_wp_drawn;                             // 已经画了几个航点
static int16  map_last_sx, map_last_sy;                 // 最后一个轨迹点的屏幕坐标
static int16  map_cur_sx, map_cur_sy;                   // 当前位置标记的屏幕坐标
static uint8  map_cur_ok;

// ------------------------- 航点页状态 -------------------------
static uint8  wp_sel;                                   // 当前选中行（最后一行 = 清空全部）
static uint8  wp_top;                                   // 从第几行开始显示

//-------------------------------------------------------------------------------------------------------------------
// 版面工具
//-------------------------------------------------------------------------------------------------------------------
static uint16 imu_line_h (void)
{
    return (menu_display_height() >= 240) ? (MENU_UI_LINE_H) : (18);
}

static uint16 imu_line_y (uint8 row)
{
    return (uint16)(MENU_UI_ITEM_Y0 - 4 + (uint16)row * imu_line_h());
}

static uint16 imu_hint_y (void)
{
    return (uint16)(menu_display_height() - 18);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     一行 ASCII 文本（自动用空格补到行尾，把上一次的内容盖掉）
//-------------------------------------------------------------------------------------------------------------------
static void imu_row (uint8 row, const char *text)
{
    char   tmp[48];
    uint8  maxc = (uint8)(menu_display_width() / 8 - 1);
    uint8  n = 0;

    if (maxc > (uint8)(sizeof(tmp) - 1)) { maxc = (uint8)(sizeof(tmp) - 1); }

    while (('\0' != text[n]) && (n < (uint8)(sizeof(tmp) - 1))) { tmp[n] = text[n]; n ++; }
    while (n < maxc) { tmp[n] = ' '; n ++; }
    tmp[n] = '\0';

    menu_display_set_color(MENU_COLOR_PEN, MENU_COLOR_BG);
    menu_display_string(IMU_X, imu_line_y(row), tmp);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     一行中英混排文本（按像素宽度补空格）
//-------------------------------------------------------------------------------------------------------------------
static void imu_row_cn (uint8 row, const char *text)
{
    char   tmp[64];
    uint16 maxw = (uint16)(menu_display_width() - IMU_X - 2);
    uint16 w = menu_display_text_width(text);
    uint8  n = 0;

    while (('\0' != text[n]) && (n < (uint8)(sizeof(tmp) - 1))) { tmp[n] = text[n]; n ++; }
    while ((w < maxw) && (n < (uint8)(sizeof(tmp) - 1)))
    {
        tmp[n] = ' ';
        n ++;
        w = (uint16)(w + 8);
    }
    tmp[n] = '\0';

    menu_display_set_color(MENU_COLOR_PEN, MENU_COLOR_BG);
    menu_display_cn(IMU_X, imu_line_y(row), tmp);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     标题 / 底部操作提示
//-------------------------------------------------------------------------------------------------------------------
static void imu_title (const char *title)
{
    menu_display_set_color(MENU_COLOR_TITLE, MENU_COLOR_BG);
    menu_display_cn(IMU_X, MENU_UI_TITLE_Y, title);
    menu_display_set_color(MENU_COLOR_PEN, MENU_COLOR_BG);
}

static void imu_hint (const char *wide, const char *narrow)
{
    const char *s = (menu_display_width() >= 200) ? (wide) : (narrow);

    menu_display_fill(0, imu_hint_y(), menu_display_width(), 18, MENU_COLOR_BG);
    menu_display_set_color(RGB565_BLUE, MENU_COLOR_BG);
    menu_display_cn(IMU_X, imu_hint_y(), s);
    menu_display_set_color(MENU_COLOR_PEN, MENU_COLOR_BG);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     底部消息（按键操作的结果，一直留着到下次操作）
//-------------------------------------------------------------------------------------------------------------------
static void imu_msg_set (const char *text)
{
    uint8 n = 0;

    while (('\0' != text[n]) && (n < (uint8)(sizeof(imu_msg) - 1))) { imu_msg[n] = text[n]; n ++; }
    imu_msg[n] = '\0';
}

static void imu_msg_draw (void)
{
    imu_row_cn(imu_msg_row, imu_msg);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     格式化带一位小数的数（不用 %f，TASKING 的 %f 又大又慢）
//-------------------------------------------------------------------------------------------------------------------
static void imu_fmt_dec (char *out, const char *name, float v, const char *unit)
{
    int32  scaled = (int32)(v * 10.0f + ((v >= 0.0f) ? (0.5f) : (-0.5f)));
    const char *sign = "";
    int32  ip;
    int32  fp;

    if (scaled < 0) { sign = "-"; scaled = -scaled; }
    ip = scaled / 10;
    fp = scaled % 10;

    snprintf(out, 48, "%-5s %s%ld.%ld %s", name, sign, (long)ip, (long)fp, unit);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     格式化整数（mm）
//-------------------------------------------------------------------------------------------------------------------
static void imu_fmt_int (char *out, const char *name, float v, const char *unit)
{
    int32 mm = (int32)(v + ((v >= 0.0f) ? (0.5f) : (-0.5f)));
    const char *sign = "";

    if (mm < 0) { sign = "-"; mm = -mm; }

    snprintf(out, 48, "%-5s %s%ld %s", name, sign, (long)mm, unit);
}

// ===================================================================================================================
//                                              页面 1：姿态显示
// ===================================================================================================================
//-------------------------------------------------------------------------------------------------------------------
// 函数简介     把一个加速度值格式化成 "+0.02" 这样（g，两位小数）
// 备注信息     加速度计测的是「比力」：平放静止时 Z 约 +1.00g、X/Y 约 0；加减速 / 上坡时会偏离 1g
//-------------------------------------------------------------------------------------------------------------------
static void imu_fmt_val2 (char *out, float v)
{
    int32       scaled = (int32)(v * 100.0f + ((v >= 0.0f) ? (0.5f) : (-0.5f)));
    const char *sign = "";

    if (scaled < 0) { sign = "-"; scaled = -scaled; }

    snprintf(out, 16, "%s%ld.%02ld", sign, (long)(scaled / 100), (long)(scaled % 100));
}

static void att_draw_values (void)
{
    float bx = 0.0f;
    float by = 0.0f;
    float bz = 0.0f;

    imu_fmt_dec(imu_buf, "Roll",  imu_attitude_roll(),  "deg"); imu_row(0, imu_buf);
    imu_fmt_dec(imu_buf, "Pitch", imu_attitude_pitch(), "deg"); imu_row(1, imu_buf);
    imu_fmt_dec(imu_buf, "Yaw",   imu_attitude_yaw(),   "deg"); imu_row(2, imu_buf);

    snprintf(imu_buf, sizeof(imu_buf), "%s   IMU %s",
             (0 != imu_attitude_still()) ? ("静止") : ("运动"),
             (0 != imu_attitude_ok())    ? ("OK")   : ("ERR"));
    imu_row_cn(3, imu_buf);

    imu_attitude_bias_get(&bx, &by, &bz);
    if (menu_display_width() >= 200)
    {
        snprintf(imu_buf, sizeof(imu_buf), "Bias %+ld %+ld %+ld",
                 (long)(bx * 100.0f), (long)(by * 100.0f), (long)(bz * 100.0f));
        imu_row(4, imu_buf);
    }
    else
    {
        snprintf(imu_buf, sizeof(imu_buf), "BiasZ %+ld", (long)(bz * 100.0f));
        imu_row(4, imu_buf);
    }

    // 加速度计：互补滤波里 pitch / roll 就是靠它修正的
    // 平放静止时应该是 X≈0  Y≈0  Z≈+1.00g，|a|=1.00g
    // 对不上就是：模块没放平 / 芯片朝下（Z 是 -1g）/ 装了方向不对（XY 互换）
    if (menu_display_height() >= 240)
    {
        char  v1[16];
        char  v2[16];
        float ax = imu_attitude_acc(0);
        float ay = imu_attitude_acc(1);
        float az = imu_attitude_acc(2);
        float an = sqrtf(ax * ax + ay * ay + az * az);

        imu_fmt_val2(v1, ax);
        imu_fmt_val2(v2, ay);
        snprintf(imu_buf, sizeof(imu_buf), "AccX %s  AccY %s", v1, v2);
        imu_row(5, imu_buf);

        imu_fmt_val2(v1, az);
        imu_fmt_val2(v2, an);
        snprintf(imu_buf, sizeof(imu_buf), "AccZ %s  a %s g", v1, v2);
        imu_row(6, imu_buf);
    }
}

static void att_draw_all (void)
{
    imu_msg_row = (menu_display_height() >= 240) ? (7) : (IMU_MSG_ROW);   // 宽屏多了两行加速度，消息往下挪
    menu_display_clear();
    imu_title("姿态显示");
    att_draw_values();
    imu_msg_draw();
    imu_hint("1=原点 2=清轨迹 3=校准 4=返回", "1原2清3校4返");
}

// ===================================================================================================================
//                                              页面 2：陀螺校准
// ===================================================================================================================
static void calib_draw_values (void)
{
    float bx = 0.0f;
    float by = 0.0f;
    float bz = 0.0f;

    imu_attitude_bias_get(&bx, &by, &bz);

    imu_fmt_dec(imu_buf, "BiasX", bx, "dps"); imu_row(0, imu_buf);
    imu_fmt_dec(imu_buf, "BiasY", by, "dps"); imu_row(1, imu_buf);
    imu_fmt_dec(imu_buf, "BiasZ", bz, "dps"); imu_row(2, imu_buf);

    snprintf(imu_buf, sizeof(imu_buf), "来源 %s", (0 != imu_attitude_calib_done()) ? ("已校准") : ("默认0"));
    imu_row_cn(3, imu_buf);

    snprintf(imu_buf, sizeof(imu_buf), "解算 %lu 次", (unsigned long)imu_attitude_sample_cnt());
    imu_row_cn(4, imu_buf);
}

static void calib_draw_all (void)
{
    imu_msg_row = IMU_MSG_ROW;
    menu_display_clear();
    imu_title("陀螺校准");
    calib_draw_values();
    imu_msg_draw();
    imu_hint("3=校准 1=保存 2=清零 4=返回", "3校1存2清4返");
}

// ===================================================================================================================
//                                              页面 3：里程定位
// ===================================================================================================================
static void odom_draw_values (void)
{
    imu_fmt_int(imu_buf, "X",   imu_odom_x_mm(), "mm"); imu_row(0, imu_buf);
    imu_fmt_int(imu_buf, "Y",   imu_odom_y_mm(), "mm"); imu_row(1, imu_buf);
    imu_fmt_dec(imu_buf, "Yaw", imu_attitude_yaw(), "deg"); imu_row(2, imu_buf);
    imu_fmt_int(imu_buf, "S",   imu_odom_dist_mm(), "mm"); imu_row(3, imu_buf);

    snprintf(imu_buf, sizeof(imu_buf), "轨迹%d 航点%d", (int)imu_odom_trail_num(), (int)imu_odom_wp_num());
    imu_row_cn(4, imu_buf);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     实际坐标 -> 屏幕坐标
// 备注信息     X（车头方向）往上画，Y（车左侧）往左画
//-------------------------------------------------------------------------------------------------------------------
static void map_xy (int16 mx, int16 my, int16 *sx, int16 *sy)
{
    int32 px = (int32)map_cx - (int32)my * (int32)map_half / map_range_mm;
    int32 py = (int32)map_cy - (int32)mx * (int32)map_half / map_range_mm;

    if (px < map_x0) { px = map_x0; }                                   // 别把点画到屏幕外面去（uint16 会翻转成花屏）
    if (px > map_x1) { px = map_x1; }
    if (py < map_y0) { py = map_y0; }
    if (py > map_y1) { py = map_y1; }

    *sx = (int16)px;
    *sy = (int16)py;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     画一个实心小方块（航点 / 当前位置标记）
//-------------------------------------------------------------------------------------------------------------------
static void map_dot (int16 sx, int16 sy, uint16 color)
{
    menu_display_fill((uint16)(sx - 2), (uint16)(sy - 2), 5, 5, color);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     整块重画地图（进页面时 / 超出范围时）
//-------------------------------------------------------------------------------------------------------------------
static void odom_map_full (void)
{
    uint16 sw = menu_display_width();
    uint16 sh = menu_display_height();
    int16  x0 = 2;
    int16  x1 = (int16)(sw - 4);
    int16  y0 = (int16)imu_line_y(IMU_MAP_ROW);
    int16  y1 = (int16)(sh - 20);
    uint16 n = 0;
    uint16 i = 0;
    int16  tx = 0;
    int16  ty = 0;
    int16  sx = 0;
    int16  sy = 0;
    int16  px = 0;
    int16  py = 0;
    uint8  wpn = 0;

    map_ready = 0;

    if ((y1 - y0) < IMU_MAP_MIN_H) { return; }                  // 屏太矮（1.8 寸）就不画地图

    if (0 == map_range_mm) { map_range_mm = 1500; }

    map_cx   = (int16)((x0 + x1) / 2);
    map_cy   = (int16)((y0 + y1) / 2);
    map_half = (int16)(((x1 - x0) < (y1 - y0)) ? ((x1 - x0) / 2 - 4) : ((y1 - y0) / 2 - 4));
    map_x0   = x0;
    map_y0   = y0;
    map_x1   = x1;
    map_y1   = y1;

    // ------------------------- 1. 底色 + 边框 + 十字轴线 -------------------------
    menu_display_fill((uint16)x0, (uint16)y0, (uint16)(x1 - x0), (uint16)(y1 - y0), MENU_COLOR_BG);
    menu_display_frame((uint16)x0, (uint16)y0, (uint16)(x1 - x0), (uint16)(y1 - y0), RGB565_BLUE);
    menu_display_line((uint16)map_cx, (uint16)y0, (uint16)map_cx, (uint16)y1, RGB565_GRAY);
    menu_display_line((uint16)x0, (uint16)map_cy, (uint16)x1, (uint16)map_cy, RGB565_GRAY);

    // ------------------------- 2. 轨迹（绿色折线） -------------------------
    n = imu_odom_trail_num();
    for (i = 0; i < n; i ++)
    {
        imu_odom_trail_get(i, &tx, &ty);
        map_xy(tx, ty, &sx, &sy);
        if (0 != i) { menu_display_line((uint16)px, (uint16)py, (uint16)sx, (uint16)sy, RGB565_GREEN); }
        px = sx;
        py = sy;
    }
    map_last_sx = px;
    map_last_sy = py;
    map_drawn   = n;

    // ------------------------- 3. 航点（黄色方块） -------------------------
    wpn = imu_odom_wp_num();
    for (i = 0; i < wpn; i ++)
    {
        imu_odom_wp_get((uint8)i, &tx, &ty, NULL);
        map_xy(tx, ty, &sx, &sy);
        map_dot(sx, sy, RGB565_YELLOW);
    }
    map_wp_drawn = wpn;

    // ------------------------- 4. 当前位置（红色方块） -------------------------
    map_xy((int16)imu_odom_x_mm(), (int16)imu_odom_y_mm(), &sx, &sy);
    map_dot(sx, sy, RGB565_RED);
    map_cur_sx = sx;
    map_cur_sy = sy;
    map_cur_ok = 1;

    // ------------------------- 5. 范围文字（左上角，白字） -------------------------
    snprintf(imu_buf, sizeof(imu_buf), "R%dmm", (int)map_range_mm);
    menu_display_set_color(MENU_COLOR_PEN, MENU_COLOR_BG);
    menu_display_string((uint16)(x0 + 3), (uint16)(y0 + 2), imu_buf);

    map_ready = 1;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     增量刷新地图：只画新长出来的那几段轨迹 + 移动当前位置标记
//-------------------------------------------------------------------------------------------------------------------
static void odom_map_live (void)
{
    uint16 n = imu_odom_trail_num();
    uint8  wpn = imu_odom_wp_num();
    uint16 i = 0;
    int16  tx = 0;
    int16  ty = 0;
    int16  sx = 0;
    int16  sy = 0;

    if (0 == map_ready) { return; }

    // 位置 / 轨迹跑出地图范围：范围放大一倍，整块重画
    if ((fabsf(imu_odom_x_mm()) > (float)map_range_mm * 0.95f) || (fabsf(imu_odom_y_mm()) > (float)map_range_mm * 0.95f))
    {
        map_range_mm = map_range_mm * 2;
        odom_map_full();
        return;
    }

    if (n < map_drawn)                                                  // 轨迹被清过（轨迹点数变少了）
    {
        odom_map_full();
        return;
    }

    for (i = map_drawn; i < n; i ++)
    {
        imu_odom_trail_get(i, &tx, &ty);
        map_xy(tx, ty, &sx, &sy);
        menu_display_line((uint16)map_last_sx, (uint16)map_last_sy, (uint16)sx, (uint16)sy, RGB565_GREEN);
        map_last_sx = sx;
        map_last_sy = sy;
    }
    map_drawn = n;

    if (0 != map_cur_ok) { map_dot(map_cur_sx, map_cur_sy, MENU_COLOR_BG); }   // 擦掉旧的位置标记
    map_xy((int16)imu_odom_x_mm(), (int16)imu_odom_y_mm(), &sx, &sy);
    map_dot(sx, sy, RGB565_RED);
    map_cur_sx = sx;
    map_cur_sy = sy;
    map_cur_ok = 1;

    while (map_wp_drawn < wpn)                                          // 新记录的航点补画
    {
        imu_odom_wp_get(map_wp_drawn, &tx, &ty, NULL);
        map_xy(tx, ty, &sx, &sy);
        map_dot(sx, sy, RGB565_YELLOW);
        map_wp_drawn ++;
    }
}

static void odom_draw_all (void)
{
    imu_msg_row = IMU_MSG_ROW;
    menu_display_clear();
    imu_title("里程定位");
    odom_draw_values();
    imu_msg_draw();
    imu_hint("1=原点 2=清轨迹 3=记点 4=返回", "1原2清3记4返");
    odom_map_full();
}

// ===================================================================================================================
//                                              页面 4：航点记录
// ===================================================================================================================
static uint8 wp_visible_rows (void)
{
    uint16 y0 = imu_line_y(0);
    uint16 y1 = (uint16)(imu_hint_y() - 2);
    uint8  n;

    if (y1 <= y0) { return 1; }

    n = (uint8)((y1 - y0) / imu_line_h());
    return (0 == n) ? (1) : (n);
}

static void wp_fix_scroll (uint8 rows)
{
    uint8 vis = wp_visible_rows();

    if (wp_sel < wp_top)                   { wp_top = wp_sel; }
    if (wp_sel >= (uint8)(wp_top + vis))   { wp_top = (uint8)(wp_sel - vis + 1); }
    if (wp_top >= rows)                    { wp_top = 0; }
}

static void wp_draw_all (void)
{
    imu_msg_row = IMU_MSG_ROW;
    uint8 num = imu_odom_wp_num();
    uint8 rows = (uint8)(num + 1);                                      // 最后一行 = 清空全部
    uint8 vis = wp_visible_rows();
    uint8 i;
    int16 wx = 0;
    int16 wy = 0;
    int16 wyaw = 0;

    wp_fix_scroll(rows);

    menu_display_clear();
    imu_title("航点记录");

    for (i = 0; i < vis; i ++)
    {
        uint8 row = (uint8)(wp_top + i);
        uint8 y0;

        if (row >= rows) { break; }

        y0     = (uint8)imu_line_y(i);

        if (0 != ((row == wp_sel) ? (1) : (0)))
        {
            menu_display_fill(0, y0, menu_display_width(), 18, MENU_COLOR_SEL);   // 选中行蓝色高亮
        }

        if (row < num)
        {
            imu_odom_wp_get(row, &wx, &wy, &wyaw);
            if (menu_display_width() >= 200)
            {
                snprintf(imu_buf, sizeof(imu_buf), "#%d  X%ld  Y%ld  A%ld",
                         (int)(row + 1), (long)wx, (long)wy, (long)(wyaw / 10));
            }
            else
            {
                snprintf(imu_buf, sizeof(imu_buf), "#%d %ld,%ld", (int)(row + 1), (long)wx, (long)wy);
            }
            menu_display_set_color(MENU_COLOR_PEN, MENU_COLOR_BG);
            menu_display_string(IMU_X, y0, imu_buf);
        }
        else
        {
            menu_display_set_color(MENU_COLOR_PEN, MENU_COLOR_BG);
            menu_display_cn(IMU_X, y0, "清空全部航点");
        }
    }

    imu_msg_draw();
    imu_hint("3=记录 长按3=删除 4=返回", "3记 长3删 4返");
}

static void wp_redraw_values (void)
{
    // 航点页整屏内容都跟选中行有关，简单点：直接整屏重画（它不需要 10Hz 刷新）
    wp_draw_all();
}

// ===================================================================================================================
//                                              对外接口
// ===================================================================================================================
void menu_imu_enter (uint8 page)
{
    imu_page_now = page;
    imu_msg_set("");

    if (MENU_IMU_PAGE_ODOM == page)
    {
        map_ready    = 0;
        map_range_mm = 1500;
        map_drawn    = 0;
        map_wp_drawn = 0;
        map_cur_ok   = 0;
    }
    if (MENU_IMU_PAGE_WP == page)
    {
        wp_sel = 0;
        wp_top = 0;
    }

    menu_imu_draw(page);
}

void menu_imu_draw (uint8 page)
{
    switch (page)
    {
        case MENU_IMU_PAGE_ATT:     att_draw_all();     break;
        case MENU_IMU_PAGE_CALIB:   calib_draw_all();   break;
        case MENU_IMU_PAGE_ODOM:    odom_draw_all();    break;
        case MENU_IMU_PAGE_WP:      wp_draw_all();      break;
        default:                                        break;
    }
}

void menu_imu_live (uint8 page)
{
    switch (page)
    {
        case MENU_IMU_PAGE_ATT:
            att_draw_values();
            break;

        case MENU_IMU_PAGE_CALIB:
            calib_draw_values();
            break;

        case MENU_IMU_PAGE_ODOM:
            odom_draw_values();
            odom_map_live();
            break;

        default:
            break;                                                      // 航点页不自动刷新（内容是静态列表）
    }
}

uint8 menu_imu_key (uint8 page, uint8 k_up, uint8 k_down, uint8 k_ok, uint8 k_ok_long, uint8 k_back)
{
    uint8 ret = 0;

    switch (page)
    {
        // ------------------------------------------------ 姿态显示 ------------------------------------------------
        case MENU_IMU_PAGE_ATT:
            if (0 != k_up)                                               // 1 = 原点归零
            {
                imu_odom_reset();
                map_range_mm = 1500;
                imu_msg_set("原点已归零");
                imu_msg_draw();
            }
            if (0 != k_down)                                             // 2 = 清轨迹
            {
                imu_odom_trail_clear();
                imu_msg_set("轨迹已清空");
                imu_msg_draw();
            }
            if (0 != k_ok) { ret = 2; }                                  // 3 = 去校准页
            if (0 != k_back) { ret = 1; }                                // 4 = 返回
            break;

        // ------------------------------------------------ 陀螺校准 ------------------------------------------------
        case MENU_IMU_PAGE_CALIB:
            if (0 != k_ok)                                               // 3 = 开始校准（阻塞约 1~3 秒）
            {
                uint8 r;

                imu_msg_set("采样中 请保持静止");
                imu_msg_draw();

                r = imu_attitude_calibrate();

                if (0 == r)
                {
                    if (0 == imu_store_save()) { imu_msg_set("校准成功 已存Flash"); }
                    else                       { imu_msg_set("校准成功 存Flash失败"); }
                }
                else if (1 == r) { imu_msg_set("校准失败 零漂过大"); }
                else             { imu_msg_set("校准失败 车在动"); }

                calib_draw_all();
            }
            if (0 != k_up)                                               // 1 = 手动保存到 Flash
            {
                if (0 == imu_store_save()) { imu_msg_set("已存Flash"); }
                else                       { imu_msg_set("存Flash失败"); }
                calib_draw_all();
            }
            if (0 != k_down)                                             // 2 = 零漂清零
            {
                imu_attitude_bias_clear();
                imu_msg_set("零漂已清零");
                calib_draw_all();
            }
            if (0 != k_back) { ret = 1; }
            break;

        // ------------------------------------------------ 里程定位 ------------------------------------------------
        case MENU_IMU_PAGE_ODOM:
            if (0 != k_up)                                               // 1 = 原点归零
            {
                imu_odom_reset();
                map_range_mm = 1500;
                odom_draw_all();
                imu_msg_set("原点已归零");
                imu_msg_draw();
            }
            if (0 != k_down)                                             // 2 = 清轨迹
            {
                imu_odom_trail_clear();
                map_ready = 0;
                odom_draw_all();
                imu_msg_set("轨迹已清空");
                imu_msg_draw();
            }
            if (0 != k_ok)                                               // 3 = 记录当前位置
            {
                if (0 == imu_odom_wp_mark()) { imu_msg_set("航点已满 请先清空"); }
                else
                {
                    if (0 == imu_store_save()) { imu_msg_set("已记录并存Flash"); }
                    else                       { imu_msg_set("已记录 存Flash失败"); }
                }
                odom_draw_all();
                imu_msg_draw();
            }
            if (0 != k_back) { ret = 1; }
            break;

        // ------------------------------------------------ 航点记录 ------------------------------------------------
        case MENU_IMU_PAGE_WP:
        {
            uint8 num  = imu_odom_wp_num();
            uint8 rows = (uint8)(num + 1);

            if (0 != k_up)
            {
                wp_sel = (0 == wp_sel) ? (uint8)(rows - 1) : (uint8)(wp_sel - 1);
                wp_redraw_values();
            }
            if (0 != k_down)
            {
                wp_sel ++;
                if (wp_sel >= rows) { wp_sel = 0; }
                wp_redraw_values();
            }
            if (0 != k_ok_long)                                          // 长按 3 = 删除选中 / 清空全部
            {
                if (wp_sel < num)
                {
                    uint8 i;
                    int16 x = 0;
                    int16 y = 0;
                    int16 a = 0;

                    for (i = (uint8)(wp_sel + 1); i < num; i ++)         // 后面的往前挪一格
                    {
                        imu_odom_wp_get(i, &x, &y, &a);
                        imu_odom_wp_set((uint8)(i - 1), x, y, a);
                    }
                    imu_odom_wp_num_set((uint8)(num - 1));
                    if (wp_sel >= imu_odom_wp_num()) { wp_sel = (0 == imu_odom_wp_num()) ? (0) : (uint8)(imu_odom_wp_num() - 1); }
                    imu_store_save();
                    imu_msg_set("已删除该航点");
                }
                else
                {
                    imu_odom_wp_clear();
                    imu_store_save();
                    wp_sel = 0;
                    imu_msg_set("航点已全部清空");
                }
                wp_redraw_values();
            }
            else if (0 != k_ok)                                          // 3 = 记录当前位置
            {
                if (0 == imu_odom_wp_mark()) { imu_msg_set("航点已满 请先清空"); }
                else
                {
                    if (0 == imu_store_save()) { imu_msg_set("已记录并存Flash"); }
                    else                       { imu_msg_set("已记录 存Flash失败"); }
                }
                wp_redraw_values();
            }
            if (0 != k_back) { ret = 1; }
            break;
        }

        default:
            ret = 1;
            break;
    }

    (void)imu_page_now;
    return ret;
}
