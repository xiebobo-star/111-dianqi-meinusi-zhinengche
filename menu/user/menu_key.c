/*********************************************************************************************************************
* 文件名            menu_key.c
* 说明              按键扫描实现（自己写的，不依赖 zf_device_key 的扫描，原因见 menu_key.h 顶部说明）
*
*                    扫描流程（每 MENU_KEY_SCAN_PERIOD_MS 进一次 10ms 定时中断）：
*                      原始电平 -> 连续 MENU_KEY_DEBOUNCE_CNT 次一致才算稳定 -> 状态翻转时产生事件
*                      按下：清计时、开始算按住时长
*                      按住：满 MENU_KEY_LONG_MS 报一次长按；之后每隔 MENU_KEY_REPEAT_MS 报一次连发
*                      松开：没报过长按的话，报一次短按（所以「点一下就松」绝对不会丢）
*
*                    事件都是锁存的：置位后一直保持，直到主循环用 menu_key_short/long/repeat 读走
********************************************************************************************************************/
#include "menu_config.h"
#include "menu_key.h"

// 逻辑键号 -> 实际引脚（数组顺序 = 板上按键1~4，按键自检页按这个顺序显示）
static const gpio_pin_enum mk_gpio[MENU_KEY_NUM] =
{
    MENU_KEY_PIN_UP, MENU_KEY_PIN_DOWN, MENU_KEY_PIN_OK, MENU_KEY_PIN_BACK
};

// 按键按下时是什么电平（0 = 低电平按下，板上按键一端接地、另一端上拉）
#if (0 == MENU_KEY_ACTIVE_LEVEL)
#define MK_IDLE_LEVEL           (GPIO_HIGH)
#define MK_PULL_MODE            (GPI_PULL_UP)
#else
#define MK_IDLE_LEVEL           (GPIO_LOW)
#define MK_PULL_MODE            (GPI_PULL_DOWN)
#endif

static uint32   mk_period_ms;                           // 扫描周期 ms
static uint8    mk_stable[MENU_KEY_NUM];                // 消抖后的状态 1 = 按住
static uint8    mk_raw[MENU_KEY_NUM];                   // 最近一次原始电平 1 = 按下
static uint8    mk_change_cnt[MENU_KEY_NUM];            // 电平连续变化次数（消抖）
static uint32   mk_hold_ms[MENU_KEY_NUM];               // 这一轮已经按住多少 ms
static uint32   mk_next_repeat_ms[MENU_KEY_NUM];        // 下一次连发的按住时长
static uint8    mk_ev_short[MENU_KEY_NUM];              // 短按事件（锁存）
static uint8    mk_ev_long[MENU_KEY_NUM];               // 长按事件（锁存）
static uint8    mk_ev_repeat[MENU_KEY_NUM];             // 连发事件（锁存）
static uint8    mk_long_fired[MENU_KEY_NUM];            // 这一轮长按已经报过
static uint32   mk_cnt[MENU_KEY_NUM];                   // 每个键累计次数
static uint32   mk_cnt_total;                           // 4 个键累计次数
static char     mk_name_buf[8];

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     读一个键的原始状态
// 返回参数     1 = 按下（已经按 MENU_KEY_ACTIVE_LEVEL 换算过）
//-------------------------------------------------------------------------------------------------------------------
static uint8 mk_sample (uint8 key)
{
    return (MENU_KEY_ACTIVE_LEVEL == (uint8)gpio_get_level(mk_gpio[key])) ? (1) : (0);
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     按键初始化
// 参数说明     period_ms   扫描周期 ms，必须和调用 menu_key_scan() 的定时中断周期一致
//-------------------------------------------------------------------------------------------------------------------
void menu_key_init (uint32 period_ms)
{
    uint8 i;

    if (0 == period_ms)
    {
        period_ms = 10;
    }
    mk_period_ms = period_ms;

    for (i = 0; i < MENU_KEY_NUM; i ++)
    {
        gpio_init(mk_gpio[i], GPI, MK_IDLE_LEVEL, MK_PULL_MODE);

        mk_stable[i]        = 0;
        mk_raw[i]           = 0;
        mk_change_cnt[i]    = 0;
        mk_hold_ms[i]       = 0;
        mk_next_repeat_ms[i]= 0;
        mk_ev_short[i]      = 0;
        mk_ev_long[i]       = 0;
        mk_ev_repeat[i]     = 0;
        mk_long_fired[i]    = 0;
        mk_cnt[i]           = 0;
    }
    mk_cnt_total = 0;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     按键扫描（10ms 定时中断里调用）
//-------------------------------------------------------------------------------------------------------------------
void menu_key_scan (void)
{
    uint8 i;

    for (i = 0; i < MENU_KEY_NUM; i ++)
    {
        uint8 now = mk_sample(i);

        mk_raw[i] = now;

        if (now != mk_stable[i])                                        // 电平在变：累计变化次数
        {
            mk_change_cnt[i] ++;

            if (mk_change_cnt[i] >= MENU_KEY_DEBOUNCE_CNT)              // 连续几次都一样，认账
            {
                mk_change_cnt[i] = 0;
                mk_stable[i]     = now;

                if (0 != now)                                           // 刚按下
                {
                    mk_hold_ms[i]        = 0;
                    mk_next_repeat_ms[i] = MENU_KEY_REPEAT_DELAY_MS;
                    mk_long_fired[i]     = 0;
                }
                else                                                    // 刚松开
                {
                    if (0 == mk_long_fired[i])                          // 没报过长按才算一次短按
                    {
                        mk_ev_short[i] = 1;
                        mk_cnt[i] ++;
                        mk_cnt_total ++;
                    }
                    mk_hold_ms[i] = 0;
                }
            }
        }
        else
        {
            mk_change_cnt[i] = 0;
        }

        if (0 != mk_stable[i])                                          // 按住期间：长按 + 连发
        {
            mk_hold_ms[i] += mk_period_ms;

            if ((mk_hold_ms[i] >= MENU_KEY_LONG_MS) && (0 == mk_long_fired[i]))
            {
                mk_long_fired[i] = 1;
                mk_ev_long[i]    = 1;
                mk_cnt[i] ++;
                mk_cnt_total ++;
            }

            if (mk_hold_ms[i] >= mk_next_repeat_ms[i])
            {
                mk_ev_repeat[i]       = 1;
                mk_next_repeat_ms[i] += MENU_KEY_REPEAT_MS;
            }
        }
    }
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     短按事件（读到就清掉）
//-------------------------------------------------------------------------------------------------------------------
uint8 menu_key_short (uint8 key)
{
    if ((MENU_KEY_NUM <= key) || (0 == mk_ev_short[key])) { return 0; }
    mk_ev_short[key] = 0;
    return 1;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     长按事件（读到就清掉）
//-------------------------------------------------------------------------------------------------------------------
uint8 menu_key_long (uint8 key)
{
    if ((MENU_KEY_NUM <= key) || (0 == mk_ev_long[key])) { return 0; }
    mk_ev_long[key] = 0;
    return 1;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     按住连发事件（读到就清掉）
//-------------------------------------------------------------------------------------------------------------------
uint8 menu_key_repeat (uint8 key)
{
    if ((MENU_KEY_NUM <= key) || (0 == mk_ev_repeat[key])) { return 0; }
    mk_ev_repeat[key] = 0;
    return 1;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     现在是否按住
//-------------------------------------------------------------------------------------------------------------------
uint8 menu_key_down (uint8 key)
{
    if (MENU_KEY_NUM <= key) { return 0; }
    return mk_stable[key];
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     最近一次原始电平（1 = 按下），按键自检页用：能看出「线有没有接上」
//-------------------------------------------------------------------------------------------------------------------
uint8 menu_key_raw (uint8 key)
{
    if (MENU_KEY_NUM <= key) { return 0; }
    return mk_raw[key];
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     某个键累计按了多少次
//-------------------------------------------------------------------------------------------------------------------
uint32 menu_key_cnt (uint8 key)
{
    if (MENU_KEY_NUM <= key) { return 0; }
    return mk_cnt[key];
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     4 个键一共按了多少次
//-------------------------------------------------------------------------------------------------------------------
uint32 menu_key_press_cnt (void)
{
    return mk_cnt_total;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     这个键接在哪个引脚（按键自检页显示用，如 "P20_6"）
// 备注信息     Pxx_y：引脚枚举值 = 端口号 x 32 + 位号，所以高位是端口、低 5 位是位号
//-------------------------------------------------------------------------------------------------------------------
const char *menu_key_pin_name (uint8 key)
{
    uint16 pin;

    if (MENU_KEY_NUM <= key) { return "?"; }

    pin = (uint16)mk_gpio[key];
    snprintf(mk_name_buf, sizeof(mk_name_buf), "P%02d_%d", (int)(pin >> 5), (int)(pin & 0x1F));
    return mk_name_buf;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     清掉所有还没读走的事件（不清累计次数）
//-------------------------------------------------------------------------------------------------------------------
void menu_key_clear_all (void)
{
    uint8 i;

    for (i = 0; i < MENU_KEY_NUM; i ++)
    {
        mk_ev_short[i]  = 0;
        mk_ev_long[i]   = 0;
        mk_ev_repeat[i] = 0;
    }
}
