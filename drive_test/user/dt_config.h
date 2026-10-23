/*********************************************************************************************************************
* 文件名            dt_config.h
* 说明              硬件驱动自测工程 —— 引脚与开关总表（整个工程只需要改这一个文件）
* 板子              TC264 主控板 V1.2（接口与逐飞通用主板一致）
* 开发环境          AURIX Development Studio 1.10.40 + 逐飞 TC264 开源库
*--------------------------------------------------------------------------------------------------------------------
* 改这里的顺序建议：
*   1. 我手上是哪块屏？      改 DT_DISPLAY_USE_IPS200
*   2. 电机驱动板哪种接法？  改 DT_MOTOR_MODE
*   3. 陀螺仪 / 测距模块是哪个型号？改 DT_IMU_TYPE / DT_TOF_TYPE
*   （其它引脚与你板子丝印一致的话都不用动）
********************************************************************************************************************/
#ifndef _dt_config_h_
#define _dt_config_h_

#include "zf_common_headfile.h"

// =================================================== 1 屏幕 ===========================================================
// 主板「屏幕1」接口（SPI2）：SCK=P15_3  MOSI=P15_5  CS=P15_2  RST=P15_1  DC=P15_0  BLK=P15_4
// 逐飞库 zf_device_ips200.h / zf_device_tft180.h 的默认引脚就是这一组，所以库不用改
#define DT_DISPLAY_USE_IPS200       (1)                         // 1 = 2 寸 IPS200（240x320）  <-- 你手上就是这块，保持 1
                                                                // 0 = 1.8 寸 TFT180（128x160）
                                                                // 注意：两种屏指令不同，买的是哪个就填哪个，填错了屏不亮

// =================================================== 2 调试串口 =======================================================
// 1 = 每个测试页同时把结果打到调试串口（115200，UART0：TX=P14_0 RX=P14_1）
//     屏幕还没点亮、按键也没接的时候，靠串口照样能测完所有模块
// 0 = 只走屏幕
#define DT_SERIAL_DEBUG             (1)

// =================================================== 3 按键 ===========================================================
// 主板 4 个按键：按键1=P20_6  按键2=P20_7  按键3=P11_2  按键4=P11_3
// 按键脚写在库里 libraries/zf_device/zf_device_key.h 的 KEY_LIST 上，以后要换脚改那里
// 菜单里的约定：按键1=上  按键2=下  按键3=确定  按键4=返回（测试页里长按按键4 退出）
#define DT_KEY_PERIOD_MS            (5)                         // 按键扫描周期 ms，同时决定 5ms 定时中断

// 下面这 4 个只用于「按键」测试页显示原始电平（1 = 松开，0 = 按下），必须和库里的 KEY_LIST 一模一样
// 按下去屏幕上电平不变 -> 说明不是程序的问题，是这根线/这个焊点/这个脚的问题
#define DT_KEY1_PIN                 (P20_6)
#define DT_KEY2_PIN                 (P20_7)
#define DT_KEY3_PIN                 (P11_2)
#define DT_KEY4_PIN                 (P11_3)
                                                                // 库里的消抖常量是按“毫秒”写死的，周期越小按键越灵敏
// =================================================== 4 LED ============================================================
// 核心板 LED：LED1=P20_9  LED2=P20_8
// 主板「电机信号1」把 P21_5 / P21_4 当电机脚用了，所以那两个不能再当 LED
#define DT_LED1_PIN                 (P20_9)
#define DT_LED2_PIN                 (P20_8)
#define DT_LED_ON_LEVEL             (0)                         // 核心板 LED 低电平点亮；如果你的板子是高电平亮就改成 1

// =================================================== 5 电机 ===========================================================
// 主板「电机信号1」：左 = P21_2 + P21_3    右 = P21_4 + P21_5
// 两种驱动板接法，看自己手上驱动板的说明，二选一：
//   DT_MOTOR_MODE_PWM_DIR   一个脚出 PWM，另一个脚出高/低电平当方向（DRV8701、BTS7960 单 PWM 类）
//   DT_MOTOR_MODE_DUAL_PWM  两个脚都出 PWM，一个正转一个反转（TB6612 双 PWM 类、自己搭的 H 桥）
// 你手上是「逐飞 DRV8701E 双电机驱动板」：芯片是 PH/EN 接口 —— PWM 进 EN 调速、DIR 进 PH 定方向，
// 每路电机只吃 2 根信号，所以这里保持 PWM_DIR 不要改。
// 注意：那块板上有一个横向拨动式「使能开关」，没拨到使能位置两个电机都不会动（板上使能指示灯不亮）。
#define DT_MOTOR_MODE_PWM_DIR       (0)
#define DT_MOTOR_MODE_DUAL_PWM      (1)
#define DT_MOTOR_MODE               DT_MOTOR_MODE_PWM_DIR

// DRV8701E 的 PH(方向)脚：高电平 = 正转。占空比为正时这里就输出这个电平
// 如果按「1 加」轮子却往后转，说明电机两根线接反了：把这个宏改成 0，或者把电机线对调
#define DT_MOTOR_DIR_HIGH_FORWARD   (1)

#define DT_MOTOR_PWM_FREQ           (17000)                     // 17kHz，超出人耳，电机不啸叫
#define DT_MOTOR_L_PWM              (ATOM0_CH0_P21_2)           // 左电机 PWM
#define DT_MOTOR_L_PWM2             (ATOM0_CH1_P21_3)           // 左电机 第二路 PWM（双 PWM 模式用）
#define DT_MOTOR_L_DIR              (P21_3)                     // 左电机 方向脚（PWM+DIR 模式用）
#define DT_MOTOR_R_PWM              (ATOM0_CH2_P21_4)           // 右电机 PWM
#define DT_MOTOR_R_PWM2             (ATOM0_CH3_P21_5)           // 右电机 第二路 PWM（双 PWM 模式用）
#define DT_MOTOR_R_DIR              (P21_5)                     // 右电机 方向脚（PWM+DIR 模式用）

#define DT_MOTOR_STEP               (1000)                      // 测试页按一下增加 / 减少的占空比
#define DT_MOTOR_DUTY_LIMIT         (10000)                     // 占空比上限（10000 = 100%）
#define DT_MOTOR_INVERT_L           (0)                         // 左轮方向反了（想让它转却往后退）改成 1
#define DT_MOTOR_INVERT_R           (0)

// =================================================== 6 舵机 ==========================================================
// 主板「舵机」接口：P33_9
// 双 PWM 电机模式占用了 ATOM0_CH1（P21_3），所以舵机得换到 ATOM1_CH1，还是同一个脚
#if (DT_MOTOR_MODE == DT_MOTOR_MODE_DUAL_PWM)
#define DT_SERVO_PWM_PIN            (ATOM1_CH1_P33_9)
#else
#define DT_SERVO_PWM_PIN            (ATOM0_CH1_P33_9)
#endif
#define DT_SERVO_FREQ               (50)                        // 舵机固定 50Hz，20ms 一个周期
#define DT_SERVO_MIN_PULSE          (500)                       // 脉宽下限 us
#define DT_SERVO_MAX_PULSE          (2500)                      // 脉宽上限 us
#define DT_SERVO_MID_PULSE          (1500)                      // 回正脉宽 us
#define DT_SERVO_STEP               (50)                        // 测试页每按一下调整的脉宽 us

// =================================================== 7 编码器 ========================================================
// 编码器1：A=P02_8  B=P00_9  -> TIM4
// 编码器2：A=P10_3  B=P10_1  -> TIM5
#define DT_ENC_L_TIM                (TIM4_ENCODER)
#define DT_ENC_L_A                  (TIM4_ENCODER_CH1_P02_8)
#define DT_ENC_L_B                  (TIM4_ENCODER_CH2_P00_9)
#define DT_ENC_R_TIM                (TIM5_ENCODER)
#define DT_ENC_R_A                  (TIM5_ENCODER_CH1_P10_3)
#define DT_ENC_R_B                  (TIM5_ENCODER_CH2_P10_1)

#define DT_ENC_PERIOD_MS            (20)                        // 测速周期 ms
#define DT_ENC_INVERT_L             (0)                         // 正着转速度是负的 -> 改成 1
#define DT_ENC_INVERT_R             (0)
#define DT_ENC_PULSE_PER_ROUND      (1024.0f)                   // 车轮转一圈多少个脉冲（按自己编码器改）
#define DT_WHEEL_DIAMETER_MM        (61.0f)                     // 车轮直径 mm

// =================================================== 8 拨码开关 ======================================================
// 主板 SWITCH1 / SWITCH2
#define DT_SWITCH1_PIN              (P33_11)
#define DT_SWITCH2_PIN              (P33_12)

// =================================================== 9 陀螺仪（IMU） =================================================
// 主板「IMU1」接口（SPI0）：SCK=P20_11  MOSI=P20_14  MISO=P20_12  CS=P20_13
#define DT_IMU_NONE                 (0)                         // 没买 / 不测
#define DT_IMU_TYPE_660RA           (1)                         // IMU660RA / IMU660RB（逐飞常用）
#define DT_IMU_TYPE_20602           (2)                         // ICM20602
#define DT_IMU_TYPE                 DT_IMU_TYPE_660RA

// =================================================== 10 摄像头 =======================================================
// 主板「摄像头1」：D0~D7=P00_0~P00_7  VSY=P02_0  PCLK=P02_1  RX=P02_2  TX=P02_3
// 1 = 编译进工程（测的时候才初始化，不测就不占引脚）；0 = 整个摄像头测试项不编译
#define DT_CAMERA_ENABLE            (1)

// =================================================== 11 测距（TOF） ==================================================
// 主板「TOF1」：SCL=P33_4  SDA=P33_5  XS=P20_10
#define DT_TOF_NONE                 (0)
#define DT_TOF_TYPE_DL1A            (1)                         // 逐飞 DL1A（VL53L0X）
#define DT_TOF_TYPE_DL1B            (2)                         // 逐飞 DL1B
#define DT_TOF_TYPE                 DT_TOF_TYPE_DL1A

// =================================================== 12 片内 Flash ===================================================
// 用来验证 Flash 读写驱动（掉电保存参数的底层）。写在 DFLASH，不影响程序本身
#define DT_FLASH_SECTION            (0)
#define DT_FLASH_PAGE               (8)

#endif
