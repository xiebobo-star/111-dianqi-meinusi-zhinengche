/* TC264D 白色宽路面循迹 + BDS300 转向 PD + 双编码器速度 PI。
 * Default: vision-only drive, stopped until key3; key4 stops. See car_config.h.
 * 调试模式：键1/2曝光，键3阈值，键4显示模式。
 * 行驶模式：键3启动/停止，键4停车（中断读取），键1/2仅停车时调曝光。
 * 详见 patrol/ADAPTATION.md。源码保持 GBK。 */
#include "zf_common_headfile.h"
#include "car_board.h"
#include <string.h>
#include "car_image.h"                                          // 巡线识别模块
#pragma section all "cpu0_dsram"
// 将本语句与#pragma section all restore语句之间的全局变量都放在CPU0的RAM中

//================================================== 可调参数（只改这一段） ==================================================
#define CAM_USE_IPS200              (CAR_USE_IPS200)                         // 1 = 2 寸 IPS200（240x320）
                                                                // 0 = 1.8 寸 TFT180（128x160）
#define CAM_EXP_DEFAULT             (512)                       // 上电时的曝光时间
#define CAM_EXP_STEP                (64)                        // 每按一次曝光改变多少
#define CAM_EXP_MIN                 (20)                        // 曝光下限
#define CAM_EXP_MAX                 (1500)                      // 曝光上限
#define CAM_TH_STEP                 (10)                        // 按键3 每次加多少阈值
#define CAM_TH_MANUAL_MAX           (240)                       // 手动阈值上限，超过就回到自动

#define CAM_KEY1_PIN                (P20_6)                     // 按键1：曝光 +
#define CAM_KEY2_PIN                (P20_7)                     // 按键2：曝光 -
#define CAM_KEY3_PIN                (P11_2)                     // 按键3：阈值 +
#define CAM_KEY4_PIN                (P11_3)                     // 按键4：灰度/二值化
#define CAM_KEY_DEBOUNCE_CNT        (3)                         // 连续几次读到同一个状态才算数（主循环约 20ms 一圈）
#define CAM_LED1_PIN                (P20_9)                     // 核心板 LED1：1 秒翻转一次，表示程序活着

//============================================ 屏幕宏（IPS200 / TFT180 一套代码） ============================================
#if (CAM_USE_IPS200)
    #define CAM_IMG_DIS_W           (MT9V03X_W)                 // 240 宽的屏放得下原图，不缩放
    #define CAM_IMG_DIS_H           (MT9V03X_H)
    #define CAM_TXT_Y0              (124)                       // 文字第一行的 y
    #define CAM_TXT_STEP            (18)                        // 文字行间距
    #define CAM_SCREEN_INIT()           ips200_init(IPS200_TYPE_SPI)
    #define CAM_SCREEN_CLEAR()          ips200_clear()
    #define CAM_SCREEN_FONT()           ips200_set_font(IPS200_8X16_FONT)
    #define CAM_SCREEN_COLOR(p, b)      ips200_set_color((p), (b))
    #define CAM_SCREEN_STRING(x, y, s)  ips200_show_string((x), (y), (s))
    #define CAM_SCREEN_IMAGE(p, th)     ips200_show_gray_image(0, 0, (p), MT9V03X_W, MT9V03X_H, CAM_IMG_DIS_W, CAM_IMG_DIS_H, (th))
    #define CAM_SCREEN_POINT(x, y, c)   ips200_draw_point((x), (y), (c))
#else
    #define CAM_IMG_DIS_W           (126)                       // 128 宽的屏放不下 188，等比缩小
    #define CAM_IMG_DIS_H           (80)
    #define CAM_TXT_Y0              (84)
    #define CAM_TXT_STEP            (16)
    #define CAM_SCREEN_INIT()           tft180_init()
    #define CAM_SCREEN_CLEAR()          tft180_clear()
    #define CAM_SCREEN_FONT()           tft180_set_font(TFT180_8X16_FONT)
    #define CAM_SCREEN_COLOR(p, b)      tft180_set_color((p), (b))
    #define CAM_SCREEN_STRING(x, y, s)  tft180_show_string((x), (y), (s))
    #define CAM_SCREEN_IMAGE(p, th)     tft180_show_gray_image(0, 0, (p), MT9V03X_W, MT9V03X_H, CAM_IMG_DIS_W, CAM_IMG_DIS_H, (th))
    #define CAM_SCREEN_POINT(x, y, c)   tft180_draw_point((x), (y), (c))
#endif

// 图像坐标 -> 屏幕坐标（IPS200 是 1:1，TFT180 要等比缩小）
#define CAM_PT_X(x)                 ((uint16)((uint32)(x) * CAM_IMG_DIS_W / MT9V03X_W))
#define CAM_PT_Y(y)                 ((uint16)((uint32)(y) * CAM_IMG_DIS_H / MT9V03X_H))

//====================================================== 全局变量 ======================================================
static uint16 cam_exp         = CAM_EXP_DEFAULT;                // 当前曝光
#if !CAR_DRIVE_ENABLE
static uint16 cam_manual_th   = 0;                              // 0 = 自动阈值（大津法）
#endif
static uint8  cam_show_bin    = 0;                               // 0 = 显示灰度图   1 = 显示二值化图
static uint16 cam_fps         = 0;                              // 最近一秒的帧数
static uint8  cam_mean        = 0;                              // 图像灰度均值（判断曝光够不够用）
static uint32 cam_frame_cnt   = 0;
static uint32 cam_fps_t0      = 0;
static uint8  cam_led_level   = 0;

static uint8 cam_frame[MT9V03X_H][MT9V03X_W];
static uint32 cam_display_ms;
static car_control cam_control;
static car_image_info_struct  cam_info;                         // <<< 识别结果全在这里

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     4 个按键初始化（内部上拉，按下读到低电平）
// 使用示例     cam_key_init();
//-------------------------------------------------------------------------------------------------------------------
static void cam_key_init (void)
{
    gpio_init(CAM_KEY1_PIN, GPI, GPIO_HIGH, GPI_PULL_UP);
    gpio_init(CAM_KEY2_PIN, GPI, GPIO_HIGH, GPI_PULL_UP);
    gpio_init(CAM_KEY3_PIN, GPI, GPIO_HIGH, GPI_PULL_UP);
    gpio_init(CAM_KEY4_PIN, GPI, GPIO_HIGH, GPI_PULL_UP);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     按键消抖 + 边沿检测
// 参数说明     index        第几个键（0~3）
// 参数说明     raw_pressed  本次原始读数：1 = 按下  0 = 松开
// 返回参数     uint8        1 = 刚被按下这一下   0 = 其他
//-------------------------------------------------------------------------------------------------------------------
static uint8 cam_key_edge (uint8 index, uint8 raw_pressed)
{
    static uint8 stable[4] = {0, 0, 0, 0};
    static uint8 count[4]  = {0, 0, 0, 0};
    uint8 ret = 0;

    if(raw_pressed == stable[index])
    {
        count[index] = 0;                                       // 状态没变，重新计数
    }
    else
    {
        count[index] ++;
        if(count[index] >= CAM_KEY_DEBOUNCE_CNT)                // 连续 N 次都是新状态 -> 认定状态改变
        {
            count[index]  = 0;
            stable[index] = raw_pressed;
            if(raw_pressed)
            {
                ret = 1;                                        // 只在"刚按下"这一下返回 1，按住不会一直触发
            }
        }
    }
    return ret;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     图像灰度均值（隔行隔列抽样，算得快）—— 用来判断曝光够不够
//-------------------------------------------------------------------------------------------------------------------
static uint8 cam_image_mean (const uint8 *image)
{
    uint32 sum = 0;
    uint32 num = 0;
    uint16 row;
    uint16 col;

    for(row = 0; row < MT9V03X_H; row += 4)
    {
        for(col = 0; col < MT9V03X_W; col += 4)
        {
            sum += image[(uint32)row * MT9V03X_W + col];
            num ++;
        }
    }
    return (uint8)(sum / num);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     把识别结果（每行中线）画到屏上
// 参数说明     info         识别结果
// 说明         绿点 = 实测中线   黄点 = 补出来的   红点 = 丢线（沿用了下方的中线）
//-------------------------------------------------------------------------------------------------------------------
static void cam_draw_overlay (const car_image_info_struct *info)
{
    uint16 n;
    uint16 y;
    uint16 color;                                               // 颜色是 16 位 RGB565，别用 uint8

    for(n = 0; n < CAR_IMG_SCAN_ROWS; n ++)
    {
        y = (uint16)n * CAR_IMG_SCAN_STEP + (CAR_IMG_SCAN_STEP - 1);

        if(CAR_ROW_FLAG_REAL == info->row_flag[n])
        {
            color = RGB565_GREEN;
        }
        else if(CAR_ROW_FLAG_FIX == info->row_flag[n])
        {
            color = RGB565_YELLOW;
        }
        else
        {
            color = RGB565_RED;
        }

        CAM_SCREEN_POINT(CAM_PT_X(info->center[n]), CAM_PT_Y(y), color);
    }

    // 图像正中心参考线（蓝色虚线）：中线跑到它右边，说明车偏左了
    for(y = 0; y < MT9V03X_H; y += 4)
    {
        CAM_SCREEN_POINT(CAM_PT_X(CAR_IMG_CENTER), CAM_PT_Y(y), RGB565_BLUE);
    }
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     刷新屏幕下方的四行文字
//-------------------------------------------------------------------------------------------------------------------
static void cam_screen_refresh (const car_image_info_struct *info)
{
    char   txt[24];
    uint16 y = CAM_TXT_Y0;

    sprintf(txt, "TH:%3d FPS:%2d", (int)info->threshold, (int)cam_fps);
    CAM_SCREEN_STRING(0, y, txt);
    y += CAM_TXT_STEP;

    sprintf(txt, "E:%4d N:%5d", (int)info->err, (int)info->err_norm);
    CAM_SCREEN_STRING(0, y, txt);
    y += CAM_TXT_STEP;

    sprintf(txt, "ROW:%2d/%2d", (int)info->valid_rows, (int)info->lost_rows);
    CAM_SCREEN_STRING(0, y, txt);
    y += CAM_TXT_STEP;

    sprintf(txt, "Q:%3d %s", (int)info->confidence,
            CAR_DRIVE_ENABLE ? car_control_state_name(cam_control.state) : car_image_state_name(info->state));
    CAM_SCREEN_STRING(0, y, txt);
}

// **************************** 代码区域 ****************************
int core0_main(void)
{
    uint32 now;
    uint32 frame_ms;

    clock_init();                                               // 获取时钟频率<务必保留>
    debug_init();                                               // 初始化默认调试串口
    system_start();                                             // 启动计时（算帧率用）

    // ------------------------- 1. 先让"程序跑起来了"看得见 -------------------------
    gpio_init(CAM_LED1_PIN, GPO, GPIO_HIGH, GPO_PUSH_PULL);      // 核心板 LED 低电平点亮
    gpio_set_level(CAM_LED1_PIN, 1);                            // 先灭

    CAM_SCREEN_INIT();
    CAM_SCREEN_CLEAR();
    CAM_SCREEN_FONT();
    CAM_SCREEN_COLOR(RGB565_RED, RGB565_WHITE);

    cam_key_init();
    car_board_init();
    car_image_init();                                           // 识别模块初始化

    // ------------------------- 2. 摄像头初始化 -------------------------
    CAM_SCREEN_STRING(0, 0, "camera init ...");
    printf("\r\n===== patrol: camera + car_image =====\r\n");

    if(mt9v03x_init())                                          // 0 = 成功   1 = 失败
    {
        CAM_SCREEN_CLEAR();
        CAM_SCREEN_STRING(0, 40,  "CAMERA INIT FAIL");
        CAM_SCREEN_STRING(0, 64,  "1 check 3V3CAM LED");
        CAM_SCREEN_STRING(0, 84,  "2 swap TXD and RXD");
        CAM_SCREEN_STRING(0, 104, "3 check D0-D7 order");
        printf("mt9v03x_init() FAIL: check 3.3V power and TXD/RXD wiring\r\n");

        while(TRUE)                                             // 初始化失败就停在这里闪灯
        {
            gpio_set_level(CAM_LED1_PIN, 0);
            system_delay_ms(80);
            gpio_set_level(CAM_LED1_PIN, 1);
            system_delay_ms(80);
        }
    }

    mt9v03x_set_exposure_time(cam_exp);                         // 把初始曝光写进摄像头
    cam_fps_t0 = system_getval_ms();

    CAM_SCREEN_CLEAR();
    printf("camera ok: %dx%d  exp=%d\r\n", MT9V03X_W, MT9V03X_H, cam_exp);
    printf("mode=%s  line=%s\r\n",
           (CAR_IMG_MODE == CAR_IMG_MODE_SINGLE) ? "single" : "double",
           (CAR_IMG_LINE_DARK) ? "dark" : "bright");

#if CAR_DRIVE_ENABLE && CAR_RING_ENABLE
    CAM_SCREEN_STRING(0, 40, "IMU: HOLD STILL 2s");
    if(!car_board_imu_init())
        CAM_SCREEN_STRING(0, 64, "IMU FAIL: START LOCKED");
#elif CAR_VISION_ONLY
    CAM_SCREEN_STRING(0, 40, "VISION / OPEN PWM");
    printf("control=VISION_ONLY feedback=NONE drive=%d key3=start key4=stop\r\n", CAR_DRIVE_ENABLE);
#endif

    cpu_wait_event_ready();                                     // 等待所有核心初始化完毕

    pit_ms_init(CCU60_CH0, CAR_CONTROL_MS);

    while(TRUE)
    {
        car_board_status(&cam_control);
        // ------------------------- 3. 按键 -------------------------
        if(cam_key_edge(0, (uint8)(gpio_get_level(CAM_KEY1_PIN) == 0)) && cam_control.state != CAR_RUNNING)       // 按键1：曝光 +
        {
            cam_exp = (cam_exp + CAM_EXP_STEP > CAM_EXP_MAX) ? CAM_EXP_MAX : (cam_exp + CAM_EXP_STEP);
            mt9v03x_set_exposure_time(cam_exp);
            printf("exp = %d\r\n", cam_exp);
        }
        if(cam_key_edge(1, (uint8)(gpio_get_level(CAM_KEY2_PIN) == 0)) && cam_control.state != CAR_RUNNING)       // 按键2：曝光 -
        {
            cam_exp = (cam_exp < CAM_EXP_MIN + CAM_EXP_STEP) ? CAM_EXP_MIN : (cam_exp - CAM_EXP_STEP);
            mt9v03x_set_exposure_time(cam_exp);
            printf("exp = %d\r\n", cam_exp);
        }
        if(cam_key_edge(2, (uint8)(gpio_get_level(CAM_KEY3_PIN) == 0)))
        {
#if CAR_DRIVE_ENABLE
            if(cam_control.state == CAR_RUNNING) car_board_stop();
            else printf("start:%d (need fresh good frames)\r\n", car_board_start());
#else
            if(!cam_manual_th) cam_manual_th=car_image_get_threshold()+CAM_TH_STEP;
            else cam_manual_th+=CAM_TH_STEP;
            if(cam_manual_th>CAM_TH_MANUAL_MAX) cam_manual_th=0;
            car_image_set_threshold(cam_manual_th);
#endif
        }
        if(cam_key_edge(3, (uint8)(gpio_get_level(CAM_KEY4_PIN) == 0)))
        {
#if CAR_DRIVE_ENABLE
            car_board_stop();
#else
            cam_show_bin=!cam_show_bin;
#endif
        }

        // ------------------------- 4. 有新图就识别 + 显示 -------------------------
        if(mt9v03x_finish_flag)
        {
            /* ISR skips VSYNC while finish_flag is set. Copy stable DMA data
             * before releasing capture; display/vision use only this snapshot. */
            frame_ms = car_capture_ms;
            memcpy(cam_frame, mt9v03x_image, sizeof(cam_frame));
            mt9v03x_finish_flag = 0;
            car_board_status(&cam_control);
            car_image_set_route(car_control_route(&cam_control));
            car_image_process_gray(cam_frame[0], &cam_info);
            car_board_vision(&cam_info, frame_ms);
            car_board_status(&cam_control);

            cam_frame_cnt ++;
            now = system_getval_ms();
            if((now - cam_fps_t0) >= 1000)                      // 每满一秒统计一次帧率
            {
                cam_fps       = (uint16)cam_frame_cnt;
                cam_frame_cnt = 0;
                cam_fps_t0    = now;
                cam_led_level = !cam_led_level;
                gpio_set_level(CAM_LED1_PIN, cam_led_level);    // 1 秒闪一次，表示程序活着
            }

            if(now-cam_display_ms >= 200)
            {
                cam_display_ms=now;
                cam_mean=cam_image_mean(cam_frame[0]);
                CAM_SCREEN_IMAGE(cam_frame[0], (cam_show_bin ? cam_info.threshold : 0));
                cam_draw_overlay(&cam_info);
                cam_screen_refresh(&cam_info);
#if CAR_VISION_ONLY
                printf("mode:VISION th:%u e:%d bend:%d q:%u junction:%u ctl:%s speed:NA duty_target:%d pwm:%u/%u us:%u\r\n",
                       cam_info.threshold, cam_info.err_norm, cam_info.bend_norm,
                       cam_info.confidence, cam_info.junction, car_control_state_name(cam_control.state),
                       (int)cam_control.speed_target, cam_control.pwm_l, cam_control.pwm_r, cam_control.servo_us);
#else
                printf("th:%u e:%d bend:%d q:%u junction:%u ctl:%s v:%d/%d target:%d pwm:%u/%u us:%u\r\n",
                       cam_info.threshold, cam_info.err_norm, cam_info.bend_norm,
                       cam_info.confidence, cam_info.junction, car_control_state_name(cam_control.state),
                       (int)cam_control.speed_l, (int)cam_control.speed_r, (int)cam_control.speed_target,
                       cam_control.pwm_l, cam_control.pwm_r, cam_control.servo_us);
                printf("ring:%s dir:%d yaw:%d lap:%u imu:%u\r\n",
                       car_ring_state_name(cam_control.ring.state), cam_control.ring.direction,
                       (int)cam_control.ring.yaw_deg, cam_control.ring.completed, cam_control.ring.imu_ready);
#endif
            }
        }
    }
}
#pragma section all restore
// **************************** 代码区域 ****************************
