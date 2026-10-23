/*********************************************************************************************************************
* 文件名            imu_odom.c
* 说明              里程计（航迹推算）：编码器算走了多远 + 陀螺仪算车头朝哪 -> 累加出相对原点的坐标
*
*                   每个 10ms 周期：
*                     1. 读两个轮子的脉冲增量，取平均得到这一小段路程 ds（mm）
*                     2. 用当前航向 yaw 把 ds 分解到 X（前后）/ Y（左右）两个方向累加
*                     3. 走过 50mm 或车头转了 10 度，就往轨迹缓冲里存一个点（屏幕上画路径用）
*
*                   为什么用「左右轮的航向」而不是转向角：陀螺仪直接给了车头实际方向，
*                   即使打滑转不过来，画出来的轨迹也还是真实的（编码器路程 + 陀螺航向）
*
* 注意              本文件里的 cos/sinf 只在航向变化超过 0.3 度时才重算，避免每 10ms 都进一次浮点库
********************************************************************************************************************/
#include "imu_config.h"
#include "imu_odom.h"
#include "imu_encoder.h"
#include "imu_attitude.h"
#include <math.h>

#define IMU_ODO_DEG2RAD     (0.01745329f)

static float   odo_x;                               // 相对原点的 X（mm，前方为正）
static float   odo_y;                               // 相对原点的 Y（mm，左侧为正）
static float   odo_dist;                            // 总路程（mm，只加不减）
static float   odo_speed;                           // 当前速度（mm/s，前进为正）
static int32   odo_last_pulse[IMU_ENC_NUM];         // 上次读到的累计脉冲

static int16   trail_x[IMU_ODO_TRAIL_MAX];          // 轨迹点 X（mm）
static int16   trail_y[IMU_ODO_TRAIL_MAX];          // 轨迹点 Y（mm）
static uint16  trail_head;                          // 环形缓冲：下一个写的位置
static uint16  trail_cnt;                           // 环形缓冲：现在有几个点（最多 IMU_ODO_TRAIL_MAX）
static float   trail_acc_mm;                        // 距离上次记点走了多远
static float   trail_last_yaw;                      // 上一个轨迹点的航向

static int16   wp_x[IMU_ODO_WP_MAX];                // 航点 X
static int16   wp_y[IMU_ODO_WP_MAX];                // 航点 Y
static int16   wp_yaw10[IMU_ODO_WP_MAX];            // 航点航向 x10
static uint8   wp_num;                              // 已记录几个航点

static float   odo_sin;                             // 缓存的 sin(yaw) / cos(yaw)
static float   odo_cos;
static float   odo_yaw_cache;                       // 缓存对应的 yaw
static uint8   odo_yaw_ok;                          // 缓存是否有效

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     坐标限幅（int16 存 mm，超过 ±32m 就没法表示了）
//-------------------------------------------------------------------------------------------------------------------
static int16 odo_clamp (float v)
{
    if (v >  (float)IMU_ODO_COORD_LIMIT_MM) { return (int16)IMU_ODO_COORD_LIMIT_MM;  }
    if (v < -(float)IMU_ODO_COORD_LIMIT_MM) { return (int16)(-IMU_ODO_COORD_LIMIT_MM); }
    return (int16)v;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     往轨迹环形缓冲里加一个点
//-------------------------------------------------------------------------------------------------------------------
static void odo_trail_add (void)
{
    trail_x[trail_head]   = odo_clamp(odo_x);
    trail_y[trail_head]   = odo_clamp(odo_y);

    trail_head ++;
    if (trail_head >= IMU_ODO_TRAIL_MAX) { trail_head = 0; }

    if (trail_cnt < IMU_ODO_TRAIL_MAX) { trail_cnt ++; }
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     初始化
//-------------------------------------------------------------------------------------------------------------------
void imu_odom_init (void)
{
    uint8 i = 0;

    odo_x        = 0.0f;
    odo_y        = 0.0f;
    odo_dist     = 0.0f;
    odo_speed    = 0.0f;
    trail_head   = 0;
    trail_cnt    = 0;
    trail_acc_mm = 0.0f;
    trail_last_yaw = 0.0f;
    wp_num       = 0;
    odo_sin      = 0.0f;
    odo_cos      = 1.0f;
    odo_yaw_cache = 0.0f;
    odo_yaw_ok   = 0;

    for (i = 0; i < IMU_ENC_NUM; i ++) { odo_last_pulse[i] = imu_encoder_total((imu_enc_id_enum)i); }
    for (i = 0; i < IMU_ODO_WP_MAX; i ++)
    {
        wp_x[i]     = 0;
        wp_y[i]     = 0;
        wp_yaw10[i] = 0;
    }

    odo_trail_add();                                                // 原点也算一个轨迹点，屏幕上路径从头画
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     里程计更新（10ms 定时中断调用，必须在 imu_attitude_update 之后）
//-------------------------------------------------------------------------------------------------------------------
void imu_odom_update (void)
{
    int32 total_l = imu_encoder_total(IMU_ENC_LEFT);
    int32 total_r = imu_encoder_total(IMU_ENC_RIGHT);
    int32 delta_l = total_l - odo_last_pulse[IMU_ENC_LEFT];
    int32 delta_r = total_r - odo_last_pulse[IMU_ENC_RIGHT];
    float ds;
    float yaw;

    odo_last_pulse[IMU_ENC_LEFT]  = total_l;
    odo_last_pulse[IMU_ENC_RIGHT] = total_r;

    ds = (float)(delta_l + delta_r) * 0.5f * IMU_ODO_MM_PER_PULSE;  // 两个轮子走的路取平均 = 车中心走的路
    odo_speed = ds * (1000.0f / (float)IMU_ENC_PERIOD_MS);

    yaw = imu_attitude_yaw();

    // ------------------------- 1. 航向的 sin/cos 缓存（变化小于 0.3 度就沿用上一次的） -------------------------
    if ((0 == odo_yaw_ok) || (fabsf(yaw - odo_yaw_cache) > 0.3f))
    {
        float rad = yaw * IMU_ODO_DEG2RAD;

        odo_sin      = sinf(rad);
        odo_cos      = cosf(rad);
        odo_yaw_cache = yaw;
        odo_yaw_ok   = 1;
    }

    // ------------------------- 2. 累加坐标 -------------------------
    odo_x    += ds * odo_cos;
    odo_y    += ds * odo_sin;
    odo_dist += fabsf(ds);

    // ------------------------- 3. 轨迹采样 -------------------------
    if (0.0f != ds)
    {
        trail_acc_mm += fabsf(ds);

        if ((trail_acc_mm >= IMU_ODO_TRAIL_STEP_MM) || (fabsf(yaw - trail_last_yaw) >= IMU_ODO_TRAIL_TURN_DEG))
        {
            odo_trail_add();
            trail_acc_mm   = 0.0f;
            trail_last_yaw = yaw;
        }
    }
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     原点归零：位置清零 + 航向归零 + 轨迹清空（编码器累计也一起清）
// 备注信息     这就是「把现在这个地方当成出发点」，之后所有坐标都是相对它的
//-------------------------------------------------------------------------------------------------------------------
void imu_odom_reset (void)
{
    imu_encoder_clear();
    imu_attitude_yaw_set(0.0f);

    odo_x        = 0.0f;
    odo_y        = 0.0f;
    odo_dist     = 0.0f;
    odo_speed    = 0.0f;
    odo_last_pulse[IMU_ENC_LEFT]  = 0;
    odo_last_pulse[IMU_ENC_RIGHT] = 0;
    trail_head   = 0;
    trail_cnt    = 0;
    trail_acc_mm = 0.0f;
    trail_last_yaw = 0.0f;
    odo_yaw_ok   = 0;

    odo_trail_add();

#if (1 == IMU_SERIAL_DEBUG)
    printf("[odom] reset origin\r\n");
#endif
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     只清轨迹，不动坐标和航向
//-------------------------------------------------------------------------------------------------------------------
void imu_odom_trail_clear (void)
{
    trail_head   = 0;
    trail_cnt    = 0;
    trail_acc_mm = 0.0f;
    trail_last_yaw = imu_attitude_yaw();
    odo_trail_add();

#if (1 == IMU_SERIAL_DEBUG)
    printf("[odom] trail cleared\r\n");
#endif
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     读坐标 / 路程 / 速度
//-------------------------------------------------------------------------------------------------------------------
float imu_odom_x_mm       (void) { return odo_x;     }
float imu_odom_y_mm       (void) { return odo_y;     }
float imu_odom_dist_mm    (void) { return odo_dist;  }
float imu_odom_speed_mm_s (void) { return odo_speed; }

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     读轨迹点（index = 0 是最老的点）
//-------------------------------------------------------------------------------------------------------------------
uint16 imu_odom_trail_num (void)
{
    return trail_cnt;
}

void imu_odom_trail_get (uint16 index, int16 *x, int16 *y)
{
    uint16 idx;

    if (index >= trail_cnt)
    {
        if (NULL != x) { *x = 0; }
        if (NULL != y) { *y = 0; }
        return;
    }

    idx = (uint16)(trail_head + IMU_ODO_TRAIL_MAX - trail_cnt + index);
    while (idx >= IMU_ODO_TRAIL_MAX) { idx = (uint16)(idx - IMU_ODO_TRAIL_MAX); }

    if (NULL != x) { *x = trail_x[idx]; }
    if (NULL != y) { *y = trail_y[idx]; }
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     记录航点：把当前位置存下来
// 返回参数     记录后的航点总数，0 = 已经满了（不再记录）
// 备注信息     航点是「位置」，不是轨迹点：轨迹点会自动滚动丢掉老的，航点会一直留着（还能存进 Flash）
//-------------------------------------------------------------------------------------------------------------------
uint8 imu_odom_wp_mark (void)
{
    if (wp_num >= IMU_ODO_WP_MAX) { return 0; }

    wp_x[wp_num]     = odo_clamp(odo_x);
    wp_y[wp_num]     = odo_clamp(odo_y);
    wp_yaw10[wp_num] = (int16)(imu_attitude_yaw() * 10.0f);
    wp_num ++;

#if (1 == IMU_SERIAL_DEBUG)
    printf("[odom] mark wp %d  x %d  y %d\r\n", (int)wp_num, (int)wp_x[wp_num - 1], (int)wp_y[wp_num - 1]);
#endif

    return wp_num;
}

uint8 imu_odom_wp_num (void)
{
    return wp_num;
}

void imu_odom_wp_get (uint8 index, int16 *x, int16 *y, int16 *yaw10)
{
    if (index >= wp_num)
    {
        if (NULL != x)     { *x     = 0; }
        if (NULL != y)     { *y     = 0; }
        if (NULL != yaw10) { *yaw10 = 0; }
        return;
    }

    if (NULL != x)     { *x     = wp_x[index];     }
    if (NULL != y)     { *y     = wp_y[index];     }
    if (NULL != yaw10) { *yaw10 = wp_yaw10[index]; }
}

void imu_odom_wp_clear (void)
{
    uint8 i = 0;

    for (i = 0; i < IMU_ODO_WP_MAX; i ++)
    {
        wp_x[i]     = 0;
        wp_y[i]     = 0;
        wp_yaw10[i] = 0;
    }
    wp_num = 0;
}

void imu_odom_wp_set (uint8 index, int16 x, int16 y, int16 yaw10)
{
    if (index >= IMU_ODO_WP_MAX) { return; }

    wp_x[index]     = x;
    wp_y[index]     = y;
    wp_yaw10[index] = yaw10;
}

void imu_odom_wp_num_set (uint8 num)
{
    wp_num = (num > IMU_ODO_WP_MAX) ? (IMU_ODO_WP_MAX) : (num);
}
