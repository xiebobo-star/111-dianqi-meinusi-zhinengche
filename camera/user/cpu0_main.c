/*********************************************************************************************************************
* TC264 智能车 —— 摄像头（逐飞 MT9V034「总钻风」）测试工程
* 文件名            cpu0_main.c
* 说明              这个工程只干一件事：让摄像头出图，屏幕上看得到，并且能现场调曝光 / 二值化阈值
*
* 接线（主板「摄像头接口1」，8x2 排针，按丝印名字对，不要按位置整排插）
*     摄像头 D0~D7   -> P00_0 ~ P00_7
*     摄像头 VSY     -> P02_0
*     摄像头 PCLK    -> P02_1
*     摄像头 TXD     -> P02_3    （进单片机的 RX，注意是交叉的）
*     摄像头 RXD     -> P02_2    （进单片机的 TX）
*     摄像头 VCC     -> 3.3V     （板上 3V3CAM 那一路）
*     摄像头 GND     -> GND
*     摄像头 HREF    -> 不接
*
* 屏幕（主板「屏幕1」SPI2）：SCK=P15_3  MOSI=P15_5  CS=P15_2  RST=P15_1  DC=P15_0  BLK=P15_4
* 按键（主板 4 个键）：按键1=P20_6  按键2=P20_7  按键3=P11_2  按键4=P11_3
* 调试串口：TX=P14_0  RX=P14_1  115200
*
* 按键功能
*     按键1 = 曝光 +（图太暗就按它）
*     按键2 = 曝光 -（图太亮就按它）
*     按键3 = 灰度 / 二值化 显示切换
*     按键4 = 二值化阈值 +20（20~240 循环，只在二值化模式下有用）
*
* 开发环境          AURIX Development Studio 1.10.40 + 逐飞 TC264 开源库
* 日期              2026-10-17
********************************************************************************************************************/
#include "zf_common_headfile.h"
#pragma section all "cpu0_dsram"
// 将本语句与#pragma section all restore语句之间的全局变量都放在CPU0的RAM中

// ================================================== 可调参数（只改这一段） ==================================================
#define CAM_USE_IPS200              (1)                         // 1 = 2 寸 IPS200（240x320）
                                                                // 0 = 1.8 寸 TFT180（128x160），买的是哪个就改哪个
#define CAM_EXP_DEFAULT             (512)                       // 上电时的曝光时间
#define CAM_EXP_STEP                (64)                        // 每按一次曝光改变多少
#define CAM_EXP_MIN                 (20)                        // 曝光下限
#define CAM_EXP_MAX                 (1500)                      // 曝光上限
#define CAM_THRESHOLD_DEFAULT       (100)                       // 二值化阈值默认值（白底黑线一般在 80~150 之间）
#define CAM_THRESHOLD_STEP          (20)                        // 每按一次阈值改变多少
#define CAM_THRESHOLD_MIN           (20)
#define CAM_THRESHOLD_MAX           (240)

#define CAM_KEY1_PIN                (P20_6)                     // 按键1：曝光 +
#define CAM_KEY2_PIN                (P20_7)                     // 按键2：曝光 -
#define CAM_KEY3_PIN                (P11_2)                     // 按键3：灰度/二值化
#define CAM_KEY4_PIN                (P11_3)                     // 按键4：阈值 +20
#define CAM_KEY_DEBOUNCE_CNT        (3)                         // 连续几次读到同一个状态才算数（主循环约 20ms 一圈 -> 约 60ms 消抖）
#define CAM_LED1_PIN                (P20_9)                     // 核心板 LED1：1 秒翻转一次，表示程序活着

// ============================================ 屏幕宏（IPS200 / TFT180 一套代码） ============================================
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
    #define CAM_SCREEN_UINT(x, y, v, n) ips200_show_uint((x), (y), (v), (n))
    #define CAM_SCREEN_IMAGE(p, th)     ips200_show_gray_image(0, 0, (p), MT9V03X_W, MT9V03X_H, CAM_IMG_DIS_W, CAM_IMG_DIS_H, (th))
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
    #define CAM_SCREEN_UINT(x, y, v, n) tft180_show_uint((x), (y), (v), (n))
    #define CAM_SCREEN_IMAGE(p, th)     tft180_show_gray_image(0, 0, (p), MT9V03X_W, MT9V03X_H, CAM_IMG_DIS_W, CAM_IMG_DIS_H, (th))
#endif

// ==================================================== 全局变量 ====================================================
static uint16 cam_exp          = CAM_EXP_DEFAULT;               // 当前曝光
static uint8  cam_mode         = 0;                             // 0 = 灰度显示   1 = 二值化显示
static uint16 cam_threshold    = CAM_THRESHOLD_DEFAULT;         // 当前二值化阈值
static uint16 cam_fps          = 0;                             // 最近一秒的帧数
static uint8  cam_mean         = 0;                             // 图像灰度均值（判断曝光够不够用）
static uint32 cam_frame_cnt    = 0;
static uint32 cam_fps_t0       = 0;
static uint8  cam_led_level    = 0;

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
        count[index]++;
        if(count[index] >= CAM_KEY_DEBOUNCE_CNT)                // 连续 N 次都是新状态 -> 认定状态改变
        {
            count[index]  = 0;
            stable[index] = raw_pressed;
            if(raw_pressed)
            {
                ret = 1;                                        // 只在“刚按下”这一下返回 1，按住不会一直触发
            }
        }
    }
    return ret;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     图像灰度均值（隔行隔列抽样，算得快）
// 参数说明     image        图像首地址，一般传 mt9v03x_image[0]
// 返回参数     uint8        平均灰度 0~255
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
            sum += image[row * MT9V03X_W + col];
            num ++;
        }
    }
    return (uint8)(sum / num);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     刷新屏幕下方的四行文字
//-------------------------------------------------------------------------------------------------------------------
static void cam_screen_refresh (void)
{
    uint16 y = CAM_TXT_Y0;

    CAM_SCREEN_STRING(0, y, "FPS :");   CAM_SCREEN_UINT(48, y, cam_fps, 2);
    y += CAM_TXT_STEP;
    CAM_SCREEN_STRING(0, y, "EXP :");   CAM_SCREEN_UINT(48, y, cam_exp, 4);
    y += CAM_TXT_STEP;
    CAM_SCREEN_STRING(0, y, "MEAN:");   CAM_SCREEN_UINT(48, y, cam_mean, 3);
    y += CAM_TXT_STEP;
    CAM_SCREEN_STRING(0, y, "MODE:");
    CAM_SCREEN_STRING(56, y, (cam_mode ? "BIN " : "GRAY"));
    CAM_SCREEN_UINT(96, y, cam_threshold, 3);
}

// **************************** 代码区域 ****************************
int core0_main(void)
{
    uint32 now;

    clock_init();                                               // 获取时钟频率<务必保留>
    debug_init();                                               // 初始化默认调试串口
    system_start();                                             // 启动计时（算帧率用）

    // ------------------------- 1. 先让“程序跑起来了”看得见 -------------------------
    gpio_init(CAM_LED1_PIN, GPO, GPIO_HIGH, GPO_PUSH_PULL);      // 核心板 LED 低电平点亮
    gpio_set_level(CAM_LED1_PIN, 1);                            // 先灭

    CAM_SCREEN_INIT();
    CAM_SCREEN_CLEAR();
    CAM_SCREEN_FONT();
    CAM_SCREEN_COLOR(RGB565_RED, RGB565_WHITE);

    cam_key_init();

    // ------------------------- 2. 摄像头初始化 -------------------------
    CAM_SCREEN_STRING(0, 0, "camera init ...");
    printf("\r\n===== MT9V034 camera test =====\r\n");
    printf("D0-D7=P00_0-P00_7 VSY=P02_0 PCLK=P02_1 CAM_TXD->P02_3 CAM_RXD->P02_2\r\n");

    if(mt9v03x_init())                                          // 0 = 成功   1 = 失败
    {
        CAM_SCREEN_CLEAR();
        CAM_SCREEN_STRING(0, 40,  "CAMERA INIT FAIL");
        CAM_SCREEN_STRING(0, 64,  "1 check 3V3CAM LED");
        CAM_SCREEN_STRING(0, 84,  "2 swap TXD and RXD");
        CAM_SCREEN_STRING(0, 104, "3 check D0-D7 order");
        printf("mt9v03x_init() FAIL: check 3.3V power and TXD/RXD wiring\r\n");

        while(TRUE)                                             // 初始化失败就停在这里闪灯，不要继续往下跑
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

    cpu_wait_event_ready();                                     // 等待所有核心初始化完毕

    while(TRUE)
    {
        // ------------------------- 3. 按键：每圈都扫，跟有没有新图无关 -------------------------
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
        if(cam_key_edge(2, (uint8)(gpio_get_level(CAM_KEY3_PIN) == 0)))       // 按键3：灰度 / 二值化
        {
            cam_mode = !cam_mode;
            printf("mode = %s\r\n", cam_mode ? "binary" : "gray");
        }
        if(cam_key_edge(3, (uint8)(gpio_get_level(CAM_KEY4_PIN) == 0)))       // 按键4：阈值 +20
        {
            cam_threshold += CAM_THRESHOLD_STEP;
            if(cam_threshold > CAM_THRESHOLD_MAX)
            {
                cam_threshold = CAM_THRESHOLD_MIN;
            }
            printf("threshold = %d\r\n", cam_threshold);
        }

        // ------------------------- 4. 有新图就显示 -------------------------
        if(mt9v03x_finish_flag)
        {
            mt9v03x_finish_flag = 0;                            // 必须自己清 0，不清图像就定住不动

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

            // 阈值传 0 = 显示灰度；传 cam_threshold = 显示二值化黑白图
            CAM_SCREEN_IMAGE(mt9v03x_image[0], (cam_mode ? cam_threshold : 0));
            cam_screen_refresh();

            printf("fps:%d exp:%d mean:%d th:%d\r\n", cam_fps, cam_exp, cam_mean, cam_threshold);
        }
    }
}
#pragma section all restore
// **************************** 代码区域 ****************************
