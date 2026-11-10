/*********************************************************************************************************************
* 文件名            cpu0_main.c
* 说明              TC264 循迹跑车 —— CPU0 主程序（菜单 + 白色路面循迹 + 转向 PD + 速度 PI + 圆环）
*
*   一个工程里两块活：
*     1) 菜单（屏幕 / 4 个按键 / 参数掉电保存 / 状态页 / 自检）—— 框架来自 menu 工程
*     2) 跑车（识别 car_image / 调度 car_control / 弯道 car_turn / 圆环 car_ring / PID car_pid / 硬件 car_board）
*
*   硬件（都在 menu_config.h / car_config.h 里改）
*     屏幕 SPI2     SCK=P15_3 MOSI=P15_5 CS=P15_2 RST=P15_1 DC=P15_0 BLK=P15_4   ★ 3.3V 供电
*     按键          1=P20_6 2=P20_7 3=P11_2 4=P11_3
*     摄像头        D0~D7=P00_0~P00_7  VSY=P02_0  PCLK=P02_1  TXD=P02_3  RXD=P02_2  ★ 3.3V
*     电机（左/右） P21_2+P21_3 / P21_4+P21_5   17kHz PWM+DIR
*     舵机          P33_9  50Hz  中位默认 1540us（菜单里标定）
*     编码器 PD-1503 A=P02_8 B=P00_9 (TIM4) / A=P10_3 B=P10_1 (TIM5)
*     IMU660RA      SPI0 P20_11/14/12/13（只用来累计圆环转角）
*     调试串口      TX=P14_0 RX=P14_1 115200
*     核心板 LED    LED1=P20_9 LED2=P20_8
*
*   上电顺序：参数（Flash）-> 屏幕 / 按键 -> 电机 / 舵机 / 编码器 -> 陀螺零漂 -> 10ms 中断 -> 菜单
*   ★ 上电一律停车；只有「循迹跑车」页按 3 才启动；按键 4 / 长按 4 / 任何故障都立刻停车。
*   ★ 摄像头是懒初始化：进「循迹跑车」或「摄像头调试」页才 mt9v03x_init()。
*********************************************************************************************************************/
#include "zf_common_headfile.h"
#include "menu_config.h"
#include "menu_display.h"
#include "menu_key.h"
#include "menu_param.h"
#include "menu_flash.h"
#include "menu_ui.h"
#include "car_config.h"
#include "car_board.h"
#include "menu_run.h"
#include "enc_pd1503.h"

#pragma section all "cpu0_dsram"
// 将本语句与#pragma section all restore语句之间的全局变量都放在CPU0的RAM中

// **************************** 代码区域 ****************************
int core0_main(void)
{
    clock_init();                                               // 获取时钟频率<务必保留>
    debug_init();                                               // 初始化默认调试串口

    // ------------------------- 0. 先把电机引脚压成安全态（★ 必须是第一步） -------------------------
    // P21_2~P21_5 复位后是输入态，驱动板 EN 上的上拉会让电机全速转 —— 上电车会直接飞。
    // 屏幕初始化要几百 ms，这期间车必须是停的，所以这里先拉低，再去做别的初始化。
    car_board_motor_safe();

    // ------------------------- 1. 核心板 LED（保存成功 / 自检时闪） -------------------------
    gpio_init(MENU_LED1_PIN, GPO, GPIO_HIGH, GPO_PUSH_PULL);
    gpio_init(MENU_LED2_PIN, GPO, GPIO_HIGH, GPO_PUSH_PULL);
    MENU_LED1_OFF();
    MENU_LED2_OFF();

    // ------------------------- 2. 菜单：屏幕 + 参数（先读 Flash 的参数，后面标定值才是对的）+ 按键 -------------------------
    menu_ui_init();

    // ------------------------- 3. 车：电机 / 舵机 / 编码器 + 控制器状态清零（保持停车） -------------------------
    car_board_init();

    // ------------------------- 4. 陀螺零漂校准（圆环要累计转角；失败不影响跑直道，圆环会报 IMU STOP） -------------------------
    if (0 == car_board_imu_init())
    {
        printf("imu init/calib FAIL -> ring disabled at runtime\r\n");
    }

    // ------------------------- 5. 上电延时（菜单参数，防止手还在车上车就冲出去） -------------------------
    system_delay_ms((uint32)menu_param_get(MENU_PARAM_IDX_BOOT_DELAY));

    // ------------------------- 6. 10ms 定时中断：按键扫描 + 界面计时 + 编码器/陀螺/控制/PWM -------------------------
    interrupt_global_enable(0);
    pit_ms_init(CCU60_CH0, MENU_KEY_SCAN_PERIOD_MS);

#if (1 == MENU_SERIAL_DEBUG)
    printf("\r\n===== TC264 循迹跑车 ready =====\r\n");
    printf("display: %s   param: %d 项\r\n",
           (0 != MENU_DISPLAY_USE_IPS200) ? ("IPS200") : ("TFT180"), (int)menu_param_item_total());
    printf("servo mid %d us  steer Kp %d Kd %d\r\n",
           (int)car_tune_servo_mid_us(), (int)menu_param_get(MENU_PARAM_IDX_STEER_KP),
           (int)menu_param_get(MENU_PARAM_IDX_STEER_KD));
    printf("speed straight %d mm/s  cpr %d  wheel %d.%dmm\r\n",
           (int)menu_param_get(MENU_PARAM_IDX_SPEED_STRAIGHT), (int)menu_param_get(MENU_PARAM_IDX_ENC_CPR),
           (int)(menu_param_get(MENU_PARAM_IDX_WHEEL) / 10), (int)(menu_param_get(MENU_PARAM_IDX_WHEEL) % 10));
    printf("ring %s   KEY1/2 exp  KEY3 start  KEY4 stop\r\n",
           (0 != car_tune_ring_enable()) ? ("on") : ("off"));
#endif

    cpu_wait_event_ready();                                     // 等待所有核心初始化完毕

    while (TRUE)
    {
        menu_ui_run();                                          // 菜单主循环（不返回）
    }
}
#pragma section all restore
// **************************** 代码区域 ****************************
