/*********************************************************************************************************************
* 文件名            cpu0_main.c
* 说明              TC264 智能车合并工程 —— CPU0 主程序（一个工程，菜单里逐项单独测）
*
*   把原来 12 个独立测试工程的功能都收到一个菜单里：
*     摄像头循迹（camera + patrol）  姿态定位（imu）      测距模块（Ranging Module）
*     电机编码器（drive_test）       舵机标定（servo_bds300）  按键自检 / 硬件自检（keytest / drive_test）
*     参数掉电保存（menu / imu / servo_bds300 的同一套框架，5 组 21 项合并成一张表）
*
* 硬件接线（都在 menu_config.h / imu_config.h / tof_config.h / test_config.h 里改）
*   屏幕 SPI2     SCK=P15_3  MOSI=P15_5  CS=P15_2  RST=P15_1  DC=P15_0  BLK=P15_4
*   按键          按键1=P20_6(上)  按键2=P20_7(下)  按键3=P11_2(确定)  按键4=P11_3(返回)
*   摄像头        D0~D7=P00_0~P00_7  VSY=P02_0  PCLK=P02_1  TXD=P02_3  RXD=P02_2  VCC=3.3V
*   IMU SPI0      CLK=P20_11  MOSI=P20_14  MISO=P20_12  CS=P20_13
*   编码器1/2     A=P02_8 B=P00_9 (TIM4) / A=P10_3 B=P10_1 (TIM5)
*   电机          P21_2+P21_3（左）  P21_4+P21_5（右）  17 kHz
*   舵机          P33_9  50 Hz
*   TOF 测距      SCL=P33_4  SDA=P33_5  XS=P20_10  VCC=5V
*   核心板 LED    LED1=P20_9  LED2=P20_8（保存成功闪一下；P21_4/P21_5 被电机信号占了，别当灯用）
*   调试串口      TX=P14_0  RX=P14_1  115200（USB-TTL 接电脑看 printf）
*   参数存储      TC264 片内 DFlash 第 10/11 页（菜单参数），第 8/9 页（陀螺零漂 + 航点）
*
* 上电顺序（不能随便换）
*   1. 编码器 / IMU / 里程计 / 从 Flash 读回零漂和航点
*   2. 菜单：屏幕 -> 参数默认值 -> 读回 Flash 参数 -> 按键 -> 画主菜单
*   3. 舵机：按参数表里的三点标定初始化，停在中位（参数里「Pulse en」关掉就不出波）
*   4. 测距模块：放最后，屏幕先亮起来，模块复位那 ~0.5 s 不会让屏黑着
*   5. 开 10ms 中断：按键扫描 + 界面计时 + TOF 收数 + 编码器 + 陀螺解算 + 里程计
*   6. 进菜单主循环（不返回）
* 摄像头 / 电机都是「懒初始化」：进到对应页面才 init，没进就不占引脚、电机绝不上电就转
*********************************************************************************************************************/
#include "zf_common_headfile.h"
#pragma section all "cpu0_dsram"
// 将本语句与#pragma section all restore语句之间的全局变量都放在CPU0的RAM中

#include "menu_config.h"
#include "menu_display.h"
#include "menu_key.h"
#include "menu_param.h"
#include "menu_flash.h"
#include "menu_ui.h"
#include "tof_config.h"
#include "tof_ranging.h"
#include "imu_config.h"
#include "imu_attitude.h"
#include "imu_encoder.h"
#include "imu_odom.h"
#include "imu_store.h"
#include "servo_control.h"
#include "test_config.h"

// **************************** 代码区域 ****************************
int core0_main(void)
{
    clock_init();                                               // 获取时钟频率<务必保留>
    debug_init();                                               // 初始化默认调试串口

    // ------------------------- 1. 核心板 LED（保存成功 / 自检时会闪一下） -------------------------
    gpio_init(MENU_LED1_PIN, GPO, GPIO_HIGH, GPO_PUSH_PULL);
    gpio_init(MENU_LED2_PIN, GPO, GPIO_HIGH, GPO_PUSH_PULL);
    MENU_LED1_OFF();
    MENU_LED2_OFF();

    // ------------------------- 2. 传感器与里程计 -------------------------
    // 顺序：编码器 -> 陀螺仪 -> 里程计 -> 从 Flash 读回零漂和航点
    imu_encoder_init();                                         // 两个编码器（方向解码模式，TIM4 / TIM5）
    imu_attitude_init();                                        // IMU660RA（SPI0），自检失败也继续跑
    imu_odom_init();                                            // 里程计清零，轨迹从原点开始
    imu_store_load();                                           // 第 8/9 页：校准过的零漂 + 航点

    // ------------------------- 3. 菜单（屏幕 + 参数 + Flash + 按键） -------------------------
    menu_ui_init();

    // ------------------------- 4. 舵机 -------------------------
    // 用参数表里的脉宽 / 三点标定 / 使能去初始化，先停在中位（装到机构上之前一定先在这里确认中位）
    servo_control_init((uint16)menu_param_get(MENU_PARAM_IDX_SERVO_PULSE),
                       (uint16)menu_param_get(MENU_PARAM_IDX_SERVO_MIN),
                       (uint16)menu_param_get(MENU_PARAM_IDX_SERVO_CENTER),
                       (uint16)menu_param_get(MENU_PARAM_IDX_SERVO_MAX),
                       (uint8)menu_param_get(MENU_PARAM_IDX_SERVO_ENABLE));

    // ------------------------- 5. TOF 测距模块 -------------------------
    // 初始化失败（模块没插好 / 型号选错）不影响菜单，测距页会显示「未初始化」
    tof_module_init();

    // ------------------------- 6. 上电延时（这个值本身也是菜单里能改、能保存的参数） -------------------------
    system_delay_ms((uint32)menu_param_get(MENU_PARAM_IDX_BOOT_DELAY));

    // ------------------------- 7. 10ms 定时中断 -------------------------
    // 按键的消抖(10ms)和长按(800ms)都按这个周期算，周期必须和 menu_config.h 的
    // MENU_KEY_SCAN_PERIOD_MS、imu_config.h 的 IMU_ATT_PERIOD_MS / IMU_ENC_PERIOD_MS 一致
    interrupt_global_enable(0);                                 // 打开全局中断
    pit_ms_init(CCU60_CH0, MENU_KEY_SCAN_PERIOD_MS);            // 10ms 周期中断（见 user/isr.c）

#if (1 == MENU_SERIAL_DEBUG)
    printf("\r\n===== TC264 smartcar merged project ready =====\r\n");
    printf("display: %s   param item: %d\r\n",
           (0 != MENU_DISPLAY_USE_IPS200) ? ("IPS200") : ("TFT180"), (int)menu_param_item_total());
    printf("imu ok: %d  calib: %d  flash: %d  wp: %d\r\n",
           (int)imu_attitude_ok(), (int)imu_attitude_calib_done(),
           (int)imu_store_valid(), (int)imu_odom_wp_num());
    printf("tof: %s  state: %s  alarm: %ld mm\r\n",
           (0 != tof_module_ok()) ? ("ok") : ("FAIL"),
           tof_module_state_str(),
           (long)menu_param_get(MENU_PARAM_IDX_TOF_ALARM));
    printf("servo: pulse %d us  enable %d\r\n",
           (int)menu_param_get(MENU_PARAM_IDX_SERVO_PULSE),
           (int)menu_param_get(MENU_PARAM_IDX_SERVO_ENABLE));
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
