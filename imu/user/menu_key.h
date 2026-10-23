/*********************************************************************************************************************
* 文件名            menu_key.h
* 说明              4 个按键的封装（主板 4 个按键 P20_6 / P20_7 / P11_2 / P11_3）
*                   底层直接用库里的 zf_device_key，消抖（10ms）和长按（1000ms）都是库自带的，不要自己再写 delay 消抖
* 用法              menu_key_init(MENU_KEY_SCAN_PERIOD_MS);     // 上电初始化，扫描周期 10ms
*                   menu_key_scan() 已经在 10ms 定时中断里被调用，主循环里不用管
*                   if (menu_key_short(MENU_KEY_OK)) { ... }     // 短按：读到一次自动清状态
*                   if (menu_key_long (MENU_KEY_OK)) { ... }     // 长按 1 秒以上
********************************************************************************************************************/
#ifndef _menu_key_h_
#define _menu_key_h_

#include "zf_common_headfile.h"

void            menu_key_init       (uint32 period_ms);             // 上电初始化，参数 = 扫描周期 ms
void            menu_key_scan       (void);                         // 扫描一下按键（放定时中断里）
key_state_enum  menu_key_state      (uint8 key_index);              // 读状态（释放/短按/长按），不清状态
uint8           menu_key_short      (uint8 key_index);              // 是否短按（读到一次自动清状态）
uint8           menu_key_long       (uint8 key_index);              // 是否长按（读到一次自动清状态）
void            menu_key_clear_all  (void);
uint32          menu_key_press_cnt  (void);                         // 累计按键次数（运行状态页显示用）

#endif
