/*********************************************************************************************************************
* 文件名            menu_motor.c
* 说明              电机 / 编码器菜单页面
*
*   电机输出页   选「两轮 / 左轮 / 右轮」，用按键1/2 加减占空比，屏上同时显示两轮编码器速度
*                —— 车必须架起来，或者把驱动板上的使能开关关掉再按
*   编码器页     本周期脉冲数 / 累计脉冲 / 速度 mm/s / 累计里程，按键3 清零
*
* 硬件               主板「电机信号1」 左 = P21_2(PWM) + P21_3(DIR)   右 = P21_4(PWM) + P21_5(DIR)
*                   驱动板 DRV8701E（PH/EN）：PWM 进 EN 调速、DIR 进 PH 定方向
*                   17 kHz 载频（超出人耳，电机不啸叫）  占空比满量程 PWM_DUTY_MAX = 10000
*                   编码器：imu_encoder.c 里 TIM4 / TIM5 方向解码，10ms 中断更新
*
* 安全               进页面时占空比一律 0；退出页面 / 长按回主菜单前都会 stop（两个轮子停）
*********************************************************************************************************************/
#include "menu_config.h"
#include "menu_display.h"
#include "menu_param.h"
#include "menu_motor.h"
#include "imu_encoder.h"
#include "imu_odom.h"

// ============================================ 引脚与参数（板子换了只改这里） ============================================
#define MOTOR_PWM_FREQ              (17000)                     // 17 kHz
#define MOTOR_L_PWM                 (ATOM0_CH0_P21_2)           // 左电机 PWM（EN）
#define MOTOR_L_DIR                 (P21_3)                     // 左电机 方向（PH）
#define MOTOR_R_PWM                 (ATOM0_CH2_P21_4)           // 右电机 PWM（EN）
#define MOTOR_R_DIR                 (P21_5)                     // 右电机 方向（PH）
#define MOTOR_DIR_HIGH_FORWARD      (1)                         // 1 = 方向脚高电平正转
#define MOTOR_DUTY_STEP             (1000)                      // 按键1/2 一次加减多少（1000 = 10%）

// ============================================ 页面内部状态 ============================================
static uint8  motor_inited;                                     // 1 = 已经 pwm_init 过
static uint8  motor_pick;                                       // 0 = 两轮一起  1 = 只左轮  2 = 只右轮
static int16  motor_duty;                                       // 当前占空比（0 ~ PWM_DUTY_MAX）

// 文字行数（窄屏 4 行，宽屏 6 行）
#if (MENU_DISPLAY_USE_IPS200)
    #define MOTOR_LINE_NUM          (6)
    #define MOTOR_LINE_Y0           (40)
    #define MOTOR_LINE_LH           (22)
#else
    #define MOTOR_LINE_NUM          (6)                             // 128x160：22 + 6x20 = 142，提示行也放得下
    #define MOTOR_LINE_Y0           (22)
    #define MOTOR_LINE_LH           (20)
#endif

// 文字区「上一次画到屏上的是什么」—— 只重画变了的那几行，别整屏 clear（那样每 200ms 闪一下）
static char        motor_buf[MOTOR_LINE_NUM][24];
static char        motor_last[MOTOR_LINE_NUM][24];
static const char *motor_line[MOTOR_LINE_NUM];
static uint8       motor_text_ok;

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     电机 PWM / 方向脚初始化（懒初始化，只做一次）
//-------------------------------------------------------------------------------------------------------------------
static void motor_hw_init (void)
{
    if (0 != motor_inited) { return; }

    pwm_init(MOTOR_L_PWM, MOTOR_PWM_FREQ, 0);                   // 占空比 0 = 不动
    pwm_init(MOTOR_R_PWM, MOTOR_PWM_FREQ, 0);
    gpio_init(MOTOR_L_DIR, GPO, GPIO_LOW, GPO_PUSH_PULL);
    gpio_init(MOTOR_R_DIR, GPO, GPIO_LOW, GPO_PUSH_PULL);

    motor_inited = 1;
    motor_duty   = 0;
    motor_pick   = 0;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     给某一轮输出占空比
// 参数说明     index  0 = 左轮   1 = 右轮
// 参数说明     duty   带符号：正数前进，负数后退，0 停
//-------------------------------------------------------------------------------------------------------------------
static void motor_apply (uint8 index, int16 duty)
{
    uint8  forward;
    uint32 mag;

    if (0 != index)
    {
        if (0 != menu_param_get(MENU_PARAM_IDX_MOTOR_INV_R)) { duty = (int16)(-duty); }
    }
    else
    {
        if (0 != menu_param_get(MENU_PARAM_IDX_MOTOR_INV_L)) { duty = (int16)(-duty); }
    }

    forward = (duty >= 0) ? (1) : (0);
    mag     = (uint32)((duty >= 0) ? duty : -duty);
    if (mag > (uint32)PWM_DUTY_MAX) { mag = (uint32)PWM_DUTY_MAX; }

    if (0 != index)
    {
        gpio_set_level(MOTOR_R_DIR, (0 != forward) ? MOTOR_DIR_HIGH_FORWARD : !MOTOR_DIR_HIGH_FORWARD);
        pwm_set_duty(MOTOR_R_PWM, mag);
    }
    else
    {
        gpio_set_level(MOTOR_L_DIR, (0 != forward) ? MOTOR_DIR_HIGH_FORWARD : !MOTOR_DIR_HIGH_FORWARD);
        pwm_set_duty(MOTOR_L_PWM, mag);
    }
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     按当前选择把占空比推给电机（0 = 两轮都停）
//-------------------------------------------------------------------------------------------------------------------
static void motor_push (void)
{
    int16 l = 0;
    int16 r = 0;

    if ((0 == motor_pick) || (1 == motor_pick)) { l = motor_duty; }
    if ((0 == motor_pick) || (2 == motor_pick)) { r = motor_duty; }

    motor_apply(0, l);
    motor_apply(1, r);
}

static void motor_stop (void)
{
    motor_duty = 0;
    motor_apply(0, 0);
    motor_apply(1, 0);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     立刻停两个轮子（没初始化过就什么都不做）—— 任何「离开电机页」的路径都要过这里
//-------------------------------------------------------------------------------------------------------------------
void menu_motor_stop_all (void)
{
    if (0 == motor_inited) { return; }
    motor_stop();
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     把要显示的文字填进 motor_buf（6 行，没用的行填空串）
//-------------------------------------------------------------------------------------------------------------------
static void motor_build_lines (uint8 page)
{
    uint8 i;

    for (i = 0; i < MOTOR_LINE_NUM; i ++)
    {
        motor_buf[i][0] = '\0';
        motor_line[i]   = motor_buf[i];
    }

    if (MENU_MOTOR_PAGE_MOTOR == page)
    {
        snprintf(motor_buf[0], sizeof(motor_buf[0]), "占空比 %d", (int)motor_duty);
        snprintf(motor_buf[1], sizeof(motor_buf[1]), "选中 %s",
                 (0 == motor_pick) ? ("两轮") : ((1 == motor_pick) ? ("左轮") : ("右轮")));
        snprintf(motor_buf[2], sizeof(motor_buf[2]), "左 %d mm/s", (int)imu_encoder_speed_mm_s(IMU_ENC_LEFT));
        snprintf(motor_buf[3], sizeof(motor_buf[3]), "右 %d mm/s", (int)imu_encoder_speed_mm_s(IMU_ENC_RIGHT));
        snprintf(motor_buf[4], sizeof(motor_buf[4]), "1加 2减 3选轮");
        snprintf(motor_buf[5], sizeof(motor_buf[5]), "把车轮架起来再测");
    }
    else
    {
        snprintf(motor_buf[0], sizeof(motor_buf[0]), "左 %d 右 %d 脉冲",
                 (int)imu_encoder_count(IMU_ENC_LEFT), (int)imu_encoder_count(IMU_ENC_RIGHT));
        snprintf(motor_buf[1], sizeof(motor_buf[1]), "左 %d mm/s", (int)imu_encoder_speed_mm_s(IMU_ENC_LEFT));
        snprintf(motor_buf[2], sizeof(motor_buf[2]), "右 %d mm/s", (int)imu_encoder_speed_mm_s(IMU_ENC_RIGHT));
        snprintf(motor_buf[3], sizeof(motor_buf[3]), "里程 %d mm", (int)imu_odom_dist_mm());
        snprintf(motor_buf[4], sizeof(motor_buf[4]), "3键 清零脉冲");
        snprintf(motor_buf[5], sizeof(motor_buf[5]), "4键 返回");
    }
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     把 motor_buf 画到屏上：整页只在进页面时清一次，之后只补画变了的那几行
// 参数说明     full    1 = 整屏重画（会 clear）    0 = 只重画变了的那一行
//-------------------------------------------------------------------------------------------------------------------
static void motor_flush (const char *title, uint8 full)
{
    uint16 y = (uint16)MOTOR_LINE_Y0;
    uint8  i;

    if (0 != full)
    {
        menu_display_clear();
        menu_display_set_color(MENU_COLOR_TITLE, MENU_COLOR_BG);
        menu_display_cn(4, 2, title);
        motor_text_ok = 0;
    }

    menu_display_set_color(MENU_COLOR_PEN, MENU_COLOR_BG);

    for (i = 0; i < MOTOR_LINE_NUM; i ++)
    {
        if ('\0' == motor_buf[i][0]) { break; }

        if ((0 == full) && (0 != motor_text_ok) && (0 == strcmp(motor_buf[i], motor_last[i])))
        {
            y = (uint16)(y + MOTOR_LINE_LH);                    // 这一行没变：不动屏
            continue;
        }

        menu_display_fill(0, (uint16)(y - 1), menu_display_width(), MOTOR_LINE_LH, MENU_COLOR_BG);
        menu_display_cn(4, y, motor_buf[i]);
        strncpy(motor_last[i], motor_buf[i], sizeof(motor_last[i]) - 1);
        motor_last[i][sizeof(motor_last[i]) - 1] = '\0';
        y = (uint16)(y + MOTOR_LINE_LH);
    }
    motor_text_ok = 1;
}

void menu_motor_draw (uint8 page)
{
    motor_hw_init();
    motor_build_lines(page);
    motor_flush((MENU_MOTOR_PAGE_ENC == page) ? ("编码器") : ("电机输出"), 1);
}

void menu_motor_enter (uint8 page)
{
    motor_hw_init();
    motor_stop();                                               // 进页面先停机，别把上一次的占空比留在轮子上
    motor_build_lines(page);
    motor_flush((MENU_MOTOR_PAGE_ENC == page) ? ("编码器") : ("电机输出"), 1);
}

void menu_motor_live (uint8 page)
{
    motor_build_lines(page);                                    // 只补画变了的行（速度 / 脉冲在变）
    motor_flush((MENU_MOTOR_PAGE_ENC == page) ? ("编码器") : ("电机输出"), 0);
}

uint8 menu_motor_key (uint8 page, uint8 k_up, uint8 k_down, uint8 k_ok, uint8 k_back)
{
    // menu_ui 的主循环每圈都会调这个函数：一个键都没动就直接返回，别每圈重画整屏
    if ((0 == k_up) && (0 == k_down) && (0 == k_ok) && (0 == k_back)) { return 0; }

    if (0 != k_back)
    {
        motor_stop();                                           // 退出前一定停
        return 1;
    }

    if (0 != k_ok)
    {
        if (MENU_MOTOR_PAGE_MOTOR == page)                      // 电机页：两轮 -> 左轮 -> 右轮
        {
            motor_pick = (uint8)((motor_pick + 1) % 3);
        }
        else                                                    // 编码器页：清零
        {
            imu_encoder_clear();
        }
    }

    if (0 != k_up)
    {
        motor_duty = (int16)(motor_duty + MOTOR_DUTY_STEP);
        if (motor_duty > PWM_DUTY_MAX) { motor_duty = PWM_DUTY_MAX; }
        motor_push();
    }
    if (0 != k_down)
    {
        motor_duty = (int16)(motor_duty - MOTOR_DUTY_STEP);
        if (motor_duty < 0) { motor_duty = 0; }
        motor_push();
    }

    motor_build_lines(page);
    motor_flush((MENU_MOTOR_PAGE_ENC == page) ? ("编码器") : ("电机输出"), 0);
    return 0;
}
