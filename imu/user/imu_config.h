/*********************************************************************************************************************
* 文件名            imu_config.h
* 说明              陀螺仪 / 编码器 / 里程计 / 姿态存储 的配置总表 —— 跟 IMU 有关的东西只改这一个文件
*                   屏幕上那些菜单参数（轮径、Kp、LED 之类）还是 menu_config.h 管，本文件把它 include 进来
* 硬件              主板 IMU1（SPI0）+ 两个编码器接口 + TC264 片内 DFlash
* 开发环境          AURIX Development Studio 1.10.40 / 逐飞 TC264 开源库
* 日期              2026-10-17
********************************************************************************************************************/
#ifndef _imu_config_h_
#define _imu_config_h_

#include "zf_common_headfile.h"
#include "menu_config.h"

// ============================================ 1. 陀螺仪（主板 IMU1 接口） ============================================
// 主板丝印 IMU1（8P）：SCL/SPC=P20_11  SDA/MOSI=P20_14  SDO/MISO=P20_12  CS=P20_13
// 逐飞库 zf_device_imu660ra.h 里的默认引脚正好就是这一组，库文件一行都不用改
// ★ 换成 ICM20602 的话：把 IMU_DEV_USE_IMU660RA 改成 0，再把 imu_attitude.c 顶部那 4 处
//   imu660ra_xxx 换成 icm20602_xxx（库里两个模块的接口名几乎一样），其它代码不用动
#define IMU_DEV_USE_IMU660RA        (1)                         // 1 = IMU660RA（SPI0）  0 = 别的模块（自己改驱动）

#define IMU_ATT_PERIOD_MS           (10)                        // 解算周期 ms，必须和 10ms 定时中断一致
#define IMU_ATT_PERIOD_S            (0.01f)                     // 同上，单位秒（积分用）

#define IMU_ATT_COMP_K              (0.980f)                    // 互补滤波系数：陀螺权重
                                                                // 越大越信陀螺（平滑但会慢慢漂），越小越信加速度计（跟得紧但抖）
                                                                // 0.95~0.99 都正常，车上有震动就往 0.99 调
#define IMU_ATT_ACC_TRUST_LOW       (0.70f)                     // 加速度向量模长在 [0.70g, 1.30g] 之外就不信它（车在急加速/急刹）
#define IMU_ATT_ACC_TRUST_HIGH      (1.30f)

#define IMU_ATT_STILL_GYRO_DPS      (1.5f)                      // 三轴角速度都小于这个值 + 轮子没转 = 车静止
#define IMU_ATT_STILL_COUNT         (30)                        // 连续 30 个周期（0.3 秒）才认静止，防止误判
#define IMU_ATT_ZUPT_ENABLE         (1)                         // 1 = 静止时锁住 yaw 并慢慢修零漂（强烈建议开）
                                                                // 关掉以后 yaw 会一直跟着陀螺漂，停一会儿就偏
#define IMU_ATT_ZUPT_RATE           (0.002f)                    // 静止时零漂修正速度：每次修 0.2% 的残差

#define IMU_ATT_CALIB_SAMPLE        (500)                       // 零漂校准采样点数
#define IMU_ATT_CALIB_DELAY_MS      (2)                         // 每次采样间隔 ms（500 x 2ms = 1 秒）
#define IMU_ATT_CALIB_BIAS_MAX      (20.0f)                     // 算出来的零漂超过 ±20dps 就当校准失败
#define IMU_ATT_CALIB_DEV_MAX       (3.0f)                      // 采样过程中偏离均值超过 3dps = 车在动，判失败

// 安装方向（模块装反 / 转了方向不用拆线，改这里就行）
// 默认：模块平放、芯片朝上、X 轴朝车头、Y 轴朝车左、Z 轴朝上 → yaw 逆时针（左转）为正
#define IMU_ATT_GYRO_SIGN_X         (1)                         // 装反了改成 -1
#define IMU_ATT_GYRO_SIGN_Y         (1)
#define IMU_ATT_GYRO_SIGN_Z         (1)                         // ★ 模块倒着贴（芯片朝下）时这里改 -1
#define IMU_ATT_ACC_SIGN_X          (1)
#define IMU_ATT_ACC_SIGN_Y          (1)
#define IMU_ATT_ACC_SIGN_Z          (1)

// ============================================ 2. 编码器（主板两个编码器接口） ============================================
// 编码器1：A(脉冲)=P02_8  B(方向)=P00_9  -> TIM4
// 编码器2：A(脉冲)=P10_3  B(方向)=P10_1  -> TIM5
#define IMU_ENC_LEFT_TIM            (TIM4_ENCODER)
#define IMU_ENC_LEFT_A              (TIM4_ENCODER_CH1_P02_8)
#define IMU_ENC_LEFT_B              (TIM4_ENCODER_CH2_P00_9)
#define IMU_ENC_RIGHT_TIM           (TIM5_ENCODER)
#define IMU_ENC_RIGHT_A             (TIM5_ENCODER_CH1_P10_3)
#define IMU_ENC_RIGHT_B             (TIM5_ENCODER_CH2_P10_1)

#define IMU_ENC_PERIOD_MS           (10)                        // 测速周期 ms（与 10ms 定时中断一致）
#define IMU_ENC_LEFT_INVERT         (0)                         // 左轮往前推、读出来是负数 -> 改成 1
#define IMU_ENC_RIGHT_INVERT        (0)
#define IMU_ENC_PULSE_PER_ROUND     (1024.0f)                   // 车轮转一圈多少个脉冲（按自己编码器改）
#define IMU_ENC_WHEEL_MM            (61.0f)                     // 车轮直径 mm（拿尺子量，填错里程就不准）

// ============================================ 3. 里程计 / 相对位置 ============================================
// 走 1mm 需要多少个脉冲：一圈脉冲数 / 一圈周长
#define IMU_ODO_MM_PER_PULSE        (3.1415926f * IMU_ENC_WHEEL_MM / IMU_ENC_PULSE_PER_ROUND)
#define IMU_ODO_TRAIL_MAX           (256)                       // 轨迹最多留多少个点（环形缓冲，满了丢最老的）
#define IMU_ODO_TRAIL_STEP_MM       (50.0f)                     // 每走 50mm 记一个轨迹点
#define IMU_ODO_TRAIL_TURN_DEG      (10.0f)                     // 或者车头方向变了 10 度也记一个点（转弯时轨迹才不会糊成一条直线）
#define IMU_ODO_WP_MAX              (16)                        // 最多能记多少个航点（位置点）
#define IMU_ODO_COORD_LIMIT_MM      (32000)                     // 坐标限幅（int16 存 mm，极限 ±32m）

// ============================================ 4. 片内 Flash（零漂 + 航点） ============================================
// TC264 内置 DFlash 96KB 分 12 页，每页 4KB
// 菜单参数用第 10 / 11 页（menu_config.h 里的 MENU_FLASH_PAGE_A/B），本工程用第 8 / 9 页存「零漂 + 航点」
// 同样是双页备份：每次写另一页，写一半断电还有一页是好的
#define IMU_FLASH_PAGE_A            (8)
#define IMU_FLASH_PAGE_B            (9)

// ============================================ 5. 调试串口 ============================================
// 1 = 校准 / 记录航点 / 定位变化的时候同时往调试串口（UART0 P14_0/P14_1 115200）打一行
#define IMU_SERIAL_DEBUG            (1)

#endif
