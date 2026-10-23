/*********************************************************************************************************************
* 文件名            imu_encoder.c
* 说明              两路编码器测速 + 累计脉冲（里程计的数据来源）
*                   每 10ms 读一次编码器计数并清零：本周期脉冲数 x 100 = 脉冲/秒，累计脉冲用于算路程
* 注意              encoder_get_count 读出来是有符号的（方向解码模式），所以倒车是负数，别把它当无符号用
********************************************************************************************************************/
#include "imu_config.h"
#include "imu_encoder.h"

static const encoder_index_enum    enc_tim[IMU_ENC_NUM] = {IMU_ENC_LEFT_TIM,  IMU_ENC_RIGHT_TIM};
static const encoder_channel1_enum enc_ch1[IMU_ENC_NUM] = {IMU_ENC_LEFT_A,    IMU_ENC_RIGHT_A};
static const encoder_channel2_enum enc_ch2[IMU_ENC_NUM] = {IMU_ENC_LEFT_B,    IMU_ENC_RIGHT_B};

static int16 enc_count[IMU_ENC_NUM];        // 最近一个周期的脉冲数
static int32 enc_speed[IMU_ENC_NUM];        // 脉冲/秒
static int32 enc_total[IMU_ENC_NUM];        // 累计脉冲
static uint8 enc_moving;                    // 1 = 至少一个轮子在转

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     编码器初始化
//-------------------------------------------------------------------------------------------------------------------
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
