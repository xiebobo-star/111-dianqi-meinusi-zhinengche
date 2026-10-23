/*********************************************************************************************************************
* 文件名            menu_config.h
* 说明              TC264 多级菜单 + 参数掉电保存 —— 引脚、屏幕、按键、Flash 全部配置都在这一个文件里
*                   以后要改引脚 / 换屏幕 / 换页号，只改这一个文件，其它 .c 不用动
* 硬件              主板「屏幕1」接口（SPI2）+ 4 个按键 + TC264 片内 DFlash（不用外挂存储芯片）
* 开发环境          AURIX Development Studio 1.10.40 / 逐飞 TC264 开源库
* 日期              2026-10-16
********************************************************************************************************************/
#ifndef _menu_config_h_
#define _menu_config_h_

#include "zf_common_headfile.h"

// ============================================ 1. 屏幕 ============================================
// 主板丝印 -> TC264 引脚：SCK=P15_3  MOSI=P15_5  CS=P15_2  RST=P15_1  DC=P15_0  BLK=P15_4
// 逐飞库 zf_device_ips200.h / zf_device_tft180.h 的默认引脚正好是这一组，库文件不用改
#define MENU_DISPLAY_USE_IPS200     (1)                         // 1 = 2 寸 IPS200（SPI 驱动，工程默认）
                                                                // 0 = 1.8 寸 TFT180
                                                                // ★ 两块屏指令不通用，手上是哪块就改成哪块

// 屏幕背光：主板屏幕接口的 BLK 脚由程序控制（P15_4），ips200_init() 里已经拉高，这里不用管
// 如果用的是 TFT180，屏幕跑到一半黑掉，先怀疑供电（必须 3.3V，别接 5V）

// ============================================ 2. 按键 ============================================
// 顺序必须与库文件 zf_device_key.h 里的 KEY_LIST {P20_6, P20_7, P11_2, P11_3} 一致
#define MENU_KEY_UP                 (KEY_1)                     // 按键1 P20_6 -> 上选
#define MENU_KEY_DOWN               (KEY_2)                     // 按键2 P20_7 -> 下选
#define MENU_KEY_OK                 (KEY_3)                     // 按键3 P11_2 -> 确定（进入 / 确认 / 弹窗确认）
#define MENU_KEY_BACK               (KEY_4)                     // 按键4 P11_3 -> 返回（退出 / 取消）

#define MENU_KEY_SCAN_PERIOD_MS     (10)                        // 按键扫描周期 ms，必须与 10ms 定时中断一致
                                                                // 库里的消抖(10ms)和长按(1000ms)都按这个周期算

// ============================================ 3. 片内 Flash（参数掉电保存） ============================================
// TC264 内置 DFlash：96KB 一共 12 页，每页 4KB（= 1024 个 uint32）
// 这里用最后两页做「双页备份」：每次保存写另一页，写的时候另一页还是完整的，
// 所以保存过程中断电，最差也只是丢掉这一次修改，不会把参数全丢
#define MENU_FLASH_PAGE_A           (10)                        // 参数主份页  参数范围 0-11
#define MENU_FLASH_PAGE_B           (11)                        // 参数备份页

// ============================================ 4. 界面排版 ============================================
#define MENU_UI_ROW_H               (20)                        // 每行高度（16 像素字 + 4 像素间距）
#define MENU_UI_TITLE_Y             (2)                         // 标题 y 坐标
#define MENU_UI_ITEM_Y0             (38)                        // 第一行菜单项的 y 坐标
#define MENU_UI_BAR_H               (18)                        // 选中高亮条高度
#define MENU_UI_LINE_H              (20)                        // 信息页每行高度
#define MENU_TOAST_MS               (1500)                      // 提示框（保存成功之类）自动消失时间 ms

// 界面配色
#define MENU_COLOR_PEN              (RGB565_WHITE)              // 普通文字
#define MENU_COLOR_BG               (RGB565_BLACK)              // 背景
#define MENU_COLOR_SEL              (RGB565_BLUE)               // 选中项高亮条
#define MENU_COLOR_EDIT             (RGB565_YELLOW)             // 正在修改的项 / 弹窗边框
#define MENU_COLOR_TITLE            (RGB565_GREEN)              // 标题
#define MENU_COLOR_OK               (RGB565_GREEN)              // 提示框：成功
#define MENU_COLOR_ERR              (RGB565_RED)                // 提示框：失败

// ============================================ 5. 核心板 LED ============================================
// 核心板 4 个 LED 里 P21_5 / P21_4 被电机信号接口占了，这里只用 P20_9 / P20_8
// 用途：参数保存成功时闪一下（可以用参数「LED指示」关掉），顺便证明程序真的在跑
#define MENU_LED1_PIN               (P20_9)
#define MENU_LED2_PIN               (P20_8)
#define MENU_LED_ON_LEVEL           (0)                         // 核心板 LED 低电平点亮（高电平点亮的板子改成 1）
#define MENU_LED1_ON()              gpio_set_level(MENU_LED1_PIN, MENU_LED_ON_LEVEL)
#define MENU_LED1_OFF()             gpio_set_level(MENU_LED1_PIN, !MENU_LED_ON_LEVEL)
#define MENU_LED2_ON()              gpio_set_level(MENU_LED2_PIN, MENU_LED_ON_LEVEL)
#define MENU_LED2_OFF()             gpio_set_level(MENU_LED2_PIN, !MENU_LED_ON_LEVEL)

// ============================================ 6. 参数表下标（和 menu_param.c 里的表一一对应） ============================================
// 想加 / 删参数时，menu_param.c 的表和这里要一起改
#define MENU_PARAM_IDX_LED          (8)                         // 「LED指示」开关
#define MENU_PARAM_IDX_SERIAL       (9)                         // 「串口调试」开关
#define MENU_PARAM_IDX_BOOT_DELAY   (10)                        // 「上电延时」ms

// ============================================ 7. 串口调试输出 ============================================
// 1 = 每次按键 / 保存动作同时往调试串口（UART0，P14_0/P14_1，115200）打一行 ASCII 日志
// 屏幕还没调通的时候，靠串口也能确认菜单逻辑跑没跑
#define MENU_SERIAL_DEBUG           (1)

// ============================================ 8. 版本号（系统信息页显示） ============================================
#define MENU_VERSION_STR            "V1.0"

#endif
