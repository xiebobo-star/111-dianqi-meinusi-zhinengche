/*********************************************************************************************************************
* 文件名            car_tune.c
* 说明              运行期参数桥的实现（取值 + 限幅 + 单位换算）
* 备注信息          这里每个函数都是「每一圈控制都会调」的，所以只做一次整数读取 + 乘除，
*                   不做滤波、不加延时。参数极端时（还没初始化、菜单被改坏）都会退到安全默认值。
*********************************************************************************************************************/
#include "menu_config.h"
#include "menu_param.h"
#include "car_tune.h"

#pragma section all "cpu0_dsram"

#define TUNE_PI              (3.1415926f)
#define TUNE_CPR_DEFAULT     (1024.0f)                  // PD-1503 常见 1024 线
#define TUNE_WHEEL_DEFAULT   (61.0f)                    // 61.0mm

static float car_tune_clampf (float x, float lo, float hi)
{
    return (x < lo) ? (lo) : ((x > hi) ? (hi) : (x));
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     车轮直径 mm（菜单内部 0.1mm）
//-------------------------------------------------------------------------------------------------------------------
float car_tune_wheel_mm (void)
{
    float w = (float)menu_param_get(MENU_PARAM_IDX_WHEEL) * 0.1f;

    if (w < 10.0f) { return TUNE_WHEEL_DEFAULT; }
    return w;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     车轮转一圈的脉冲数（把车架起来手转一圈，看「编码总脉冲」加了多少，填进菜单）
//-------------------------------------------------------------------------------------------------------------------
float car_tune_cpr (void)
{
    float c = (float)menu_param_get(MENU_PARAM_IDX_ENC_CPR);

    if (c < 1.0f) { return TUNE_CPR_DEFAULT; }
    return c;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     mm/s -> 脉冲/10ms      脉冲数 = 每 10ms 走的圈数 x 每圈脉冲
//-------------------------------------------------------------------------------------------------------------------
float car_tune_mm_s_to_counts (float mm_s)
{
    return mm_s * car_tune_cpr() / (100.0f * TUNE_PI * car_tune_wheel_mm());
}

float car_tune_counts_to_mm_s (float counts)
{
    return counts * 100.0f * TUNE_PI * car_tune_wheel_mm() / car_tune_cpr();
}

// 脉冲 -> mm：里程是「累计脉冲」直接线性换算，没有「每 10ms」这回事，所以这里不能乘 100
float car_tune_counts_to_mm (float counts)
{
    return counts * TUNE_PI * car_tune_wheel_mm() / car_tune_cpr();
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     舵机三点
//-------------------------------------------------------------------------------------------------------------------
uint16 car_tune_servo_mid_us (void)
{
    return (uint16)menu_param_get(MENU_PARAM_IDX_SERVO_MID);
}

uint16 car_tune_servo_min_us (void)
{
    return (uint16)menu_param_get(MENU_PARAM_IDX_SERVO_LEFT);
}

uint16 car_tune_servo_max_us (void)
{
    return (uint16)menu_param_get(MENU_PARAM_IDX_SERVO_RIGHT);
}

float car_tune_steer_kp (void)
{
    return (float)menu_param_get(MENU_PARAM_IDX_STEER_KP);
}

float car_tune_steer_kd (void)
{
    return (float)menu_param_get(MENU_PARAM_IDX_STEER_KD);
}

float car_tune_turn_ratio (void)
{
    return car_tune_clampf((float)menu_param_get(MENU_PARAM_IDX_TURN_RATIO) / 100.0f, 0.0f, 0.6f);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     速度（菜单 mm/s -> 算法要的脉冲/10ms）
//-------------------------------------------------------------------------------------------------------------------
float car_tune_speed_straight (void)
{
    return car_tune_mm_s_to_counts((float)menu_param_get(MENU_PARAM_IDX_SPEED_STRAIGHT));
}

float car_tune_speed_bend (void)
{
    return car_tune_mm_s_to_counts((float)menu_param_get(MENU_PARAM_IDX_SPEED_BEND));
}

float car_tune_speed_cautious (void)
{
    return car_tune_mm_s_to_counts((float)menu_param_get(MENU_PARAM_IDX_SPEED_CAUTIOUS));
}

float car_tune_speed_kp (void)
{
    return (float)menu_param_get(MENU_PARAM_IDX_SPEED_KP);
}

float car_tune_speed_ki (void)
{
    return (float)menu_param_get(MENU_PARAM_IDX_SPEED_KI);
}

float car_tune_pwm_limit (void)
{
    int32 v = menu_param_get(MENU_PARAM_IDX_PWM_LIMIT);

    if (v < 0)                 { v = 0; }
    if (v > PWM_DUTY_MAX)      { v = PWM_DUTY_MAX; }
    return (float)v;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     圆环
//-------------------------------------------------------------------------------------------------------------------
uint8 car_tune_ring_enable (void)
{
    return (0 != menu_param_get(MENU_PARAM_IDX_RING_EN)) ? (1) : (0);
}

int16 car_tune_ring_dir (void)
{
    int32 v = menu_param_get(MENU_PARAM_IDX_RING_DIR);

    if (v > 0)  { return 1; }
    if (v < 0)  { return -1; }
    return 0;
}

float car_tune_ring_speed (void)
{
    return car_tune_mm_s_to_counts((float)menu_param_get(MENU_PARAM_IDX_RING_SPEED));
}

uint32 car_tune_ring_timeout_ms (void)
{
    int32 s = menu_param_get(MENU_PARAM_IDX_RING_TIMEOUT);

    if (s < 1) { s = 1; }
    return (uint32)s * 1000u;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     电机 / 编码器方向
//-------------------------------------------------------------------------------------------------------------------
uint8 car_tune_motor_inv_l (void) { return (0 != menu_param_get(MENU_PARAM_IDX_MOTOR_INV_L)) ? (1) : (0); }
uint8 car_tune_motor_inv_r (void) { return (0 != menu_param_get(MENU_PARAM_IDX_MOTOR_INV_R)) ? (1) : (0); }

int8 car_tune_enc_sign_l (void) { return (0 != menu_param_get(MENU_PARAM_IDX_ENC_INV_L)) ? (-1) : (1); }
int8 car_tune_enc_sign_r (void) { return (0 != menu_param_get(MENU_PARAM_IDX_ENC_INV_R)) ? (-1) : (1); }

#pragma section all restore
