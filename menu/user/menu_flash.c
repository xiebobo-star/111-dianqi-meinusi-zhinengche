/*********************************************************************************************************************
* 文件名            menu_flash.c
* 说明              片内 Flash 读写实现。一张参数「记录」= 8 个字的头 + N 个字的数据：
*                     [0] 魔数        'ZFPM'，用来判断这一页有没有写过我们的数据
*                     [1] 版本号      V1.0
*                     [2] 保存序号    每保存一次 +1，双页里取序号大的那页当最新
*                     [3] 参数项数    对不上说明参数表改过了，这一页作废（回默认值）
*                     [4] 参数组数
*                     [5] 保留
*                     [6] 校验和      前面所有字求和，防掉电写坏
*                     [7] 保留
*                     [8] 起          参数值（int32），顺序和 menu_param_value[] 一致
* 双页策略           都有效 -> 写序号小的那页（旧页）；只有一页有效 -> 写另一页；都无效 -> 写 A 页
*                   这样任何时刻至少有一页是「上次保存成功的完整数据」
********************************************************************************************************************/
#include "menu_config.h"
#include "menu_param.h"
#include "menu_flash.h"

#define MENU_FLASH_MAGIC        (0x5A46504DU)                      // 'ZFPM'
#define MENU_FLASH_VERSION      (0x00010000U)                      // V1.0
#define MENU_FLASH_HEAD_LEN     (8)                                // 头部 8 个 uint32
#define MENU_FLASH_CHK_INDEX    (6)                                // 校验和放在第 6 个字

static uint32 menu_flash_word[MENU_FLASH_HEAD_LEN + MENU_PARAM_ITEM_MAX];   // 组好的数据（也用来做回读比对）
static uint8  menu_flash_valid[2];                                 // A 页 / B 页是否有效
static uint32 menu_flash_seq[2];                                   // A 页 / B 页的保存序号
static uint8  menu_flash_src;                                      // 参数来源：0 默认值 1 Flash
static uint8  menu_flash_result;                                   // 0 没保存过 1 成功 2 失败
static uint8  menu_flash_count;                                    // 保存成功次数

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     校验一页数据（把这一页读进缓冲区，检查魔数 / 版本 / 参数项数 / 校验和）
// 参数说明     page        页号
// 参数说明     seq         输出：这一页的保存序号（校验通过才有意义）
// 返回参数     1 = 有效，0 = 无效（空页、写坏了、参数表改了都算无效）
// 备注信息     读完整页（1024 个字）只是内存搬运，很快；有效数据只在最后 8 个字里没有 0xFF
//-------------------------------------------------------------------------------------------------------------------
static uint8 menu_flash_check_page (uint8 page, uint32 *seq)
{
    uint32 total = (uint32)MENU_FLASH_HEAD_LEN + menu_param_item_total();
    uint32 sum = 0;
    uint32 i;

    flash_read_page_to_buffer(0, page);

    if (MENU_FLASH_MAGIC   != flash_union_buffer[0].uint32_type) { return 0; }
    if (MENU_FLASH_VERSION != flash_union_buffer[1].uint32_type) { return 0; }
    if ((uint32)menu_param_item_total() != flash_union_buffer[3].uint32_type) { return 0; }
    if ((uint32)MENU_PARAM_GROUP_NUM    != flash_union_buffer[4].uint32_type) { return 0; }

    for (i = 0; i < total; i ++)
    {
        if (MENU_FLASH_CHK_INDEX != i)
        {
            sum += flash_union_buffer[i].uint32_type;
        }
    }
    if (sum != flash_union_buffer[MENU_FLASH_CHK_INDEX].uint32_type) { return 0; }

    *seq = flash_union_buffer[2].uint32_type;
    return 1;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     上电载入参数
// 返回参数     1 = 从 Flash 载入成功；0 = Flash 里没有有效数据，用默认值
// 使用示例     menu_param_init();  menu_flash_load();
// 备注信息     载入时会走 menu_param_set()，坏数据会被自动限幅到合法范围，参数表改了也不会越界
//-------------------------------------------------------------------------------------------------------------------
uint8 menu_flash_load (void)
{
    uint32 seq_a = 0;
    uint32 seq_b = 0;
    uint8  page;
    uint8  i;

    menu_flash_valid[0] = menu_flash_check_page(MENU_FLASH_PAGE_A, &seq_a);
    menu_flash_valid[1] = menu_flash_check_page(MENU_FLASH_PAGE_B, &seq_b);
    menu_flash_seq[0]   = seq_a;
    menu_flash_seq[1]   = seq_b;

    if ((0 != menu_flash_valid[0]) && (0 != menu_flash_valid[1]))
    {
        page = (seq_a >= seq_b) ? (MENU_FLASH_PAGE_A) : (MENU_FLASH_PAGE_B);    // 取保存序号大的（更新的）那页
    }
    else if (0 != menu_flash_valid[0])
    {
        page = MENU_FLASH_PAGE_A;
    }
    else if (0 != menu_flash_valid[1])
    {
        page = MENU_FLASH_PAGE_B;
    }
    else
    {
        menu_flash_src    = 0;
        menu_flash_result = 0;
        return 0;                                                               // 两页都无效 -> 默认值
    }

    flash_read_page_to_buffer(0, page);                                         // 把这一页读回缓冲区
    for (i = 0; i < menu_param_item_total(); i ++)
    {
        menu_param_set(i, flash_union_buffer[MENU_FLASH_HEAD_LEN + i].int32_type);
    }

    menu_param_clear_all_dirty();                                               // 刚从 Flash 读出来 = 已保存
    menu_flash_src    = 1;
    menu_flash_result = 1;
    return 1;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     保存全部参数到 Flash（双页备份 + 写后回读校验）
// 返回参数     0 = 保存成功，1 = 保存失败
// 使用示例     if (0 == menu_flash_save()) { 显示「保存成功」; }
// 备注信息     擦除 + 写一整页在 ms 级，只在用户按保存时执行一次，界面在这期间不响应按键（本来就是阻塞调用）
//-------------------------------------------------------------------------------------------------------------------
uint8 menu_flash_save (void)
{
    uint32 seq_a = 0;
    uint32 seq_b = 0;
    uint32 new_seq;
    uint32 total = (uint32)MENU_FLASH_HEAD_LEN + menu_param_item_total();
    uint32 sum = 0;
    uint8  page;
    uint8  i;
    uint8  ok = 1;

    menu_flash_valid[0] = menu_flash_check_page(MENU_FLASH_PAGE_A, &seq_a);
    menu_flash_valid[1] = menu_flash_check_page(MENU_FLASH_PAGE_B, &seq_b);
    menu_flash_seq[0]   = seq_a;
    menu_flash_seq[1]   = seq_b;

    // ------------------------- 1. 决定往哪一页写（永远写「另一页」，正在用的那页不动） -------------------------
    if ((0 != menu_flash_valid[0]) && (0 != menu_flash_valid[1]))
    {
        if (seq_a <= seq_b) { page = MENU_FLASH_PAGE_A; new_seq = seq_b + 1; }
        else                { page = MENU_FLASH_PAGE_B; new_seq = seq_a + 1; }
    }
    else if (0 != menu_flash_valid[0])
    {
        page = MENU_FLASH_PAGE_B; new_seq = seq_a + 1;
    }
    else if (0 != menu_flash_valid[1])
    {
        page = MENU_FLASH_PAGE_A; new_seq = seq_b + 1;
    }
    else
    {
        page = MENU_FLASH_PAGE_A; new_seq = 1;
    }

    // ------------------------- 2. 把参数按格式组进缓冲区 -------------------------
    flash_buffer_clear();                                                       // 整页先填 0xFF（空 Flash 的状态）
    flash_union_buffer[0].uint32_type = MENU_FLASH_MAGIC;
    flash_union_buffer[1].uint32_type = MENU_FLASH_VERSION;
    flash_union_buffer[2].uint32_type = new_seq;
    flash_union_buffer[3].uint32_type = (uint32)menu_param_item_total();
    flash_union_buffer[4].uint32_type = (uint32)MENU_PARAM_GROUP_NUM;
    flash_union_buffer[5].uint32_type = 0;
    flash_union_buffer[6].uint32_type = 0;                                      // 校验和先占位
    flash_union_buffer[7].uint32_type = 0;

    for (i = 0; i < menu_param_item_total(); i ++)
    {
        flash_union_buffer[MENU_FLASH_HEAD_LEN + i].int32_type = menu_param_value[i];
    }

    for (i = 0; i < (uint8)total; i ++)
    {
        if (MENU_FLASH_CHK_INDEX != i)
        {
            sum += flash_union_buffer[i].uint32_type;
        }
    }
    flash_union_buffer[MENU_FLASH_CHK_INDEX].uint32_type = sum;

    // 留一份副本：回读时缓冲区会被整页覆盖，得拿它比对
    for (i = 0; i < (uint8)total; i ++)
    {
        menu_flash_word[i] = flash_union_buffer[i].uint32_type;
    }

    // ------------------------- 3. 擦页 -> 写页 -------------------------
    flash_erase_page(0, page);                                                  // Flash 只能 1 变 0，写之前必须先擦
    flash_write_page(0, page, (const uint32 *)flash_union_buffer, (uint16)total);

    // ------------------------- 4. 写后回读校验（判断保存成没成功） -------------------------
    flash_read_page_to_buffer(0, page);
    for (i = 0; i < (uint8)total; i ++)
    {
        if (menu_flash_word[i] != flash_union_buffer[i].uint32_type)
        {
            ok = 0;                                                             // 有一个字对不上就是没写成功
            break;
        }
    }

    if (0 != ok)
    {
        menu_flash_result = 1;
        menu_flash_src    = 1;
        menu_flash_count ++;
        if (MENU_FLASH_PAGE_A == page) { menu_flash_valid[0] = 1; menu_flash_seq[0] = new_seq; }
        else                           { menu_flash_valid[1] = 1; menu_flash_seq[1] = new_seq; }
        menu_param_clear_all_dirty();                                           // 参数已经落到 Flash，清掉「未保存」
        return 0;
    }

    menu_flash_result = 2;
    return 1;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     状态查询（存储状态页用）
//-------------------------------------------------------------------------------------------------------------------
uint8 menu_flash_page_valid (uint8 index)
{
    return (index < 2) ? (menu_flash_valid[index]) : (0);
}

uint32 menu_flash_page_seq (uint8 index)
{
    return (index < 2) ? (menu_flash_seq[index]) : (0);
}

uint8 menu_flash_source (void)
{
    return menu_flash_src;
}

uint8 menu_flash_last_result (void)
{
    return menu_flash_result;
}

uint8 menu_flash_save_count (void)
{
    return menu_flash_count;
}
