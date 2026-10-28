/*********************************************************************************************************************
* 文件名            menu_key.h
* 说明              4 个按键的封装（主板 4 个按键），引脚在 menu_config.h 里配置
*
*                    这一版不再用库里的 zf_device_key，原因（正是「确定」按不出来 的两个坑）：
*                      1. 库里 key_press_time 记的是「扫描次数」，却直接跟 10ms(KEY_MAX_SHOCK_PERIOD) 比
*                         —— 扫描周期 10ms 时必须按住 100ms 以上才算一次短按，快点一下就当抖动丢了
*                      2. 库里的短按标志只在「松手那一次扫描」里存在，下一次扫描就被清成 RELEASE；
*                         主循环正好在刷屏（整屏几十 ms）时就彻底错过 —— 「确定」这种一按就松的动作最容易丢
*
*                    这里改成自己扫描：电平连续 2 次(20ms)一致就认，**松手一定产生一个短按事件**，
*                    事件一直锁存到被读走为止（主循环再慢也不会丢），另外带长按和按住连发
*
* 用法              menu_key_init(MENU_KEY_SCAN_PERIOD_MS);   // 上电初始化一次
*                   menu_key_scan() 已经在 10ms 定时中断里被调用，主循环不用管
*                   if (menu_key_short(MENU_KEY_OK))   { ... } // 短按按键3 = 确定
*                   if (menu_key_short(MENU_KEY_BACK)) { ... } // 短按按键4 = 返回
*                   if (menu_key_long (MENU_KEY_BACK)) { ... } // 长按按键4 = 回主菜单
*                   if (menu_key_repeat(MENU_KEY_UP))  { ... } // 按住连发（上下选可以快速翻）
********************************************************************************************************************/
#ifndef _menu_key_h_
#define _menu_key_h_

#include "zf_common_headfile.h"

#define MENU_KEY_NUM            (4)             // 按键个数（顺序 = 板上按键1~4 的顺序）

#define MENU_KEY_UP             (0)             // 按键1（P20_6）：上选 / 修改时加
#define MENU_KEY_DOWN           (1)             // 按键2（P20_7）：下选 / 修改时减
#define MENU_KEY_OK             (2)             // 按键3（P11_2）：确定
#define MENU_KEY_BACK           (3)             // 按键4（P11_3）：返回（长按 = 回主菜单）

void            menu_key_init        (uint32 period_ms);    // 上电初始化，参数 = 扫描周期 ms
void            menu_key_scan        (void);                // 扫描一次（放 10ms 定时中断）

uint8           menu_key_short       (uint8 key);           // 短按（按一下放开），读到一次自动清掉
uint8           menu_key_long        (uint8 key);           // 长按（按住超过 MENU_KEY_LONG_MS），读到一次自动清掉
uint8           menu_key_repeat      (uint8 key);           // 按住连发（上下选加速用）
uint8           menu_key_down        (uint8 key);           // 现在是否按住（消抖后的稳定状态）
uint8           menu_key_raw         (uint8 key);           // 最近一次原始电平 1=按下（按键自检页用）
uint32          menu_key_cnt         (uint8 key);           // 这个键累计按了多少次（自检页用）
uint32          menu_key_press_cnt   (void);                // 4 个键一共按了多少次
const char     *menu_key_pin_name    (uint8 key);           // 这个键接在哪个脚，如 "P20_6"
void            menu_key_clear_all   (void);                // 清掉所有还没读走的事件

#endif
