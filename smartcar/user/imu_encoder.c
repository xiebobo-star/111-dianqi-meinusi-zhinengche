/*********************************************************************************************************************
* 文件名            imu_encoder.c
* 说明              两路编码器测速 + 累计脉冲（里程计的数据来源）
*                   每 10ms 读一次编码器计数并清零：本周期脉冲数 x 100 = 脉冲/秒，累计脉冲用于算路程
* 注意              encoder_get_count 读出来是有符号的（方向解码模式），所以倒车是负数，别把它当无符号用
********************************************************************************************************************/
#include "imu_config.h"
#include "menu_param.h"
#include "imu_encoder.h"

static const encoder_index_enum    enc_tim[IMU_ENC_NUM] = {IMU_ENC_LEFT_TIM,  IMU_ENC_RIGHT_TIM};
static const encoder_channel1_enum enc_ch1[IMU_ENC_NUM] = {IMU_ENC_LEFT_A,    IMU_ENC_RIGHT_A};
static const encoder_channel2_enum enc_ch2[IMU_ENC_NUM] = {IMU_ENC_LEFT_B,    IMU_ENC_RIGHT_B};

static int16 enc_count[IMU_ENC_NUM];        // 最近一个周期的脉冲数
static int32 enc_speed[IMU_ENC_NUM];        // 脉冲/秒
static int32 enc_total[IMU_ENC_NUM];        // 累计脉冲
static uint8 enc_moving;                    // 1 = 至少一个轮子在转

// 编码自检：4 个脚的电平统计（只看信号有没有进来，不参与控制）
static const gpio_pin_enum enc_pin[IMU_ENC_PIN_NUM] =
{
    IMU_ENC_LEFT_A_PIN, IMU_ENC_LEFT_B_PIN, IMU_ENC_RIGHT_A_PIN, IMU_ENC_RIGHT_B_PIN
};
static uint16 enc_pin_high[IMU_ENC_PIN_NUM];   // 本统计窗口里读到高的次数
static uint16 enc_pin_tick;                    // 本统计窗口已经采了几次
static uint8  enc_pin_duty[IMU_ENC_PIN_NUM];   // 上一个窗口算出的占空比 0~100
static uint8  enc_pin_lvl [IMU_ENC_PIN_NUM];   // 最近一次读到的电平

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     编码器初始化
//-------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------
// 函数简介     车轮直径 mm（菜单参数「轮径」内部是 0.1mm，没配就退回 imu_config.h 的默认值）
// 备注信息     里程计 / 轮速换算都走这里，所以菜单里改完轮径，里程立刻按新值算
//-------------------------------------------------------------------------------------------------------------------
float imu_wheel_mm_get (void)
{
    int32 v = menu_param_get(MENU_PARAM_IDX_WHEEL);

    if ((v < 100) || (v > 5000)) { return IMU_ENC_WHEEL_MM_DEFAULT; }     // 参数还没初始化 / 离谱值
    return (float)v * 0.1f;
}

void imu_encoder_init (void)
{
    uint8 i = 0;

    for (i = 0; i < IMU_ENC_NUM; i ++)
    {
        encoder_dir_init(enc_tim[i], enc_ch1[i], enc_ch2[i]);
        encoder_clear_count(enc_tim[i]);
        enc_count[i] = 0;
        enc_speed[i] = 0;
        enc_total[i] = 0;
    }
    enc_moving = 0;

    for (i = 0; i < IMU_ENC_PIN_NUM; i ++)
    {
        enc_pin_high[i] = 0;
        enc_pin_duty[i] = 0;
        enc_pin_lvl[i]  = 0;
    }
    enc_pin_tick = 0;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     编码器更新（10ms 定时中断调用）
//-------------------------------------------------------------------------------------------------------------------
void imu_encoder_update (void)
{
    uint8 i = 0;

    enc_moving = 0;

    for (i = 0; i < IMU_ENC_NUM; i ++)
    {
        int16 count = encoder_get_count(enc_tim[i]);

        encoder_clear_count(enc_tim[i]);                                // 读完立刻清零，下个周期才是「这一段」的增量

        if ((IMU_ENC_LEFT  == i) && (0 != IMU_ENC_LEFT_INVERT))  { count = -count; }
        if ((IMU_ENC_RIGHT == i) && (0 != IMU_ENC_RIGHT_INVERT)) { count = -count; }

        enc_count[i]  = count;
        enc_total[i] += (int32)count;
        enc_speed[i]  = (int32)count * (1000 / IMU_ENC_PERIOD_MS);      // 10ms 里的脉冲数 -> 每秒

        if (0 != count) { enc_moving = 1; }
    }

    // ------------------------- 编码自检：采 4 个脚的电平 -------------------------
    // 10ms 采一次、1 秒算一次占空比。手转编码器轴时方波 -> ≈50%（因为采样比信号慢，会围着 50% 抖）；
    // 停着不动时恒 0% 或恒 100%；一个方向恒 0% / 恒 100% 一直不变 = 这个脚没有信号。
    {
        uint8 p = 0;

        for (p = 0; p < IMU_ENC_PIN_NUM; p ++)
        {
            enc_pin_lvl[p] = (0 != gpio_get_level(enc_pin[p])) ? (1) : (0);
            if (0 != enc_pin_lvl[p]) { enc_pin_high[p] ++; }
        }

        enc_pin_tick ++;
        if (enc_pin_tick >= 100)                                    // 10ms x 100 = 1 秒一个统计窗口
        {
            for (p = 0; p < IMU_ENC_PIN_NUM; p ++)
            {
                enc_pin_duty[p] = (uint8)((uint32)enc_pin_high[p] * 100u / (uint32)enc_pin_tick);
                enc_pin_high[p] = 0;
            }
            enc_pin_tick = 0;
        }
    }
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     清零（原点归零时调用，编码器和里程计一起归零）
//-------------------------------------------------------------------------------------------------------------------
void imu_encoder_clear (void)
{
    uint8 i = 0;

    for (i = 0; i < IMU_ENC_NUM; i ++)
    {
        encoder_clear_count(enc_tim[i]);
        enc_count[i] = 0;
        enc_speed[i] = 0;
        enc_total[i] = 0;
    }
    enc_moving = 0;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     读数据
//-------------------------------------------------------------------------------------------------------------------
int16 imu_encoder_count (imu_enc_id_enum enc_n)
{
    return (enc_n < IMU_ENC_NUM) ? (enc_count[enc_n]) : (0);
}

int32 imu_encoder_total (imu_enc_id_enum enc_n)
{
    return (enc_n < IMU_ENC_NUM) ? (enc_total[enc_n]) : (0);
}

int32 imu_encoder_speed (imu_enc_id_enum enc_n)
{
    return (enc_n < IMU_ENC_NUM) ? (enc_speed[enc_n]) : (0);
}

float imu_encoder_speed_mm_s (imu_enc_id_enum enc_n)
{
    if (enc_n >= IMU_ENC_NUM) { return 0.0f; }

    return (float)enc_speed[enc_n] * IMU_ODO_MM_PER_PULSE;
}

uint8 imu_encoder_moving (void)
{
    return enc_moving;
}

uint8 imu_encoder_pin_duty (imu_enc_pin_enum pin_n)
{
    return (pin_n < IMU_ENC_PIN_NUM) ? (enc_pin_duty[pin_n]) : (0);
}

uint8 imu_encoder_pin_level (imu_enc_pin_enum pin_n)
{
    return (pin_n < IMU_ENC_PIN_NUM) ? (enc_pin_lvl[pin_n]) : (0);
}
