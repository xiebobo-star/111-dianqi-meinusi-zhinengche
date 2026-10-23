/*********************************************************************************************************************
* 文件名            dt_cn_font.h
* 说明              16x16 汉字点阵表（自测菜单用到的汉字），点阵由 Python + PIL 生成
*                   格式与逐飞库 ips200_show_chinese / tft180_show_chinese 一致：
*                     逐行取模，一个汉字 16 行 x 2 字节 = 32 字节，高位在左
********************************************************************************************************************/
#ifndef _dt_cn_font_h_
#define _dt_cn_font_h_

#include "zf_common_typedef.h"

typedef struct
{
    uint16 code;                    // GBK 编码，高字节在前（例如「电」= 0xB5E7）
    uint8  data[32];                // 16x16 点阵
}dt_cn16_struct;

extern const dt_cn16_struct dt_cn16_table[];
extern const uint16         dt_cn16_table_size;

const uint8 *dt_cn_font_get (uint8 code_high, uint8 code_low);

#endif
