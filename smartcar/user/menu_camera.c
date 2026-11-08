/*********************************************************************************************************************
* 文件名            menu_camera.c
* 说明              摄像头 / 循迹菜单页面：把 patrol 工程（图像识别）和 camera 工程（曝光 / 二值化调参）合到一起
*
*   图像显示页   原图或二值图 + 曝光 / 阈值 / 帧率 / 亮度均值（调曝光就看这一页）
*   循迹识别页   二值图 + 每行中线叠加（绿 = 真找到，黄 = 补出来的，红 = 丢线）+ 偏差 / 状态
*
*   识别算法一行没改，全在 car_image.c 里；这一层只负责「喂图 + 画图 + 按键」
*
* 接线（主板「摄像头接口」，按丝印名字对，别按位置整排插）
*   摄像头 D0~D7 -> P00_0 ~ P00_7      VSY -> P02_0     PCLK -> P02_1
*   摄像头 TXD   -> P02_3（进单片机 RX，交叉）            RXD -> P02_2
*   摄像头 VCC   -> 3.3V               GND -> GND
*********************************************************************************************************************/
#include "menu_config.h"
#include "menu_display.h"
#include "menu_param.h"
#include "menu_camera.h"
#include "car_image.h"

// ============================================ 屏幕排版（TFT180 128x160 / IPS200 240x320） ============================================
#if (MENU_DISPLAY_USE_IPS200)
    #define CAM_DIS_W               (MT9V03X_W)                     // 240 宽的屏幕放得下原图，不用缩
    #define CAM_DIS_H               (MT9V03X_H)
    #define CAM_IMG_Y               (18)
    #define CAM_TXT_X               (4)
    #define CAM_TXT_Y0              (CAM_IMG_Y + CAM_DIS_H + 6)
    #define CAM_TXT_LH              (18)
    #define CAM_TXT_NUM             (5)
#else
    #define CAM_DIS_W               (126)                           // 128 宽的屏放不下 188，等比缩小
    #define CAM_DIS_H               (80)
    #define CAM_IMG_Y               (0)
    #define CAM_TXT_X               (0)
    #define CAM_TXT_Y0              (CAM_IMG_Y + CAM_DIS_H + 4)
    #define CAM_TXT_LH              (17)
    #define CAM_TXT_NUM             (4)                             // 128x160 也放得下 4 行（84~151）
#endif

// 图像坐标 -> 屏幕坐标（IPS200 是 1:1，TFT180 要等比缩小）
#define CAM_PX(x)                   ((uint16)((uint32)(x) * CAM_DIS_W / MT9V03X_W))
#define CAM_PY(y)                   ((uint16)(CAM_IMG_Y + (uint32)(y) * CAM_DIS_H / MT9V03X_H))

#define CAM_TH_STEP                 (10)                        // 循迹页按 3 一次加多少阈值
#define CAM_TH_MANUAL_MAX           (240)                       // 手动阈值加到这儿，再按一次回自动

// ============================================ 页面内部状态 ============================================
static uint8  cam_state;                                        // 0 = 还没初始化  1 = 初始化成功  2 = 初始化失败
static uint8  cam_view;                                         // 图像页显示：0 = 灰度图  1 = 二值图
static uint16 cam_exp;                                          // 当前曝光（和参数表同步）
static uint16 cam_th;                                           // 当前手动阈值（0 = 自动大津法）
static uint16 cam_fps;                                          // 最近一秒的帧数
static uint16 cam_mean;                                         // 图像灰度均值（判断曝光够不够）
static uint32 cam_frame_cnt;                                    // 一秒内累计帧数
static uint32 cam_t0;                                           // 帧率统计起点
static car_image_info_struct cam_info;                          // 识别结果（car_image.c 填）

// 文字区「上一次画到屏上的是什么」—— 只有内容真的变了才重画那一行，否则整块 fill + 重画
// 会跟摄像头帧率叠在一起，看着就是频闪
static char  cam_text_last[CAM_TXT_NUM][24];
static uint8 cam_text_ok;
static uint8 cam_frozen;                                        // 1 = 画面冻结（长按按键3 切换）

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     摄像头初始化（懒初始化：第一次进页面才做，没进就不占 P00/P02 口）
// 返回参数     1 = 成功  0 = 失败
//-------------------------------------------------------------------------------------------------------------------
static uint8 cam_hw_init (void)
{
    if (1 == cam_state) { return 1; }
    if (2 == cam_state) { return 0; }                           // 已经失败过，不再反复复位摄像头

    if (0 != mt9v03x_init())                                    // 0 = 成功，非 0 = 失败
    {
        cam_state = 2;
        printf("mt9v03x_init FAIL: check 3V3 and TXD/RXD\n");
        return 0;
    }

    car_image_init();
    cam_exp = (uint16)menu_param_get(MENU_PARAM_IDX_CAM_EXP);
    cam_th  = (uint16)menu_param_get(MENU_PARAM_IDX_CAM_TH);
    cam_view = (0 != menu_param_get(MENU_PARAM_IDX_CAM_VIEW)) ? (1) : (0);
    mt9v03x_set_exposure_time(cam_exp);
    car_image_set_threshold(cam_th);
    cam_t0    = system_getval_ms();
    cam_state = 1;
    printf("camera ok: %dx%d exp=%d th=%d\n", MT9V03X_W, MT9V03X_H, (int)cam_exp, (int)cam_th);
    return 1;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     把参数表里的曝光 / 阈值同步到摄像头（菜单里改了参数，进页面立刻生效）
//-------------------------------------------------------------------------------------------------------------------
static void cam_sync_param (void)
{
    uint16 exp = (uint16)menu_param_get(MENU_PARAM_IDX_CAM_EXP);
    uint16 th  = (uint16)menu_param_get(MENU_PARAM_IDX_CAM_TH);

    if (exp != cam_exp)
    {
        cam_exp = exp;
        mt9v03x_set_exposure_time(cam_exp);
    }
    if (th != cam_th)
    {
        cam_th = th;
        car_image_set_threshold(cam_th);
    }
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     图像灰度均值（隔 4 行 4 列抽样，算得快）—— 判断曝光够不够
//-------------------------------------------------------------------------------------------------------------------
static uint16 cam_image_mean (const uint8 *image)
{
    uint32 sum = 0;
    uint32 num = 0;
    uint16 row;
    uint16 col;

    for (row = 0; row < MT9V03X_H; row += 4)
    {
        for (col = 0; col < MT9V03X_W; col += 4)
        {
            sum += image[(uint32)row * MT9V03X_W + col];
            num ++;
        }
    }
    return (uint16)(sum / num);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     画图像（灰度或二值）
//-------------------------------------------------------------------------------------------------------------------
static void cam_draw_image (void)
{
    uint16 th = 0;

    if (0 != cam_view)
    {
        th = cam_info.threshold;                                // 二值图用识别时那一帧算出来的阈值
        if (0 == th) { th = (0 != cam_th) ? (cam_th) : (128); }
    }
    menu_display_gray_image(0, CAM_IMG_Y, mt9v03x_image[0], MT9V03X_W, MT9V03X_H,
                            CAM_DIS_W, CAM_DIS_H, th);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     循迹页：把每行中线叠到图像上（绿 = 真找到 / 黄 = 补出来 / 红 = 丢线）
//-------------------------------------------------------------------------------------------------------------------
static void cam_draw_overlay (void)
{
    uint16 n;
    uint16 y;
    uint16 color;

    for (n = 0; n < CAR_IMG_SCAN_ROWS; n ++)
    {
        y = (uint16)(n * CAR_IMG_SCAN_STEP + (CAR_IMG_SCAN_STEP - 1));

        if (CAR_ROW_FLAG_REAL == cam_info.row_flag[n])      { color = RGB565_GREEN;  }
        else if (CAR_ROW_FLAG_FIX == cam_info.row_flag[n])  { color = RGB565_YELLOW; }
        else                                                { color = RGB565_RED;    }

        menu_display_point(CAM_PX(cam_info.center[n]), CAM_PY(y), color);
    }

    for (y = 0; y < MT9V03X_H; y += 6)                          // 图像正中心参考线（蓝色虚线）
    {
        menu_display_point(CAM_PX(CAR_IMG_CENTER), CAM_PY(y), RGB565_BLUE);
    }
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     画文字区
// 参数说明     page    页面号（图像显示 / 循迹识别）
// 参数说明     full    1 = 整块重画（进页面、按键改了参数）   0 = 只重画数值变了的那一行
// 备注信息     ★ 别整块 fill + 重画：摄像头一帧刷 25 次，整块清屏再写会明显频闪
//-------------------------------------------------------------------------------------------------------------------
static void cam_draw_text (uint8 page, uint8 full)
{
    uint16 y = (uint16)CAM_TXT_Y0;
    uint8  i;
    char   buf[CAM_TXT_NUM][24];

    for (i = 0; i < CAM_TXT_NUM; i ++) { buf[i][0] = '\0'; }

    if (MENU_CAM_PAGE_IMAGE == page)
    {
        snprintf(buf[0], sizeof(buf[0]), "exp%d  fps%d", (int)cam_exp, (int)cam_fps);
        if (0 == cam_th)
        {
            snprintf(buf[1], sizeof(buf[1]), "th auto  %s", (0 != cam_view) ? ("bin") : ("gray"));
        }
        else
        {
            snprintf(buf[1], sizeof(buf[1]), "th %d  %s", (int)cam_th, (0 != cam_view) ? ("bin") : ("gray"));
        }
        snprintf(buf[2], sizeof(buf[2]), "mean %d", (int)cam_mean);
        snprintf(buf[3], sizeof(buf[3]), (0 != cam_frozen) ? ("已冻结 长按3恢复") : ("1/2曝光 3切换"));
        snprintf(buf[4], sizeof(buf[4]), "4键 返回");
    }
    else
    {
        snprintf(buf[0], sizeof(buf[0]), "err %d norm %d", (int)cam_info.err, (int)cam_info.err_norm);
        snprintf(buf[1], sizeof(buf[1]), "state %s", car_image_state_name(cam_info.state));
        snprintf(buf[2], sizeof(buf[2]), "row %d th %d", (int)cam_info.valid_rows, (int)cam_info.threshold);
        snprintf(buf[3], sizeof(buf[3]), (0 != cam_frozen) ? ("已冻结 长按3恢复") : ("1/2曝光 3阈值"));
        snprintf(buf[4], sizeof(buf[4]), "4键 返回");
    }

    menu_display_set_color(MENU_COLOR_PEN, MENU_COLOR_BG);

    for (i = 0; i < CAM_TXT_NUM; i ++)
    {
        if ('\0' == buf[i][0]) { break; }

        if ((0 == full) && (0 != cam_text_ok) && (0 == strcmp(buf[i], cam_text_last[i])))
        {
            y = (uint16)(y + CAM_TXT_LH);                       // 这一行没变：跳过，不动屏
            continue;
        }

        menu_display_fill(0, (uint16)(y - 1), menu_display_width(), CAM_TXT_LH, MENU_COLOR_BG);
        menu_display_cn((uint16)CAM_TXT_X, y, buf[i]);
        strncpy(cam_text_last[i], buf[i], sizeof(cam_text_last[i]) - 1);
        cam_text_last[i][sizeof(cam_text_last[i]) - 1] = '\0';
        y = (uint16)(y + CAM_TXT_LH);
    }
    cam_text_ok = 1;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     整屏重画（图像 + 文字）—— 进页面 / 按键后调用
//-------------------------------------------------------------------------------------------------------------------
void menu_cam_draw (uint8 page)
{
    if (0 == cam_hw_init())
    {
        menu_display_clear();
        menu_display_set_color(MENU_COLOR_ERR, MENU_COLOR_BG);
        menu_display_cn(4, 20, "摄像头未初始化");
        menu_display_set_color(MENU_COLOR_PEN, MENU_COLOR_BG);
        menu_display_cn(4, 44, "1 查 3.3V 供电");
        menu_display_cn(4, 64, "2 对调 TXD/RXD");
        menu_display_cn(4, 84, "3 查 D0~D7 排线");
        menu_display_cn(4, 120, "4 键返回菜单");
        return;
    }

    cam_draw_image();
    if (MENU_CAM_PAGE_LINE == page) { cam_draw_overlay(); }
    cam_draw_text(page, 1);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     来一帧就刷一次：图像重画（这就是实时预览），文字只画改动的那一行
//-------------------------------------------------------------------------------------------------------------------
static void cam_live_draw (uint8 page)
{
    cam_draw_image();
    if (MENU_CAM_PAGE_LINE == page) { cam_draw_overlay(); }
    cam_draw_text(page, 0);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     进入页面：初始化摄像头，整屏画一次
//-------------------------------------------------------------------------------------------------------------------
void menu_cam_enter (uint8 page)
{
    cam_state     = 0;                                          // 允许重新试一次（线插好了不用断电）
    cam_frame_cnt = 0;
    cam_fps       = 0;
    cam_mean      = 0;
    cam_text_ok   = 0;
    menu_display_clear();
    menu_cam_draw(page);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     周期任务：有新图就识别 + 刷新（主循环每圈调一次）
//-------------------------------------------------------------------------------------------------------------------
void menu_cam_live (uint8 page)
{
    uint32 now;

    if (0 == cam_hw_init()) { return; }
    if (0 != cam_frozen)                                        // 冻结中：不取新图、不重画，屏上留这一帧
    {
        mt9v03x_finish_flag = 0;
        return;
    }
    cam_sync_param();

    if (0 == mt9v03x_finish_flag) { return; }                   // 还没收到新图
    mt9v03x_finish_flag = 0;                                    // 必须自己清 0，不清图像就定住不动

    car_image_process(&cam_info);                               // 整帧识别就这一句
    cam_mean = cam_image_mean(mt9v03x_image[0]);

    cam_frame_cnt ++;
    now = system_getval_ms();
    if ((uint32)(now - cam_t0) >= 1000u)                        // 每满一秒统计一次帧率
    {
        cam_fps       = (uint16)cam_frame_cnt;
        cam_frame_cnt = 0;
        cam_t0        = now;
    }

    cam_live_draw(page);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     按键：1/2 曝光加减，3 显示模式 / 阈值，4 返回
// 返回参数     0 = 留在本页   1 = 退回菜单
//-------------------------------------------------------------------------------------------------------------------
uint8 menu_cam_key (uint8 page, uint8 k_up, uint8 k_down, uint8 k_ok, uint8 k_ok_long, uint8 k_back)
{
    // menu_ui 的主循环每圈都会调这个函数：一个键都没动就直接返回，别每圈重画整屏
    if ((0 == k_up) && (0 == k_down) && (0 == k_ok) && (0 == k_ok_long) && (0 == k_back)) { return 0; }

    if (0 != k_back) { return 1; }

    if (0 != k_ok_long)                                         // 长按按键3：冻结 / 恢复画面
    {
        cam_frozen  = (0 != cam_frozen) ? (0) : (1);
        cam_text_ok = 0;
        menu_cam_draw(page);
        return 0;
    }

    if (0 != k_up)                                              // 按键1：曝光 +
    {
        menu_param_set(MENU_PARAM_IDX_CAM_EXP, menu_param_get(MENU_PARAM_IDX_CAM_EXP) + 20);
    }
    if (0 != k_down)                                            // 按键2：曝光 -
    {
        menu_param_set(MENU_PARAM_IDX_CAM_EXP, menu_param_get(MENU_PARAM_IDX_CAM_EXP) - 20);
    }

    if (0 != k_ok)                                              // 按键3
    {
        if (MENU_CAM_PAGE_IMAGE == page)                        // 图像页：灰度 / 二值切换
        {
            menu_param_set(MENU_PARAM_IDX_CAM_VIEW,
                           (0 != menu_param_get(MENU_PARAM_IDX_CAM_VIEW)) ? (0) : (1));
        }
        else                                                    // 循迹页：阈值 +10，加到 240 之后回自动
        {
            int32 th = menu_param_get(MENU_PARAM_IDX_CAM_TH);

            if (0 == th)
            {
                th = car_image_get_threshold() + CAM_TH_STEP;   // 从自动切手动：以当前自动阈值为起点
            }
            else
            {
                th += CAM_TH_STEP;
            }
            if (th > CAM_TH_MANUAL_MAX) { th = 0; }             // 加到头了 -> 回到自动大津法
            menu_param_set(MENU_PARAM_IDX_CAM_TH, th);
        }
    }

    cam_view = (0 != menu_param_get(MENU_PARAM_IDX_CAM_VIEW)) ? (1) : (0);
    cam_sync_param();
    menu_cam_draw(page);
    return 0;
}
