/*********************************************************************************************************************
* 文件名            imu_attitude.c
* 说明              IMU660RA 姿态解算：互补滤波算 roll / pitch / yaw
*
*                   为什么用互补滤波：
*                     陀螺仪积分短期准、长期漂（零漂积分会越跑越远）；
*                     加速度计测倾角长期准、短期抖（车一震动就乱跳）。
*                     两者加权融合：roll/pitch 用（1-K）份加速度计 + K 份陀螺；
*                     yaw 没有磁力计可以纠正，只能靠陀螺积分 +「静止时锁住 + 修零漂」来压漂移。
*
*                   静止检测（ZUPT）：三轴角速度都很小 + 两个轮子都没有脉冲 = 车真的没动。
*                     这时 ① yaw 不再积分（停着不会自己转） ② 把陀螺残差一点点加进零漂（越用越准）
*
* 硬件              IMU1 接口，SPI0：CLK=P20_11 MOSI=P20_14 MISO=P20_12 CS=P20_13（库默认引脚）
* 用法              imu_attitude_init();
*                   10ms 定时中断里：imu_attitude_update(moving);
*                   主循环里随便读：imu_attitude_roll() / pitch() / yaw()
* 注意              本文件是 GBK 编码（中文注释在 ADS 里才不会乱码）
********************************************************************************************************************/
#include "imu_config.h"
#include "imu_attitude.h"
#include <math.h>

#define IMU_ATT_RAD2DEG         (57.29577951f)
#define IMU_ATT_DEG2RAD         (0.01745329f)

static uint8  att_ok;                       // 1 = IMU 通信正常（自检通过）
static uint8  att_calib_done;               // 1 = 零漂校准过（本次上电）
static uint8  att_calib_busy;               // 1 = 正在校准，中断里先别采样，免得抢 SPI
static float  att_bias[3];                  // 零漂 dps：X / Y / Z
static float  att_gyro[3];                  // 去零漂后的角速度 dps
static float  att_acc[3];                   // 加速度 g
static float  att_roll, att_pitch, att_yaw; // 姿态角 度
static uint8  att_still_cnt;                // 连续静止周期计数
static uint8  att_still;                    // 1 = 静止
static uint32 att_sample_cnt;               // 解算次数（状态页显示，也能验证中断真在跑）

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     把角度折到 -180 ~ 180
//-------------------------------------------------------------------------------------------------------------------
static float att_wrap180 (float deg)
{
    while (deg > 180.0f)  { deg -= 360.0f; }
    while (deg < -180.0f) { deg += 360.0f; }
    return deg;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     读一次 IMU，换算成物理量（dps / g）
// 备注信息     用 SPI 读 6 字节约 69us，放 10ms 中断里完全没压力
//-------------------------------------------------------------------------------------------------------------------
static void att_read (void)
{
    imu660ra_get_gyro();
    imu660ra_get_acc();

    att_gyro[0] = IMU_ATT_GYRO_SIGN_X * imu660ra_gyro_transition(imu660ra_gyro_x) - att_bias[0];
    att_gyro[1] = IMU_ATT_GYRO_SIGN_Y * imu660ra_gyro_transition(imu660ra_gyro_y) - att_bias[1];
    att_gyro[2] = IMU_ATT_GYRO_SIGN_Z * imu660ra_gyro_transition(imu660ra_gyro_z) - att_bias[2];

    att_acc[0]  = IMU_ATT_ACC_SIGN_X * imu660ra_acc_transition(imu660ra_acc_x);
    att_acc[1]  = IMU_ATT_ACC_SIGN_Y * imu660ra_acc_transition(imu660ra_acc_y);
    att_acc[2]  = IMU_ATT_ACC_SIGN_Z * imu660ra_acc_transition(imu660ra_acc_z);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     初始化
// 备注信息     自检失败（没接线 / 模块坏）也会继续跑，只是 att_ok = 0，屏幕上会显示「IMU错误」
//-------------------------------------------------------------------------------------------------------------------
void imu_attitude_init (void)
{
    uint8 i = 0;
    uint8 ret = 0;

    att_ok         = 0;
    att_calib_done = 0;
    att_calib_busy = 0;
    att_still      = 1;
    att_still_cnt  = 0;
    att_sample_cnt = 0;
    att_roll       = 0.0f;
    att_pitch      = 0.0f;
    att_yaw        = 0.0f;

    for (i = 0; i < 3; i ++)
    {
        att_bias[i] = 0.0f;
        att_gyro[i] = 0.0f;
        att_acc[i]  = 0.0f;
    }

    ret = imu660ra_init();                  // 0 = 成功，1 = 自检/配置失败
    if (0 == ret) { att_ok = 1; }

#if (1 == IMU_SERIAL_DEBUG)
    printf("[imu] init %s\r\n", (0 != att_ok) ? ("ok") : ("FAILED"));
#endif
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     姿态解算（10ms 周期调用）
// 参数说明     moving      1 = 轮子有脉冲（车在动），0 = 轮子没动
// 备注信息     放 10ms 定时中断里：imq_attitude_update(imu_encoder_moving());
//-------------------------------------------------------------------------------------------------------------------
void imu_attitude_update (uint8 moving)
{
    float gx, gy, gz;
    float ax, ay, az;
    float acc_norm;
    float roll_acc, pitch_acc;
    float k;
    uint8 i;

    if (0 == att_ok)        { return; }
    if (0 != att_calib_busy) { return; }    // 校准过程中不要在中断里抢 SPI

    att_read();
    att_sample_cnt ++;
    gx = att_gyro[0];
    gy = att_gyro[1];
    gz = att_gyro[2];
    ax = att_acc[0];
    ay = att_acc[1];
    az = att_acc[2];

    // ------------------------- 1. 静止判定（角速度小 + 轮子不动） -------------------------
    if ((fabsf(gx) < IMU_ATT_STILL_GYRO_DPS) && (fabsf(gy) < IMU_ATT_STILL_GYRO_DPS) && (fabsf(gz) < IMU_ATT_STILL_GYRO_DPS) && (0 == moving))
    {
        if (att_still_cnt < 255) { att_still_cnt ++; }
    }
    else
    {
        att_still_cnt = 0;
    }
    att_still = (att_still_cnt >= IMU_ATT_STILL_COUNT) ? (1) : (0);

    // ------------------------- 2. 加速度计算倾角（只在加速度模长正常时才用） -------------------------
    acc_norm = sqrtf(ax * ax + ay * ay + az * az);

    // ------------------------- 3. 陀螺积分 -------------------------
    att_roll  = att_roll  + gx * IMU_ATT_PERIOD_S;
    att_pitch = att_pitch + gy * IMU_ATT_PERIOD_S;

#if (0 == IMU_ATT_ZUPT_ENABLE)
    att_yaw = att_yaw + gz * IMU_ATT_PERIOD_S;                      // 关掉静止锁定时 yaw 一直积分
#else
    if (0 == att_still)
    {
        att_yaw = att_yaw + gz * IMU_ATT_PERIOD_S;                  // 只有动的时候才积分 yaw
    }
#endif

    // ------------------------- 4. 加速度计修正 roll / pitch -------------------------
    if ((acc_norm > IMU_ATT_ACC_TRUST_LOW) && (acc_norm < IMU_ATT_ACC_TRUST_HIGH))
    {
        roll_acc  = atan2f(ay, az) * IMU_ATT_RAD2DEG;               // 左右倾
        pitch_acc = atan2f(-ax, sqrtf(ay * ay + az * az)) * IMU_ATT_RAD2DEG;    // 前后倾

        k = IMU_ATT_COMP_K;
        att_roll  = att_roll  * k + roll_acc  * (1.0f - k);
        att_pitch = att_pitch * k + pitch_acc * (1.0f - k);
    }

    // ------------------------- 5. 静止时慢慢修零漂（车停着，陀螺读到的就全是零漂） -------------------------
#if (1 == IMU_ATT_ZUPT_ENABLE)
    if (0 != att_still)
    {
        for (i = 0; i < 3; i ++)
        {
            att_bias[i] += att_gyro[i] * IMU_ATT_ZUPT_RATE;         // 残差一点点吃进零漂
            att_gyro[i] -= att_gyro[i] * IMU_ATT_ZUPT_RATE;         // 本周期算完的角速度也同步修掉
        }
    }
#endif

    att_roll  = att_wrap180(att_roll);
    att_pitch = att_wrap180(att_pitch);
    att_yaw   = att_wrap180(att_yaw);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     零漂校准：车静止放 1 秒，采样求平均
// 返回参数     0 = 成功    1 = 算出来的零漂太大（模块没放平 / 接线不对）   2 = 采样过程中车在动
// 使用示例     if (0 == imu_attitude_calibrate()) { 校准成功，可以保存到 Flash; }
// 备注信息     这是阻塞函数，大概 1 秒（500 点 x 2ms），只能在主循环里调，别在中断里调
//              校准期间中断里的解算会暂停（att_calib_busy），所以角度不会因为校准乱跳
//-------------------------------------------------------------------------------------------------------------------
uint8 imu_attitude_calibrate (void)
{
    float sum[3] = {0.0f, 0.0f, 0.0f};
    float mean[3];
    float max_dev = 0.0f;
    float val;
    uint16 i = 0;
    uint8  axis = 0;
    uint8  result = 0;

    if (0 == att_ok) { return 1; }

    att_calib_busy = 1;                                             // 告诉中断：先别抢 SPI

    // 先丢掉前 10 次采样（模块刚从省电模式唤醒，第一把数据不准）
    for (i = 0; i < 10; i ++)
    {
        imu660ra_get_gyro();
        system_delay_ms(IMU_ATT_CALIB_DELAY_MS);
    }

    for (i = 0; i < IMU_ATT_CALIB_SAMPLE; i ++)
    {
        imu660ra_get_gyro();
        sum[0] += IMU_ATT_GYRO_SIGN_X * imu660ra_gyro_transition(imu660ra_gyro_x);
        sum[1] += IMU_ATT_GYRO_SIGN_Y * imu660ra_gyro_transition(imu660ra_gyro_y);
        sum[2] += IMU_ATT_GYRO_SIGN_Z * imu660ra_gyro_transition(imu660ra_gyro_z);
        system_delay_ms(IMU_ATT_CALIB_DELAY_MS);
    }

    for (axis = 0; axis < 3; axis ++) { mean[axis] = sum[axis] / (float)IMU_ATT_CALIB_SAMPLE; }

    // 再采 100 点看看抖动有多大：车在动 / 手在碰，偏差就会很大
    for (i = 0; i < 100; i ++)
    {
        imu660ra_get_gyro();
        val = fabsf(IMU_ATT_GYRO_SIGN_X * imu660ra_gyro_transition(imu660ra_gyro_x) - mean[0]);
        if (val > max_dev) { max_dev = val; }
        val = fabsf(IMU_ATT_GYRO_SIGN_Y * imu660ra_gyro_transition(imu660ra_gyro_y) - mean[1]);
        if (val > max_dev) { max_dev = val; }
        val = fabsf(IMU_ATT_GYRO_SIGN_Z * imu660ra_gyro_transition(imu660ra_gyro_z) - mean[2]);
        if (val > max_dev) { max_dev = val; }
        system_delay_ms(IMU_ATT_CALIB_DELAY_MS);
    }

    if (max_dev > IMU_ATT_CALIB_DEV_MAX)
    {
        result = 2;                                                 // 采样时在动
    }
    else if ((fabsf(mean[0]) > IMU_ATT_CALIB_BIAS_MAX) || (fabsf(mean[1]) > IMU_ATT_CALIB_BIAS_MAX) || (fabsf(mean[2]) > IMU_ATT_CALIB_BIAS_MAX))
    {
        result = 1;                                                 // 零漂大得离谱
    }
    else
    {
        att_bias[0]    = mean[0];
        att_bias[1]    = mean[1];
        att_bias[2]    = mean[2];
        att_calib_done = 1;
        result         = 0;
    }

    att_calib_busy = 0;

#if (1 == IMU_SERIAL_DEBUG)
    printf("[imu] calib %d bias x%d y%d z%d (0.01dps) dev %d\r\n",
           (int)result, (int)(att_bias[0] * 100.0f), (int)(att_bias[1] * 100.0f), (int)(att_bias[2] * 100.0f),
           (int)(max_dev * 100.0f));
#endif

    return result;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     读零漂 / 写零漂 / 清零漂
//-------------------------------------------------------------------------------------------------------------------
void imu_attitude_bias_get (float *x, float *y, float *z)
{
    if (NULL != x) { *x = att_bias[0]; }
    if (NULL != y) { *y = att_bias[1]; }
    if (NULL != z) { *z = att_bias[2]; }
}

void imu_attitude_bias_set (float x, float y, float z)
{
    att_bias[0]    = x;
    att_bias[1]    = y;
    att_bias[2]    = z;
    att_calib_done = 1;                                     // 只要写进来过就认为「已设置」（Flash 载入的零漂也算）
}

void imu_attitude_bias_clear (void)
{
    att_bias[0]    = 0.0f;
    att_bias[1]    = 0.0f;
    att_bias[2]    = 0.0f;
    att_calib_done = 0;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     各种读接口
//-------------------------------------------------------------------------------------------------------------------
uint8 imu_attitude_ok         (void) { return att_ok;         }
float imu_attitude_roll       (void) { return att_roll;       }
float imu_attitude_pitch      (void) { return att_pitch;      }
float imu_attitude_yaw        (void) { return att_yaw;        }
float imu_attitude_yaw_rad    (void) { return att_yaw * IMU_ATT_DEG2RAD; }
uint8 imu_attitude_still      (void) { return att_still;      }
uint8 imu_attitude_calib_done (void) { return att_calib_done; }
uint32 imu_attitude_sample_cnt(void) { return att_sample_cnt; }

void imu_attitude_yaw_set (float deg)
{
    att_yaw = att_wrap180(deg);
}

float imu_attitude_gyro (uint8 axis)
{
    return (axis < 3) ? (att_gyro[axis]) : (0.0f);
}

float imu_attitude_acc (uint8 axis)
{
    return (axis < 3) ? (att_acc[axis]) : (0.0f);
}
