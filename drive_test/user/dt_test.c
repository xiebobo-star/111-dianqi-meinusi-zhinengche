/*********************************************************************************************************************
* 文件名            dt_test.c
* 说明              硬件驱动自测主程序
*                   一个模块一个测试页：按键选、屏幕看、串口也能看（屏幕没亮的时候照样能测）
* 按键约定          按键1=上  按键2=下  按键3=确定  按键4=返回（测试页里长按按键4 退出）
* 安全提醒          测电机之前把车轮架起来，别让车窜出去
********************************************************************************************************************/
#include "zf_common_headfile.h"
#include "dt_config.h"
#include "dt_display.h"
#include "dt_test.h"

// ==================================================== 全局变量 =======================================================
volatile uint32 dt_ms = 0;                                   // 5ms 定时中断里累加（在 isr.c 里）

#if DT_SERIAL_DEBUG
#define DT_LOG(...)                 printf(__VA_ARGS__)
#else
#define DT_LOG(...)                 ((void)0)
#endif

#define DT_MENU_VISIBLE             (5)
#define DT_RESULT_PASS              (0)
#define DT_RESULT_FAIL              (1)

// ==================================================== 按键小工具 =====================================================
//-------------------------------------------------------------------------------------------------------------------
// 函数简介      短按一次（读一次就清状态，长按不会误触发）
//-------------------------------------------------------------------------------------------------------------------
static uint8 dt_key_hit (key_index_enum key)
{
    if(KEY_SHORT_PRESS == key_get_state(key))
    {
        key_clear_state(key);
        return 1;
    }
    return 0;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介      长按一次（按住只触发一次，松开之后才能再次触发）
//-------------------------------------------------------------------------------------------------------------------
static uint8 dt_key_hold (key_index_enum key)
{
    static uint8 latch[KEY_NUMBER] = {0, 0, 0, 0};

    if(KEY_LONG_PRESS == key_get_state(key))
    {
        if(0 == latch[key])
        {
            latch[key] = 1;
            key_clear_state(key);
            return 1;
        }
    }
    else
    {
        latch[key] = 0;
    }
    return 0;
}

// ==================================================== 页面通用 =======================================================
static void dt_page_begin (const char *title)
{
    dt_display_color(DT_COLOR_TEXT, DT_COLOR_BG);
    dt_display_clear();
    dt_display_cn(0, 0, title);
    dt_display_line(0, 18, dt_display_width() - 1, 18, DT_COLOR_LINE);
}

static void dt_page_hint (const char *hint)
{
    dt_display_color(DT_COLOR_TEXT, DT_COLOR_BG);
    dt_display_cn(0, 142, hint);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介      在指定行显示 通过 / 失败
//-------------------------------------------------------------------------------------------------------------------
static void dt_page_result (uint16 x, uint16 y, uint8 result)
{
    if(DT_RESULT_PASS == result)
    {
        dt_display_color(DT_COLOR_OK, DT_COLOR_BG);
        dt_display_cn(x, y, "通过");
    }
    else
    {
        dt_display_color(DT_COLOR_ERR, DT_COLOR_BG);
        dt_display_cn(x, y, "失败");
    }
    dt_display_color(DT_COLOR_TEXT, DT_COLOR_BG);
}

// ==================================================== 自动检测子函数 =================================================
// 这几个函数被单独的测试页和「全部自检」共用，省得同一段逻辑写两遍

//-------------------------------------------------------------------------------------------------------------------
// 函数简介      陀螺仪自检
// 返回参数      DT_RESULT_PASS / DT_RESULT_FAIL
//-------------------------------------------------------------------------------------------------------------------
static uint8 dt_check_imu (void)
{
#if (DT_IMU_TYPE == DT_IMU_NONE)
    return DT_RESULT_FAIL;
#elif (DT_IMU_TYPE == DT_IMU_TYPE_660RA)
    return (0 == imu660ra_init()) ? DT_RESULT_PASS : DT_RESULT_FAIL;
#else
    return (0 == icm20602_init()) ? DT_RESULT_PASS : DT_RESULT_FAIL;
#endif
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介      摄像头自检：初始化 + 等一帧图像
// 参数说明      timeout_ms  等图像的超时时间
//-------------------------------------------------------------------------------------------------------------------
static uint8 dt_check_camera (uint32 timeout_ms)
{
#if DT_CAMERA_ENABLE
    uint32 t0 = 0;
    uint8  ret = 0;

    ret = mt9v03x_init();
    if(0 != ret)
    {
        return DT_RESULT_FAIL;
    }
    mt9v03x_finish_flag = 0;
    t0 = dt_ms;
    while((dt_ms - t0) < timeout_ms)
    {
        if(0 != mt9v03x_finish_flag)
        {
            mt9v03x_finish_flag = 0;
            return DT_RESULT_PASS;
        }
        system_delay_ms(5);
    }
    return DT_RESULT_FAIL;
#else
    (void)timeout_ms;
    return DT_RESULT_FAIL;
#endif
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介      测距模块自检：初始化 + 读一个合理距离
// 参数说明      distance_mm  读到的距离（可以为 NULL）
//-------------------------------------------------------------------------------------------------------------------
static uint8 dt_check_tof (uint16 *distance_mm)
{
#if (DT_TOF_TYPE == DT_TOF_NONE)
    (void)distance_mm;
    return DT_RESULT_FAIL;
#else
    uint32 t0   = 0;
    uint8  ret  = 0;
    uint8  res  = DT_RESULT_FAIL;
    uint16 dist = 0;

#if (DT_TOF_TYPE == DT_TOF_TYPE_DL1A)
    ret = dl1a_init();
    dl1a_finsh_flag = 0;
#else
    ret = dl1b_init();
    dl1b_finsh_flag = 0;
#endif
    if(0 == ret)
    {
        t0 = dt_ms;
        while((dt_ms - t0) < 600)
        {
#if (DT_TOF_TYPE == DT_TOF_TYPE_DL1A)
            dl1a_get_distance();
            dist = dl1a_distance_mm;
#else
            dl1b_get_distance();
            dist = dl1b_distance_mm;
#endif
            if((dist > 10) && (dist < 4000))
            {
                res = DT_RESULT_PASS;
                break;
            }
            system_delay_ms(20);
        }
    }
    if(NULL != distance_mm)
    {
        *distance_mm = dist;
    }
    return res;
#endif
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介      片内 Flash 自检：写一个值再读回来比对
// 参数说明      read_value  读回来的值（可以为 NULL）
//-------------------------------------------------------------------------------------------------------------------
static uint8 dt_check_flash (uint32 *read_value)
{
    const uint32 write_value = 0x1234ABCD;
    uint32 read_back = 0;

    if(flash_check(DT_FLASH_SECTION, DT_FLASH_PAGE))
    {
        flash_erase_page(DT_FLASH_SECTION, DT_FLASH_PAGE);
    }
    flash_buffer_clear();
    flash_union_buffer[0].uint32_type = write_value;
    flash_write_page_from_buffer(DT_FLASH_SECTION, DT_FLASH_PAGE);
    flash_buffer_clear();
    flash_read_page_to_buffer(DT_FLASH_SECTION, DT_FLASH_PAGE);
    read_back = flash_union_buffer[0].uint32_type;

    if(NULL != read_value)
    {
        *read_value = read_back;
    }
    return (write_value == read_back) ? DT_RESULT_PASS : DT_RESULT_FAIL;
}

// ==================================================== 1 LED 灯 =======================================================
static void dt_test_led (void)
{
    uint8  led_on = 0;
    uint32 t_last = 0;

    DT_LOG("\r\n[LED] 核心板 LED1(P20_9) 与 LED2(P20_8) 应该交替闪；不闪就是这俩脚或 LED 有问题\r\n");

    gpio_init(DT_LED1_PIN, GPO, GPIO_HIGH, GPO_PUSH_PULL);
    gpio_init(DT_LED2_PIN, GPO, GPIO_HIGH, GPO_PUSH_PULL);

    dt_page_begin("LED灯");
    dt_display_cn(0, 22, "灯1");
    dt_display_cn(0, 40, "灯2");
    dt_display_cn(0, 76, "两个灯交替闪");
    dt_page_hint("长按4返回");

    t_last = dt_ms;
    while(1)
    {
        if((dt_ms - t_last) >= 250)
        {
            t_last = dt_ms;
            led_on = !led_on;
            gpio_set_level(DT_LED1_PIN, (led_on) ? DT_LED_ON_LEVEL : !DT_LED_ON_LEVEL);
            gpio_set_level(DT_LED2_PIN, (led_on) ? !DT_LED_ON_LEVEL : DT_LED_ON_LEVEL);

            dt_display_color(DT_COLOR_TEXT, DT_COLOR_BG);
            dt_display_cn(48, 22, (led_on) ? "亮" : "灭");
            dt_display_cn(48, 40, (led_on) ? "灭" : "亮");
        }
        if(dt_key_hold(KEY_4))
        {
            break;
        }
        system_delay_ms(5);
    }

    gpio_set_level(DT_LED1_PIN, !DT_LED_ON_LEVEL);
    gpio_set_level(DT_LED2_PIN, !DT_LED_ON_LEVEL);
    DT_LOG("[LED] 结束\r\n");
}

// ==================================================== 2 按键 =========================================================
static void dt_test_key (void)
{
    static const char *state_name[3] = {"释放", "短按", "长按"};
    static const char *key_name[4]   = {"按键1", "按键2", "按键3", "按键4"};
    uint8 last_state[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    uint8 last_level[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    uint8 i = 0;

    DT_LOG("\r\n[按键] 逐个按一下，屏幕和串口会显示 释放/短按/长按；长按按键4 退出\r\n");
    DT_LOG("[按键] 引脚对照：按键1=P20_6  按键2=P20_7  按键3=P11_2  按键4=P11_3\r\n");

    dt_page_begin("按键");
    for(i = 0; i < KEY_NUMBER; i++)
    {
        dt_display_cn(0, 22 + i * 18, key_name[i]);
    }
    dt_display_cn(0, 100, "电平");
    dt_page_hint("长按4退出");

    key_clear_all_state();
    while(1)
    {
        for(i = 0; i < KEY_NUMBER; i++)
        {
            key_state_enum st = key_get_state((key_index_enum)i);
            if((uint8)st != last_state[i])
            {
                dt_display_color(DT_COLOR_TEXT, DT_COLOR_BG);
                dt_display_cn(64, 22 + i * 18, state_name[(uint8)st]);
                DT_LOG("[按键] %s -> %s\r\n", key_name[i], state_name[(uint8)st]);
                last_state[i] = (uint8)st;
            }
        }
        {
            // 原始电平：1 = 松开（内部上拉），0 = 按下。按键没反应就看这 4 个数字动不动
            uint8 lv[4];
            char  lv_str[5];
            lv[0] = gpio_get_level(DT_KEY1_PIN);
            lv[1] = gpio_get_level(DT_KEY2_PIN);
            lv[2] = gpio_get_level(DT_KEY3_PIN);
            lv[3] = gpio_get_level(DT_KEY4_PIN);
            lv_str[0] = (lv[0]) ? '1' : '0';
            lv_str[1] = (lv[1]) ? '1' : '0';
            lv_str[2] = (lv[2]) ? '1' : '0';
            lv_str[3] = (lv[3]) ? '1' : '0';
            lv_str[4] = '\0';
            if((lv[0] != last_level[0]) || (lv[1] != last_level[1]) ||
               (lv[2] != last_level[2]) || (lv[3] != last_level[3]))
            {
                dt_display_color(DT_COLOR_TEXT, DT_COLOR_BG);
                dt_display_string(48, 100, lv_str);
                DT_LOG("[按键] 原始电平 %s (1=松开 0=按下)\r\n", lv_str);
                last_level[0] = lv[0];
                last_level[1] = lv[1];
                last_level[2] = lv[2];
                last_level[3] = lv[3];
            }
        }
        if(dt_key_hold(KEY_4))
        {
            break;
        }
        system_delay_ms(10);
    }
    DT_LOG("[按键] 结束\r\n");
}

// ==================================================== 3 屏幕 =========================================================
static void dt_test_display (void)
{
    static const uint16 color_list[5] = {RGB565_RED, RGB565_GREEN, RGB565_BLUE, RGB565_WHITE, RGB565_BLACK};
    static const char  *name_list[5]  = {"红", "绿", "蓝", "白", "黑"};
    uint8  i  = 0;
    uint32 t_last = 0;

    DT_LOG("\r\n[屏幕] 应该依次整屏 红 绿 蓝 白 黑；颜色不对、花屏、半屏 -> 检查屏型号(IPS200/TFT180)和排线\r\n");

    t_last = dt_ms;
    while(1)
    {
        if((dt_ms - t_last) >= 800)
        {
            t_last = dt_ms;
            dt_display_full(color_list[i]);
            dt_display_color((4 == i) ? RGB565_WHITE : RGB565_BLACK, color_list[i]);
            dt_display_cn(40, 60, name_list[i]);
            dt_display_cn(0, 84, "颜色测试");
            DT_LOG("[屏幕] 整屏 %s\r\n", name_list[i]);
            i = (uint8)((i + 1) % 5);
        }
        if(dt_key_hold(KEY_4))
        {
            break;
        }
        system_delay_ms(10);
    }
    dt_display_color(DT_COLOR_TEXT, DT_COLOR_BG);
    dt_display_clear();
    DT_LOG("[屏幕] 结束\r\n");
}

// ==================================================== 4 串口 =========================================================
static void dt_test_uart (void)
{
    uint8  buf[32];
    uint32 got    = 0;
    uint32 t_last = 0;
    uint32 n      = 0;

    DT_LOG("\r\n[串口] 波特率 115200 8N1。能看到这行字，说明串口发数据是通的\r\n");

    dt_page_begin("串口");
    dt_display_cn(0, 22, "回环");
    dt_display_cn(0, 58, "TX和RX短接测回环");
    dt_display_cn(0, 76, "波特率115200");
    dt_page_hint("长按4返回");

    t_last = dt_ms;
    while(1)
    {
        if((dt_ms - t_last) >= 500)
        {
            t_last = dt_ms;
            printf("DT-UART-LOOP\r\n");
            n = debug_read_ring_buffer(buf, sizeof(buf));
            while(n > 0)
            {
                got += n;
                n = debug_read_ring_buffer(buf, sizeof(buf));
            }
            dt_display_color(DT_COLOR_TEXT, DT_COLOR_BG);
            dt_display_uint(64, 22, got, 6);
        }
        if(dt_key_hold(KEY_4))
        {
            break;
        }
        system_delay_ms(10);
    }
    DT_LOG("[串口] 收到回环字节数 %lu\r\n", got);
}

// ==================================================== 5 电机 =========================================================
static void dt_motor_hw_init (void)
{
#if (DT_MOTOR_MODE == DT_MOTOR_MODE_DUAL_PWM)
    pwm_init(DT_MOTOR_L_PWM,  DT_MOTOR_PWM_FREQ, 0);
    pwm_init(DT_MOTOR_L_PWM2, DT_MOTOR_PWM_FREQ, 0);
    pwm_init(DT_MOTOR_R_PWM,  DT_MOTOR_PWM_FREQ, 0);
    pwm_init(DT_MOTOR_R_PWM2, DT_MOTOR_PWM_FREQ, 0);
#else
    pwm_init(DT_MOTOR_L_PWM, DT_MOTOR_PWM_FREQ, 0);
    pwm_init(DT_MOTOR_R_PWM, DT_MOTOR_PWM_FREQ, 0);
    gpio_init(DT_MOTOR_L_DIR, GPO, GPIO_LOW, GPO_PUSH_PULL);
    gpio_init(DT_MOTOR_R_DIR, GPO, GPIO_LOW, GPO_PUSH_PULL);
#endif
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介      给某个轮子输出占空比
// 参数说明      index  0=左轮 1=右轮
// 参数说明      duty   -10000 ~ +10000，正数前进，0 停
//-------------------------------------------------------------------------------------------------------------------
static void dt_motor_apply (uint8 index, int16 duty)
{
    int16 d = 0;

    if(0 == index)
    {
        d = (DT_MOTOR_INVERT_L) ? (int16)(-duty) : duty;
    }
    else
    {
        d = (DT_MOTOR_INVERT_R) ? (int16)(-duty) : duty;
    }

#if (DT_MOTOR_MODE == DT_MOTOR_MODE_DUAL_PWM)
    if(0 == index)
    {
        if(d >= 0)
        {
            pwm_set_duty(DT_MOTOR_L_PWM, (uint32)d);
            pwm_set_duty(DT_MOTOR_L_PWM2, 0);
        }
        else
        {
            pwm_set_duty(DT_MOTOR_L_PWM, 0);
            pwm_set_duty(DT_MOTOR_L_PWM2, (uint32)(-d));
        }
    }
    else
    {
        if(d >= 0)
        {
            pwm_set_duty(DT_MOTOR_R_PWM, (uint32)d);
            pwm_set_duty(DT_MOTOR_R_PWM2, 0);
        }
        else
        {
            pwm_set_duty(DT_MOTOR_R_PWM, 0);
            pwm_set_duty(DT_MOTOR_R_PWM2, (uint32)(-d));
        }
    }
#else
    gpio_set_level((0 == index) ? DT_MOTOR_L_DIR : DT_MOTOR_R_DIR,
                   (d >= 0) ? DT_MOTOR_DIR_HIGH_FORWARD : !DT_MOTOR_DIR_HIGH_FORWARD);
    pwm_set_duty((0 == index) ? DT_MOTOR_L_PWM : DT_MOTOR_R_PWM, (uint32)((d >= 0) ? d : -d));
#endif
}

static void dt_motor_show (int16 *duty, uint8 sel)
{
    dt_display_color(DT_COLOR_TEXT, DT_COLOR_BG);
    dt_display_cn(0, 22, "左");
    dt_display_int(40, 22, duty[0], 6);
    dt_display_cn(0, 40, "右");
    dt_display_int(40, 40, duty[1], 6);
    dt_display_cn(0, 58, "选中");
    dt_display_cn(56, 58, (0 == sel) ? "左" : "右");
}

static void dt_test_motor (void)
{
    int16 duty[2] = {0, 0};
    uint8 sel = 0;

    DT_LOG("\r\n[电机] 测之前先把车轮架起来！按键1 加一档，按键2 减一档，按键3 切换左右轮，短按按键4 停，长按按键4 退出\r\n");

    dt_motor_hw_init();
    dt_page_begin("电机");
    dt_display_cn(0, 76, "先把车轮架起来");
    dt_display_cn(0, 94, "1加 2减 3换轮");
    dt_display_cn(0, 112, "4停 长按4退出");
    dt_motor_show(duty, sel);

    while(1)
    {
        if(dt_key_hit(KEY_1))
        {
            duty[sel] = (int16)(duty[sel] + DT_MOTOR_STEP);
            if(duty[sel] > DT_MOTOR_DUTY_LIMIT)
            {
                duty[sel] = DT_MOTOR_DUTY_LIMIT;
            }
            dt_motor_apply(sel, duty[sel]);
            dt_motor_show(duty, sel);
            DT_LOG("[电机] %s = %d\r\n", (0 == sel) ? "左轮" : "右轮", duty[sel]);
        }
        if(dt_key_hit(KEY_2))
        {
            duty[sel] = (int16)(duty[sel] - DT_MOTOR_STEP);
            if(duty[sel] < -DT_MOTOR_DUTY_LIMIT)
            {
                duty[sel] = -DT_MOTOR_DUTY_LIMIT;
            }
            dt_motor_apply(sel, duty[sel]);
            dt_motor_show(duty, sel);
            DT_LOG("[电机] %s = %d\r\n", (0 == sel) ? "左轮" : "右轮", duty[sel]);
        }
        if(dt_key_hit(KEY_3))
        {
            sel = (uint8)(1 - sel);
            dt_motor_show(duty, sel);
            DT_LOG("[电机] 当前选中 %s\r\n", (0 == sel) ? "左轮" : "右轮");
        }
        if(dt_key_hit(KEY_4))
        {
            duty[0] = 0;
            duty[1] = 0;
            dt_motor_apply(0, 0);
            dt_motor_apply(1, 0);
            dt_motor_show(duty, sel);
            DT_LOG("[电机] 两个轮子已停\r\n");
        }
        if(dt_key_hold(KEY_4))
        {
            break;
        }
        system_delay_ms(5);
    }

    dt_motor_apply(0, 0);
    dt_motor_apply(1, 0);
    DT_LOG("[电机] 结束，占空比已清零\r\n");
}

// ==================================================== 6 舵机 =========================================================
static void dt_servo_show (uint16 pulse)
{
    int32 angle = (int32)(((int32)pulse - (int32)DT_SERVO_MIN_PULSE) * 180 /
                          ((int32)DT_SERVO_MAX_PULSE - (int32)DT_SERVO_MIN_PULSE));

    dt_display_color(DT_COLOR_TEXT, DT_COLOR_BG);
    dt_display_cn(0, 22, "脉宽");
    dt_display_uint(48, 22, pulse, 4);
    dt_display_cn(0, 40, "角度");
    dt_display_int(48, 40, angle, 3);
}

static void dt_test_servo (void)
{
    uint16 pulse = DT_SERVO_MID_PULSE;

    DT_LOG("\r\n[舵机] 按键1 加脉宽，按键2 减脉宽，按键3 回中；范围 %d~%d us\r\n", DT_SERVO_MIN_PULSE, DT_SERVO_MAX_PULSE);

    pwm_init(DT_SERVO_PWM_PIN, DT_SERVO_FREQ, pulse / 2);
    dt_page_begin("舵机");
    dt_display_cn(0, 58, "1加 2减 3回中");
    dt_page_hint("长按4返回");
    dt_servo_show(pulse);

    while(1)
    {
        if(dt_key_hit(KEY_1))
        {
            pulse = (uint16)(pulse + DT_SERVO_STEP);
            if(pulse > DT_SERVO_MAX_PULSE)
            {
                pulse = DT_SERVO_MAX_PULSE;
            }
            pwm_set_duty(DT_SERVO_PWM_PIN, pulse / 2);
            dt_servo_show(pulse);
            DT_LOG("[舵机] 脉宽 %d us\r\n", pulse);
        }
        if(dt_key_hit(KEY_2))
        {
            pulse = (uint16)(pulse - DT_SERVO_STEP);
            if(pulse < DT_SERVO_MIN_PULSE)
            {
                pulse = DT_SERVO_MIN_PULSE;
            }
            pwm_set_duty(DT_SERVO_PWM_PIN, pulse / 2);
            dt_servo_show(pulse);
            DT_LOG("[舵机] 脉宽 %d us\r\n", pulse);
        }
        if(dt_key_hit(KEY_3))
        {
            pulse = DT_SERVO_MID_PULSE;
            pwm_set_duty(DT_SERVO_PWM_PIN, pulse / 2);
            dt_servo_show(pulse);
            DT_LOG("[舵机] 回中 %d us\r\n", pulse);
        }
        if(dt_key_hold(KEY_4))
        {
            break;
        }
        system_delay_ms(5);
    }

    pwm_set_duty(DT_SERVO_PWM_PIN, DT_SERVO_MID_PULSE / 2);
    DT_LOG("[舵机] 结束，已回中\r\n");
}

// ==================================================== 7 编码器 =======================================================
static void dt_test_encoder (void)
{
    int16  cnt_l   = 0;
    int16  cnt_r   = 0;
    int32  spd_l   = 0;
    int32  spd_r   = 0;
    int32  total_l = 0;
    int32  total_r = 0;
    int32  mm_l    = 0;
    int32  mm_r    = 0;
    float  k_mm    = 0.0f;
    uint32 t_last  = 0;

    DT_LOG("\r\n[编码器] 用手转车轮，左右两路的速度和计数应该跟着变；不装编码器就一直是 0\r\n");

    k_mm = 3.1415926f * DT_WHEEL_DIAMETER_MM / DT_ENC_PULSE_PER_ROUND;      // 一个脉冲对应多少毫米

    encoder_quad_init(DT_ENC_L_TIM, DT_ENC_L_A, DT_ENC_L_B);
    encoder_quad_init(DT_ENC_R_TIM, DT_ENC_R_A, DT_ENC_R_B);
    encoder_clear_count(DT_ENC_L_TIM);
    encoder_clear_count(DT_ENC_R_TIM);

    dt_page_begin("编码器");
    dt_display_cn(0, 22, "左速");
    dt_display_cn(0, 40, "右速");
    dt_display_cn(0, 58, "左计");
    dt_display_cn(0, 76, "右计");
    dt_display_cn(0, 100, "手转车轮看数值");
    dt_page_hint("长按4返回");

    t_last = dt_ms;
    while(1)
    {
        if((dt_ms - t_last) >= DT_ENC_PERIOD_MS)
        {
            t_last = dt_ms;

            cnt_l = encoder_get_count(DT_ENC_L_TIM);
            cnt_r = encoder_get_count(DT_ENC_R_TIM);
            encoder_clear_count(DT_ENC_L_TIM);
            encoder_clear_count(DT_ENC_R_TIM);

            if(DT_ENC_INVERT_L)
            {
                cnt_l = (int16)(-cnt_l);
            }
            if(DT_ENC_INVERT_R)
            {
                cnt_r = (int16)(-cnt_r);
            }

            spd_l = (int32)cnt_l * 1000 / DT_ENC_PERIOD_MS;                  // 脉冲/秒
            spd_r = (int32)cnt_r * 1000 / DT_ENC_PERIOD_MS;
            total_l += cnt_l;
            total_r += cnt_r;
            mm_l = (int32)((float)spd_l * k_mm);
            mm_r = (int32)((float)spd_r * k_mm);

            dt_display_color(DT_COLOR_TEXT, DT_COLOR_BG);
            dt_display_int(48, 22, mm_l, 6);
            dt_display_int(48, 40, mm_r, 6);
            dt_display_int(48, 58, total_l, 6);
            dt_display_int(48, 76, total_r, 6);
        }
        if(dt_key_hold(KEY_4))
        {
            break;
        }
        system_delay_ms(2);
    }
    DT_LOG("[编码器] 左累计 %d 右累计 %d\r\n", total_l, total_r);
}

// ==================================================== 8 拨码开关 =====================================================
static void dt_test_switch (void)
{
    uint8 s1 = 0xFF;
    uint8 s2 = 0xFF;
    uint8 last1 = 0xFF;
    uint8 last2 = 0xFF;

    DT_LOG("\r\n[拨码] 拨动 SWITCH1 / SWITCH2，屏幕上的 0/1 应该跟着变\r\n");

    gpio_init(DT_SWITCH1_PIN, GPI, GPIO_HIGH, GPI_PULL_UP);
    gpio_init(DT_SWITCH2_PIN, GPI, GPIO_HIGH, GPI_PULL_UP);

    dt_page_begin("拨码");
    dt_display_cn(0, 22, "开关1");
    dt_display_cn(0, 40, "开关2");
    dt_display_cn(0, 70, "拨动看0/1变化");
    dt_page_hint("长按4返回");

    while(1)
    {
        s1 = gpio_get_level(DT_SWITCH1_PIN);
        s2 = gpio_get_level(DT_SWITCH2_PIN);
        if((s1 != last1) || (s2 != last2))
        {
            dt_display_color(DT_COLOR_TEXT, DT_COLOR_BG);
            dt_display_cn(64, 22, (s1) ? "1" : "0");
            dt_display_cn(64, 40, (s2) ? "1" : "0");
            DT_LOG("[拨码] SW1=%d SW2=%d\r\n", s1, s2);
            last1 = s1;
            last2 = s2;
        }
        if(dt_key_hold(KEY_4))
        {
            break;
        }
        system_delay_ms(20);
    }
    DT_LOG("[拨码] 结束\r\n");
}

// ==================================================== 9 陀螺仪 =======================================================
static void dt_test_imu (void)
{
    uint8 res = DT_RESULT_FAIL;
    int16 ax = 0;
    int16 ay = 0;
    int16 az = 0;
    int16 gz = 0;
    uint32 t_last = 0;

    DT_LOG("\r\n[陀螺仪] 正在初始化...\r\n");
    dt_page_begin("陀螺仪");
    dt_display_cn(0, 60, "正在初始化");
    dt_page_hint("长按4返回");

    res = dt_check_imu();

    dt_display_clear();
    dt_display_color(DT_COLOR_TEXT, DT_COLOR_BG);
    dt_display_cn(0, 0, "陀螺仪");
    dt_display_line(0, 18, dt_display_width() - 1, 18, DT_COLOR_LINE);
    dt_display_cn(0, 22, "加速度X");
    dt_display_cn(0, 40, "加速度Y");
    dt_display_cn(0, 58, "加速度Z");
    dt_display_cn(0, 76, "陀螺Z");
    dt_display_cn(0, 100, "状态");
    dt_page_result(48, 100, res);
    dt_page_hint("长按4返回");

    if(DT_RESULT_PASS == res)
    {
        DT_LOG("[陀螺仪] 初始化通过，数据开始刷新(静止时 ACC Z 约等于 1g)\r\n");
    }
    else
    {
        DT_LOG("[陀螺仪] 初始化失败：查 IMU1 接口接线 / SPI0 引脚 / 型号是否选对\r\n");
    }

    t_last = dt_ms;
    while(1)
    {
        if(DT_RESULT_PASS == res)
        {
            if((dt_ms - t_last) >= 100)
            {
                t_last = dt_ms;

#if (DT_IMU_TYPE == DT_IMU_TYPE_660RA)
                imu660ra_get_acc();
                imu660ra_get_gyro();
                ax = imu660ra_acc_x;
                ay = imu660ra_acc_y;
                az = imu660ra_acc_z;
                gz = imu660ra_gyro_z;
#elif (DT_IMU_TYPE == DT_IMU_TYPE_20602)
                icm20602_get_acc();
                icm20602_get_gyro();
                ax = icm20602_acc_x;
                ay = icm20602_acc_y;
                az = icm20602_acc_z;
                gz = icm20602_gyro_z;
#else
                ax = 0;
                ay = 0;
                az = 0;
                gz = 0;
#endif
                dt_display_color(DT_COLOR_TEXT, DT_COLOR_BG);
                dt_display_int(64, 22, ax, 6);
                dt_display_int(64, 40, ay, 6);
                dt_display_int(64, 58, az, 6);
                dt_display_int(64, 76, gz, 6);
            }
        }
        if(dt_key_hold(KEY_4))
        {
            break;
        }
        system_delay_ms(5);
    }
    DT_LOG("[陀螺仪] 结束\r\n");
}

// ==================================================== 10 摄像头 ======================================================
static void dt_test_camera (void)
{
#if DT_CAMERA_ENABLE
    uint8  res    = DT_RESULT_FAIL;
    uint32 frames = 0;
    uint32 fps    = 0;
    uint32 t_fps  = 0;

    DT_LOG("\r\n[摄像头] 正在初始化...\r\n");
    dt_page_begin("摄像头");
    dt_display_cn(0, 60, "正在初始化");
    dt_page_hint("长按4返回");

    res = dt_check_camera(3000);

    dt_display_clear();
    dt_display_color(DT_COLOR_TEXT, DT_COLOR_BG);
    dt_display_cn(0, 0, "摄像头");
    dt_display_line(0, 18, dt_display_width() - 1, 18, DT_COLOR_LINE);

    if(DT_RESULT_PASS != res)
    {
        dt_display_cn(0, 40, "无图像");
        dt_display_cn(0, 60, "查摄像头排线");
        dt_display_cn(0, 78, "和主板接口1");
        dt_page_result(0, 100, res);
        dt_page_hint("长按4返回");
        DT_LOG("[摄像头] 没有图像：查摄像头接口1 的排线、摄像头有没有供电(3V3CAM)\r\n");
        while(1)
        {
            if(dt_key_hold(KEY_4))
            {
                break;
            }
            system_delay_ms(20);
        }
        DT_LOG("[摄像头] 结束\r\n");
        return;
    }

    DT_LOG("[摄像头] 初始化通过，开始显示图像\r\n");
    t_fps = dt_ms;
    while(1)
    {
        if(0 != mt9v03x_finish_flag)
        {
            mt9v03x_finish_flag = 0;
            dt_display_image(mt9v03x_image[0]);
            frames++;

            dt_display_color(RGB565_WHITE, RGB565_BLACK);
            dt_display_string(0, 86, "FPS");
            dt_display_uint(32, 86, fps, 3);
            dt_display_color(DT_COLOR_TEXT, DT_COLOR_BG);
            dt_display_cn(0, 142, "长按4返回");
        }
        if((dt_ms - t_fps) >= 1000)
        {
            fps = frames;
            frames = 0;
            t_fps = dt_ms;
            DT_LOG("[摄像头] 帧率 %lu fps\r\n", fps);
        }
        if(dt_key_hold(KEY_4))
        {
            break;
        }
        system_delay_ms(1);
    }
    dt_display_clear();
    DT_LOG("[摄像头] 结束\r\n");
#else
    dt_page_begin("摄像头");
    dt_display_cn(0, 40, "工程里没打开");
    dt_page_hint("长按4返回");
    while(1)
    {
        if(dt_key_hold(KEY_4))
        {
            break;
        }
        system_delay_ms(20);
    }
#endif
}

// ==================================================== 11 测距 ========================================================
static void dt_test_tof (void)
{
    uint16 dist   = 0;
    uint32 t_last = 0;
    uint8  count  = 0;

    DT_LOG("\r\n[测距] 正在初始化...\r\n");
    dt_page_begin("测距");
    dt_display_cn(0, 60, "正在初始化");
    dt_page_hint("长按4返回");

    (void)dt_check_tof(&dist);

    dt_display_clear();
    dt_display_color(DT_COLOR_TEXT, DT_COLOR_BG);
    dt_display_cn(0, 0, "测距");
    dt_display_line(0, 18, dt_display_width() - 1, 18, DT_COLOR_LINE);
    dt_display_cn(0, 22, "距离");
    dt_display_cn(0, 58, "单位毫米");
    dt_display_cn(0, 76, "遮挡测距头看数变");
    dt_page_hint("长按4返回");

    t_last = dt_ms;
    while(1)
    {
        if((dt_ms - t_last) >= 100)
        {
            t_last = dt_ms;
#if (DT_TOF_TYPE == DT_TOF_TYPE_DL1A)
            dl1a_get_distance();
            dist = dl1a_distance_mm;
#elif (DT_TOF_TYPE == DT_TOF_TYPE_DL1B)
            dl1b_get_distance();
            dist = dl1b_distance_mm;
#else
            dist = 0;
#endif
            count++;
            if(0 == (count % 10))
            {
                DT_LOG("[测距] %d mm\r\n", dist);
            }
            dt_display_color(DT_COLOR_TEXT, DT_COLOR_BG);
            dt_display_uint(48, 22, dist, 5);
        }
        if(dt_key_hold(KEY_4))
        {
            break;
        }
        system_delay_ms(5);
    }
    DT_LOG("[测距] 结束\r\n");
}

// ==================================================== 12 片内 Flash =================================================
static void dt_test_flash (void)
{
    uint8  res = DT_RESULT_FAIL;
    uint32 rd  = 0;

    DT_LOG("\r\n[Flash] 向扇区 %d 页 %d 写 0x1234ABCD 再读回来比对\r\n", DT_FLASH_SECTION, DT_FLASH_PAGE);

    dt_page_begin("Flash");
    dt_display_cn(0, 22, "写入");
    dt_display_string(48, 22, "0x1234ABCD");
    dt_display_cn(0, 40, "读出");

    res = dt_check_flash(&rd);

    dt_display_uint(32, 40, rd, 10);
    dt_display_cn(0, 58, "结果");
    dt_page_result(48, 58, res);
    dt_display_cn(0, 84, "掉电保存就靠它");
    dt_page_hint("长按4返回");

    if(DT_RESULT_PASS == res)
    {
        DT_LOG("[Flash] 通过，读回 0x%08lX\r\n", rd);
    }
    else
    {
        DT_LOG("[Flash] 失败，读回 0x%08lX（应为 0x1234ABCD）\r\n", rd);
    }

    while(1)
    {
        if(dt_key_hold(KEY_4))
        {
            break;
        }
        system_delay_ms(20);
    }
    DT_LOG("[Flash] 结束\r\n");
}

// ==================================================== 13 全部自检 ====================================================
static void dt_test_auto (void)
{
    uint8  r_imu   = DT_RESULT_FAIL;
    uint8  r_cam   = DT_RESULT_FAIL;
    uint8  r_tof   = DT_RESULT_FAIL;
    uint8  r_flash = DT_RESULT_FAIL;
    uint16 tof_mm  = 0;
    uint32 flash_v = 0;

    DT_LOG("\r\n========== 全部自检开始 ==========\r\n");

    dt_page_begin("全部自检");
    dt_display_cn(0, 40, "正在测陀螺仪");
    r_imu = dt_check_imu();
    DT_LOG("[自检] 陀螺仪 %s\r\n", (DT_RESULT_PASS == r_imu) ? "通过" : "失败");

    dt_display_cn(0, 40, "正在测摄像头");
    r_cam = dt_check_camera(3000);
    DT_LOG("[自检] 摄像头 %s\r\n", (DT_RESULT_PASS == r_cam) ? "通过" : "失败");

    dt_display_cn(0, 40, "正在测测距  ");
    r_tof = dt_check_tof(&tof_mm);
    DT_LOG("[自检] 测距 %s (%d mm)\r\n", (DT_RESULT_PASS == r_tof) ? "通过" : "失败", tof_mm);

    dt_display_cn(0, 40, "正在测Flash ");
    r_flash = dt_check_flash(&flash_v);
    DT_LOG("[自检] Flash %s (读回 0x%08lX)\r\n", (DT_RESULT_PASS == r_flash) ? "通过" : "失败", flash_v);

    dt_display_color(DT_COLOR_TEXT, DT_COLOR_BG);
    dt_page_begin("全部自检结果");
    dt_display_cn(0, 22, "陀螺仪");
    dt_page_result(72, 22, r_imu);
    dt_display_cn(0, 40, "摄像头");
    dt_page_result(72, 40, r_cam);
    dt_display_cn(0, 58, "测距");
    dt_page_result(72, 58, r_tof);
    dt_display_cn(0, 76, "Flash");
    dt_page_result(72, 76, r_flash);
    dt_display_cn(0, 104, "电机舵机LED屏幕");
    dt_display_cn(0, 122, "请到单项里测");
    dt_page_hint("长按4返回");

    DT_LOG("========== 全部自检结束 ==========\r\n");

    while(1)
    {
        if(dt_key_hold(KEY_4))
        {
            break;
        }
        system_delay_ms(20);
    }
}

// ==================================================== 菜单 ==========================================================
typedef struct
{
    const char *name;
    void (*func)(void);
}dt_item_struct;

static const dt_item_struct dt_items[] =
{
    {"LED灯",    dt_test_led},
    {"按键",     dt_test_key},
    {"屏幕",     dt_test_display},
    {"串口",     dt_test_uart},
    {"电机",     dt_test_motor},
    {"舵机",     dt_test_servo},
    {"编码器",   dt_test_encoder},
    {"拨码",     dt_test_switch},
    {"陀螺仪",   dt_test_imu},
    {"摄像头",   dt_test_camera},
    {"测距",     dt_test_tof},
    {"Flash",    dt_test_flash},
    {"全部自检", dt_test_auto},
};

static const uint8 dt_items_num = (uint8)(sizeof(dt_items) / sizeof(dt_items[0]));

static void dt_menu_draw (uint8 sel, uint8 top)
{
    uint8 i = 0;

    dt_display_color(DT_COLOR_TEXT, DT_COLOR_BG);
    dt_display_clear();
    dt_display_cn(0, 0, "硬件驱动自测");
    dt_display_line(0, 18, dt_display_width() - 1, 18, DT_COLOR_LINE);

    for(i = 0; i < DT_MENU_VISIBLE; i++)
    {
        uint8 index = (uint8)(top + i);
        if(index >= dt_items_num)
        {
            break;
        }
        if(index == sel)
        {
            dt_display_color(DT_COLOR_SEL, DT_COLOR_BG);
            dt_display_string(0, 24 + i * 18, ">");
            dt_display_cn(16, 24 + i * 18, dt_items[index].name);
            dt_display_color(DT_COLOR_TEXT, DT_COLOR_BG);
        }
        else
        {
            dt_display_cn(16, 24 + i * 18, dt_items[index].name);
        }
    }

    dt_display_cn(0, 124, "1上 2下 3确定");
    dt_display_cn(0, 142, "按键4返回");
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介      进入自测菜单（死循环，不会返回）
// 使用示例      cpu0_main.c 里 dt_test_run();
//-------------------------------------------------------------------------------------------------------------------
void dt_test_run (void)
{
    uint8 sel = 0;
    uint8 top = 0;

    dt_menu_draw(sel, top);

    while(1)
    {
        if(dt_key_hit(KEY_1))
        {
            if(sel > 0)
            {
                sel--;
                if(sel < top)
                {
                    top = sel;
                }
                dt_menu_draw(sel, top);
            }
        }
        else if(dt_key_hit(KEY_2))
        {
            if((uint8)(sel + 1) < dt_items_num)
            {
                sel++;
                if(sel >= (uint8)(top + DT_MENU_VISIBLE))
                {
                    top = (uint8)(sel - DT_MENU_VISIBLE + 1);
                }
                dt_menu_draw(sel, top);
            }
        }
        else if(dt_key_hit(KEY_3))
        {
            DT_LOG("\r\n---- 进入测试项：%s ----\r\n", dt_items[sel].name);
            dt_items[sel].func();
            key_clear_all_state();
            dt_menu_draw(sel, top);
        }
        system_delay_ms(10);
    }
}
