/*********************************************************************************************************************
* 文件名            menu_param.c
* 说明              参数表 + 参数值的读写。想加 / 删 / 改参数，只改下面两张表：
*                     menu_param_item_table[]   参数项（名字、类型、默认值、上下限、步进、单位）
*                     menu_param_group_table[]  参数组（组名 + 本组从第几项开始 + 几项）
*                   加完参数以后，Flash 保存、菜单页面、参数总览页会自动多出来，不用改别的文件
* 注意              本文件是 GBK 编码写的中文，用别的编辑器改完记得还保存成 GBK，否则 ADS 里中文会乱码
********************************************************************************************************************/
#include "menu_config.h"
#include "menu_param.h"

// ============================================ 参数项表 ============================================
// 分组顺序要和下面 menu_param_group_table[] 里的 first_item / item_num 对上
const menu_param_item_struct menu_param_item_table[] =
{
    // ------------------------------ 第 1 组：电机参数（3 项） ------------------------------
    {"测试占空比", MENU_VAL_INT, 0,  3000,     0, 10000, 1000, NULL  },   // 下标 0   电机测试页的占空比 3000 = 30%
    {"左轮取反",   MENU_VAL_TOGGLE,0,    0,     0,     1,    1, NULL  },   // 下标 1   按「加」轮子却后退 -> 改成开
    {"右轮取反",   MENU_VAL_TOGGLE,0,    0,     0,     1,    1, NULL  },   // 下标 2
    // ------------------------------ 第 2 组：舵机参数（6 项） ------------------------------
    {"脉宽us",     MENU_VAL_INT, 0,   1540,   500,  2500,   10, "us"  },   // 下标 3   舵机脉宽（唯一真值），1540 = 中位
    {"角度",       MENU_VAL_FIXED, 1,  900,     0,  1800,   10, "deg" },   // 下标 4   等效角度（内部 0.1°），改它脉宽跟着变
    {"Pulse min",  MENU_VAL_INT, 0,   1040,  1000,  1200,   10, "us"  },   // 下标 5   0° 标定点
    {"Pulse cen",  MENU_VAL_INT, 0,   1540,  1300,  1700,   10, "us"  },   // 下标 6   90° 标定点（舵机装不正就挪这一项）
    {"Pulse max",  MENU_VAL_INT, 0,   2040,  1800,  2200,   10, "us"  },   // 下标 7   180° 标定点
    {"Pulse en",   MENU_VAL_TOGGLE,0,    1,     0,     1,    1, NULL  },   // 下标 8   舵机输出使能，关 = 不出波
    // ------------------------------ 第 3 组：摄像头参数（3 项） ------------------------------
    {"曝光",       MENU_VAL_INT, 0,    512,    20,  1500,   20, NULL  },   // 下标 9   图太暗就加，太白就减
    {"二值阈值",   MENU_VAL_INT, 0,      0,     0,   255,    5, NULL  },   // 下标 10  0 = 自动大津法，非 0 = 手动阈值
    {"循迹显示",   MENU_VAL_TOGGLE,0,    1,     0,     1,    1, NULL  },   // 下标 11  0 = 灰度图  1 = 二值图
    // ------------------------------ 第 4 组：测距参数（5 项） ------------------------------
    {"报警距离", MENU_VAL_INT,   0,   300,    50,  1500,   10, "mm"  },   // 下标 12  小于这个距离就报警
    {"零点偏移", MENU_VAL_INT,   0,     0,  -500,   500,    1, "mm"  },   // 下标 13  模块凹进车壳多少就填多少
    {"滤波等级", MENU_VAL_INT,   0,     1,     0,     2,    1, NULL  },   // 下标 14  0 原始 / 1 中值 / 2 中值+平滑
    {"单位cm",   MENU_VAL_TOGGLE,0,     0,     0,     1,    1, NULL  },   // 下标 15  开 = 屏上按 cm 显示
    {"报警开关", MENU_VAL_TOGGLE,0,     1,     0,     1,    1, NULL  },   // 下标 16  关 = 只测距不报警
    // ------------------------------ 第 5 组：系统参数（4 项） ------------------------------
    {"LED指示",  MENU_VAL_TOGGLE,0,     1,     0,     1,    1, NULL  },   // 下标 17  开/关（保存成功时闪一下）
    {"串口调试", MENU_VAL_TOGGLE,0,     1,     0,     1,    1, NULL  },   // 下标 18  开/关
    {"上电延时", MENU_VAL_INT,   0,   500,     0,  3000,  100, "ms"  },   // 下标 19
    {"轮径",     MENU_VAL_FIXED, 1,   610,   400,   800,    5, "mm"  },   // 下标 20  61.0（里程计按它换算）
};

// ============================================ 参数组表 ============================================
const menu_param_group_struct menu_param_group_table[MENU_PARAM_GROUP_NUM] =
{
    {"电机参数",   0, 3},       // 组 0：参数表下标 0~2
    {"舵机参数",   3, 6},       // 组 1：参数表下标 3~8
    {"摄像头参数", 9, 3},       // 组 2：参数表下标 9~11
    {"测距参数",  12, 5},       // 组 3：参数表下标 12~16
    {"系统参数",  17, 4},       // 组 4：参数表下标 17~20
};

// ============================================ 运行时的参数值 ============================================
int32 menu_param_value[MENU_PARAM_ITEM_MAX];

static uint8 menu_param_dirty[MENU_PARAM_GROUP_NUM];             // 1 = 这一组改过但没写进 Flash
static char  menu_param_str_buffer[20];                          // 值字符串的静态缓冲（取完立刻显示，别存指针）

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     参数项总数
//-------------------------------------------------------------------------------------------------------------------
uint8 menu_param_item_total (void)
{
    return (uint8)(sizeof(menu_param_item_table) / sizeof(menu_param_item_table[0]));
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     某一组有几项 / 某一组第一项的下标
//-------------------------------------------------------------------------------------------------------------------
uint8 menu_param_group_item_num (uint8 group)
{
    zf_assert(MENU_PARAM_GROUP_NUM > group);
    return menu_param_group_table[group].item_num;
}

uint8 menu_param_group_first_item (uint8 group)
{
    zf_assert(MENU_PARAM_GROUP_NUM > group);
    return menu_param_group_table[group].first_item;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     某一项属于哪一组
//-------------------------------------------------------------------------------------------------------------------
uint8 menu_param_item_group (uint8 item_index)
{
    uint8 group;

    for (group = 0; group < MENU_PARAM_GROUP_NUM; group ++)
    {
        uint8 first = menu_param_group_table[group].first_item;
        uint8 num   = menu_param_group_table[group].item_num;

        if ((item_index >= first) && (item_index < (uint8)(first + num)))
        {
            return group;
        }
    }
    return 0;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     取名字 / 单位
//-------------------------------------------------------------------------------------------------------------------
const char *menu_param_group_name (uint8 group)
{
    zf_assert(MENU_PARAM_GROUP_NUM > group);
    return menu_param_group_table[group].name;
}

const char *menu_param_item_name (uint8 item_index)
{
    zf_assert(menu_param_item_total() > item_index);
    return menu_param_item_table[item_index].name;
}

const char *menu_param_item_unit (uint8 item_index)
{
    zf_assert(menu_param_item_total() > item_index);
    return menu_param_item_table[item_index].unit;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     读当前值
//-------------------------------------------------------------------------------------------------------------------
int32 menu_param_get (uint8 item_index)
{
    zf_assert(menu_param_item_total() > item_index);
    return menu_param_value[item_index];
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     把「值」格式化成字符串
// 参数说明     item_index      参数项下标
// 参数说明     value           要显示的值（定点数要传放大后的整数）
// 返回参数     字符串指针（内部静态缓冲，下一次调用会被覆盖）
// 使用示例     menu_display_cn(x, y, menu_param_value_str_ex(0, 1500));     // 显示 15.00
// 备注信息     开关类型显示「开 / 关」，定点小数按 point 补零
//-------------------------------------------------------------------------------------------------------------------
const char *menu_param_value_str_ex (uint8 item_index, int32 value)
{
    const menu_param_item_struct *item;

    zf_assert(menu_param_item_total() > item_index);
    item = &menu_param_item_table[item_index];

    if (MENU_VAL_TOGGLE == item->type)
    {
        return (0 == value) ? "关" : "开";
    }

    if ((MENU_VAL_FIXED == item->type) && (item->point > 0))
    {
        int32 point_base = (1 == item->point) ? (10) : (100);
        int32 int_part   = value / point_base;
        int32 fix_part   = value % point_base;                   // 负数取模也是负数，下面取绝对值

        if (fix_part < 0) { fix_part = -fix_part; }

        if (1 == item->point)
        {
            snprintf(menu_param_str_buffer, sizeof(menu_param_str_buffer), "%ld.%01ld", (long)int_part, (long)fix_part);
        }
        else
        {
            snprintf(menu_param_str_buffer, sizeof(menu_param_str_buffer), "%ld.%02ld", (long)int_part, (long)fix_part);
        }
    }
    else
    {
        snprintf(menu_param_str_buffer, sizeof(menu_param_str_buffer), "%ld", (long)value);
    }

    return menu_param_str_buffer;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     把当前值格式化成字符串
//-------------------------------------------------------------------------------------------------------------------
const char *menu_param_value_str (uint8 item_index)
{
    return menu_param_value_str_ex(item_index, menu_param_get(item_index));
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     把值限幅到这一项的 [最小值, 最大值]
// 备注信息     修改界面里用它：按键加减的时候先限幅，按「确定」才真正写进参数表
//-------------------------------------------------------------------------------------------------------------------
int32 menu_param_limit (uint8 item_index, int32 value)
{
    const menu_param_item_struct *item;

    zf_assert(menu_param_item_total() > item_index);
    item = &menu_param_item_table[item_index];

    if (value < item->min_value) { value = item->min_value; }
    if (value > item->max_value) { value = item->max_value; }
    return value;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     取这一项的步进值（按一下按键变化多少）
//-------------------------------------------------------------------------------------------------------------------
int32 menu_param_item_step (uint8 item_index)
{
    zf_assert(menu_param_item_total() > item_index);
    return menu_param_item_table[item_index].step;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     参数初始化：全部填默认值，清掉「未保存」标志
// 备注信息     上电时先调它，再调 menu_flash_load()；Flash 里有有效数据就覆盖成 Flash 里的值
//-------------------------------------------------------------------------------------------------------------------
void menu_param_init (void)
{
    uint8 i;

    for (i = 0; i < menu_param_item_total(); i ++)
    {
        menu_param_value[i] = menu_param_item_table[i].def_value;
    }
    menu_param_clear_all_dirty();
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     写入参数（自动限幅；值有变化就把所在组标记成「未保存」）
//-------------------------------------------------------------------------------------------------------------------
void menu_param_set (uint8 item_index, int32 value)
{
    const menu_param_item_struct *item;

    zf_assert(menu_param_item_total() > item_index);
    item = &menu_param_item_table[item_index];

    if (value < item->min_value) { value = item->min_value; }
    if (value > item->max_value) { value = item->max_value; }

    if (menu_param_value[item_index] != value)
    {
        menu_param_value[item_index] = value;
        menu_param_dirty[menu_param_item_group(item_index)] = 1;
    }
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     按一档加减（按键用）
// 参数说明     dir     大于 0 加一档，小于 0 减一档
//-------------------------------------------------------------------------------------------------------------------
void menu_param_step (uint8 item_index, int8 dir)
{
    zf_assert(menu_param_item_total() > item_index);

    menu_param_set(item_index, menu_param_value[item_index] + ((dir > 0) ? (menu_param_item_table[item_index].step) : (-menu_param_item_table[item_index].step)));
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     某一组恢复默认值（会标记「未保存」，得再按保存才写进 Flash）
//-------------------------------------------------------------------------------------------------------------------
void menu_param_group_set_default (uint8 group)
{
    uint8 first = menu_param_group_first_item(group);
    uint8 num   = menu_param_group_item_num(group);
    uint8 i;

    for (i = 0; i < num; i ++)
    {
        menu_param_value[first + i] = menu_param_item_table[first + i].def_value;
    }
    menu_param_dirty[group] = 1;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     全部恢复默认值
//-------------------------------------------------------------------------------------------------------------------
void menu_param_all_set_default (void)
{
    uint8 group;

    for (group = 0; group < MENU_PARAM_GROUP_NUM; group ++)
    {
        menu_param_group_set_default(group);
    }
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     「未保存」标志的读写
//-------------------------------------------------------------------------------------------------------------------
uint8 menu_param_group_dirty (uint8 group)
{
    zf_assert(MENU_PARAM_GROUP_NUM > group);
    return menu_param_dirty[group];
}

void menu_param_group_dirty_set (uint8 group, uint8 dirty)
{
    zf_assert(MENU_PARAM_GROUP_NUM > group);
    menu_param_dirty[group] = (0 != dirty) ? (1) : (0);
}

void menu_param_clear_all_dirty (void)
{
    uint8 group;

    for (group = 0; group < MENU_PARAM_GROUP_NUM; group ++)
    {
        menu_param_dirty[group] = 0;
    }
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     有几组还没保存（存储状态页显示）
//-------------------------------------------------------------------------------------------------------------------
uint8 menu_param_dirty_group_num (void)
{
    uint8 group;
    uint8 num = 0;

    for (group = 0; group < MENU_PARAM_GROUP_NUM; group ++)
    {
        if (0 != menu_param_dirty[group]) { num ++; }
    }
    return num;
}
