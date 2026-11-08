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
    // ------------------------------ 第 1 组：舵机参数（6 项） ------------------------------
    {"中位us",   MENU_VAL_INT,   0,  1540,  1000,  2000,   10, "us"  },   // 下标 0  车身对不正就挪这一项
    {"左限us",   MENU_VAL_INT,   0,  1340,   900,  1800,   10, "us"  },   // 下标 1  打满左的脉宽
    {"右限us",   MENU_VAL_INT,   0,  1740,  1200,  2100,   10, "us"  },   // 下标 2  打满右的脉宽
    {"转向Kp",   MENU_VAL_INT,   0,   150,     0,   800,    5, NULL  },   // 下标 3  单位 us / 归一化误差
    {"转向Kd",   MENU_VAL_INT,   0,     6,     0,   200,    1, NULL  },   // 下标 4  抑制摆头
    {"差速%",    MENU_VAL_FIXED, 2,    15,     0,    60,    1, "%"   },   // 下标 5  0.15 = 弯道外轮快 15%
    // ------------------------------ 第 2 组：速度 / 编码器参数（8 项） ------------------------------
    {"直道速",   MENU_VAL_INT,   0,   300,     0,  2500,   20, "mm/s"},   // 下标 6
    {"弯道速",   MENU_VAL_INT,   0,   150,     0,  2000,   10, "mm/s"},   // 下标 7
    {"谨慎速",   MENU_VAL_INT,   0,   110,     0,  1500,   10, "mm/s"},   // 下标 8  置信度低时用它
    {"速度Kp",   MENU_VAL_INT,   0,    25,     0,   300,    1, NULL  },   // 下标 9  PWM / (脉冲每 10ms)
    {"速度Ki",   MENU_VAL_INT,   0,    40,     0,   500,    5, NULL  },   // 下标 10
    {"PWM上限",  MENU_VAL_INT,   0,  2500,     0, 10000,  100, NULL  },   // 下标 11 10000 = 100%
    {"每圈脉冲", MENU_VAL_INT,   0,  1024,   100, 20000,   10, NULL  },   // 下标 12 PD1503：手转一圈数脉冲
    {"轮径",     MENU_VAL_FIXED, 1,   610,   200,  1500,    5, "mm"  },   // 下标 13 61.0mm
    // ------------------------------ 第 3 组：摄像头参数（3 项） ------------------------------
    {"曝光",     MENU_VAL_INT,   0,   512,    20,  1500,   20, NULL  },   // 下标 14
    {"二值阈",   MENU_VAL_INT,   0,     0,     0,   255,    5, NULL  },   // 下标 15 0 = 自动大津法
    {"显示",     MENU_VAL_TOGGLE,0,     0,     0,     1,    1, NULL  },   // 下标 16 0 灰度 1 二值
    // ------------------------------ 第 4 组：圆环参数（4 项） ------------------------------
    {"圆环",     MENU_VAL_TOGGLE,0,     1,     0,     1,    1, NULL  },   // 下标 17 关掉就只当普通弯道跑
    {"入环向",   MENU_VAL_INT,   0,     0,    -1,     1,    1, NULL  },   // 下标 18 0 自动 / -1 左 / +1 右
    {"环速度",   MENU_VAL_INT,   0,   110,     0,  1500,   10, "mm/s"},   // 下标 19 环内限速
    {"环超时",   MENU_VAL_INT,   0,    30,     5,    60,    5, "s"   },   // 下标 20
    // ------------------------------ 第 5 组：系统参数（8 项） ------------------------------
    {"LED",      MENU_VAL_TOGGLE,0,     1,     0,     1,    1, NULL  },   // 下标 21 保存成功闪一下
    {"串口",     MENU_VAL_TOGGLE,0,     1,     0,     1,    1, NULL  },   // 下标 22
    {"上电延时", MENU_VAL_INT,   0,   500,     0,  3000,  100, "ms"  },   // 下标 23
    {"电机L反",  MENU_VAL_TOGGLE,0,     0,     0,     1,    1, NULL  },   // 下标 24 左轮后退却前进 -> 开
    {"电机R反",  MENU_VAL_TOGGLE,0,     0,     0,     1,    1, NULL  },   // 下标 25
    {"编码L反",  MENU_VAL_TOGGLE,0,     0,     0,     1,    1, NULL  },   // 下标 26 前进时计数为负 -> 开
    {"编码R反",  MENU_VAL_TOGGLE,0,     0,     0,     1,    1, NULL  },   // 下标 27
    {"编码模式", MENU_VAL_TOGGLE,0,     0,     0,     1,    1, NULL  },   // 下标 28 0 脉冲+方向 1 正交AB
};

// ============================================ 参数组表 ============================================
const menu_param_group_struct menu_param_group_table[MENU_PARAM_GROUP_NUM] =
{
    {"舵机参数",   0, 6},       // 组 0：下标 0~5
    {"速度参数",   6, 8},       // 组 1：下标 6~13
    {"摄像头参数",14, 3},       // 组 2：下标 14~16
    {"圆环参数",  17, 4},       // 组 3：下标 17~20
    {"系统参数",  21, 8},       // 组 4：下标 21~28
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
