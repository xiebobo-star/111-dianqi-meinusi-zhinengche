/*********************************************************************************************************************
* 文件名            menu_param.h
* 说明              菜单参数表（3 组：Servo / 传感器参数 / 系统参数，共 13 个数据项）
*                   每一项的「名字、默认值、上下限、步进、小数位、单位」都在 menu_param.c 的表里，
*                   想加参数就往那张表里加一行，菜单、Flash 保存、参数总览会自动跟着变
* 用法              menu_param_init();                       // 上电先填一遍默认值（之后 menu_flash_load() 会覆盖）
*                   menu_param_step(idx, +1);                // 加一档（自动限幅 + 标记「未保存」）
*                   const char *s = menu_param_value_str(idx);// 取值字符串（内部静态缓冲，取完立刻显示）
********************************************************************************************************************/
#ifndef _menu_param_h_
#define _menu_param_h_

#include "zf_common_headfile.h"

#define MENU_PARAM_GROUP_NUM        (3)                         // 参数组数（Flash 至少能存 3 组）
#define MENU_PARAM_ITEM_MAX         (16)                        // 参数项总数的上限（数组按它开大小）

typedef enum
{
    MENU_VAL_INT    = 0,                                        // 整数
    MENU_VAL_FIXED  = 1,                                        // 定点小数：内部存的是放大 10^point 倍后的整数
    MENU_VAL_TOGGLE = 2,                                        // 开关：0 = 关，1 = 开
}menu_val_type_enum;

typedef struct
{
    const char  *name;                                          // 参数名（GBK 编码，菜单里显示，建议不超过 4 个汉字）
    uint8        type;                                          // menu_val_type_enum
    uint8        point;                                         // 小数位数（type = MENU_VAL_FIXED 时有效，1 或 2）
    int32        def_value;                                     // 默认值（定点数要写放大后的值，例如 1.50 -> 150）
    int32        min_value;                                     // 最小值
    int32        max_value;                                     // 最大值
    int32        step;                                          // 按一下按键变化多少
    const char  *unit;                                          // 单位字符串，可填 NULL
}menu_param_item_struct;

typedef struct
{
    const char  *name;                                          // 组名（GBK 编码）
    uint8        first_item;                                    // 本组第一项在参数表里的下标
    uint8        item_num;                                      // 本组有几项
}menu_param_group_struct;

extern const menu_param_item_struct  menu_param_item_table[];
extern const menu_param_group_struct menu_param_group_table[];
extern int32                         menu_param_value[MENU_PARAM_ITEM_MAX];     // 当前值（运行时的值都在这里）

uint8        menu_param_item_total          (void);                             // 参数项总数
uint8        menu_param_group_item_num      (uint8 group);                      // 某组有几项
uint8        menu_param_group_first_item    (uint8 group);                      // 某组第一项的下标
uint8        menu_param_item_group          (uint8 item_index);                 // 某一项属于哪一组
const char  *menu_param_group_name          (uint8 group);
const char  *menu_param_item_name           (uint8 item_index);
const char  *menu_param_item_unit           (uint8 item_index);
int32        menu_param_get                 (uint8 item_index);
const char  *menu_param_value_str           (uint8 item_index);                 // 当前值的字符串
const char  *menu_param_value_str_ex        (uint8 item_index, int32 value);    // 指定值的字符串（编辑中用）

void         menu_param_init                (void);                             // 全部填默认值 + 清「未保存」标志
int32        menu_param_limit               (uint8 item_index, int32 value);    // 把值限幅到 [min, max]（只算不写，修改界面用）
int32        menu_param_item_step           (uint8 item_index);                 // 这一项的步进值
void         menu_param_set                 (uint8 item_index, int32 value);    // 限幅后写入（值有变化就置「未保存」）
void         menu_param_set_silent          (uint8 item_index, int32 value);    // 限幅后写入但不置「未保存」（上电对齐联动值用）
void         menu_param_step                (uint8 item_index, int8 dir);       // dir > 0 加一档，dir < 0 减一档
void         menu_param_group_set_default   (uint8 group);                      // 某一组恢复默认值
void         menu_param_all_set_default     (void);                             // 全部恢复默认值

uint8        menu_param_group_dirty         (uint8 group);                      // 1 = 这一组改过但还没保存
void         menu_param_group_dirty_set     (uint8 group, uint8 dirty);
void         menu_param_clear_all_dirty     (void);                             // 保存成功后调用
uint8        menu_param_dirty_group_num     (void);                             // 有几组没保存

#endif
