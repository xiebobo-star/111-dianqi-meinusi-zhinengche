/*********************************************************************************************************************
* 文件名            cpu0_main.c
* 说明              TC264 硬件驱动自测 —— 主核入口
*                   上电流程：时钟 -> 调试串口 -> 屏幕 -> LED -> 按键(5ms 扫描) -> 进自测菜单
* 注意              自测菜单是死循环，进去就出不来了，复位才回菜单
********************************************************************************************************************/
#include "zf_common_headfile.h"
#include "dt_config.h"
#include "dt_display.h"
#include "dt_test.h"

#pragma section all "cpu0_dsram"
// 将本语句与 #pragma section all restore 语句之间的全局变量都放在 CPU0 的 RAM 中

// **************************** 代码区域 ****************************
int core0_main(void)
{
    clock_init();                                   // 获取时钟频率 <务必保留>
    debug_init();                                   // 初始化默认调试串口 115200

    dt_display_init();                              // 屏幕（IPS200 或 TFT180，在 dt_config.h 里选）

    gpio_init(DT_LED1_PIN, GPO, GPIO_HIGH, GPO_PUSH_PULL);   // 核心板 LED1 上电先灭
    gpio_init(DT_LED2_PIN, GPO, GPIO_HIGH, GPO_PUSH_PULL);   // 核心板 LED2 上电先灭

    key_init(DT_KEY_PERIOD_MS);                     // 按键初始化，扫描周期要和下面的定时中断一致
    pit_ms_init(CCU60_CH0, DT_KEY_PERIOD_MS);       // 5ms 周期中断：按键扫描 + 毫秒计数（见 isr.c）

    printf("\r\n\r\n===== TC264 硬件驱动自测 =====\r\n");
    printf("屏幕程序配置 : %s\r\n", (DT_DISPLAY_USE_IPS200) ? "IPS200 2.0寸" : "TFT180 1.8寸");
    printf("按键约定     : 1上  2下  3确定  4返回，测试页里长按4退出\r\n");
    printf("调试串口     : 115200 8N1  TX=P14_0  RX=P14_1\r\n");
    printf("电机模式     : %s\r\n", (DT_MOTOR_MODE == DT_MOTOR_MODE_DUAL_PWM) ? "双PWM" : "PWM+方向");
    printf("自测菜单已就绪，屏幕不亮就看这行串口日志\r\n");

    cpu_wait_event_ready();                         // 等待所有核心初始化完毕

    dt_test_run();                                  // 进自测菜单（不会返回）

    while(TRUE)
    {
    }
}
#pragma section all restore
// **************************** 代码区域 ****************************
