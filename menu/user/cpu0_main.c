/*********************************************************************************************************************
* 文件名            cpu0_main.c
* 说明              TC264 多级菜单 + 参数掉电保存 —— CPU0 主程序
* 硬件接线（主板「屏幕1」+ 4 个按键 + 片内 Flash，引脚在 menu_config.h 里改）
*   屏幕 SPI2     SCK=P15_3  MOSI=P15_5  CS=P15_2  RST=P15_1  DC=P15_0  BLK=P15_4
*   按键          按键1=P20_6(上)  按键2=P20_7(下)  按键3=P11_2(确定)  按键4=P11_3(返回)
*   核心板 LED    LED1=P20_9  LED2=P20_8（保存成功闪一下；P21_4/P21_5 被电机信号占了，别用）
*   调试串口      TX=P14_0  RX=P14_1  115200（USB-TTL 接电脑看 printf）
*   参数存储      TC264 片内 DFlash 第 10 / 11 页，双页备份，不需要外挂存储芯片
* 本文件基于逐飞 TC264 开源库的空工程模板修改，库文件一行没动
********************************************************************************************************************/
#include "zf_common_headfile.h"
#pragma section all "cpu0_dsram"
// 将本语句与#pragma section all restore语句之间的全局变量都放在CPU0的RAM中

#include "menu_config.h"
#include "menu_display.h"
#include "menu_key.h"
#include "menu_param.h"
#include "menu_flash.h"
#include "menu_ui.h"

// **************************** 代码区域 ****************************
int core0_main(void)
{
    clock_init();                                               // 获取时钟频率<务必保留>
    debug_init();                                               // 初始化默认调试串口

    // ------------------------- 1. 核心板 LED（保存成功时会闪一下） -------------------------
    gpio_init(MENU_LED1_PIN, GPO, GPIO_HIGH, GPO_PUSH_PULL);
    gpio_init(MENU_LED2_PIN, GPO, GPIO_HIGH, GPO_PUSH_PULL);
    MENU_LED1_OFF();
    MENU_LED2_OFF();

    // ------------------------- 2. 菜单初始化 -------------------------
    // 里面依次做：屏幕初始化 -> 参数填默认值 -> 从片内 Flash 读回上次保存的参数
    //             -> 按键初始化 -> 画出主菜单（并提示「已载入上次参数 / 使用默认参数」）
    menu_ui_init();

    // ------------------------- 3. 上电延时（这个值本身也是菜单里能改、能保存的参数） -------------------------
    system_delay_ms((uint32)menu_param_get(MENU_PARAM_IDX_BOOT_DELAY));

    // ------------------------- 4. 10ms 定时中断：按键扫描 + 菜单计时 -------------------------
    // 按键的消抖(10ms)和长按(1000ms)都是按这个周期算的，周期必须和 menu_config.h 里的
    // MENU_KEY_SCAN_PERIOD_MS 一致（见 user/isr.c 的 cc60_pit_ch0_isr）
    interrupt_global_enable(0);                                 // 打开全局中断
    pit_ms_init(CCU60_CH0, MENU_KEY_SCAN_PERIOD_MS);            // 10ms 周期中断

#if (1 == MENU_SERIAL_DEBUG)
    printf("\r\n===== TC264 menu ready =====\r\n");
    printf("boot delay param: %ld ms\r\n", (long)menu_param_get(MENU_PARAM_IDX_BOOT_DELAY));
    printf("led param: %ld   serial param: %ld\r\n",
           (long)menu_param_get(MENU_PARAM_IDX_LED), (long)menu_param_get(MENU_PARAM_IDX_SERIAL));
#endif

    cpu_wait_event_ready();                                     // 等待所有核心初始化完毕

    while (TRUE)
    {
        // 菜单主循环：读按键 -> 更新界面 -> 画屏，不会返回
        menu_ui_run();
    }
}
#pragma section all restore
// **************************** 代码区域 ****************************
