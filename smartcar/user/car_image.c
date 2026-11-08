/*********************************************************************************************************************
* TC264 智能车 —— 摄像头图像识别（巡线）模块  car_image.c
*
* 一帧图的处理流程（这就是整个"识别"的全部内容）
*   1. 取阈值      大津法自动算一个"黑白分界灰度"，加限幅、加低通
*   2. 逐行二值化  把一行 188 个像素按阈值变成 0/1（1 = 线）
*   3. 找线段      在这一行里找出每一段连续的 1，记下起点、终点、中心
*   4. 定中线      单线模式挑一段；双线模式取最左段和最右段的中点
*   5. 算偏差      近 / 中 / 远 三行的中线减图像中心，加权合成
*   6. 判状态      直道 / 左偏 / 右偏 / 大弯 / 丢线
*
* 为什么从下往上扫（而不是从上往下）
*   图像最下面那几行贴着车头，一定看得见线；越往上越远越容易断。
*   从下往上扫，上面某一行断了就可以拿"下面已经算出来的中线"撑住，
*   这就是最简单的跟踪（逐行连续），比每行各自独立去猜要稳得多。
********************************************************************************************************************/
#include "car_image.h"

#pragma section all "cpu0_dsram"
// 下面这些变量只有 CPU0 用，放在 CPU0 的 RAM 里

static uint16 img_threshold  = 0;                                       // 当前阈值（跨帧保持，做低通用）
static uint16 img_manual_th  = 0;                                       // 0 = 自动（大津法）；非 0 = 用这个固定阈值
static uint8  img_last_width = 0;                                       // 上一帧量到的赛道宽度（双线模式补线用）

static uint8  img_row_buf[CAR_IMG_W];                                   // 一行的二值化结果：1 = 线
static uint8  img_seg_start[CAR_IMG_SEG_MAX];                           // 本行第 n 段线的起始列
static uint8  img_seg_end  [CAR_IMG_SEG_MAX];                           // 本行第 n 段线的结束列
static uint8  img_seg_center[CAR_IMG_SEG_MAX];                          // 本行第 n 段线的中心列
static uint8  img_seg_count = 0;                                        // 本行找到几段线

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     大津法（Otsu）自动阈值
// 参数说明     image        图像首地址，传 &mt9v03x_image[0][0]
// 返回参数     uint16       阈值 0~255
// 逻辑说明
//     把 0~255 每一个灰度都当成"切一刀"，把像素分成"暗的一堆"和"亮的一堆"，
//     然后衡量这两堆分得有多开：
//         类间方差 = 暗堆像素数 x 亮堆像素数 x (暗堆平均灰度 - 亮堆平均灰度)^2
//     哪一刀让这个值最大，哪一刀就是阈值。
//     好处：光线变亮变暗它自己会跟着变，不用每换个场地就手调阈值。
//-------------------------------------------------------------------------------------------------------------------
static uint16 img_otsu_threshold (const uint8 *image)
{
    uint16 hist[256];                                                   // 灰度直方图：每个灰度有多少个像素
    uint32 total   = 0;                                                 // 取样像素总数
    uint32 sum     = 0;                                                 // 所有像素灰度之和
    uint32 sum_b   = 0;                                                 // 暗堆灰度之和
    uint32 w_b     = 0;                                                 // 暗堆像素数
    uint32 w_f;                                                         // 亮堆像素数
    float  mean_b;                                                      // 暗堆平均灰度
    float  mean_f;                                                      // 亮堆平均灰度
    float  diff;
    float  var;
    float  var_max = -1.0f;
    uint16 i;
    uint16 row;
    uint16 col;
    uint16 best    = 128;

    for(i = 0; i < 256; i ++)
    {
        hist[i] = 0;
    }

    // 隔行隔列取样：只统计 1/4 的像素，阈值几乎不变，速度快 4 倍
    for(row = 0; row < CAR_IMG_H; row += 2)
    {
        const uint8 *line = image + (uint32)row * CAR_IMG_W;
        for(col = 0; col < CAR_IMG_W; col += 2)
        {
            hist[line[col]] ++;
            total ++;
        }
    }
    if(0 == total)
    {
        return 128;                                                     // 空图，给个中间值
    }

    for(i = 0; i < 256; i ++)
    {
        sum += (uint32)i * hist[i];
    }

    for(i = 0; i < 256; i ++)
    {
        w_b += hist[i];                                                 // 灰度 <= i 的都归到暗堆
        if(0 == w_b)
        {
            continue;                                                   // 暗堆是空的，算不了方差
        }
        w_f = total - w_b;                                              // 剩下的归亮堆
        if(0 == w_f)
        {
            break;                                                      // 亮堆空了，后面不用看了
        }
        sum_b += (uint32)i * hist[i];
        mean_b = (float)sum_b / (float)w_b;
        mean_f = (float)(sum - sum_b) / (float)w_f;
        diff   = mean_b - mean_f;
        var    = (float)w_b * (float)w_f * diff * diff;
        if(var > var_max)
        {
            var_max = var;
            best    = i;
        }
    }
    return best;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     更新本帧阈值：自动（大津法）或手动，加限幅和一阶低通
// 参数说明     image        图像首地址
// 返回参数     uint16       本帧要用的阈值
//-------------------------------------------------------------------------------------------------------------------
static uint16 img_update_threshold (const uint8 *image)
{
    uint16 th;

    if(0 != img_manual_th)                                              // 手动模式：直接用调试时定下的值
    {
        img_threshold = img_manual_th;
        return img_threshold;
    }

    th = img_otsu_threshold(image);
    if(th < CAR_IMG_TH_MIN)                                             // 限幅：极端光照下别让它算出离谱的值
    {
        th = CAR_IMG_TH_MIN;
    }
    if(th > CAR_IMG_TH_MAX)
    {
        th = CAR_IMG_TH_MAX;
    }

    if(0 == img_threshold)                                              // 第一帧没有历史值，直接用
    {
        img_threshold = th;
    }
    else
    {
        // 一阶低通：新阈值只占 1/2^SHIFT，阈值平滑过渡，边线才不会一帧一跳
        img_threshold = (uint16)(((uint32)img_threshold * ((1 << CAR_IMG_TH_LOWPASS_SHIFT) - 1) + th)
                                 >> CAR_IMG_TH_LOWPASS_SHIFT);
    }
    return img_threshold;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     把图像的某一行二值化到 img_row_buf
// 参数说明     image        图像首地址
// 参数说明     row          行号 0~119
// 参数说明     threshold    阈值
// 返回参数     void
// 说明         img_row_buf 里 1 表示"线的颜色"，0 表示背景
//-------------------------------------------------------------------------------------------------------------------
static void img_load_row (const uint8 *image, uint16 row, uint16 threshold)
{
    const uint8 *line = image + (uint32)row * CAR_IMG_W;
    uint16 col;

#if (CAR_IMG_LINE_DARK)
    for(col = 0; col < CAR_IMG_W; col ++)
    {
        img_row_buf[col] = (line[col] <= threshold) ? 1 : 0;            // 不亮于阈值 = 黑线
    }
#else
    for(col = 0; col < CAR_IMG_W; col ++)
    {
        img_row_buf[col] = (line[col] > threshold) ? 1 : 0;             // 亮于阈值 = 白线
    }
#endif
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     在一行里找出所有"连续的线段"
// 返回参数     void
// 说明         结果放在 img_seg_start / img_seg_end / img_seg_center / img_seg_count 里
//              长度小于 CAR_IMG_SEG_MIN_LEN 的段直接丢掉（那是噪点，不是线）
// 逻辑说明     这是最经典的"跳变检测"：一直往右走，碰到 1 就记下起点，
//              再一直走到变回 0 为止，就得到一段 [起点, 终点]。
//-------------------------------------------------------------------------------------------------------------------
static void img_find_segments (void)
{
    uint16 col   = 0;
    uint16 start;

    img_seg_count = 0;

    while((col < CAR_IMG_W) && (img_seg_count < CAR_IMG_SEG_MAX))
    {
        if(0 == img_row_buf[col])                                       // 背景，不是线，跳过
        {
            col ++;
            continue;
        }

        start = col;                                                    // 线的起点
        while((col < CAR_IMG_W) && (0 != img_row_buf[col]))
        {
            col ++;                                                     // 一直走，直到又变回背景
        }

        if((col - start) >= CAR_IMG_SEG_MIN_LEN)                        // 够长才算一段真线
        {
            img_seg_start [img_seg_count] = (uint8)start;
            img_seg_end   [img_seg_count] = (uint8)(col - 1);
            img_seg_center[img_seg_count] = (uint8)((start + col - 1) / 2);
            img_seg_count ++;
        }
    }
}

#if (CAR_IMG_MODE == CAR_IMG_MODE_SINGLE)
//-------------------------------------------------------------------------------------------------------------------
// 函数简介     两个 uint8 的绝对差（单线模式判断"跳变"用）
//-------------------------------------------------------------------------------------------------------------------
static uint8 img_abs_diff (uint8 a, uint8 b)
{
    return (a > b) ? (uint8)(a - b) : (uint8)(b - a);
}
#endif

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     逐行扫描，算出每一行的左 / 右 / 中线
// 参数说明     image        图像首地址
// 参数说明     info         结果结构体
// 返回参数     void
//-------------------------------------------------------------------------------------------------------------------
static void img_scan (const uint8 *image, car_image_info_struct *info)
{
    uint8  r;                                                           // 循环用：从 CAR_IMG_SCAN_ROWS 数到 1
    uint8  last_center;                                                 // 下方最近一次算出来的中线
    uint8  last_width;                                                  // 最近一次量到的赛道宽度
#if (CAR_IMG_MODE == CAR_IMG_MODE_SINGLE)
    uint8  k;                                                           // 线段循环
    uint8  best;                                                        // 挑中的是第几段
    uint8  d;
    uint8  d_best;
#endif

    last_center = CAR_IMG_CENTER;
    last_width  = img_last_width;                                       // 接上一帧的宽度，第一帧是 0

    info->valid_rows = 0;
    info->lost_rows  = 0;

    for(r = CAR_IMG_SCAN_ROWS; r > 0; r --)
    {
        uint8 n = (uint8)(r - 1);                                       // 扫描行序号：0 = 最上面，59 = 最下面

        // 这一组取组里最靠下的那一行（离车最近、最可靠）
        img_load_row(image, (uint16)n * CAR_IMG_SCAN_STEP + (CAR_IMG_SCAN_STEP - 1), info->threshold);
        img_find_segments();

        // 先按"这行丢线"填好：中线沿用下方最近一次的结果
        info->row_flag[n] = CAR_ROW_FLAG_LOST;
        info->left  [n]   = last_center;
        info->right [n]   = last_center;
        info->center[n]   = last_center;
        info->width [n]   = 0;

#if (CAR_IMG_MODE == CAR_IMG_MODE_SINGLE)
        /* ---------------- 单线模式：在所有线段里挑"中心离上一行最近"的那一段 ----------------
           为什么不直接挑最长的一段、或者最中间的一段？
             地面上可能有反光、黑胶带、别人的线。挑离上一行最近的一段，
             等于"顺着线往上摸"，线在画面里是连续的，这是最稳的挑法。*/
        if(img_seg_count > 0)
        {
            uint8 len_best;

            best = 0;
            if(0 == info->valid_rows)
            {
                // 从底部往上第一行看到线：还没有参考，挑最长的一段当起点
                for(k = 1; k < img_seg_count; k ++)
                {
                    if((img_seg_end[k] - img_seg_start[k]) > (img_seg_end[best] - img_seg_start[best]))
                    {
                        best = k;
                    }
                }
                d_best = 0;                                                 // 没有"上一行"，不算跳变
            }
            else
            {
                // 其余行：挑中心离上一行最近的那一段（顺着线往上摸）
                d_best = 255;
                for(k = 0; k < img_seg_count; k ++)
                {
                    d = img_abs_diff(img_seg_center[k], last_center);
                    if(d < d_best)
                    {
                        d_best = d;
                        best   = k;
                    }
                }
            }

            len_best = (uint8)(img_seg_end[best] - img_seg_start[best] + 1);

            // 两个条件都满足才算找到线：跳变不大 + 不是整行全黑
            if((d_best <= CAR_IMG_JUMP_MAX) && (len_best < CAR_IMG_SEG_FULL_LEN))
            {
                info->left  [n]   = img_seg_center[best];
                info->right [n]   = img_seg_center[best];
                info->center[n]   = img_seg_center[best];
                info->width [n]   = (uint8)(img_seg_end[best] - img_seg_start[best] + 1);
                info->row_flag[n] = CAR_ROW_FLAG_REAL;
            }
        }
#else
        /* ---------------- 双线模式：最左一段 = 左边界，最右一段 = 右边界 ---------------- */
        if(img_seg_count >= 2)
        {
            uint8 l = img_seg_center[0];
            uint8 r2 = img_seg_center[img_seg_count - 1];

            if(((r2 - l) >= CAR_IMG_WIDTH_MIN) && ((r2 - l) <= CAR_IMG_WIDTH_MAX))
            {
                info->left  [n]   = l;
                info->right [n]   = r2;
                info->width [n]   = (uint8)(r2 - l);
                info->center[n]   = (uint8)((l + r2) / 2);
                info->row_flag[n] = CAR_ROW_FLAG_REAL;
                last_width        = info->width[n];
            }
        }
        else if((1 == img_seg_count) && (last_width > 0))
        {
            /* 只看到一条边线（另一边被挡住 / 曝光不够）：按"最近一次量到的赛道宽度"把另一边补出来。
               这就是最基础的补线，比赛里进弯道、被前车挡住时天天用。*/
            if(img_seg_center[0] < CAR_IMG_CENTER)                      // 看到的是左边界
            {
                info->left [n] = img_seg_center[0];
                info->right[n] = (uint8)((((uint16)img_seg_center[0] + last_width) < CAR_IMG_W)
                                       ? ((uint16)img_seg_center[0] + last_width) : (CAR_IMG_W - 1));
            }
            else                                                        // 看到的是右边界
            {
                info->right[n] = img_seg_center[0];
                info->left [n] = ((uint16)img_seg_center[0] > last_width)
                               ? (uint8)((uint16)img_seg_center[0] - last_width) : 0;
            }
            info->width [n]   = last_width;
            info->center[n]   = (uint8)((info->left[n] + info->right[n]) / 2);
            info->row_flag[n] = CAR_ROW_FLAG_FIX;
        }
#endif

        if(CAR_ROW_FLAG_LOST == info->row_flag[n])                      // 这一行没找到线
        {
            info->lost_rows ++;
        }
        else
        {
            info->valid_rows ++;
            last_center = info->center[n];                              // 给上面一行当参考
        }
    }

    img_last_width = last_width;                                        // 存起来，下一帧接着用
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     算偏差：近 / 中 / 远 三段 + 归一化
// 参数说明     info         结果结构体
// 返回参数     void
// 逻辑说明
//     近端（贴着车头）：告诉单片机"车现在压在赛道的哪一边" —— 用来纠正位置
//     远端（看得最远）：告诉单片机"前面往哪边拐" —— 用来提前打方向
//     两个都重要，所以远端权重给最大；直道上画龙就反过来调。
//     归一化到 ±1000 是为了以后换分辨率、换摄像头不用重调 PID 参数。
//-------------------------------------------------------------------------------------------------------------------
static void img_calc_error (car_image_info_struct *info)
{
    info->err_far  = (int16)((int16)info->center[CAR_IMG_ROW_FAR]  - (int16)CAR_IMG_CENTER);
    info->err_mid  = (int16)((int16)info->center[CAR_IMG_ROW_MID]  - (int16)CAR_IMG_CENTER);
    info->err_near = (int16)((int16)info->center[CAR_IMG_ROW_NEAR] - (int16)CAR_IMG_CENTER);

    info->err = (int16)(((int32)info->err_near * CAR_IMG_WEIGHT_NEAR
                       + (int32)info->err_mid  * CAR_IMG_WEIGHT_MID
                       + (int32)info->err_far  * CAR_IMG_WEIGHT_FAR)
                       / (CAR_IMG_WEIGHT_NEAR + CAR_IMG_WEIGHT_MID + CAR_IMG_WEIGHT_FAR));

    info->err_norm = (int16)((int32)info->err * 1000 / CAR_IMG_CENTER);
    if(info->err_norm >  1000)
    {
        info->err_norm =  1000;
    }
    if(info->err_norm < -1000)
    {
        info->err_norm = -1000;
    }
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     判位置状态
// 参数说明     info         结果结构体
// 返回参数     void
//-------------------------------------------------------------------------------------------------------------------
static void img_judge_state (car_image_info_struct *info)
{
    if(0 == info->valid)
    {
        info->state = CAR_STATE_LOST;
    }
    else if(info->err_norm >= CAR_IMG_ERR_NORM_BIG)
    {
        info->state = CAR_STATE_RIGHT_BIG;
    }
    else if(info->err_norm >= CAR_IMG_ERR_NORM_STRAIGHT)
    {
        info->state = CAR_STATE_RIGHT;
    }
    else if(info->err_norm <= -CAR_IMG_ERR_NORM_BIG)
    {
        info->state = CAR_STATE_LEFT_BIG;
    }
    else if(info->err_norm <= -CAR_IMG_ERR_NORM_STRAIGHT)
    {
        info->state = CAR_STATE_LEFT;
    }
    else
    {
        info->state = CAR_STATE_CENTER;
    }
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     初始化（上电调用一次）
// 使用示例     car_image_init();
//-------------------------------------------------------------------------------------------------------------------
void car_image_init (void)
{
    img_threshold  = 0;
    img_manual_th  = 0;
    img_last_width = 0;
    img_seg_count  = 0;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     处理一帧图像（每来一帧新图调用一次）
// 参数说明     info         结果结构体，函数返回后里面的数据就是这一帧的识别结果
// 返回参数     void
// 使用示例
//     if(mt9v03x_finish_flag)
//     {
//         mt9v03x_finish_flag = 0;
//         car_image_process(&cam_info);
//     }
//-------------------------------------------------------------------------------------------------------------------
void car_image_process (car_image_info_struct *info)
{
    const uint8 *image = &mt9v03x_image[0][0];                          // 188x120 灰度图首地址

    info->threshold = img_update_threshold(image);                      // 1. 定阈值

    img_scan(image, info);                                              // 2~4. 逐行二值化、找线、定中线

    info->valid       = (info->valid_rows >= CAR_IMG_VALID_MIN_ROWS) ? 1 : 0;
    info->near_center = info->center[CAR_IMG_ROW_NEAR];
    info->far_center  = info->center[CAR_IMG_ROW_FAR];

    img_calc_error(info);                                               // 5. 算偏差
    img_judge_state(info);                                              // 6. 判状态
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     设置阈值
// 参数说明     threshold    0 = 自动（大津法）；1~255 = 固定用这个值（调试时找好阈值再固定下来更快）
// 使用示例     car_image_set_threshold(0);        // 回到自动
//-------------------------------------------------------------------------------------------------------------------
void car_image_set_threshold (uint16 threshold)
{
    if(threshold > 255)
    {
        threshold = 255;
    }
    img_manual_th = threshold;
    if(0 != threshold)
    {
        img_threshold = threshold;
    }
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     读当前阈值（屏幕上显示用）
//-------------------------------------------------------------------------------------------------------------------
uint16 car_image_get_threshold (void)
{
    return img_threshold;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     状态转成字符串（屏幕 / 串口打印用）
// 参数说明     state        car_image_state_enum
// 返回参数     const char*  只读字符串
//-------------------------------------------------------------------------------------------------------------------
const char *car_image_state_name (uint8 state)
{
    switch(state)
    {
        case CAR_STATE_CENTER:      return "CENTER";
        case CAR_STATE_LEFT:        return "LEFT";
        case CAR_STATE_RIGHT:       return "RIGHT";
        case CAR_STATE_LEFT_BIG:    return "LEFT!";
        case CAR_STATE_RIGHT_BIG:   return "RIGHT!";
        default:                    return "LOST";
    }
}

#pragma section all restore
