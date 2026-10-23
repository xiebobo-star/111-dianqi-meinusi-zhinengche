/*********************************************************************************************************************
* TC264 智能车 —— 巡线：图像识别测试工程（CPU0 主核）
* 文件名            cpu0_main.c
* 说明              这个工程只干一件事：让摄像头出图、把识别结果显示出来，方便你现场看识别对不对。
*
*                    真正的识别算法全在 car_image.c / car_image.h 里，本文件只负责：
*                      1) 初始化摄像头和屏幕
*                      2) 每来一帧图，调一次 car_image_process()
*                      3) 把二值化图 + 算出来的中线画到屏上，并把偏差/状态打到串口
*
*                    —— 也就是说：以后要接电机/舵机，不用改这里，读 cam_info 就行。
*
* 屏幕显示说明
*     白色图   = 摄像头原始图（灰度模式）/ 黑白图（二值化模式）
*     绿色点   = 这一行真找到线了，点的位置就是算出来的中线
*     黄色点   = 这一行只看到一条边线，另一边是按赛道宽度补出来的（双线模式）
*     红色点   = 这一行没找到线，沿用了下方最近一次的中线
*     蓝色虚线 = 图像正中心参考线（col = 94）。中线在这条线右边 -> 车偏左 -> 要往右打
*
* 按键功能
*     按键1 = 曝光 +（图太暗就按它）
*     按键2 = 曝光 -
*     按键3 = 阈值 +10（按一下就进入"手动阈值"；一直加到 240 之后再按一次回到自动大津法）
*     按键4 = 灰度 / 二值化 显示切换
*
* 接线（主板「摄像头接口」，按丝印名字对，不要按位置整排插）
*     摄像头 D0~D7 -> P00_0 ~ P00_7      摄像头 VSY  -> P02_0
*     摄像头 PCLK  -> P02_1              摄像头 TXD  -> P02_3（进单片机 RX，交叉）
*     摄像头 RXD   -> P02_2              摄像头 VCC  -> 3.3V     GND -> GND
* 屏幕（SPI2）：SCK=P15_3  MOSI=P15_5  CS=P15_2  RST=P15_1  DC=P15_0  BLK=P15_4
*
* 开发环境          AURIX Development Studio 1.10.40 + 逐飞 TC264 开源库
* 日期              2026-10-20
********************************************************************************************************************/
#include "zf_common_headfile.h"
#include "car_image.h"                                          // 巡线识别模块
#pragma section all "cpu0_dsram"
// 将本语句与#pragma section all restore语句之间的全局变量都放在CPU0的RAM中

//================================================== 可调参数（只改这一段） ==================================================
#define CAM_USE_IPS200              (1)                         // 1 = 2 寸 IPS200（240x320）
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
static uint16 cam_manual_th   = 0;                              // 0 = 自动阈值（大津法）
static uint8  cam_show_bin    = 0;                               // 0 = 显示灰度图   1 = 显示二值化图
static uint16 cam_fps         = 0;                              // 最近一秒的帧数
static uint8  cam_mean        = 0;                              // 图像灰度均值（判断曝光够不够用）
static uint32 cam_frame_cnt   = 0;
static uint32 cam_fps_t0      = 0;
static uint8  cam_led_level   = 0;

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

    CAM_SCREEN_STRING(0,  y, "STATE:");
    CAM_SCREEN_STRING(48, y, car_image_state_name(info->state));
}

// **************************** 代码区域 ****************************
int core0_main(void)
{
    uint32 now;

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

    cpu_wait_event_ready();                                     // 等待所有核心初始化完毕

    while(TRUE)
    {
        // ------------------------- 3. 按键 -------------------------
        if(cam_key_edge(0, (uint8)(gpio_get_level(CAM_KEY1_PIN) == 0)))       // 按键1：曝光 +
        {
            cam_exp = (cam_exp + CAM_EXP_STEP > CAM_EXP_MAX) ? CAM_EXP_MAX : (cam_exp + CAM_EXP_STEP);
            mt9v03x_set_exposure_time(cam_exp);
            printf("exp = %d\r\n", cam_exp);
        }
        if(cam_key_edge(1, (uint8)(gpio_get_level(CAM_KEY2_PIN) == 0)))       // 按键2：曝光 -
        {
            cam_exp = (cam_exp < CAM_EXP_MIN + CAM_EXP_STEP) ? CAM_EXP_MIN : (cam_exp - CAM_EXP_STEP);
            mt9v03x_set_exposure_time(cam_exp);
            printf("exp = %d\r\n", cam_exp);
        }
        if(cam_key_edge(2, (uint8)(gpio_get_level(CAM_KEY3_PIN) == 0)))       // 按键3：阈值 +
        {
            if(0 == cam_manual_th)                                           // 从自动切到手动：以当前自动阈值为起点
            {
                cam_manual_th = car_image_get_threshold() + CAM_TH_STEP;
            }
            else
            {
                cam_manual_th += CAM_TH_STEP;
                if(cam_manual_th > CAM_TH_MANUAL_MAX)
                {
                    cam_manual_th = 0;                                       // 加到头了 -> 回到自动
                }
            }
            car_image_set_threshold(cam_manual_th);
            printf("threshold = %s%d\r\n", (cam_manual_th ? "manual " : "auto "), cam_manual_th);
        }
        if(cam_key_edge(3, (uint8)(gpio_get_level(CAM_KEY4_PIN) == 0)))       // 按键4：灰度 / 二值化
        {
            cam_show_bin = !cam_show_bin;
            printf("display = %s\r\n", cam_show_bin ? "binary" : "gray");
        }

        // ------------------------- 4. 有新图就识别 + 显示 -------------------------
        if(mt9v03x_finish_flag)
        {
            mt9v03x_finish_flag = 0;                            // 必须自己清 0，不清图像就定住不动

            car_image_process(&cam_info);                       // <<< 整帧识别就这一句

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

            cam_mean = cam_image_mean(mt9v03x_image[0]);

            // 阈值传 0 = 显示灰度；传阈值 = 显示二值化黑白图（和识别用的是同一个阈值）
            CAM_SCREEN_IMAGE(mt9v03x_image[0], (cam_show_bin ? cam_info.threshold : 0));
            cam_draw_overlay(&cam_info);
            cam_screen_refresh(&cam_info);

            printf("th:%d err:%d norm:%d state:%s rows:%d/%d mean:%d\r\n",
                   cam_info.threshold, cam_info.err, cam_info.err_norm,
                   car_image_state_name(cam_info.state), cam_info.valid_rows, cam_info.lost_rows,
                   cam_mean);
        }
    }
}
#pragma section all restore
// **************************** 代码区域 ****************************
