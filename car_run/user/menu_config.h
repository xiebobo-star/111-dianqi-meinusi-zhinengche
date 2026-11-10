/*********************************************************************************************************************
* 文件名            menu_config.h
* 说明              TC264 多级菜单 + 参数掉电保存 —— 引脚、屏幕、按键、Flash 全部配置都在这一个文件里
*                   以后要改引脚 / 换屏幕 / 换页号，只改这一个文件，其它 .c 不用动
* 硬件              主板「屏幕1」接口（SPI2）+ 4 个按键 + TC264 片内 DFlash（不用外挂存储芯片）
* 开发环境          AURIX Development Studio 1.10.40 / 逐飞 TC264 开源库
* 日期              2026-10-16
********************************************************************************************************************/
#ifndef _menu_config_h_
#define _menu_config_h_

#include "zf_common_headfile.h"

// ============================================ 1. 屏幕 ============================================
// 主板丝印 -> TC264 引脚：SCK=P15_3  MOSI=P15_5  CS=P15_2  RST=P15_1  DC=P15_0  BLK=P15_4
// 逐飞库 zf_device_ips200.h / zf_device_tft180.h 的默认引脚正好是这一组，库文件不用改
#define MENU_DISPLAY_USE_IPS200     (0)                         // 1 = 2 寸 IPS200（SPI 驱动，工程默认）
                                                                // 0 = 1.8 寸 TFT180
                                                                // ★ 两块屏指令不通用，手上是哪块就改成哪块

// 屏幕背光：主板屏幕接口的 BLK 脚由程序控制（P15_4），ips200_init() 里已经拉高，这里不用管
// 如果用的是 TFT180，屏幕跑到一半黑掉，先怀疑供电（必须 3.3V，别接 5V）

// ============================================ 2. 按键 ============================================
// 这一版按键是工程里自己扫描的（user/menu_key.c），引脚直接写在这儿，换键/换板只改这几行。
// 4 个按键各管一件事：
//   按键1（P20_6）  上选 / 修改时加     按住连发
//   按键2（P20_7）  下选 / 修改时减     按住连发
//   按键3（P11_2）  确定（进入 / 确认 / 弹窗确认）
//   按键4（P11_3）  返回（退一层 / 取消修改 / 取消弹窗）   长按 = 直接回主菜单
#define MENU_KEY_PIN_UP             (P20_6)                     // 按键1 -> 上选
#define MENU_KEY_PIN_DOWN           (P20_7)                     // 按键2 -> 下选
#define MENU_KEY_PIN_OK             (P11_2)                     // 按键3 -> 确定
#define MENU_KEY_PIN_BACK           (P11_3)                     // 按键4 -> 返回（长按回主菜单）

#define MENU_KEY_ACTIVE_LEVEL       (0)                         // 0 = 按下是低电平（板上按键一端接地、上拉）
                                                                // 1 = 按下是高电平（外部下拉的板子才用）
#define MENU_KEY_SCAN_PERIOD_MS     (10)                        // 扫描周期 ms，必须与 10ms 定时中断一致
#define MENU_KEY_DEBOUNCE_CNT       (2)                         // 连续 2 次(=20ms)电平一样才认，消抖
#define MENU_KEY_LONG_MS            (800)                       // 按住多久算长按（长按按键4 = 回主菜单）
#define MENU_KEY_REPEAT_DELAY_MS    (450)                       // 按住多久开始连发（上下选快速翻）
#define MENU_KEY_REPEAT_MS          (90)                        // 连发间隔

// 按键自检页（系统信息 -> 按键自检）：按哪个键，屏上对应那行就变「按住」，还能看到每个键的次数。
// 某个键按下去次数一直是 0 就是那个键/那根线的问题，跟菜单程序无关。

// ============================================ 3. 片内 Flash（参数掉电保存） ============================================
// TC264 内置 DFlash：96KB 一共 12 页，每页 4KB（= 1024 个 uint32）
// 这里用最后两页做「双页备份」：每次保存写另一页，写的时候另一页还是完整的，
// 所以保存过程中断电，最差也只是丢掉这一次修改，不会把参数全丢
#define MENU_FLASH_PAGE_A           (10)                        // 参数主份页  参数范围 0-11
#define MENU_FLASH_PAGE_B           (11)                        // 参数备份页

// 自动保存：参数改完不用再按「保存」，停手这么久（ms）就自动写进 Flash；
// 退出修改界面 / 回上一层 / 回主菜单时还会立刻写一次，所以「退出去那一下」就存好了
#define MENU_AUTOSAVE_IDLE_MS       (800)

// ============================================ 4. 界面排版 ============================================
#if (MENU_DISPLAY_USE_IPS200)
    // 2 寸 IPS200：240 x 320，一屏 13 行
#define MENU_UI_ROW_H               (20)                        // 每行高度（16 像素字 + 4 像素间距）
#define MENU_UI_TITLE_Y             (2)                         // 标题 y 坐标
#define MENU_UI_ITEM_Y0             (38)                        // 第一行菜单项的 y 坐标
#define MENU_UI_BAR_H               (18)                        // 选中高亮条高度
#define MENU_UI_LINE_H              (20)                        // 底部提示行占的高度
#define MENU_UI_INFO_Y0             (32)                        // 信息页第一行的 y
#define MENU_UI_INFO_LINE_H         (20)                        // 信息页行距
#define MENU_UI_INFO_BOTTOM         (292)                       // 信息页最后一行允许的起始 y（再往下就压到提示行了）
#else
    // 1.8 寸 TFT180：128 x 160，一屏 7 行（行高压到 17 像素，舵机那组的 7 项刚好一屏放完）
#define MENU_UI_ROW_H               (17)                        // 每行高度（16 像素字 + 1 像素间距）
#define MENU_UI_TITLE_Y             (2)                         // 标题 y 坐标
#define MENU_UI_ITEM_Y0             (22)                        // 第一行菜单项的 y 坐标
#define MENU_UI_BAR_H               (17)                        // 选中高亮条高度（和行高一样，否则字最下面一行掉在条外）
#define MENU_UI_LINE_H              (18)                        // 底部提示行占的高度（提示画在 160-18+2=144，16 像素字正好到 159）
#define MENU_UI_INFO_Y0             (22)                        // 信息页第一行的 y（再往上会压到标题）
#define MENU_UI_INFO_LINE_H         (17)                        // 信息页行距（16 像素字 + 1 像素缝）
#define MENU_UI_INFO_BOTTOM         (124)                       // 信息页最后一行允许的起始 y（22+6*17，16 像素字到 139）
#endif
#define MENU_TOAST_MS               (1500)                      // 提示框（保存成功之类）自动消失时间 ms

// 界面配色
#define MENU_COLOR_PEN              (RGB565_WHITE)              // 普通文字
#define MENU_COLOR_BG               (RGB565_BLACK)              // 背景
#define MENU_COLOR_SEL              (RGB565_BLUE)               // 选中项高亮条
#define MENU_COLOR_EDIT             (RGB565_YELLOW)             // 正在修改的项 / 弹窗边框
#define MENU_COLOR_TITLE            (RGB565_GREEN)              // 标题
#define MENU_COLOR_OK               (RGB565_GREEN)              // 提示框：成功
#define MENU_COLOR_ERR              (RGB565_RED)                // 提示框：失败

// ============================================ 5. 核心板 LED ============================================
// 核心板 4 个 LED 里 P21_5 / P21_4 被电机信号接口占了，这里只用 P20_9 / P20_8
// 用途：参数保存成功时闪一下（可以用参数「LED指示」关掉），顺便证明程序真的在跑
#define MENU_LED1_PIN               (P20_9)
#define MENU_LED2_PIN               (P20_8)
#define MENU_LED_ON_LEVEL           (0)                         // 核心板 LED 低电平点亮（高电平点亮的板子改成 1）
#define MENU_LED1_ON()              gpio_set_level(MENU_LED1_PIN, MENU_LED_ON_LEVEL)
#define MENU_LED1_OFF()             gpio_set_level(MENU_LED1_PIN, !MENU_LED_ON_LEVEL)
#define MENU_LED2_ON()              gpio_set_level(MENU_LED2_PIN, MENU_LED_ON_LEVEL)
#define MENU_LED2_OFF()             gpio_set_level(MENU_LED2_PIN, !MENU_LED_ON_LEVEL)

// ============================================ 6. 参数表下标（和 menu_param.c 里的表一一对应） ============================================
// ★ 改参数表就必须一起改这里：加一项后面的全部往后挪
// 参数表一共 5 组 29 项（组号 = menu_param.c 里 menu_param_group_table[] 的顺序）

// ---- 第 1 组：舵机参数（下标 0~5）----
#define MENU_PARAM_IDX_SERVO_MID    (0)                         // 中位脉宽 us（BDS300 装车后必须标定）
#define MENU_PARAM_IDX_SERVO_LEFT   (1)                         // 左限脉宽 us
#define MENU_PARAM_IDX_SERVO_RIGHT  (2)                         // 右限脉宽 us
#define MENU_PARAM_IDX_STEER_KP     (3)                         // 转向 PD 的 Kp（us / 归一化误差）
#define MENU_PARAM_IDX_STEER_KD     (4)                         // 转向 PD 的 Kd
#define MENU_PARAM_IDX_TURN_RATIO   (5)                         // 差速比例 %（弯道外轮比内轮快多少）

// ---- 第 2 组：速度 / 编码器参数（下标 6~13）----
#define MENU_PARAM_IDX_SPEED_STRAIGHT (6)                       // 直道目标速度 mm/s
#define MENU_PARAM_IDX_SPEED_BEND     (7)                       // 弯道目标速度 mm/s
#define MENU_PARAM_IDX_SPEED_CAUTIOUS (8)                       // 谨慎速度（置信度低 / 看不清远端）mm/s
#define MENU_PARAM_IDX_SPEED_KP       (9)                       // 速度 PI 的 Kp（单位：PWM / 每 10ms 脉冲数）
#define MENU_PARAM_IDX_SPEED_KI       (10)                      // 速度 PI 的 Ki
#define MENU_PARAM_IDX_PWM_LIMIT      (11)                      // PWM 上限（10000 = 100%）
#define MENU_PARAM_IDX_ENC_CPR        (12)                      // 编码器：车轮转一圈多少脉冲（PD1503 标定用）
#define MENU_PARAM_IDX_WHEEL          (13)                      // 轮径 0.1mm（里程 / 速度换算）

// ---- 第 3 组：摄像头参数（下标 14~16）----
#define MENU_PARAM_IDX_CAM_EXP        (14)                      // 曝光时间
#define MENU_PARAM_IDX_CAM_TH         (15)                      // 二值阈值（0 = 自动大津法）
#define MENU_PARAM_IDX_CAM_VIEW       (16)                      // 显示：0 灰度 1 二值

// ---- 第 4 组：圆环参数（下标 17~20）----
#define MENU_PARAM_IDX_RING_EN        (17)                      // 圆环功能开关
#define MENU_PARAM_IDX_RING_DIR       (18)                      // 入环方向：0 自动 / -1 左 / +1 右
#define MENU_PARAM_IDX_RING_SPEED     (19)                      // 环内限速 mm/s
#define MENU_PARAM_IDX_RING_TIMEOUT   (20)                      // 圆环超时 s（超了就故障停车）

// ---- 第 5 组：系统参数（下标 21~28）----
#define MENU_PARAM_IDX_LED            (21)                      // 「LED指示」开关
#define MENU_PARAM_IDX_SERIAL         (22)                      // 「串口调试」开关
#define MENU_PARAM_IDX_BOOT_DELAY     (23)                      // 「上电延时」ms
#define MENU_PARAM_IDX_MOTOR_INV_L    (24)                      // 左电机方向取反
#define MENU_PARAM_IDX_MOTOR_INV_R    (25)                      // 右电机方向取反
#define MENU_PARAM_IDX_ENC_INV_L      (26)                      // 左编码器方向取反
#define MENU_PARAM_IDX_ENC_INV_R      (27)                      // 右编码器方向取反
#define MENU_PARAM_IDX_ENC_FILTER     (28)                      // 编码器速度滤波 0~90%（0 = 不滤波）

// 组号（menu_param_group_table[] 的下标）
#define MENU_GROUP_SERVO            (0)
#define MENU_GROUP_SPEED            (1)
#define MENU_GROUP_CAMERA           (2)
#define MENU_GROUP_RING             (3)
#define MENU_GROUP_SYSTEM           (4)

// ============================================ 8. 串口调试输出 ============================================
// 1 = 每次按键 / 保存动作同时往调试串口（UART0，P14_0/P14_1，115200）打一行 ASCII 日志
// 屏幕还没调通的时候，靠串口也能确认菜单逻辑跑没跑
#define MENU_SERIAL_DEBUG           (1)

// ============================================ 9. 版本号（系统信息页显示） ============================================
#define MENU_VERSION_STR            "V1.0 循迹跑车"

#endif
