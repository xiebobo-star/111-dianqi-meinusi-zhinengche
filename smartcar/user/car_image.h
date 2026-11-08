/*********************************************************************************************************************
* TC264 智能车 —— 摄像头图像识别（巡线）模块  car_image.h
*
* 这个模块只干一件事：看图。
*     灰度图 -> 二值化 -> 逐行找线 -> 算出每行中线 -> 算出车相对赛道的偏差 -> 给一个位置状态
*
* 它不碰电机、不碰舵机、不碰屏幕、不进中断，所以可以单独测、上层想怎么控制都行。
*
* 用法（三行）
*     car_image_init();                                                                    // 上电调用一次
*     if(mt9v03x_finish_flag) { mt9v03x_finish_flag = 0; car_image_process(&info); }        // 每来一帧图算一次
*     // 之后读 info.err_norm（-1000~1000 的偏差）、info.state（位置状态）、info.center[]（每行中线）
*
* 图像坐标约定（后面所有代码都按这个来，记住它）
*     mt9v03x_image[row][col]
*         row = 0     图像最上面  -> 看得最远
*         row = 119   图像最下面  -> 贴着车头
*         col = 0     最左边
*         col = 187   最右边
*
*     偏差 err = 本帧中线 - 图像中心(94)
*         err > 0  ->  赛道中心在图像右边  ->  车偏左了  ->  方向要往右打
*         err < 0  ->  赛道中心在图像左边  ->  车偏右了  ->  方向要往左打
********************************************************************************************************************/
#ifndef _car_image_h_
#define _car_image_h_

#include "zf_common_headfile.h"

//=================================================== 一、只改这一段 ===================================================

#define CAR_IMG_MODE_SINGLE         (0)                         // 单线：场上只有一条黑线，车跟着线走
#define CAR_IMG_MODE_DOUBLE         (1)                         // 双线：赛道左右各一条黑边线，走两条线中间

#define CAR_IMG_MODE                (CAR_IMG_MODE_SINGLE)       // 你的赛道是哪种，就选哪个

#define CAR_IMG_LINE_DARK           (1)                         // 1 = 白底黑线（线比背景暗，最常见）
                                                                // 0 = 黑底白线（线比背景亮）

#define CAR_IMG_SCAN_STEP           (2)                         // 每隔几行扫一行：2 = 120 行里扫 60 行
                                                                // 够用而且快一倍；想更细就改 1

#define CAR_IMG_SEG_MIN_LEN         (4)                         // 一行里连续几个"线的颜色"才算一段真线
                                                                // 太小会把噪点当线，太大细线会丢

#define CAR_IMG_SEG_FULL_LEN        (170)                       // 一段线长到这么多像素（几乎整行）就当"整行全黑/全白"
                                                                // 冲出赛道、镜头被挡、太阳直射过曝就是这种图

#define CAR_IMG_JUMP_MAX            (40)                        // 单线模式：本行线中心离"上一行中心"最远允许多少像素
                                                                // 超过就认为找错了，判丢线

#define CAR_IMG_WIDTH_MIN           (20)                        // 双线模式：两条边线的最小距离（像素）
#define CAR_IMG_WIDTH_MAX           (170)                       // 双线模式：两条边线的最大距离（像素）

#define CAR_IMG_TH_MIN              (30)                        // 大津法阈值的下限（防止极端光照算出离谱的值）
#define CAR_IMG_TH_MAX              (200)                       // 大津法阈值的上限

#define CAR_IMG_TH_LOWPASS_SHIFT    (2)                         // 阈值低通：新阈值 = (旧阈值*(2^N-1) + 新阈值) / 2^N
                                                                // 2 -> 新的只占 1/4。阈值一帧一跳边线就会抖

#define CAR_IMG_WEIGHT_NEAR         (2)                         // 近端偏差权重（车现在在哪儿）
#define CAR_IMG_WEIGHT_MID          (3)                         // 中端偏差权重
#define CAR_IMG_WEIGHT_FAR          (5)                         // 远端偏差权重（前面往哪儿拐，提前预判）
                                                                // 直道上左右画龙：把 NEAR 加大、FAR 减小

#define CAR_IMG_VALID_MIN_ROWS      (5)                         // 一帧里至少几行找到线才算有效，少于此数判丢线

#define CAR_IMG_ERR_NORM_STRAIGHT   (120)                       // 归一化偏差 |e| < 120 算"在中间"
#define CAR_IMG_ERR_NORM_BIG        (500)                       // 归一化偏差 |e| > 500 算"大弯"

//=================================================== 二、下面不用改 ===================================================

#define CAR_IMG_W                   (MT9V03X_W)                             // 图像宽 188
#define CAR_IMG_H                   (MT9V03X_H)                             // 图像高 120
#define CAR_IMG_CENTER              (CAR_IMG_W / 2)                         // 图像中心列 94
#define CAR_IMG_SCAN_ROWS           (CAR_IMG_H / CAR_IMG_SCAN_STEP)         // 要扫的行数 60
#define CAR_IMG_SEG_MAX             (8)                                     // 一行最多认 8 段线，再多就是噪点

// 三段偏差分别取哪一行（0 = 最远，CAR_IMG_SCAN_ROWS-1 = 最近）
#define CAR_IMG_ROW_FAR             (CAR_IMG_SCAN_ROWS *  8 / 100)          // 远端
#define CAR_IMG_ROW_MID             (CAR_IMG_SCAN_ROWS * 45 / 100)          // 中端
#define CAR_IMG_ROW_NEAR            (CAR_IMG_SCAN_ROWS - 8)                 // 近端

// 每一行的可信程度
#define CAR_ROW_FLAG_LOST           (0)                                     // 没找到线，值是"撑"出来的
#define CAR_ROW_FLAG_REAL           (1)                                     // 真找到的
#define CAR_ROW_FLAG_FIX            (2)                                     // 只看到一条边线，另一边是补出来的

//=================================================== 位置状态 ===================================================

typedef enum
{
    CAR_STATE_LOST      = 0,                                            // 整帧没找到线（冲出赛道 / 全黑全白）
    CAR_STATE_CENTER    = 1,                                            // 赛道在正前方，直道
    CAR_STATE_LEFT      = 2,                                            // 赛道中心偏左 -> 车偏右了，要往左打
    CAR_STATE_RIGHT     = 3,                                            // 赛道中心偏右 -> 车偏左了，要往右打
    CAR_STATE_LEFT_BIG  = 4,                                            // 大幅偏左
    CAR_STATE_RIGHT_BIG = 5,                                            // 大幅偏右
}car_image_state_enum;

//=================================================== 识别结果 ===================================================

typedef struct
{
    uint8  valid;                                                       // 1 = 这一帧找到线
    uint8  state;                                                       // car_image_state_enum 位置状态
    uint16 threshold;                                                   // 本帧用的二值化阈值
    uint8  valid_rows;                                                  // 找到线的扫描行数
    uint8  lost_rows;                                                   // 丢线的扫描行数

    uint8  left  [CAR_IMG_SCAN_ROWS];                                   // 每行左边线列坐标
    uint8  right [CAR_IMG_SCAN_ROWS];                                   // 每行右边线列坐标
    uint8  center[CAR_IMG_SCAN_ROWS];                                   // 每行中线列坐标
    uint8  width [CAR_IMG_SCAN_ROWS];                                   // 每行赛道宽度（单线模式下是线宽）
    uint8  row_flag[CAR_IMG_SCAN_ROWS];                                 // 每行可信程度 CAR_ROW_FLAG_xxx

    uint8  near_center;                                                 // 近端中线
    uint8  far_center;                                                  // 远端中线

    int16  err_near;                                                    // 近端偏差（像素）
    int16  err_mid;                                                     // 中端偏差（像素）
    int16  err_far;                                                     // 远端偏差（像素）
    int16  err;                                                         // 三段加权后的偏差（像素）
    int16  err_norm;                                                    // 归一化偏差 -1000 ~ +1000
}car_image_info_struct;

//=================================================== 对外函数 ===================================================

void        car_image_init            (void);
void        car_image_process         (car_image_info_struct *info);
void        car_image_set_threshold   (uint16 threshold);
uint16      car_image_get_threshold   (void);
const char *car_image_state_name      (uint8 state);

#endif
