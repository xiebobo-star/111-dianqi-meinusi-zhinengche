#ifndef CAR_CONFIG_H
#define CAR_CONFIG_H

/* TC264D / C 车模 / MT9V034 / BDS300 —— 硬件接口与安全阈值总表
 *
 * 运行期能调的量（舵机三点、转向 PD、速度 PI、目标速度、编码器每圈脉冲、轮径、圆环开关…）
 * 全部搬到菜单「参数设置」里了，改完立刻生效、掉电保存，实现见 car_tune.c。
 * 本文件只留：引脚 / 频率 / 超时与安全阈值 / 图像 ROI / 运行期参数开关。 */

/* ---- 运行期参数开关 ----
 * 1 = 从菜单参数表读（目标工程）
 * 0 = 用下面 #else 分支的固定值（tests/run_host_tests.py 会带 -DCAR_TUNE_RUNTIME=0，测同一份算法） */
#ifndef CAR_TUNE_RUNTIME
#define CAR_TUNE_RUNTIME        1
#endif
#if CAR_TUNE_RUNTIME
#include "car_tune.h"
#endif

/* ---- 机型 / 屏 ---- */
#define CAR_DRIVE_ENABLE        1       /* 1 = 初始化电机/编码器/舵机；上电仍停机，按键3 才走 */
#define CAR_USE_IPS200          0       /* 0 = 1.8 寸 TFT180（128x160）；1 = 2 寸 IPS200 */

/* ---- 周期与安全阈值 ---- */
#define CAR_CONTROL_MS          10
#define CAR_FRAME_TIMEOUT_MS    150
#define CAR_START_GOOD_FRAMES   3
#define CAR_LOST_STOP_FRAMES    3
#define CAR_JUNCTION_STOP_FRAMES 3
#define CAR_STALL_MS            500
#define CAR_STALL_PWM           700
#define CAR_ENC_MAX_PER_TICK    2000

/* ---- 圆环：流程常量固定，开关/方向/限速/超时是运行期参数 ---- */
#define CAR_RING_COMPILED       1
#define CAR_RING_CLEAR_FRAMES   4
#define CAR_RING_EXIT_MIN_DEG   300.0f
#define CAR_RING_MAX_DEG        430.0f
#define CAR_RING_BRIDGE_MS      3000UL
#define CAR_RING_HINT_PIXELS    6
#define CAR_RING_HINT_FRAMES    3

/* ---- IMU：只用来累计圆环转角，不参与循迹 ---- */
#define CAR_IMU_YAW_SIGN        (-1.0f)
#define CAR_IMU_MAX_DPS         500.0f
#define CAR_IMU_TIMEOUT_MS      50
#define CAR_IMU_CAL_SAMPLES     200

/* ---- 电机（PWM+DIR 驱动板）---- */
#define CAR_MOTOR_L_PWM         ATOM0_CH0_P21_2
#define CAR_MOTOR_R_PWM         ATOM0_CH2_P21_4
#define CAR_MOTOR_L_DIR         P21_3
#define CAR_MOTOR_R_DIR         P21_5
#define CAR_MOTOR_HZ            17000

/* ---- 舵机（和电机不是同一个 ATOM 实例）---- */
#define CAR_SERVO_PWM           ATOM1_CH1_P33_9
#define CAR_SERVO_HZ            50
#define CAR_SERVO_SIGN          1       /* 图像偏差为正必须右转，反了改 -1 */
#define CAR_SERVO_SLEW_US       12      /* 每 10ms 最多变化多少 us */
#define CAR_STEER_D_ALPHA       0.25f   /* 转向微分低通 */

/* ---- 编码器引脚（脉冲+方向 / 正交AB 由菜单「编码模式」选）---- */
#define CAR_ENC_L_TIMER         TIM4_ENCODER
#define CAR_ENC_L_A             TIM4_ENCODER_CH1_P02_8
#define CAR_ENC_L_B             TIM4_ENCODER_CH2_P00_9
#define CAR_ENC_R_TIMER         TIM5_ENCODER
#define CAR_ENC_R_A             TIM5_ENCODER_CH1_P10_3
#define CAR_ENC_R_B             TIM5_ENCODER_CH2_P10_1

/* ---- 速度环平滑与加速度限制 ---- */
#define CAR_SPEED_ACCEL         0.20f
#define CAR_SPEED_DECEL         1.00f
#define CAR_SPEED_FILTER        0.35f

/* ---- 图像 ROI（避开远处背景和车头，装车后按实际视角调）---- */
#define CAR_ROI_TOP             32
#define CAR_ROI_BOTTOM          111
#define CAR_ROAD_MIN_WIDTH      12
#define CAR_ROAD_MAX_WIDTH      174
#define CAR_ROAD_JUMP           28
#define CAR_SEAM_GAP            3
#define CAR_MIN_CONTRAST        24
#define CAR_WIDTH_MEMORY_FRAMES 5

/* ============================================ 运行期参数（菜单里改，掉电保存） ============================================ */
#if CAR_TUNE_RUNTIME

#define CAR_RING_ENABLE         (car_tune_ring_enable())
#define CAR_RING_ENTRY_DIR      (car_tune_ring_dir())
#define CAR_RING_SPEED          (car_tune_ring_speed())
#define CAR_RING_TIMEOUT_MS     (car_tune_ring_timeout_ms())
#define CAR_SERVO_MID_US        (car_tune_servo_mid_us())
#define CAR_SERVO_MIN_US        (car_tune_servo_min_us())
#define CAR_SERVO_MAX_US        (car_tune_servo_max_us())
#define CAR_STEER_KP            (car_tune_steer_kp())
#define CAR_STEER_KD            (car_tune_steer_kd())
#define CAR_TURN_SPEED_RATIO    (car_tune_turn_ratio())
#define CAR_SPEED_STRAIGHT      (car_tune_speed_straight())
#define CAR_SPEED_BEND          (car_tune_speed_bend())
#define CAR_SPEED_CAUTIOUS      (car_tune_speed_cautious())
#define CAR_SPEED_KP_L          (car_tune_speed_kp())
#define CAR_SPEED_KP_R          (car_tune_speed_kp())
#define CAR_SPEED_KI_L          (car_tune_speed_ki())
#define CAR_SPEED_KI_R          (car_tune_speed_ki())
#define CAR_MOTOR_LIMIT         (car_tune_pwm_limit())
#define CAR_MOTOR_L_FORWARD     ((uint8)(1 - car_tune_motor_inv_l()))
#define CAR_MOTOR_R_FORWARD     ((uint8)(1 - car_tune_motor_inv_r()))
#define CAR_ENC_L_SIGN          (car_tune_enc_sign_l())
#define CAR_ENC_R_SIGN          (car_tune_enc_sign_r())

#else

#define CAR_RING_ENABLE         (1)
#define CAR_RING_ENTRY_DIR      (0)
#define CAR_RING_SPEED          (6.0f)
#define CAR_RING_TIMEOUT_MS     (30000UL)
#define CAR_SERVO_MID_US        (1540)
#define CAR_SERVO_MIN_US        (1340)
#define CAR_SERVO_MAX_US        (1740)
#define CAR_STEER_KP            (150.0f)
#define CAR_STEER_KD            (6.0f)
#define CAR_TURN_SPEED_RATIO    (0.15f)
#define CAR_SPEED_STRAIGHT      (18.0f)
#define CAR_SPEED_BEND          (8.0f)
#define CAR_SPEED_CAUTIOUS      (6.0f)
#define CAR_SPEED_KP_L          (25.0f)
#define CAR_SPEED_KP_R          (25.0f)
#define CAR_SPEED_KI_L          (40.0f)
#define CAR_SPEED_KI_R          (40.0f)
#define CAR_MOTOR_LIMIT         (2500)
#define CAR_MOTOR_L_FORWARD     (1)
#define CAR_MOTOR_R_FORWARD     (1)
#define CAR_ENC_L_SIGN          (1)
#define CAR_ENC_R_SIGN          (1)

#endif

#endif
