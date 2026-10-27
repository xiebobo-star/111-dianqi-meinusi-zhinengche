/*********************************************************************************************************************
* 文件名            menu_flash.h
* 说明              参数掉电保存 —— 用 TC264 片内 DFlash，不需要外挂 EEPROM / Flash 芯片
*                   一页 4KB = 1024 个 uint32，参数只占前面 20 个，所以直接用「页」当存储单位
*                   ★ 双页备份：A 页 / B 页轮流写。写之前先擦，擦坏 / 写一半断电，另一页还是完整的老参数
*                   ★ 写之前必须整页擦除（Flash 只能 1->0），所以界面上一律做成「按确定 + 弹窗确认」才保存
*                   ★ 擦写寿命约 10 万次，只在按保存时写一次，绝不能在循环 / 中断里写
* 用法              menu_param_init();               // 先填默认值
*                   menu_flash_load();               // 上电读一次：Flash 里有有效数据就覆盖上面的默认值
*                   menu_flash_save();               // 用户按保存（0 = 成功）
********************************************************************************************************************/
#ifndef _menu_flash_h_
#define _menu_flash_h_

#include "zf_common_headfile.h"

uint8   menu_flash_load        (void);      // 上电载入参数：1 = 成功用 Flash 里的参数，0 = 用默认值
uint8   menu_flash_save        (void);      // 保存全部参数：0 = 成功，1 = 失败（回读校验没过）
uint8   menu_flash_page_valid  (uint8 index);   // index：0 = A 页，1 = B 页；1 = 这一页的数据有效
uint32  menu_flash_page_seq    (uint8 index);   // 这一页保存的序号（越大越新）
uint8   menu_flash_source      (void);      // 0 = 当前参数来自默认值，1 = 来自 Flash
uint8   menu_flash_last_result (void);      // 本次上电最后一次保存结果：0 = 还没保存过，1 = 成功，2 = 失败
uint8   menu_flash_save_count  (void);      // 本次上电保存成功的次数

#endif
