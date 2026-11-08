/*********************************************************************************************************************
* 文件名            menu_run.c
* 说明              循迹跑车页 + 摄像头调试页
*
*   数据流：摄像头 DMA 采完一帧 -> mt9v03x_finish_flag
*           -> 本文件把整帧复制到自己的缓冲（复制期间 DMA 已经停，不会撕裂）
*           -> car_image.c 识别出中线 / 偏差 / 状态 / 置信度 / 岔路
*           -> 跑车页把识别结果交给 car_board（控制层：转向 PD、速度 PI、圆环、停车保护）
*           -> 10ms 中断里 car_board_tick() 算 PWM 和舵机
*
*   ★ 显示只在 5Hz 刷（200ms），识别和控制按摄像头帧率走 —— 屏慢不能拖慢车。
*   ★ 上电一律停车：只有跑车页按键 3 才启动，任何故障 / 按键 4 / 长按 4 都立刻停车。
*********************************************************************************************************************/
#include "menu_config.h"
#include "menu_display.h"
#include "menu_param.h"
#include "car_config.h"
#include "car_image.h"
#include "car_board.h"
#include "car_tune.h"
#include "enc_pd1503.h"
#include "menu_run.h"
#include <string.h>

// ============================================ 屏幕排版（TFT180 128x160 / IPS200 240x320） ============================================
#if (MENU_DISPLAY_USE_IPS200)
    #define RUN_DIS_W               (MT9V03X_W)
    #define RUN_DIS_H               (MT9V03X_H)
    #define RUN_IMG_Y               (18)
    #define RUN_TXT_X               (4)
    #define RUN_TXT_Y0              (RUN_IMG_Y + RUN_DIS_H + 6)
    #define RUN_TXT_LH              (18)
    #define RUN_TXT_NUM             (6)
#else
    #define RUN_DIS_W               (126)
    #define RUN_DIS_H               (80)
    #define RUN_IMG_Y               (0)
    #define RUN_TXT_X               (0)
    #define RUN_TXT_Y0              (RUN_IMG_Y + RUN_DIS_H + 4)
    #define RUN_TXT_LH              (17)
    #define RUN_TXT_NUM             (4)
#endif

#define RUN_PX(x)                   ((uint16)((uint32)(x) * RUN_DIS_W / MT9V03X_W))
#define RUN_PY(y)                   ((uint16)(RUN_IMG_Y + (uint32)(y) * RUN_DIS_H / MT9V03X_H))

#define RUN_DRAW_MS                 (200)               // 屏刷新周期（5Hz）
#define RUN_TH_STEP                 (10)
#define RUN_TH_MANUAL_MAX           (240)

// ============================================ 页面内部状态 ============================================
static uint8  run_buf[MT9V03X_H][MT9V03X_W];            // 整帧快照（识别和显示都用这一份）
static uint32 run_cap_ms;                               // 这帧采完的时刻（来自 DMA 中断）
static car_image_info_struct run_img;                   // 识别结果
static uint8  run_img_ok;                               // 1 = 已经识别过至少一帧

static uint8  run_cam_state;                            // 0 未初始化 1 成功 2 失败
static uint8  run_view;                                 // 0 灰度 1 二值
static uint16 run_exp;
static uint16 run_th;
static uint16 run_fps;
static uint16 run_mean;
static uint32 run_frame_cnt;
static uint32 run_fps_t0;
static uint32 run_draw_ms;
static uint8  run_frozen;                               // 摄像头页冻结画面

static char   run_text_last[RUN_TXT_NUM][26];           // 上一屏文字（只重画变了的行，防频闪）
static uint8  run_text_ok;

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     摄像头初始化（懒初始化：第一次进页面才做）
// 返回参数     1 = 成功  0 = 失败
//-------------------------------------------------------------------------------------------------------------------
static uint8 run_cam_init (void)
{
    if (1 == run_cam_state) { return 1; }
    if (2 == run_cam_state) { return 0; }

    if (0 != mt9v03x_init())
    {
        run_cam_state = 2;
        printf("mt9v03x_init FAIL: check 3V3 and TXD/RXD\r\n");
        return 0;
    }

    run_exp = (uint16)menu_param_get(MENU_PARAM_IDX_CAM_EXP);
    run_th  = (uint16)menu_param_get(MENU_PARAM_IDX_CAM_TH);
    run_view = (0 != menu_param_get(MENU_PARAM_IDX_CAM_VIEW)) ? (1) : (0);
    mt9v03x_set_exposure_time(run_exp);
    car_image_set_threshold(run_th);
    run_fps_t0    = system_getval_ms();
    run_cam_state = 1;
    printf("camera ok %dx%d exp=%d th=%d\r\n", MT9V03X_W, MT9V03X_H, (int)run_exp, (int)run_th);
    return 1;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     把菜单里的曝光 / 阈值同步给摄像头
//-------------------------------------------------------------------------------------------------------------------
static void run_sync_param (void)
{
    uint16 exp = (uint16)menu_param_get(MENU_PARAM_IDX_CAM_EXP);
    uint16 th  = (uint16)menu_param_get(MENU_PARAM_IDX_CAM_TH);

    if (exp != run_exp)
    {
        run_exp = exp;
        mt9v03x_set_exposure_time(run_exp);
    }
    if (th != run_th)
    {
        run_th = th;
        car_image_set_threshold(run_th);
    }
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     取一帧快照（有就复制走并返回 1）
// 备注信息     复制完才清 finish_flag：复制期间 DMA 不会再写这块内存，图像不会撕裂
//-------------------------------------------------------------------------------------------------------------------
static uint8 run_frame_take (void)
{
    if (0 == mt9v03x_finish_flag) { return 0; }

    memcpy(run_buf, mt9v03x_image, sizeof(run_buf));
    run_cap_ms = car_capture_ms;
    mt9v03x_finish_flag = 0;
    return 1;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     图像灰度均值（隔 4 行 4 列抽样）
//-------------------------------------------------------------------------------------------------------------------
static uint16 run_image_mean (const uint8 *image)
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
// 函数简介     画图像（灰度 / 二值），二值优先用识别那一帧算出来的阈值
//-------------------------------------------------------------------------------------------------------------------
static void run_draw_image (void)
{
    uint16 th = 0;

    if (0 != run_view)
    {
        th = run_img.threshold;
        if (0 == th) { th = (0 != run_th) ? (run_th) : (128); }
    }
    menu_display_gray_image(0, RUN_IMG_Y, run_buf[0], MT9V03X_W, MT9V03X_H,
                            RUN_DIS_W, RUN_DIS_H, th);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     把每行中线叠到图上（绿 = 真找到 / 黄 = 单边估计 / 红 = 无效行）
//-------------------------------------------------------------------------------------------------------------------
static void run_draw_overlay (void)
{
    uint16 n;
    uint16 y;
    uint16 color;

    for (n = 0; n < CAR_IMG_SCAN_ROWS; n ++)
    {
        y = (uint16)(n * CAR_IMG_SCAN_STEP + (CAR_IMG_SCAN_STEP - 1));

        if (CAR_ROW_FLAG_REAL == run_img.row_flag[n])      { color = RGB565_GREEN;  }
        else if (CAR_ROW_FLAG_FIX == run_img.row_flag[n])  { color = RGB565_YELLOW; }
        else                                               { color = RGB565_RED;    }

        menu_display_point(RUN_PX(run_img.center[n]), RUN_PY(y), color);
    }

    for (y = 0; y < MT9V03X_H; y += 6)                      // 画面中心参考线
    {
        menu_display_point(RUN_PX(CAR_IMG_CENTER), RUN_PY(y), RGB565_BLUE);
    }
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     画文字区：只重画「内容变了的」那一行
// 参数说明     full    1 = 整块重画（进页面 / 按键后）    0 = 只画变了的行
//-------------------------------------------------------------------------------------------------------------------
static void run_draw_text (uint8 page, uint8 full)
{
    uint16 y = (uint16)RUN_TXT_Y0;
    uint8  i;
    char   buf[RUN_TXT_NUM][26];
    car_control st;

    for (i = 0; i < RUN_TXT_NUM; i ++) { buf[i][0] = '\0'; }

    if (MENU_RUN_PAGE_RACE == page)
    {
        car_board_status(&st);

        snprintf(buf[0], sizeof(buf[0]), "%s E%d C%d", car_control_state_name(st.state),
                 (int)run_img.err_norm, (int)run_img.confidence);
        snprintf(buf[1], sizeof(buf[1]), "V%d/%d mm/s",
                 (int)car_tune_counts_to_mm_s(st.speed_target),
                 (int)enc_speed_mm_s(ENC_L));
        snprintf(buf[2], sizeof(buf[2]), "PWM %d/%d", (int)st.pwm_l, (int)st.pwm_r);
#if (MENU_DISPLAY_USE_IPS200)
        snprintf(buf[3], sizeof(buf[3]), "sv %d us  ring %s", (int)st.servo_us, car_ring_state_name(st.ring.state));
        snprintf(buf[4], sizeof(buf[4]), "Q%d %s", (int)run_img.confidence, car_image_state_name(run_img.state));
        snprintf(buf[5], sizeof(buf[5]), "3启动 长按3清零 4停");
#else
        snprintf(buf[3], sizeof(buf[3]), "sv%d %s", (int)st.servo_us, (0 == st.state) ? ("3启动 4停") : ("3停 4停"));
#endif
    }
    else
    {
        snprintf(buf[0], sizeof(buf[0]), "exp%d fps%d%s", (int)run_exp, (int)run_fps,
                 (0 != run_frozen) ? (" *") : (""));
        if (0 == run_th)
        {
            snprintf(buf[1], sizeof(buf[1]), "th auto  %s", (0 != run_view) ? ("bin") : ("gray"));
        }
        else
        {
            snprintf(buf[1], sizeof(buf[1]), "th %d  %s", (int)run_th, (0 != run_view) ? ("bin") : ("gray"));
        }
        snprintf(buf[2], sizeof(buf[2]), "mean %d  Q%d", (int)run_mean, (int)run_img.confidence);
        snprintf(buf[3], sizeof(buf[3]), "1/2曝光 3切换");
#if (MENU_DISPLAY_USE_IPS200)
        snprintf(buf[4], sizeof(buf[4]), "长按3 冻结画面");
        snprintf(buf[5], sizeof(buf[5]), "4键 返回");
#endif
    }

    menu_display_set_color(MENU_COLOR_PEN, MENU_COLOR_BG);

    for (i = 0; i < RUN_TXT_NUM; i ++)
    {
        if ('\0' == buf[i][0]) { break; }

        if ((0 == full) && (0 != run_text_ok) && (0 == strcmp(buf[i], run_text_last[i])))
        {
            y = (uint16)(y + RUN_TXT_LH);
            continue;
        }

        menu_display_fill(0, (uint16)(y - 1), menu_display_width(), RUN_TXT_LH, MENU_COLOR_BG);
        menu_display_cn((uint16)RUN_TXT_X, y, buf[i]);
        strncpy(run_text_last[i], buf[i], sizeof(run_text_last[i]) - 1);
        run_text_last[i][sizeof(run_text_last[i]) - 1] = '\0';
        y = (uint16)(y + RUN_TXT_LH);
    }
    run_text_ok = 1;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     整屏重画
//-------------------------------------------------------------------------------------------------------------------
void menu_run_draw (uint8 page)
{
    if (0 == run_cam_init())
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

    if (0 != run_img_ok)
    {
        run_draw_image();
        if (MENU_RUN_PAGE_RACE == page) { run_draw_overlay(); }
    }
    run_draw_text(page, 1);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     进页面
//-------------------------------------------------------------------------------------------------------------------
void menu_run_enter (uint8 page)
{
    run_cam_state = 0;                                      // 允许重试（线插好了不用断电）
    run_text_ok   = 0;
    run_frame_cnt = 0;
    run_fps       = 0;
    run_mean      = 0;
    menu_display_clear();
    menu_run_draw(page);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     立刻停车（两个轮子 PWM 归零，舵机保持当前角度）
//-------------------------------------------------------------------------------------------------------------------
void menu_run_stop_all (void)
{
    car_board_stop();
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     主循环任务：收帧 -> 识别 -> （跑车页）控制 -> 5Hz 刷屏
//-------------------------------------------------------------------------------------------------------------------
void menu_run_live (uint8 page)
{
    uint32 now;

    if (0 == run_cam_init()) { return; }
    if ((0 != run_frozen) && (MENU_RUN_PAGE_CAM == page))
    {
        mt9v03x_finish_flag = 0;                            // 冻结：扔掉新帧，屏上留这一张
        return;
    }
    run_sync_param();

    if (0 != run_frame_take())
    {
        car_image_process_gray(run_buf[0], &run_img);
        run_img_ok = 1;
        run_mean   = run_image_mean(run_buf[0]);
        if (MENU_RUN_PAGE_RACE == page)
        {
            car_board_vision(&run_img, run_cap_ms);          // <<< 识别结果送进控制层
        }

        run_frame_cnt ++;
        now = system_getval_ms();
        if ((uint32)(now - run_fps_t0) >= 1000u)
        {
            run_fps       = (uint16)run_frame_cnt;
            run_frame_cnt = 0;
            run_fps_t0    = now;
        }
    }

    now = system_getval_ms();
    if ((uint32)(now - run_draw_ms) < RUN_DRAW_MS) { return; }      // 屏 5Hz
    run_draw_ms = now;

    if (0 != run_img_ok) { run_draw_image(); }
    if (MENU_RUN_PAGE_RACE == page)
    {
        if (0 != run_img_ok) { run_draw_overlay(); }
    }
    run_draw_text(page, 0);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     按键
// 返回参数     0 = 留在本页   1 = 退回菜单
//-------------------------------------------------------------------------------------------------------------------
uint8 menu_run_key (uint8 page, uint8 k_up, uint8 k_down, uint8 k_ok, uint8 k_ok_long, uint8 k_back)
{
    car_control st;

    if ((0 == k_up) && (0 == k_down) && (0 == k_ok) && (0 == k_ok_long) && (0 == k_back)) { return 0; }

    if (0 != k_back)                                        // 按键4：停车 + 返回
    {
        car_board_stop();
        return 1;
    }

    if (0 != k_ok_long)                                     // 长按按键3
    {
        if (MENU_RUN_PAGE_RACE == page)
        {
            enc_clear();                                    // 里程 / 总脉冲清零（重新标定「每圈脉冲」用）
            menu_run_draw(page);
        }
        else
        {
            run_frozen  = (0 != run_frozen) ? (0) : (1);
            run_text_ok = 0;
            menu_run_draw(page);
        }
        return 0;
    }

    if (MENU_RUN_PAGE_RACE == page)
    {
        if (0 != k_ok)                                      // 按键3：启动 / 停车
        {
            car_board_status(&st);
            if (CAR_RUNNING == st.state) { car_board_stop(); }
            else
            {
                if (0 == car_board_start())                  // 图像不好 / IMU 没准备好会失败
                {
                    printf("start refused: need %d good frames\r\n", CAR_START_GOOD_FRAMES);
                }
            }
        }
        if ((0 != k_up) || (0 != k_down))                   // 只有停车时才允许调曝光
        {
            car_board_status(&st);
            if (CAR_RUNNING != st.state)
            {
                menu_param_set(MENU_PARAM_IDX_CAM_EXP,
                               menu_param_get(MENU_PARAM_IDX_CAM_EXP) + ((0 != k_up) ? (20) : (-20)));
            }
        }
    }
    else
    {
        if (0 != k_ok)                                      // 按键3：灰度 / 二值
        {
            menu_param_set(MENU_PARAM_IDX_CAM_VIEW,
                           (0 != menu_param_get(MENU_PARAM_IDX_CAM_VIEW)) ? (0) : (1));
        }
        if ((0 != k_up) || (0 != k_down))
        {
            menu_param_set(MENU_PARAM_IDX_CAM_EXP,
                           menu_param_get(MENU_PARAM_IDX_CAM_EXP) + ((0 != k_up) ? (20) : (-20)));
        }
    }

    run_view = (0 != menu_param_get(MENU_PARAM_IDX_CAM_VIEW)) ? (1) : (0);
    run_sync_param();
    menu_run_draw(page);
    return 0;
}
