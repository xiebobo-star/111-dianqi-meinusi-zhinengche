/*********************************************************************************************************************
* 文件名            enc_pd1503.c
* 说明              PD-1503 编码器实现：10ms 读一次并清零，方向取反走菜单参数
* 备注信息          · 逐飞库 encoder_get_count() 读出来就顺手清零，所以「本周期脉冲」= 两次读之间的差
*                   · 「每圈脉冲」标定：车架起来 -> 看屏上「总脉冲」-> 手推轮子转一圈 -> 读数差填进菜单
*                   · 读数突然巨大（> ENC_RAW_LIMIT）说明线序/共地/干扰有问题，标记异常让上层停车
*********************************************************************************************************************/
#include "menu_config.h"
#include "menu_param.h"
#include "car_tune.h"
#include "enc_pd1503.h"

#pragma section all "cpu0_dsram"

#define ENC_FILTER          (0.35f)                 // 和原工程 car_control 里的滤波系数一致
#define ENC_RAW_LIMIT       (20000)                 // 单周期脉冲上限，超过就当异常

static int32 enc_last  [ENC_NUM];                   // 本周期脉冲（带符号）
static float enc_filt  [ENC_NUM];                   // 滤波后的速度（脉冲/10ms）
static int32 enc_sum   [ENC_NUM];                   // 累计脉冲
static uint8 enc_bad;                               // 1 = 最近一次读到异常值

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     初始化：按「编码模式」参数选 脉冲+方向 / 正交AB，然后清计数
//-------------------------------------------------------------------------------------------------------------------
void enc_init (void)
{
    uint8 i;

    if (0 != menu_param_get(MENU_PARAM_IDX_ENC_MODE))
    {
        encoder_quad_init(TIM4_ENCODER, TIM4_ENCODER_CH1_P02_8, TIM4_ENCODER_CH2_P00_9);
        encoder_quad_init(TIM5_ENCODER, TIM5_ENCODER_CH1_P10_3, TIM5_ENCODER_CH2_P10_1);
    }
    else
    {
        encoder_dir_init(TIM4_ENCODER, TIM4_ENCODER_CH1_P02_8, TIM4_ENCODER_CH2_P00_9);
        encoder_dir_init(TIM5_ENCODER, TIM5_ENCODER_CH1_P10_3, TIM5_ENCODER_CH2_P10_1);
    }

    encoder_clear_count(TIM4_ENCODER);
    encoder_clear_count(TIM5_ENCODER);

    for (i = 0; i < ENC_NUM; i ++)
    {
        enc_last[i] = 0;
        enc_filt[i] = 0.0f;
        enc_sum[i]  = 0;
    }
    enc_bad = 0;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     10ms 更新：读两路脉冲 -> 清零 -> 方向取反 -> 限幅 -> 滤波 -> 累计
//-------------------------------------------------------------------------------------------------------------------
void enc_update (void)
{
    int32 raw[ENC_NUM];
    int32 sign[ENC_NUM];
    uint8 i;

    raw[ENC_L] = (int32)encoder_get_count(TIM4_ENCODER);
    raw[ENC_R] = (int32)encoder_get_count(TIM5_ENCODER);
    encoder_clear_count(TIM4_ENCODER);
    encoder_clear_count(TIM5_ENCODER);

    sign[ENC_L] = car_tune_enc_sign_l();
    sign[ENC_R] = car_tune_enc_sign_r();

    enc_bad = 0;
    for (i = 0; i < ENC_NUM; i ++)
    {
        int32 c = raw[i] * sign[i];

        if ((c > ENC_RAW_LIMIT) || (c < -ENC_RAW_LIMIT))
        {
            enc_bad = 1;
            c = 0;
        }

        enc_last[i] = c;
        enc_sum[i] += c;
        enc_filt[i] += ENC_FILTER * ((float)c - enc_filt[i]);
    }
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     清零（累计里程 / 脉冲都归零，里程计重新从 0 开始）
//-------------------------------------------------------------------------------------------------------------------
void enc_clear (void)
{
    uint8 i;

    for (i = 0; i < ENC_NUM; i ++)
    {
        enc_last[i] = 0;
        enc_filt[i] = 0.0f;
        enc_sum[i]  = 0;
    }
}

int32 enc_counts (uint8 idx)
{
    if (idx >= ENC_NUM) { return 0; }
    return enc_last[idx];
}

float enc_speed_cpt (uint8 idx)
{
    if (idx >= ENC_NUM) { return 0.0f; }
    return enc_filt[idx];
}

float enc_speed_mm_s (uint8 idx)
{
    if (idx >= ENC_NUM) { return 0.0f; }
    return car_tune_counts_to_mm_s(enc_filt[idx]);
}

int32 enc_total (uint8 idx)
{
    if (idx >= ENC_NUM) { return 0; }
    return enc_sum[idx];
}

float enc_dist_mm (uint8 idx)
{
    if (idx >= ENC_NUM) { return 0.0f; }
    return car_tune_counts_to_mm_s((float)enc_sum[idx]);
}

uint8 enc_abnormal (void)
{
    return enc_bad;
}

#pragma section all restore
