/*********************************************************************************************************************
* 文件名            cpu0_main.c
* 说明              按键引脚测试工程（keytest）—— 只干一件事：把板子上「可能接按键」的引脚全配成上拉输入，
*                   屏幕 + 串口同时显示「哪个脚现在是低电平（= 按键按下）」「累计按了几次」。
*
*                   为什么要有这个工程：
*                     菜单工程里「确定」放在 P11_2 上一直没反应，光看菜单分不清是
*                     「菜单程序没处理这个键」还是「这颗按键/这根线根本没接到 P11_2」。
*                     这个工程没有任何菜单逻辑、没有中断、没有 Flash，只有 读引脚 + 显示，
*                     按一下哪个脚变 0，就把那个脚的名字打在屏幕上 —— 一测就知道。
*
*                   用法：上电 → 按板子上的每个按键（每个按 2~3 下）
*                         屏幕会看到某一行从 1 变 0、右边计数 +1，那一行的引脚名就是这个按键真正的引脚
*                         屏幕没亮就看串口（P14_0/P14_1，115200），会打 "KEY P11_3 DOWN cnt=1"
*
*                   屏幕型号：1 = 2 寸 IPS200（默认）   0 = 1.8 寸 TFT180（两块屏驱动不通用）
********************************************************************************************************************/
#include "zf_common_headfile.h"

#define KEYTEST_USE_IPS200          (1)                         // 1 = IPS200 (240x320)   0 = TFT180 (128x160)

#define KT_NUM_MAX                  (12)                        // 最多测几个脚
#define KT_ROW_H                    (20)                        // 每行高度（8x16 字体）
#define KT_Y0                       (40)                        // 第一行的 y

typedef struct
{
    gpio_pin_enum pin;
    const char   *name;
}kt_pin_struct;

// 要测的引脚：前 4 个是「按主板丝印假设的 4 个按键」，后面几个是「万一按键接在别处」的备选
static const kt_pin_struct kt_pin[KT_NUM_MAX] =
{
    {P20_6,  "P20_6"},                                          // 假设 = 按键1
    {P20_7,  "P20_7"},                                          // 假设 = 按键2
    {P11_2,  "P11_2"},                                          // 假设 = 按键3（说这个不灵的那个）
    {P11_3,  "P11_3"},                                          // 假设 = 按键4
    {P20_5,  "P20_5"},                                          // ↓ 备选：板子上别的按键/拨码可能接在这些脚
    {P20_4,  "P20_4"},
    {P11_9,  "P11_9"},
    {P11_8,  "P11_8"},
    {P33_11, "P33_11"},                                         // 主板拨码 SWITCH1
    {P33_12, "P33_12"},                                         // 主板拨码 SWITCH2
    {P02_5,  "P02_5"},
    {P02_4,  "P02_4"},
};

#define KT_NUM          ((uint8)(sizeof(kt_pin) / sizeof(kt_pin[0])))

static uint8    kt_now[KT_NUM_MAX];                             // 1 = 高电平(松开)  0 = 低电平(按下)
static uint8    kt_shown[KT_NUM_MAX];                           // 屏幕上已经画出来的电平
static uint32   kt_cnt[KT_NUM_MAX];                             // 累计按下次数
static uint32   kt_shown_cnt[KT_NUM_MAX];                       // 屏幕上已经画出来的次数
static sint8    kt_last;                                        // 最后按下的那个引脚下标（-1 = 还没有）
static sint8    kt_last_shown;

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     画一行（引脚名 + 当前电平 + 次数）
// 备注信息     电平是 0（按下）时整行用红色，平时白色
//-------------------------------------------------------------------------------------------------------------------
static void kt_draw_row (uint8 i)
{
    uint16 y   = (uint16)(KT_Y0 + i * KT_ROW_H);
    uint16 pen = (0 == kt_now[i]) ? (RGB565_RED) : (RGB565_WHITE);

#if (1 == KEYTEST_USE_IPS200)
    ips200_set_color(pen, RGB565_BLACK);
    ips200_show_string(0, y, kt_pin[i].name);
    ips200_show_int(80, y, (int32)kt_now[i], 1);
    ips200_show_int(112, y, (int32)kt_cnt[i], 4);
#else
    tft180_set_color(pen, RGB565_BLACK);
    tft180_show_string(0, y, kt_pin[i].name);
    tft180_show_int(80, y, (int32)kt_now[i], 1);
    tft180_show_int(96, y, (int32)kt_cnt[i], 3);
#endif
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     画「最后按下的脚」那一行（放在屏幕最下面）
//-------------------------------------------------------------------------------------------------------------------
static void kt_draw_last (void)
{
    uint16 y = (uint16)(KT_Y0 + KT_NUM * KT_ROW_H + 8);

#if (1 == KEYTEST_USE_IPS200)
    ips200_set_color(RGB565_GREEN, RGB565_BLACK);
    ips200_show_string(0, y, "LAST:");
    if (kt_last >= 0)
    {
        ips200_show_string(48, y, kt_pin[kt_last].name);
    }
    else
    {
        ips200_show_string(48, y, "----");
    }
#else
    tft180_set_color(RGB565_GREEN, RGB565_BLACK);
    tft180_show_string(0, y, "LAST:");
    if (kt_last >= 0)
    {
        tft180_show_string(48, y, kt_pin[kt_last].name);
    }
    else
    {
        tft180_show_string(48, y, "----");
    }
#endif
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     主程序：初始化屏幕 + 把 12 个脚配成上拉输入，然后一直轮询
//-------------------------------------------------------------------------------------------------------------------
int core0_main (void)
{
    uint8 i;

    clock_init();                                               // 时钟（务必保留）
    debug_init();                                               // 调试串口 P14_0/P14_1 115200

#if (1 == KEYTEST_USE_IPS200)
    ips200_init(IPS200_TYPE_SPI);                               // 2 寸 IPS200，SPI 驱动
    ips200_set_dir(IPS200_PORTAIT);                             // 竖屏 240 x 320
    ips200_set_font(IPS200_8X16_FONT);                          // 8x16 ASCII
    ips200_set_color(RGB565_WHITE, RGB565_BLACK);
    ips200_full(RGB565_BLACK);                                  // 清屏（黑底）
    ips200_show_string(0, 0,  "KEY PIN TEST");
    ips200_show_string(0, 20, "pin    L  cnt");
#else
    tft180_init();
    tft180_set_dir(TFT180_PORTAIT);
    tft180_set_font(TFT180_8X16_FONT);
    tft180_set_color(RGB565_WHITE, RGB565_BLACK);
    tft180_full(RGB565_BLACK);
    tft180_show_string(0, 0,  "KEY PIN TEST");
    tft180_show_string(0, 20, "pin    L cnt");
#endif

    for (i = 0; i < KT_NUM; i ++)
    {
        gpio_init(kt_pin[i].pin, GPI, GPIO_HIGH, GPI_PULL_UP);  // 上拉输入：松开读 1，按下读 0
        kt_now[i]        = 1;
        kt_shown[i]      = 0xFF;                                // 强制先画一次
        kt_cnt[i]        = 0;
        kt_shown_cnt[i]  = 0xFFFFFFFF;
        kt_draw_row(i);
    }

    kt_last       = -1;
    kt_last_shown = -2;
    kt_draw_last();

    printf("\r\n===== KEYTEST ready =====\r\n");
    printf("press each key on the board, watch which pin goes 0\r\n");

    cpu_wait_event_ready();                                     // 和 CPU1 同步（CPU1 也在等这一句）

    while (TRUE)
    {
        for (i = 0; i < KT_NUM; i ++)
        {
            uint8 level = (0 == gpio_get_level(kt_pin[i].pin)) ? (0) : (1);   // 0 = 按下

            if (level != kt_now[i])                             // 电平变了：按下就 +1，并打串口
            {
                kt_now[i] = level;

                if (0 == level)
                {
                    kt_cnt[i] ++;
                    kt_last = (sint8)i;
                    printf("KEY %s DOWN cnt=%lu\r\n", kt_pin[i].name, (unsigned long)kt_cnt[i]);
                }
                else
                {
                    printf("KEY %s UP\r\n", kt_pin[i].name);
                }
            }

            if ((kt_now[i] != kt_shown[i]) || (kt_cnt[i] != kt_shown_cnt[i]))  // 只有变化才重画，屏幕不闪
            {
                kt_draw_row(i);
                kt_shown[i]     = kt_now[i];
                kt_shown_cnt[i] = kt_cnt[i];
            }
        }

        if (kt_last != kt_last_shown)
        {
            kt_last_shown = kt_last;
            kt_draw_last();
        }

        system_delay_ms(10);                                    // 10ms 轮询一次，人手按不可能漏
    }
}
