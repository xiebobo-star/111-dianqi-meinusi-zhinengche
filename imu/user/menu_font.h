/*********************************************************************************************************************
* 文件名            menu_font.h
* 说明              16x16 汉字点阵表（菜单里的中文靠它显示）
*                   点阵格式与逐飞库 ips200_show_chinese / tft180_show_chinese 一致：
*                     每个汉字 32 字节 = 16 行 x 2 字节，逐行取模；每行第 1 字节是左边 8 个点、第 2 字节是右边 8 个点，高位在左
*                   字表由 tools/gen_font.py 自动生成（扫源文件里所有双引号字符串里的汉字），不用手动取模
* 用法              menu_display_cn() 内部自动查表，一般不用直接调用
********************************************************************************************************************/
#ifndef _menu_font_h_
#define _menu_font_h_

#include "zf_common_typedef.h"

typedef struct
{
    uint16 code;                    // GBK 编码，高字节在前（例如「电」= 0xB5E7）
    uint8  data[32];                // 16x16 点阵
}menu_cn16_struct;

extern const menu_cn16_struct menu_cn16_table[];
extern const uint16           menu_cn16_table_size;

const uint8 *menu_font_get (uint8 code_high, uint8 code_low);       // 查字模，没这个字返回 NULL

#endif
