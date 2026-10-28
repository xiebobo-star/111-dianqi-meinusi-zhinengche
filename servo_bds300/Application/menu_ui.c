/*********************************************************************************************************************
* 文件名            menu_ui.c
* 说明              TC264 多级菜单界面：菜单树 + 参数编辑 + 弹窗确认 + 状态页
*
* 菜单结构（主菜单 3 项，每项下面 2~3 个子菜单）：
*   主菜单
*     ├─ 参数设置 ─ Servo / 传感器参数 / 系统参数（进去直接改，改完自动存 Flash，没有「保存」这一行）
*     │            └ 恢复全部默认（带弹窗确认）
*     ├─ 状态查看 ─ 运行状态 / 存储状态 / 参数总览
*     └─ 系统信息 ─ 版本信息 / 硬件信息 / 操作说明
*
* 按键：
*   菜单里      按键1 = 上选   按键2 = 下选   按键3 = 确定   按键4 = 返回上一层
*   参数页面    按键1/2 = 上下选   按键3 = 进入修改（行变黄、两侧出现箭头）
*   修改中      按键1 = 加一档   按键2 = 减一档   按键3 = 完成   按键4 = 完成（一样，退出修改）
*               ★ 每按一下就已经生效（舵机当场就动），退出修改界面立刻写进片内 Flash，
*                 停手 0.8s 也会自动写一次，所以标题右边很快就是「已保存」，不用再找保存按钮
*               ★ 刷屏只重画动过的那一两行，不整屏重来，所以按住加减是跟手的
*   长按按键4 = 任何页面直接回主菜单
*
* 视觉反馈：选中的项是一个填色高亮条 + 左边「>」箭头；子菜单右侧还有一个「>」；正在修改的项是黄色的 + 两侧箭头
********************************************************************************************************************/
#include "menu_config.h"
#include "cpu0_main.h"                      // g_AppCpu0.info.cpuFreq（系统信息页显示主频）
#include "menu_display.h"
#include "menu_key.h"
#include "menu_param.h"
#include "menu_flash.h"
#include "menu_ui.h"
#include "servo_control.h"

#include <stdarg.h>

// ============================================ 串口调试宏 ============================================
// 编译开关 MENU_SERIAL_DEBUG 在 menu_config.h；运行开关是菜单里的「串口调试」参数（默认开）
static uint8 ui_serial_on = 1;

#if (1 == MENU_SERIAL_DEBUG)
#define UI_LOG(str)                 do { if (0 != ui_serial_on) { printf(str "\r\n"); } } while (0)
#define UI_LOG_ONE(str, val)        do { if (0 != ui_serial_on) { printf(str "\r\n", (int)(val)); } } while (0)
#else
#define UI_LOG(str)
#define UI_LOG_ONE(str, val)
#endif

// ============================================ 菜单树 ============================================
typedef enum
{
    MENU_ACT_SUBMENU = 0,                   // 这个节点有子菜单
    MENU_ACT_PARAM_GROUP,                   // 打开参数组编辑页（arg = 组号）
    MENU_ACT_INFO,                          // 打开信息页（arg = 页号）
    MENU_ACT_OVERVIEW,                      // 打开参数总览页
    MENU_ACT_DEFAULT_ALL,                   // 全部恢复默认值（弹窗确认）
}menu_action_enum;

typedef enum
{
    MENU_PAGE_VERSION = 0,                  // 版本信息
    MENU_PAGE_HARDWARE,                     // 硬件信息
    MENU_PAGE_HELP,                         // 操作说明
    MENU_PAGE_RUN,                          // 运行状态
    MENU_PAGE_FLASH,                        // 存储状态
    MENU_PAGE_KEYTEST,                      // 按键自检（实时显示每个键按下/松开 + 次数）
}menu_page_enum;

typedef struct menu_node_struct
{
    const char                  *text;      // 本项显示的文字（GBK 编码，建议不超过 6 个汉字）
    menu_action_enum             action;
    const struct menu_node_struct *child;   // 子菜单数组首地址（action = SUBMENU 时有效）
    uint8                        child_num; // 子菜单有几项
    uint8                        arg;       // 组号 / 页号
}menu_node_struct;

// ---------------------------------------------------------------- 第 2 层：三组参数 ------------------------------------------------
static const menu_node_struct menu_param_setting[] =
{
    {"Servo",         MENU_ACT_PARAM_GROUP,   NULL, 0, 0},
    {"传感器参数",   MENU_ACT_PARAM_GROUP,   NULL, 0, 1},
    {"系统参数",     MENU_ACT_PARAM_GROUP,   NULL, 0, 2},
    {"恢复全部默认", MENU_ACT_DEFAULT_ALL,   NULL, 0, 0},
};

static const menu_node_struct menu_status_view[] =
{
    {"运行状态",     MENU_ACT_INFO,          NULL, 0, MENU_PAGE_RUN},
    {"存储状态",     MENU_ACT_INFO,          NULL, 0, MENU_PAGE_FLASH},
    {"参数总览",     MENU_ACT_OVERVIEW,      NULL, 0, 0},
};

static const menu_node_struct menu_system_info[] =
{
    {"版本信息",     MENU_ACT_INFO,          NULL, 0, MENU_PAGE_VERSION},
    {"硬件信息",     MENU_ACT_INFO,          NULL, 0, MENU_PAGE_HARDWARE},
    {"操作说明",     MENU_ACT_INFO,          NULL, 0, MENU_PAGE_HELP},
    {"按键自检",     MENU_ACT_INFO,          NULL, 0, MENU_PAGE_KEYTEST},   // 按哪个键哪行就变「按住」
};

// ---------------------------------------------------------------- 第 1 层：主菜单 ------------------------------------------------
static const menu_node_struct menu_main_list[] =
{
    {"参数设置", MENU_ACT_SUBMENU, menu_param_setting, 4, 0},
    {"状态查看", MENU_ACT_SUBMENU, menu_status_view,   3, 0},
    {"系统信息", MENU_ACT_SUBMENU, menu_system_info,   4, 0},
};

// ============================================ 界面状态 ============================================
typedef enum
{
    MENU_UI_STATE_MENU = 0,                 // 正在浏览菜单列表
    MENU_UI_STATE_PARAM,                    // 正在看某个参数组的参数列表
    MENU_UI_STATE_EDIT,                     // 正在修改某一个参数（在参数页上把那一行画成黄色）
    MENU_UI_STATE_POPUP,                    // 弹窗确认（保存 / 恢复默认）
    MENU_UI_STATE_TOAST,                    // 结果提示（保存成功 / 保存失败 / 已取消）
    MENU_UI_STATE_INFO,                     // 信息页（版本 / 硬件 / 说明 / 运行状态 / 存储状态）
    MENU_UI_STATE_OVERVIEW,                 // 参数总览页
}menu_ui_state_enum;

typedef enum
{
    MENU_POPUP_DEFAULT_GROUP = 0,           // 确认本组恢复默认
    MENU_POPUP_DEFAULT_ALL,                 // 确认全部恢复默认
}menu_popup_enum;

#define MENU_STACK_DEPTH        (4)         // 菜单最多压几层（本工程用到 2 层，留余量给以后加三级菜单）

typedef struct
{
    const menu_node_struct *node;           // 这一层是哪个菜单数组
    uint8                   num;            // 这一层有几项
    uint8                   index;          // 当前选中第几项
    uint8                   top;            // 滚动显示的第一项
}menu_level_struct;

static menu_level_struct menu_stack[MENU_STACK_DEPTH];
static const char       *menu_title_stack[MENU_STACK_DEPTH];
static uint8             menu_depth;                        // 当前在第几层（0 = 主菜单）

static menu_ui_state_enum ui_state;                         // 当前界面状态
static menu_ui_state_enum ui_return_state;                  // 弹窗 / 提示框结束后回到哪个状态
static menu_popup_enum    ui_popup;                         // 当前弹窗的类型
static uint8              ui_group;                         // 当前正在看的参数组
static uint8              ui_row;                           // 参数页 / 总览页当前行
static uint8              ui_top;                           // 参数页 / 总览页滚动的第一行
static int32              ui_edit_value;                    // 修改中的值（按确定才写进参数表）
static uint8              ui_edit_coarse;                   // 1 = 这一轮按住已经进入长按大步（脉宽 100us / 角度 10.0°）
static uint8              ui_page;                          // 信息页编号
static uint32             ui_ms;                            // 上电经过的毫秒数（10ms 中断累加）
static uint32             ui_run_ms;                         // 运行状态页上次重画的时刻（脉宽/角度实时刷新用）
static uint32             ui_toast_ms;                      // 提示框出现的时刻
static const char        *ui_toast_text;                    // 提示框文字
static uint16             ui_toast_color;                   // 提示框文字颜色
static uint8              ui_need_draw;                     // 1 = 需要重画整屏
static uint32             ui_keytest_sig;                    // 按键自检页的「状态签名」，变了才重画
static uint32             ui_last_key_ms;                    // 最近一次按键动作的时刻（停手一会儿就自动存 Flash）
static uint8              ui_save_now;                       // 1 = 这一圈主循环立刻存一次 Flash
static uint8              ui_save_block;                     // 1 = 上次存失败了，等下次改参数再重试
static uint8              ui_title_dirty;                    // 1 = 标题右边「未保存 / 已保存」要重画

#define UI_ROW_NONE             (0xff)                      // 「没有要局部重画的行」
static uint8              ui_row_dirty[2] = {UI_ROW_NONE, UI_ROW_NONE};   // 要局部重画的行号（一屏最多两行一起变）

#define MENU_HINT_WIDE_MENU     ("1=上选 2=下选 3=确定 4=返回")
#define MENU_HINT_NARROW_MENU   ("1上 2下 3选 4返")
#define MENU_HINT_WIDE_EDIT     ("1=加 2=减 3/4=退出")
#define MENU_HINT_NARROW_EDIT   ("1加 2减 3/4退")

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     一屏能显示几行
//-------------------------------------------------------------------------------------------------------------------
static uint8 ui_row_visible (void)
{
    return (uint8)((menu_display_height() - MENU_UI_ITEM_Y0 - MENU_UI_LINE_H) / MENU_UI_ROW_H);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     保证当前行在可见窗口里（上下滚动）
//-------------------------------------------------------------------------------------------------------------------
static void ui_scroll_fix (uint8 num)
{
    uint8 vis = ui_row_visible();

    if (0 == vis) { vis = 1; }
    if (ui_row < ui_top)                     { ui_top = ui_row; }
    if (ui_row >= (uint8)(ui_top + vis))     { ui_top = (uint8)(ui_row - vis + 1); }
    if (ui_top > num)                        { ui_top = 0; }
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     记下「这一行变了，需要重画」
// 备注信息     一屏最多两行会同时变（脉宽 + 换算出来的角度），所以只开两个槽。
//              局部重画比整屏 clear + 全部重画快十几倍，按住加减才不会一顿一顿的
//-------------------------------------------------------------------------------------------------------------------
static void ui_mark_row (uint8 r)
{
    if ((UI_ROW_NONE == ui_row_dirty[0]) || (ui_row_dirty[0] == r)) { ui_row_dirty[0] = r; return; }
    if ((UI_ROW_NONE == ui_row_dirty[1]) || (ui_row_dirty[1] == r)) { ui_row_dirty[1] = r; }
}

static void ui_clear_dirty_rows (void)
{
    ui_row_dirty[0] = UI_ROW_NONE;
    ui_row_dirty[1] = UI_ROW_NONE;
}

static uint8 ui_need_partial (void)
{
    if (UI_ROW_NONE != ui_row_dirty[0]) { return 1; }
    if (UI_ROW_NONE != ui_row_dirty[1]) { return 1; }
    return (0 != ui_title_dirty) ? (1) : (0);
}

// 改这一项时，本组还有哪一项的数值会跟着变（脉宽 <-> 角度），一起重画
static void ui_mark_linked_row (uint8 item)
{
    uint8 first = menu_param_group_first_item(ui_group);
    uint8 other;

    if (0 != first) { return; }                                 // 只有第一组（Servo）有联动

    if (MENU_PARAM_IDX_SERVO_ANGLE == item)                                     { other = MENU_PARAM_IDX_SERVO_PULSE; }
    else if (MENU_PARAM_IDX_SERVO_PULSE == item)                                { other = MENU_PARAM_IDX_SERVO_ANGLE; }
    else if ((MENU_PARAM_IDX_SERVO_MIN    == item)
          || (MENU_PARAM_IDX_SERVO_CENTER == item)
          || (MENU_PARAM_IDX_SERVO_MAX    == item))                             { other = MENU_PARAM_IDX_SERVO_ANGLE; }
    else                                                                        { return; }

    ui_mark_row((uint8)(other - first));
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     数值显示在哪一列（窄屏靠左，宽屏中间偏右）
//-------------------------------------------------------------------------------------------------------------------
static uint16 ui_value_x (void)
{
    return (menu_display_width() >= 200) ? (120) : (78);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     底部操作提示（宽屏 / 窄屏两套文字，128 宽的 TFT180 也放得下）
//-------------------------------------------------------------------------------------------------------------------
static void ui_draw_hint (void)
{
    const char *hint;
    uint8       edit = (MENU_UI_STATE_EDIT == ui_state) ? (1) : (0);

    if ((MENU_UI_STATE_INFO == ui_state) && (MENU_PAGE_KEYTEST == ui_page))      // 自检页：短按留给检测，长按才退出
    {
        menu_display_set_color(MENU_COLOR_SEL, MENU_COLOR_BG);
        menu_display_cn(2, (uint16)(menu_display_height() - MENU_UI_LINE_H + 2),
                        (menu_display_width() >= 200) ? ("按4退出 长按4回主菜单") : ("4退 长按4回"));
        menu_display_set_color(MENU_COLOR_PEN, MENU_COLOR_BG);
        return;
    }

    if (menu_display_width() >= 200)
    {
        hint = (0 != edit) ? (MENU_HINT_WIDE_EDIT) : (MENU_HINT_WIDE_MENU);
    }
    else
    {
        hint = (0 != edit) ? (MENU_HINT_NARROW_EDIT) : (MENU_HINT_NARROW_MENU);
    }

    menu_display_set_color(MENU_COLOR_SEL, MENU_COLOR_BG);
    menu_display_cn(2, (uint16)(menu_display_height() - MENU_UI_LINE_H + 2), hint);
    menu_display_set_color(MENU_COLOR_PEN, MENU_COLOR_BG);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     画标题行（左边标题，右边可以挂一段状态文字，比如「未保存」）
//-------------------------------------------------------------------------------------------------------------------
static void ui_draw_title (const char *title, const char *right)
{
    uint16 sw = menu_display_width();

    menu_display_set_color(MENU_COLOR_TITLE, MENU_COLOR_BG);
    menu_display_cn(4, MENU_UI_TITLE_Y, title);

    if (NULL != right)
    {
        uint16 tw = menu_display_text_width(right);

        if ((uint16)(tw + 8) < sw)
        {
            menu_display_cn((uint16)(sw - tw - 4), MENU_UI_TITLE_Y, right);
        }
    }
    menu_display_line(0, (uint16)(MENU_UI_TITLE_Y + 18), (uint16)(sw - 1), (uint16)(MENU_UI_TITLE_Y + 18), MENU_COLOR_SEL);
    menu_display_set_color(MENU_COLOR_PEN, MENU_COLOR_BG);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     标题右边那三个字（当前是「未保存 / 已保存 / 存失败」）
//-------------------------------------------------------------------------------------------------------------------
static const char *ui_title_state_text (void)
{
    if (0 != ui_save_block)                             { return "存失败"; }
    if (0 != menu_param_group_dirty(ui_group))           { return "未保存"; }
    return "已保存";
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     只重画标题右边那三个字
// 备注信息     自动保存之后把「未保存」刷成「已保存」，不用整屏重画
//-------------------------------------------------------------------------------------------------------------------
static void ui_draw_title_state (void)
{
    uint16      sw    = menu_display_width();
    const char *right = ui_title_state_text();
    uint16      w     = 56;                                     // 「未保存」3 个汉字 = 48px，留点余量

    if ((uint16)(sw - w) < (uint16)(76)) { w = 40; }             // 极窄屏兜底

    menu_display_fill((uint16)(sw - w), 0, w, 20, MENU_COLOR_BG);
    menu_display_set_color(MENU_COLOR_TITLE, MENU_COLOR_BG);
    menu_display_cn((uint16)(sw - menu_display_text_width(right) - 4), MENU_UI_TITLE_Y, right);
    menu_display_set_color(MENU_COLOR_PEN, MENU_COLOR_BG);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     画一行菜单 / 参数
// 参数说明     text        文字
// 参数说明     value       数值字符串（NULL = 这一行没有数值）
// 参数说明     selected    1 = 这一行被选中（画高亮条 + 左边箭头）
// 参数说明     edit        1 = 这一行正在修改（黄色高亮 + 两侧尖角箭头）
// 参数说明     arrow       1 = 这一行右边画一个「>」（表示能进去 / 是个动作）
// 备注信息     每行都先按自己的底色铺一遍，所以画完不会留下上一屏的残字
//-------------------------------------------------------------------------------------------------------------------
static void ui_draw_row (uint16 y, const char *text, const char *value, uint8 selected, uint8 edit, uint8 arrow)
{
    uint16 bg = MENU_COLOR_BG;

    if (0 != selected) { bg = (0 != edit) ? (MENU_COLOR_EDIT) : (MENU_COLOR_SEL); }

    menu_display_fill(0, (uint16)(y - 1), menu_display_width(), MENU_UI_BAR_H, bg);

    if (0 != selected) { menu_display_set_color(MENU_COLOR_BG, bg); }        // 选中：反色显示（黑字彩底）
    else               { menu_display_set_color(MENU_COLOR_PEN, bg); }

    if (0 != selected)
    {
        menu_display_char(4, y, '>');                                       // 箭头指示
    }
    menu_display_cn(14, y, text);

    if (NULL != value)
    {
        menu_display_cn(ui_value_x(), y, value);
        if (0 != edit)
        {
            if (ui_value_x() >= 100)                                // 窄屏左边放不下这个箭头，就不画了
            {
                menu_display_char((uint16)(ui_value_x() - 10), y, '<');
            }
            menu_display_char((uint16)(menu_display_width() - 12), y, '>');
        }
    }
    if (0 != arrow)
    {
        menu_display_char((uint16)(menu_display_width() - 12), y, '>');
    }

    menu_display_set_color(MENU_COLOR_PEN, MENU_COLOR_BG);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     画菜单列表的一行（整页重画和局部重画共用）
//-------------------------------------------------------------------------------------------------------------------
static void ui_draw_menu_row (uint8 r)
{
    menu_level_struct *lv  = &menu_stack[menu_depth];
    uint8              vis = ui_row_visible();

    if ((r < lv->top) || ((uint8)(r - lv->top) >= vis)) { return; }          // 不在可见窗口里就不用画

    ui_draw_row((uint16)(MENU_UI_ITEM_Y0 + (uint8)(r - lv->top) * MENU_UI_ROW_H), lv->node[r].text, NULL,
                (r == lv->index) ? (1) : (0), 0, (MENU_ACT_SUBMENU == lv->node[r].action) ? (1) : (0));
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     只重画标题右边的「第几项 / 共几项」
//-------------------------------------------------------------------------------------------------------------------
static void ui_draw_menu_counter (void)
{
    menu_level_struct *lv  = &menu_stack[menu_depth];
    char               buf[12];
    uint16             sw  = menu_display_width();

    snprintf(buf, sizeof(buf), "%d/%d", (int)(lv->index + 1), (int)lv->num);

    menu_display_fill((uint16)(sw - 44), 0, 44, 20, MENU_COLOR_BG);
    menu_display_set_color(MENU_COLOR_TITLE, MENU_COLOR_BG);
    menu_display_cn((uint16)(sw - menu_display_text_width(buf) - 4), MENU_UI_TITLE_Y, buf);
    menu_display_set_color(MENU_COLOR_PEN, MENU_COLOR_BG);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     画菜单列表页
//-------------------------------------------------------------------------------------------------------------------
static void ui_draw_menu (void)
{
    menu_level_struct *lv = &menu_stack[menu_depth];
    uint8 vis = ui_row_visible();
    uint8 i;

    ui_scroll_fix(lv->num);
    menu_display_clear();
    ui_draw_title(menu_title_stack[menu_depth], NULL);
    ui_draw_menu_counter();

    if (vis > lv->num) { vis = lv->num; }

    for (i = 0; i < vis; i ++)
    {
        if ((uint8)(lv->top + i) >= lv->num) { break; }
        ui_draw_menu_row((uint8)(lv->top + i));
    }
    ui_draw_hint();
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     用参数表里当前的三个标定点，把脉宽换成等效角度（0.1° 为单位）
//-------------------------------------------------------------------------------------------------------------------
static uint16 ui_pulse_to_angle_x10 (uint16 pulse_us)
{
    return servo_pulse_to_angle_x10_cal(pulse_us,
                                        (uint16)menu_param_get(MENU_PARAM_IDX_SERVO_MIN),
                                        (uint16)menu_param_get(MENU_PARAM_IDX_SERVO_CENTER),
                                        (uint16)menu_param_get(MENU_PARAM_IDX_SERVO_MAX));
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     反过来：角度（0.1°）换成脉宽，同样用参数表里的标定点
//-------------------------------------------------------------------------------------------------------------------
static uint16 ui_angle_x10_to_pulse (uint16 angle_x10)
{
    return servo_angle_x10_to_pulse_cal(angle_x10,
                                        (uint16)menu_param_get(MENU_PARAM_IDX_SERVO_MIN),
                                        (uint16)menu_param_get(MENU_PARAM_IDX_SERVO_CENTER),
                                        (uint16)menu_param_get(MENU_PARAM_IDX_SERVO_MAX));
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     脉宽 / 角度 双向联动（脉宽是唯一真值，角度是换算出来的显示值）
// 参数说明     item    刚刚被改动的参数项下标
// 备注信息     改脉宽 -> 角度跟着变；改角度 -> 脉宽跟着变；改三个标定点 -> 角度按新标定重算
//-------------------------------------------------------------------------------------------------------------------
static void ui_servo_sync (uint8 item)
{
    if (0 != menu_param_group_first_item(ui_group)) { return; }                     // 只有第一组（Servo）需要联动

    if (MENU_PARAM_IDX_SERVO_ANGLE == item)                                         // 改的是角度：反算脉宽
    {
        menu_param_set(MENU_PARAM_IDX_SERVO_PULSE, (int32)ui_angle_x10_to_pulse((uint16)menu_param_get(MENU_PARAM_IDX_SERVO_ANGLE)));
    }
    else if ((MENU_PARAM_IDX_SERVO_PULSE  == item)
          || (MENU_PARAM_IDX_SERVO_MIN    == item)
          || (MENU_PARAM_IDX_SERVO_CENTER == item)
          || (MENU_PARAM_IDX_SERVO_MAX    == item))
    {
        menu_param_set(MENU_PARAM_IDX_SERVO_ANGLE, (int32)ui_pulse_to_angle_x10((uint16)menu_param_get(MENU_PARAM_IDX_SERVO_PULSE)));
    }
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     画参数页的一行（参数项 / 恢复本组默认）—— 整页重画和局部重画共用
//-------------------------------------------------------------------------------------------------------------------
static void ui_draw_param_row (uint8 r)
{
    uint8  num   = menu_param_group_item_num(ui_group);
    uint8  first = menu_param_group_first_item(ui_group);
    uint8  vis   = ui_row_visible();
    uint16 y;

    if ((r < ui_top) || ((uint8)(r - ui_top) >= vis)) { return; }          // 不在可见窗口里就不用画

    y = (uint16)(MENU_UI_ITEM_Y0 + (uint8)(r - ui_top) * MENU_UI_ROW_H);

    if (r < num)                                                        // 参数项
    {
        uint8 item = (uint8)(first + r);
        uint8 edit = ((MENU_UI_STATE_EDIT == ui_state) && (r == ui_row)) ? (1) : (0);

        // 修改时每按一下都直接写进参数表了，这里直接显示真值（联动的另一项也同步好了）
        ui_draw_row(y, menu_param_item_name(item), menu_param_value_str(item),
                    (r == ui_row) ? (1) : (0), edit, 0);
    }
    else                                                                // 恢复本组默认
    {
        ui_draw_row(y, "恢复本组默认", NULL, (r == ui_row) ? (1) : (0), 0, 1);
    }
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     画参数组页面（参数项 + 末尾一行动作：恢复本组默认）
//-------------------------------------------------------------------------------------------------------------------
static void ui_draw_param (void)
{
    uint8 num  = menu_param_group_item_num(ui_group);
    uint8 rows = (uint8)(num + 1);                                  // 末尾只剩一行：恢复本组默认
    uint8 vis  = ui_row_visible();
    uint8 i;

    ui_scroll_fix(rows);
    menu_display_clear();
    ui_draw_title(menu_param_group_name(ui_group), ui_title_state_text());

    if (vis > rows) { vis = rows; }

    for (i = 0; i < vis; i ++)
    {
        if ((uint8)(ui_top + i) >= rows) { break; }
        ui_draw_param_row((uint8)(ui_top + i));
    }
    ui_draw_hint();
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     画参数总览页（每组一个小标题，下面列出这一组所有参数的值，可上下滚动）
//-------------------------------------------------------------------------------------------------------------------
static uint16 ui_overview_value_x (void)                // 总览页数值列（宽屏留出右边给单位）
{
    return (menu_display_width() >= 200) ? (138) : (78);
}

static uint8 ui_overview_rows (void)
{
    return (uint8)(MENU_PARAM_GROUP_NUM + menu_param_item_total());
}

static void ui_overview_row (uint8 row, const char **text, const char **value, uint8 *head)
{
    uint8 index = 0;
    uint8 g;

    *value = NULL;
    *head  = 0;

    for (g = 0; g < MENU_PARAM_GROUP_NUM; g ++)
    {
        uint8 k;

        if (index == row) { *text = menu_param_group_name(g); *head = 1; return; }
        index ++;

        for (k = 0; k < menu_param_group_item_num(g); k ++)
        {
            uint8 item = (uint8)(menu_param_group_first_item(g) + k);

            if (index == row) { *text = menu_param_item_name(item); *value = menu_param_value_str(item); return; }
            index ++;
        }
    }
    *text = "";
}

static uint8 ui_overview_item (uint8 row)                   // 总览页第 row 行对应的参数项下标
{
    uint8 index = 0;
    uint8 g;

    for (g = 0; g < MENU_PARAM_GROUP_NUM; g ++)
    {
        uint8 k;

        if (index == row) { return menu_param_group_first_item(g); }
        index ++;

        for (k = 0; k < menu_param_group_item_num(g); k ++)
        {
            if (index == row) { return (uint8)(menu_param_group_first_item(g) + k); }
            index ++;
        }
    }
    return 0;
}

static void ui_draw_overview (void)
{
    uint8 rows = ui_overview_rows();
    uint8 vis  = ui_row_visible();
    uint8 i;

    ui_scroll_fix(rows);
    menu_display_clear();
    ui_draw_title("参数总览", NULL);

    if (vis > rows) { vis = rows; }

    for (i = 0; i < vis; i ++)
    {
        uint8       r = (uint8)(ui_top + i);
        uint16      y = (uint16)(MENU_UI_ITEM_Y0 + i * MENU_UI_ROW_H);
        const char *text;
        const char *value;
        uint8       head;
        uint16      bg;

        if (r >= rows) { break; }
        ui_overview_row(r, &text, &value, &head);

        bg = (0 != head) ? (MENU_COLOR_TITLE) : ((r == ui_row) ? (MENU_COLOR_SEL) : (MENU_COLOR_BG));
        menu_display_fill(0, (uint16)(y - 1), menu_display_width(), MENU_UI_BAR_H, bg);

        if (0 != head)                          { menu_display_set_color(MENU_COLOR_BG, bg); }
        else if (r == ui_row)                   { menu_display_set_color(MENU_COLOR_BG, bg); }
        else                                    { menu_display_set_color(MENU_COLOR_PEN, bg); }

        menu_display_cn(14, y, text);
        if (NULL != value)
        {
            menu_display_cn(ui_overview_value_x(), y, value);        // 数值统一从固定列开始画

            if ((menu_display_width() >= 200) && (0 == head))       // 单位只有宽屏放得下，窄屏就不画了
            {
                const char *unit = menu_param_item_unit(ui_overview_item(r));

                if (NULL != unit) { menu_display_cn((uint16)(menu_display_width() - 46), y, unit); }
            }
        }
        menu_display_set_color(MENU_COLOR_PEN, MENU_COLOR_BG);
    }
    ui_draw_hint();
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     信息页 / 状态页（每页几行文字，超宽自动换行）
//-------------------------------------------------------------------------------------------------------------------
#define UI_INFO_LINE_MAX        (10)
#define UI_INFO_LINE_LEN        (40)

static char  ui_info_line[UI_INFO_LINE_MAX][UI_INFO_LINE_LEN];
static uint8 ui_info_num;
static uint8 ui_info_top;                     // 信息页从第几行开始画（窄屏一屏放不下时用 1/2 键翻）

static void ui_info_clear (void)
{
    ui_info_num = 0;
}

static void ui_info_add (const char *str)
{
    if (ui_info_num < UI_INFO_LINE_MAX)
    {
        strncpy(ui_info_line[ui_info_num], str, UI_INFO_LINE_LEN - 1);
        ui_info_line[ui_info_num][UI_INFO_LINE_LEN - 1] = '\0';
        ui_info_num ++;
    }
}

// 带边界检查的格式化写入（页数写多了也不会越界）
static void ui_info_addf (const char *fmt, ...)
{
    va_list ap;

    if (ui_info_num >= UI_INFO_LINE_MAX) { return; }

    va_start(ap, fmt);
    vsnprintf(ui_info_line[ui_info_num], UI_INFO_LINE_LEN, fmt, ap);
    va_end(ap);
    ui_info_num ++;
}

static void ui_info_build (uint8 page)
{
    ui_info_clear();

    switch (page)
    {
        case MENU_PAGE_VERSION:
            ui_info_add("工程: TC264 多级菜单");
            ui_info_add("版本: " MENU_VERSION_STR);
            ui_info_addf("编译: %s", __DATE__);
            ui_info_addf("时间: %s", __TIME__);
            ui_info_addf("主频: %ld MHz", (long)(g_AppCpu0.info.cpuFreq / 1000000.0f));
            ui_info_add("库: 逐飞 TC264 开源库");
            break;

        case MENU_PAGE_HARDWARE:
#if (MENU_DISPLAY_USE_IPS200)
            ui_info_add("屏幕: IPS200 2.0寸");
#else
            ui_info_add("屏幕: TFT180 1.8寸");
#endif
            ui_info_add("接口: SPI2 P15口");
            ui_info_add("按键: P20_6 P20_7");
            ui_info_add("      P11_2 P11_3");
            ui_info_add("主控: TC264D 双核");
            ui_info_add("存储: 片内DFlash");
            ui_info_add("      96KB 12页");
            break;

        case MENU_PAGE_HELP:
            ui_info_add("1键: 上选");
            ui_info_add("2键: 下选");
            ui_info_add("3键: 确定/进入");
            ui_info_add("4键: 返回/取消");
            ui_info_add("参数页按3进入修改");
            ui_info_add("修改时 1加 2减");
            ui_info_add("3/4键 退出修改");
            ui_info_add("改后自动保存");
            ui_info_add("长按4键 回主菜单");
            break;

        case MENU_PAGE_RUN:
            ui_info_addf("脉宽: %u us", (unsigned)servo_get_pulse());
            ui_info_addf("角度: %u.%u deg", (unsigned)(servo_pulse_to_angle_x10(servo_get_pulse()) / 10u),
                         (unsigned)(servo_pulse_to_angle_x10(servo_get_pulse()) % 10u));
            ui_info_addf("占空比: %lu/10000", (unsigned long)servo_get_duty());
            ui_info_addf("输出: %s", (0 != servo_get_enable()) ? ("开") : ("关"));
            ui_info_addf("上电: %lu s", (unsigned long)(ui_ms / 100));
            ui_info_addf("按键次数: %lu", (unsigned long)menu_key_press_cnt());
            ui_info_add("中断周期: 10 ms");
            ui_info_addf("未保存组: %d 组", (int)menu_param_dirty_group_num());
            ui_info_add("程序: 菜单主循环");
            break;

        case MENU_PAGE_KEYTEST:
        {
            uint8  i;
            uint16 w = menu_display_width();

            for (i = 0; i < MENU_KEY_NUM; i ++)                     // 数组顺序就是板上按键1~4
            {
                const char *role = (MENU_KEY_UP   == i) ? ("上选") : \
                                   (MENU_KEY_DOWN == i) ? ("下选") : \
                                   (MENU_KEY_OK   == i) ? ("确定") : ("返回");

                if (w >= 200)
                {
                    ui_info_addf("按键%d %s %s %s", (int)(i + 1), menu_key_pin_name(i), role,
                                 (0 != menu_key_down(i)) ? ("按住") : ("松开"));
                }
                else
                {
                    ui_info_addf("键%d %s %s", (int)(i + 1), role,
                                 (0 != menu_key_down(i)) ? ("按住") : ("松开"));
                }
            }
            ui_info_addf("次数 %lu %lu %lu %lu",
                         (unsigned long)menu_key_cnt(0), (unsigned long)menu_key_cnt(1),
                         (unsigned long)menu_key_cnt(2), (unsigned long)menu_key_cnt(3));
            ui_info_add("短按确定 长按返回");
            break;
        }

        case MENU_PAGE_FLASH:
            ui_info_addf("页A: %s 序号%lu",
                     (0 != menu_flash_page_valid(0)) ? ("有效") : ("空"), (unsigned long)menu_flash_page_seq(0));
            ui_info_addf("页B: %s 序号%lu",
                     (0 != menu_flash_page_valid(1)) ? ("有效") : ("空"), (unsigned long)menu_flash_page_seq(1));
            ui_info_addf("参数来源: %s", (0 != menu_flash_source()) ? ("Flash") : ("默认值"));
            ui_info_addf("上次保存: %s",
                     (1 == menu_flash_last_result()) ? ("成功") : ((2 == menu_flash_last_result()) ? ("失败") : ("无记录")));
            ui_info_addf("保存次数: %d", (int)menu_flash_save_count());
            ui_info_addf("未保存组: %d 组", (int)menu_param_dirty_group_num());
            ui_info_add("掉电后重新上电自动读回");
            break;

        default:
            ui_info_add("无数据");
            break;
    }
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     画一行文字（超宽自动折行），返回下一行的 y
//-------------------------------------------------------------------------------------------------------------------
static uint16 ui_draw_text_line (uint16 y, const char *str, uint16 y_limit, uint16 x0)
{
    uint16 sw  = menu_display_width();
    uint16 px  = x0;
    uint8  i   = 0;
    char   ch[3];

    while ('\0' != str[i])
    {
        uint8  bytes;
        uint16 cw;

        if ((0x80 > (uint8)str[i]) || ('\0' == str[i + 1]))
        {
            bytes = 1;
            cw    = 8;
            ch[0] = str[i];
            ch[1] = '\0';
        }
        else
        {
            bytes = 2;
            cw    = 16;
            ch[0] = str[i];
            ch[1] = str[i + 1];
            ch[2] = '\0';
        }

        if ((uint16)(px + cw) > sw)                     // 这一行满了，换行
        {
            px = x0;
            y  = (uint16)(y + MENU_UI_INFO_LINE_H);
        }
        if (y > y_limit)                                // 超过这一页允许的最后一行：不再画（再画就压到提示行，甚至超屏触发断言）
        {
            break;
        }
        menu_display_cn(px, y, ch);
        px = (uint16)(px + cw);
        i  = (uint8)(i + bytes);
    }
    return (uint16)(y + MENU_UI_INFO_LINE_H);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     信息页一屏最多画几行
//-------------------------------------------------------------------------------------------------------------------
static uint8 ui_info_rows (void)
{
    return (uint8)((uint16)((MENU_UI_INFO_BOTTOM - MENU_UI_INFO_Y0) / MENU_UI_INFO_LINE_H) + 1);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     信息页标题右边显示「当前行/总行数」，一屏放得下就不显示
//-------------------------------------------------------------------------------------------------------------------
static const char *ui_info_scroll_text (void)
{
    static char buf[8];

    if (ui_info_num > ui_info_rows())
    {
        snprintf(buf, sizeof(buf), "%d/%d", (int)(ui_info_top + 1), (int)ui_info_num);
        return buf;
    }
    return NULL;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     画信息页 / 状态页
//-------------------------------------------------------------------------------------------------------------------
static void ui_draw_info (void)
{
    const char *title;
    uint16 y = (uint16)(MENU_UI_INFO_Y0);
    uint8  i;
    uint8  vis;

    switch (ui_page)
    {
        case MENU_PAGE_VERSION:  title = "版本信息"; break;
        case MENU_PAGE_HARDWARE: title = "硬件信息"; break;
        case MENU_PAGE_HELP:     title = "操作说明"; break;
        case MENU_PAGE_RUN:      title = "运行状态"; break;
        case MENU_PAGE_FLASH:    title = "存储状态"; break;
        case MENU_PAGE_KEYTEST:  title = "按键自检"; break;
        default:                 title = "信息";     break;
    }

    ui_info_build(ui_page);

    if (ui_info_top >= ui_info_num) { ui_info_top = 0; }

    menu_display_clear();
    ui_draw_title(title, ui_info_scroll_text());

    vis = ui_info_rows();
    for (i = ui_info_top; i < ui_info_num; i ++)
    {
        if ((uint8)(i - ui_info_top) >= vis) { break; }
        y = ui_draw_text_line(y, ui_info_line[i], MENU_UI_INFO_BOTTOM, 4);
    }
    ui_draw_hint();
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     把「弹窗底下那个页面」画出来（弹窗只是盖在上面）
//-------------------------------------------------------------------------------------------------------------------
static void ui_draw_under (void)
{
    switch (ui_return_state)
    {
        case MENU_UI_STATE_PARAM:    ui_draw_param();    break;
        case MENU_UI_STATE_INFO:     ui_draw_info();     break;
        case MENU_UI_STATE_OVERVIEW: ui_draw_overview(); break;
        default:                     ui_draw_menu();     break;
    }
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     弹窗文字（保存 / 恢复默认的二次确认，防误操作）
//-------------------------------------------------------------------------------------------------------------------
static const char *ui_popup_text (void)
{
    switch (ui_popup)
    {
        case MENU_POPUP_DEFAULT_GROUP: return "恢复本组默认?";
        case MENU_POPUP_DEFAULT_ALL:   return "全部恢复默认?";
        default:                       return "确认?";
    }
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     画弹窗（居中一个方框，边框黄色）
//-------------------------------------------------------------------------------------------------------------------
static void ui_draw_popup_box (void)
{
    uint16 sw = menu_display_width();
    uint16 sh = menu_display_height();
    uint16 bw = (uint16)(sw - 12);
    uint16 bh = (uint16)(3 * MENU_UI_LINE_H + 8);
    uint16 bx = 6;
    uint16 by = (uint16)((sh - bh) / 2);
    uint16 y;

    menu_display_fill(bx, by, bw, bh, MENU_COLOR_BG);
    menu_display_frame(bx, by, bw, bh, MENU_COLOR_EDIT);

    menu_display_set_color(MENU_COLOR_EDIT, MENU_COLOR_BG);
    y = (uint16)(by + 4);
    y = ui_draw_text_line(y, ui_popup_text(), (uint16)(by + bh - MENU_UI_LINE_H), (uint16)(bx + 4));

    menu_display_set_color(MENU_COLOR_PEN, MENU_COLOR_BG);
    menu_display_cn(12, (uint16)(by + bh - MENU_UI_LINE_H - 2), "3=确定  4=取消");
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     画结果提示框（保存成功 / 保存失败 / 已取消）
//-------------------------------------------------------------------------------------------------------------------
static void ui_draw_toast_box (void)
{
    uint16 sw  = menu_display_width();
    uint16 sh  = menu_display_height();
    uint16 tw  = menu_display_text_width(ui_toast_text);
    uint16 bw  = (uint16)(tw + 16);
    uint16 bh  = (uint16)(MENU_UI_LINE_H + 12);
    uint16 bx;
    uint16 by;

    if (bw > (uint16)(sw - 8)) { bw = (uint16)(sw - 8); }
    bx = (uint16)((sw - bw) / 2);
    by = (uint16)((sh - bh) / 2);

    menu_display_fill(bx, by, bw, bh, MENU_COLOR_BG);
    menu_display_frame(bx, by, bw, bh, ui_toast_color);
    menu_display_set_color(ui_toast_color, MENU_COLOR_BG);
    if (tw > (uint16)(bw - 8)) { menu_display_cn((uint16)(bx + 4), (uint16)(by + 6), ui_toast_text); }
    else                       { menu_display_cn((uint16)(bx + (bw - tw) / 2), (uint16)(by + 6), ui_toast_text); }
    menu_display_set_color(MENU_COLOR_PEN, MENU_COLOR_BG);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     整屏重画（按当前状态分派）
//-------------------------------------------------------------------------------------------------------------------
static void ui_draw (void)
{
    switch (ui_state)
    {
        case MENU_UI_STATE_MENU:     ui_draw_menu();                      break;
        case MENU_UI_STATE_PARAM:    ui_draw_param();                     break;
        case MENU_UI_STATE_EDIT:     ui_draw_param();                     break;
        case MENU_UI_STATE_POPUP:    ui_draw_under(); ui_draw_popup_box(); break;
        case MENU_UI_STATE_TOAST:    ui_draw_under(); ui_draw_toast_box(); break;
        case MENU_UI_STATE_INFO:     ui_draw_info();                      break;
        case MENU_UI_STATE_OVERVIEW: ui_draw_overview();                  break;
        default:                     ui_draw_menu();                      break;
    }
    ui_clear_dirty_rows();                  // 整屏已经画过一遍，局部重画的请求就作废了
    ui_title_dirty = 0;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     局部重画：只画动过的那一两行（+ 右上角那个小字）
// 备注信息     这是「按住加减不卡」的关键：整屏 clear + 重画要几百毫秒，重画两行只要十几毫秒
//-------------------------------------------------------------------------------------------------------------------
static void ui_draw_partial (void)
{
    uint8 i;

    if ((MENU_UI_STATE_PARAM == ui_state) || (MENU_UI_STATE_EDIT == ui_state))
    {
        for (i = 0; i < 2; i ++)
        {
            if (UI_ROW_NONE != ui_row_dirty[i]) { ui_draw_param_row(ui_row_dirty[i]); }
        }
        if (0 != ui_title_dirty) { ui_draw_title_state(); }
    }
    else if (MENU_UI_STATE_MENU == ui_state)
    {
        for (i = 0; i < 2; i ++)
        {
            if (UI_ROW_NONE != ui_row_dirty[i]) { ui_draw_menu_row(ui_row_dirty[i]); }
        }
        ui_draw_menu_counter();
    }

    ui_clear_dirty_rows();
    ui_title_dirty = 0;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     自动保存：参数改完就写进片内 Flash，不用再单独按「保存」
// 备注信息     1. 只有参数真的变了才写（menu_param 的「未保存」标志）；
//              2. 手还在按键时先等，停手 MENU_AUTOSAVE_IDLE_MS 再写，连按加减不会反复擦写 Flash；
//              3. 退出修改界面 / 回上一层 / 回主菜单时会置 ui_save_now，立刻写一次 —— 退出去就一定存好了
//-------------------------------------------------------------------------------------------------------------------
static void ui_auto_save (void)
{
    uint8  ret;
    uint8  led = (0 != menu_param_get(MENU_PARAM_IDX_LED)) ? (1) : (0);

    if (0 == menu_param_dirty_group_num()) { ui_save_now = 0; return; }      // 没有改动，不写
    if (0 != ui_save_block)                { return; }                      // 上次写失败，等下次改参数再试

    if (0 == ui_save_now)
    {
        if ((uint32)(ui_ms - ui_last_key_ms) < MENU_AUTOSAVE_IDLE_MS) { return; }   // 还在按键，等停手
    }
    ui_save_now = 0;

    if (0 != led) { MENU_LED2_ON(); }                                       // 存的时候亮一下，看得见「已经存了」
    ret = menu_flash_save();
    if (0 != led) { MENU_LED2_OFF(); }

    if (0 == ret)
    {
        UI_LOG("[menu] auto save ok");
    }
    else
    {
        ui_save_block = 1;
        UI_LOG("[menu] auto save FAIL");
    }
    ui_title_dirty = 1;                                                     // 右上角「未保存 -> 已保存」
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     弹出确认框
//-------------------------------------------------------------------------------------------------------------------
static void ui_open_popup (menu_popup_enum kind)
{
    ui_popup        = kind;
    ui_return_state = (MENU_UI_STATE_EDIT == ui_state) ? (MENU_UI_STATE_PARAM) : (ui_state);
    ui_state        = MENU_UI_STATE_POPUP;
    ui_need_draw    = 1;
    UI_LOG("[menu] popup");
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     显示结果提示（1.5 秒后自动消失，或者按任意键立刻消失）
//-------------------------------------------------------------------------------------------------------------------
static void ui_set_toast (const char *text, uint16 color)
{
    ui_toast_text  = text;
    ui_toast_color = color;
    ui_toast_ms    = ui_ms;
    ui_state       = MENU_UI_STATE_TOAST;
    ui_need_draw   = 1;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     在菜单列表页按「确定」：进子菜单 / 进参数页 / 进信息页 / 弹保存确认
//-------------------------------------------------------------------------------------------------------------------
static void ui_menu_enter (void)
{
    menu_level_struct      *lv = &menu_stack[menu_depth];
    const menu_node_struct *nd = &lv->node[lv->index];

    UI_LOG_ONE("[menu] enter item %d", (int)lv->index);

    switch (nd->action)
    {
        case MENU_ACT_SUBMENU:
            if ((uint8)(menu_depth + 1) < MENU_STACK_DEPTH)
            {
                menu_depth ++;
                menu_stack[menu_depth].node  = nd->child;
                menu_stack[menu_depth].num   = nd->child_num;
                menu_stack[menu_depth].index = 0;
                menu_stack[menu_depth].top   = 0;
                menu_title_stack[menu_depth] = nd->text;
                ui_need_draw = 1;
            }
            break;

        case MENU_ACT_PARAM_GROUP:
            ui_group     = nd->arg;
            ui_row       = 0;
            ui_top       = 0;
            ui_state     = MENU_UI_STATE_PARAM;
            ui_need_draw = 1;
            break;

        case MENU_ACT_INFO:
            ui_page      = nd->arg;
            ui_info_top  = 0;
            ui_state     = MENU_UI_STATE_INFO;
            ui_need_draw = 1;
            break;

        case MENU_ACT_OVERVIEW:
            ui_row       = 0;
            ui_top       = 0;
            ui_state     = MENU_UI_STATE_OVERVIEW;
            ui_need_draw = 1;
            break;

        case MENU_ACT_DEFAULT_ALL:
            ui_open_popup(MENU_POPUP_DEFAULT_ALL);
            break;

        default:
            break;
    }
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     返回上一层菜单 / 回到主菜单
//-------------------------------------------------------------------------------------------------------------------
static void ui_menu_back (void)
{
    if (menu_depth > 0)
    {
        menu_depth --;
        ui_need_draw = 1;
        UI_LOG("[menu] back");
    }
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     不管在哪一层 / 哪个页面，直接回主菜单（长按按键 4 用）
//-------------------------------------------------------------------------------------------------------------------
static void ui_menu_home (void)
{
    menu_depth   = 0;
    ui_state     = MENU_UI_STATE_MENU;
    ui_need_draw = 1;
    UI_LOG("[menu] home");
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     菜单列表上下移动（只有真的滚屏才整屏重画，否则只重画这两行）
//-------------------------------------------------------------------------------------------------------------------
static void ui_menu_move (menu_level_struct *lv, int8 dir)
{
    uint8 old_index = lv->index;
    uint8 old_top   = lv->top;

    if (dir < 0) { lv->index = (0 == lv->index) ? (uint8)(lv->num - 1) : (uint8)(lv->index - 1); }
    else
    {
        lv->index ++;
        if (lv->index >= lv->num) { lv->index = 0; }
    }

    ui_scroll_fix(lv->num);

    if (lv->top != old_top) { ui_need_draw = 1; }
    else
    {
        ui_mark_row(old_index);
        ui_mark_row(lv->index);
    }
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     参数页上下移动（同上：没滚屏就只重画这两行）
//-------------------------------------------------------------------------------------------------------------------
static void ui_param_move (uint8 rows, int8 dir)
{
    uint8 old_row = ui_row;
    uint8 old_top = ui_top;

    if (dir < 0) { ui_row = (0 == ui_row) ? (uint8)(rows - 1) : (uint8)(ui_row - 1); }
    else
    {
        ui_row ++;
        if (ui_row >= rows) { ui_row = 0; }
    }

    ui_scroll_fix(rows);

    if (ui_top != old_top) { ui_need_draw = 1; }
    else
    {
        ui_mark_row(old_row);
        ui_mark_row(ui_row);
    }
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     按键处理（所有状态都一次把 4 个按键读完，避免状态残留）
//-------------------------------------------------------------------------------------------------------------------
static void ui_process_key (void)
{
    // 4 个按键各管一件事：1 上选  2 下选  3 确定  4 返回（长按 4 = 回主菜单）
    uint8 ev_up   = menu_key_short(MENU_KEY_UP);                    // 按键1 P20_6
    uint8 ev_down = menu_key_short(MENU_KEY_DOWN);                  // 按键2 P20_7
    uint8 ev_ok   = menu_key_short(MENU_KEY_OK);                    // 按键3 P11_2
    uint8 ev_back = menu_key_short(MENU_KEY_BACK);                  // 按键4 P11_3

    uint8 l_up   = menu_key_long(MENU_KEY_UP);                          // 按键1 长按（修改时 = 一次加 10 档）
    uint8 l_down = menu_key_long(MENU_KEY_DOWN);                        // 按键2 长按（修改时 = 一次减 10 档）

    uint8 k_up;
    uint8 k_down;
    uint8 k_ok;
    uint8 k_back;
    uint8 k_home;

    if ((0 == menu_key_down(MENU_KEY_UP)) && (0 == menu_key_down(MENU_KEY_DOWN)))
    {
        ui_edit_coarse = 0;                                             // 两个键都松开了：退出长按大步模式
    }

    k_up   = (uint8)(ev_up   | menu_key_repeat(MENU_KEY_UP));           // 短按 + 按住连发
    k_down = (uint8)(ev_down | menu_key_repeat(MENU_KEY_DOWN));
    k_ok   = ev_ok;                                                     // 确定
    k_back = ev_back;                                                   // 返回（退一层 / 退出修改）
    k_home = menu_key_long(MENU_KEY_BACK);                              // 长按按键4 = 直接回主菜单

    if ((0 != k_up) || (0 != k_down) || (0 != k_ok) || (0 != k_back))
    {
        ui_last_key_ms = ui_ms;                                         // 有按键动作：自动保存的静默计时重新开始
    }

    if (0 != k_home)                                                    // 长按按键4：任何页面直接回主菜单
    {
        ui_save_now = 1;                                                // 先把改动落盘再走
        ui_menu_home();
        return;
    }

    switch (ui_state)
    {
        // ------------------------------------------------ 菜单列表页 ------------------------------------------------
        case MENU_UI_STATE_MENU:
        {
            menu_level_struct *lv = &menu_stack[menu_depth];

            if (0 != k_up)   { ui_menu_move(lv, -1); }                  // 只重画动过的两行，翻菜单跟手
            if (0 != k_down) { ui_menu_move(lv,  1); }
            if (0 != k_ok)   { ui_menu_enter(); }

            if (0 != k_back)                                                        // 按键4 = 返回上一层
            {
                if (0 == menu_depth)                                                // 已经在主菜单：给个提示，别让人以为键坏了
                {
                    ui_set_toast("已在主菜单", MENU_COLOR_TITLE);
                    ui_return_state = MENU_UI_STATE_MENU;
                }
                else
                {
                    ui_menu_back();
                }
            }
            break;
        }

        // ------------------------------------------------ 参数组页面 ------------------------------------------------
        case MENU_UI_STATE_PARAM:
        {
            uint8 num  = menu_param_group_item_num(ui_group);
            uint8 rows = (uint8)(num + 1);                              // 末尾只有一行：恢复本组默认

            if (0 != k_up)   { ui_param_move(rows, -1); }
            if (0 != k_down) { ui_param_move(rows,  1); }

            if (0 != k_ok)
            {
                if (ui_row < num)                                                       // 进入修改
                {
                    ui_edit_value  = menu_param_get((uint8)(menu_param_group_first_item(ui_group) + ui_row));
                    ui_edit_coarse = 0;
                    ui_state       = MENU_UI_STATE_EDIT;
                    ui_need_draw   = 1;
                    UI_LOG_ONE("[menu] edit item %d", (int)ui_row);
                }
                else                                                                    // 恢复本组默认
                {
                    ui_open_popup(MENU_POPUP_DEFAULT_GROUP);
                }
            }

            if (0 != k_back)                                            // 回上一层菜单：先把改动存好
            {
                ui_save_now  = 1;
                ui_state     = MENU_UI_STATE_MENU;
                ui_need_draw = 1;
            }
            break;
        }

        // ------------------------------------------------ 修改参数中 ------------------------------------------------
        case MENU_UI_STATE_EDIT:
        {
            uint8  item    = (uint8)(menu_param_group_first_item(ui_group) + ui_row);
            int32  step    = menu_param_item_step(item);
            uint8  coarse;
            uint8  changed = 0;

            // 长按：一次走 10 档（脉宽 10us -> 100us，角度 1.0° -> 10.0°），按住会一直连发
            if (0 != (l_up | l_down)) { ui_edit_coarse = 1; }
            coarse = (0 != ui_edit_coarse) ? (1) : (0);
            if (0 != coarse) { step = step * 10; }

            if (0 != k_up)                                                              // 按键1 = 加一档
            {
                menu_param_set(item, ui_edit_value + step);                             // 立刻写进参数表
                ui_edit_value = menu_param_get(item);                                   // 取限幅后的真值
                changed = 1;
            }
            if (0 != k_down)                                                            // 按键2 = 减一档
            {
                menu_param_set(item, ui_edit_value - step);
                ui_edit_value = menu_param_get(item);
                changed = 1;
            }

            if (0 != changed)
            {
                ui_servo_sync(item);                                                    // 脉宽 / 角度 一起变
                ui_save_block  = 0;                                                     // 又改了：允许重新尝试保存
                ui_title_dirty = 1;
                ui_mark_row(ui_row);                                                    // 只重画这一行 + 联动那一行
                ui_mark_linked_row(item);
                UI_LOG("[menu] value changed");
            }

            if ((0 != k_ok) || (0 != k_back))                                           // 3 键、4 键都是「改好了，退出」
            {
                ui_edit_coarse = 0;
                ui_state       = MENU_UI_STATE_PARAM;
                ui_need_draw   = 1;
                ui_save_now    = 1;                                                     // 退出就写 Flash，掉电不丢
                UI_LOG("[menu] edit done");
            }
            break;
        }

        // ------------------------------------------------ 弹窗确认 ------------------------------------------------
        case MENU_UI_STATE_POPUP:
        {
            if (0 != k_ok)                                                              // 确定
            {
                switch (ui_popup)
                {
                    case MENU_POPUP_DEFAULT_GROUP:
                        menu_param_group_set_default(ui_group);
                        ui_save_block = 0;
                        ui_set_toast("已恢复默认", MENU_COLOR_EDIT);
                        ui_return_state = MENU_UI_STATE_PARAM;
                        ui_save_now     = 1;
                        break;
                    case MENU_POPUP_DEFAULT_ALL:
                        menu_param_all_set_default();
                        ui_save_block = 0;
                        ui_set_toast("已恢复默认", MENU_COLOR_EDIT);
                        ui_return_state = MENU_UI_STATE_MENU;
                        ui_save_now     = 1;
                        break;
                    default:
                        break;
                }
            }
            else if (0 != k_back)                                                       // 按键4 = 取消
            {
                ui_set_toast("已取消", MENU_COLOR_PEN);
                UI_LOG("[menu] popup cancel");
            }
            break;
        }

        // ------------------------------------------------ 结果提示 ------------------------------------------------
        case MENU_UI_STATE_TOAST:
            if ((0 != k_up) || (0 != k_down) || (0 != k_ok) || (0 != k_back))
            {
                ui_state     = ui_return_state;
                ui_need_draw = 1;
            }
            break;

        // ------------------------------------------------ 信息 / 状态页 ------------------------------------------------
        case MENU_UI_STATE_INFO:
            if (MENU_PAGE_KEYTEST == ui_page)                                           // 按键自检页：短按都留给检测，长按才退出
            {
                if (0 != k_back)                                                    // 按按键4 退出自检页
                {
                    ui_state     = MENU_UI_STATE_MENU;
                    ui_need_draw = 1;
                }
                break;
            }

            if (0 != k_up)                                                          // 1 = 往上翻一行
            {
                if (0 != ui_info_top) { ui_info_top --; ui_need_draw = 1; }
            }
            if (0 != k_down)                                                        // 2 = 往下翻一行
            {
                if ((uint8)(ui_info_top + 1) < ui_info_num) { ui_info_top ++; ui_need_draw = 1; }
            }
            if ((0 != k_back) || (0 != k_ok))
            {
                ui_info_top  = 0;
                ui_state     = MENU_UI_STATE_MENU;
                ui_need_draw = 1;
            }
            break;

        // ------------------------------------------------ 参数总览页（可上下滚动） ------------------------------------------------
        case MENU_UI_STATE_OVERVIEW:
        {
            uint8 rows = ui_overview_rows();

            if (0 != k_up)
            {
                ui_row = (0 == ui_row) ? (uint8)(rows - 1) : (uint8)(ui_row - 1);
                ui_need_draw = 1;
            }
            if (0 != k_down)
            {
                ui_row ++;
                if (ui_row >= rows) { ui_row = 0; }
                ui_need_draw = 1;
            }
            if ((0 != k_back) || (0 != k_ok))
            {
                ui_state     = MENU_UI_STATE_MENU;
                ui_need_draw = 1;
            }
            break;
        }

        default:
            ui_state = MENU_UI_STATE_MENU;
            ui_need_draw = 1;
            break;
    }

#if (1 == MENU_SERIAL_DEBUG)
    // 串口也打一份，按物理按键分开打 —— 屏上动作不对时，看串口就知道是哪个键被读到了
    if (0 != ev_up)   { UI_LOG("[key] 1 P20_6 up");   }
    if (0 != ev_down) { UI_LOG("[key] 2 P20_7 down"); }
    if (0 != ev_ok)   { UI_LOG("[key] 3 P11_2 ok");   }
    if (0 != ev_back) { UI_LOG("[key] 4 P11_3 back"); }
    if (0 != k_home)  { UI_LOG("[key] long 4 = home"); }
#endif
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     菜单初始化：屏幕 -> 参数 -> Flash 载入 -> 按键 -> 画主菜单
// 备注信息     上电流程里 menu_param_init() 先填默认值，menu_flash_load() 再用 Flash 里的数据覆盖，
//              所以「断电前保存过的参数」上电就会自动出现在菜单里
//-------------------------------------------------------------------------------------------------------------------
void menu_ui_init (void)
{
    uint8 loaded;

    menu_display_init();                        // 屏幕
    menu_display_clear();

    menu_param_init();                          // 参数先全部填默认值
    loaded = menu_flash_load();                 // 从片内 Flash 读回上次保存的参数

    ui_serial_on = (0 != menu_param_get(MENU_PARAM_IDX_SERIAL)) ? (1) : (0);   // 菜单里的「串口调试」参数控制日志开关

    menu_key_init(MENU_KEY_SCAN_PERIOD_MS);     // 按键

    menu_depth = 0;
    menu_stack[0].node  = menu_main_list;
    menu_stack[0].num   = (uint8)(sizeof(menu_main_list) / sizeof(menu_main_list[0]));
    menu_stack[0].index = 0;
    menu_stack[0].top   = 0;
    menu_title_stack[0] = "主菜单";

    ui_state        = MENU_UI_STATE_MENU;
    ui_return_state = MENU_UI_STATE_MENU;
    ui_group        = 0;
    ui_row          = 0;
    ui_top          = 0;
    ui_page         = MENU_PAGE_HELP;
    ui_ms           = 0;
    ui_need_draw    = 1;
    ui_last_key_ms  = 0;
    ui_save_now     = 0;
    ui_save_block   = 0;
    ui_title_dirty  = 0;
    ui_clear_dirty_rows();

    ui_draw();                                  // 先把主菜单画出来

#if (1 == MENU_SERIAL_DEBUG)
    printf("\r\n===== TC264 menu =====\r\n");
    printf("display: %s\r\n", (0 != MENU_DISPLAY_USE_IPS200) ? ("IPS200") : ("TFT180"));
    printf("param item total: %d\r\n", (int)menu_param_item_total());
    printf("flash load: %s\r\n", (0 != loaded) ? ("from flash") : ("default value"));
    printf("KEY1 up  KEY2 down  KEY3 ok  KEY4 back\r\n");
#endif

    ui_set_toast((0 != loaded) ? ("已载入上次参数") : ("使用默认参数"), MENU_COLOR_TITLE);
    ui_return_state = MENU_UI_STATE_MENU;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     菜单主循环（不返回）
//-------------------------------------------------------------------------------------------------------------------
void menu_ui_run (void)
{
    while (TRUE)
    {
        ui_process_key();

        // 参数一改就推给舵机：下一圈就把新脉宽写进 PWM，不用等「保存」
        servo_control_set_pulse((uint16)menu_param_get(MENU_PARAM_IDX_SERVO_PULSE),
                          (uint16)menu_param_get(MENU_PARAM_IDX_SERVO_MIN),
                          (uint16)menu_param_get(MENU_PARAM_IDX_SERVO_CENTER),
                          (uint16)menu_param_get(MENU_PARAM_IDX_SERVO_MAX),
                          (uint8)menu_param_get(MENU_PARAM_IDX_SERVO_ENABLE));

        ui_auto_save();                             // 有改动就自动写 Flash（退出修改 / 停手一会儿）

        // 运行状态页每 300 ms 重画一次，脉宽 / 角度 / 占空比跟着实时变
        if ((MENU_UI_STATE_INFO == ui_state) && (MENU_PAGE_RUN == ui_page)
            && ((uint32)(ui_ms - ui_run_ms) >= 300u))
        {
            ui_run_ms    = ui_ms;
            ui_need_draw = 1;
        }

        // 按键自检页：键的状态 / 次数一变就重画（按哪个键，那一行马上变「按住」）
        if ((MENU_UI_STATE_INFO == ui_state) && (MENU_PAGE_KEYTEST == ui_page))
        {
            uint32 sig = 0;
            uint8  i;

            for (i = 0; i < MENU_KEY_NUM; i ++)
            {
                sig = (sig * 31) + (uint32)(0 != menu_key_down(i));
                sig = (sig * 31) + (uint32)(menu_key_cnt(i) % 1000);
            }
            if (sig != ui_keytest_sig)
            {
                ui_keytest_sig = sig;
                ui_need_draw   = 1;
            }
        }

        if ((MENU_UI_STATE_TOAST == ui_state) && ((uint32)(ui_ms - ui_toast_ms) >= MENU_TOAST_MS))
        {
            ui_state     = ui_return_state;                     // 提示框到点自动消失
            ui_need_draw = 1;
        }

        if (0 != ui_need_draw)                  // 换页面 / 滚动：整屏重画一次
        {
            ui_draw();
            ui_need_draw = 0;
        }
        else if (0 != ui_need_partial())        // 只是数值变了：只重画那一两行
        {
            ui_draw_partial();
        }
    }
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     10ms 定时中断里调用（给提示框计时）
//-------------------------------------------------------------------------------------------------------------------
void menu_ui_tick_10ms (void)
{
    ui_ms += 10;
}
