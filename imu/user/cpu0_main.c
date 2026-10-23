/*********************************************************************************************************************
* 文件名            cpu0_main.c
* 说明              TC264 + IMU660RA 姿态解算 / 里程定位 —— CPU0 主程序
*
*   硬件接线（全部在 imu_config.h / menu_config.h 里改）
*     屏幕（SPI2）   SCK=P15_3  MOSI=P15_5  CS=P15_2  RST=P15_1  DC=P15_0  BLK=P15_4
*     按键           按键1=P20_6(上)  按键2=P20_7(下)  按键3=P11_2(确定)  按键4=P11_3(返回)
*     IMU（SPI0）    CLK=P20_11  MOSI=P20_14  MISO=P20_12  CS=P20_13
*     编码器1        A=P02_8  B=P00_9  (TIM4)
*     编码器2        A=P10_3  B=P10_1  (TIM5)
*     调试串口       TX=P14_0  RX=P14_1  115200
*     核心板 LED     LED1=P20_9  LED2=P20_8
*   存储            菜单参数 -> DFlash 第 10/11 页；零漂 + 航点 -> 第 8/9 页
*
*   上电顺序：先传感器（IMU / 编码器 / 读 Flash 里的零漂和航点），再菜单（屏幕 / 按键 / 参数）
*             最后开 10ms 中断（中断里跑：按键扫描 + 陀螺解算 + 里程计）
********************************************************************************************************************/
#include "zf_common_headfile.h"
#pragma section all "cpu0_dsram"
// 将本语句与#pragma section all restore语句之间的全局变量都放在CPU0的RAM中

#include "imu_config.h"
#include "imu_attitude.h"
#include "imu_encoder.h"
#include "imu_odom.h"
#include "imu_store.h"
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

    // ------------------------- 1. 核心板 LED -------------------------
    gpio_init(MENU_LED1_PIN, GPO, GPIO_HIGH, GPO_PUSH_PULL);
    gpio_init(MENU_LED2_PIN, GPO, GPIO_HIGH, GPO_PUSH_PULL);
    MENU_LED1_OFF();
    MENU_LED2_OFF();

    // ------------------------- 2. 传感器与里程计 -------------------------
    // 注意顺序：编码器 -> 陀螺仪 -> 里程计 -> 从 Flash 读回零漂和航点
    imu_encoder_init();                                         // 两个编码器（方向解码模式）
    imu_attitude_init();                                        // IMU660RA 初始化（SPI0），自检失败也会继续跑
    imu_odom_init();                                            // 里程计清零，轨迹从原点开始
    imu_store_load();                                           // 读第 8/9 页：有校准过的零漂就覆盖，航点也一起恢复

    // ------------------------- 3. 菜单（屏幕 + 参数 + 按键） -------------------------
    menu_ui_init();

    // ------------------------- 4. 上电延时（这个值本身也是菜单里能改、能保存的参数） -------------------------
    system_delay_ms((uint32)menu_param_get(MENU_PARAM_IDX_BOOT_DELAY));

    // ------------------------- 5. 10ms 定时中断 -------------------------
    // 中断里依次做：按键扫描 -> 界面计时 -> 编码器更新 -> 陀螺解算 -> 里程计
    // 周期必须和 imu_config.h 的 IMU_ATT_PERIOD_MS / IMU_ENC_PERIOD_MS、
    // menu_config.h 的 MENU_KEY_SCAN_PERIOD_MS 一致（都是 10ms）
    interrupt_global_enable(0);                                 // 打开全局中断
    pit_ms_init(CCU60_CH0, MENU_KEY_SCAN_PERIOD_MS);            // 10ms 周期中断

#if (1 == IMU_SERIAL_DEBUG)
    printf("\r\n===== TC264 IMU att/odom ready =====\r\n");
    printf("imu ok: %d   calib done: %d   flash: %d  wp: %d\r\n",
           (int)imu_attitude_ok(), (int)imu_attitude_calib_done(),
           (int)imu_store_valid(), (int)imu_odom_wp_num());
    printf("KEY1 up  KEY2 down  KEY3 ok  KEY4 back\r\n");
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
