/*********************************************************************************************************************
* TC264 Opensource Library 即（TC264 开源库）是一个基于官方 SDK 接口的第三方开源库
* Copyright (c) 2022 SEEKFREE 逐飞科技
*
* 本文件是 TC264 开源库的一部分
*
* TC264 开源库 是免费软件
* 您可以根据自由软件基金会发布的 GPL（GNU General Public License，即 GNU通用公共许可证）的条款
* 即 GPL 的第3版（即 GPL3.0）或（您选择的）任何后来的版本，重新发布和/或修改它
*
* 本开源库的发布是希望它能发挥作用，但并未对其作任何的保证
* 甚至没有隐含的适销性或适合特定用途的保证
* 更多细节请参见 GPL
*
* 您应该在收到本开源库的同时收到一份 GPL 的副本
* 如果没有，请参阅<https://www.gnu.org/licenses/>
*
* 额外注明：
* 本开源库使用 GPL3.0 开源许可证协议 以上许可申明为译文版本
* 许可申明英文版在 libraries/doc 文件夹下的 GPL3_permission_statement.txt 文件中
* 许可证副本在 libraries 文件夹下 即该文件夹下的 LICENSE 文件
* 欢迎各位使用并传播本程序 但修改内容时必须保留逐飞科技的版权声明（即本声明）
*
* 文件名称          cpu0_main
* 公司名称          成都逐飞科技有限公司
* 版本信息          查看 libraries/doc 文件夹内 version 文件 版本说明
* 开发环境          ADS v1.10.2
* 适用平台          TC264D
* 店铺链接          https://seekfree.taobao.com/
*
* 修改记录
* 日期              作者                备注
* 2022-09-15       pudding            first version
********************************************************************************************************************/
#include "zf_common_headfile.h"
#pragma section all "cpu0_dsram"
// 将本语句与#pragma section all restore语句之间的全局变量都放在CPU0的RAM中

// **************************** 代码区域 ****************************
//uint16  camera_exp_data = MT9V03X_EXP_TIME_DEF;          // 当前曝光度
//uint8   cur_mean;
//uint32  ae_time_us;                                      // 自动曝光耗时(us)

//uint8 auto_exposure_adjust(uint8 image[MT9V03X_H][MT9V03X_W], uint8 target_mean);

int core0_main(void)
{
    clock_init();                   // 获取时钟频率<务必保留>
    debug_init();                   // 初始化默认调试串口
    // 此处编写用户代码 例如外设初始化代码等

//    tft180_init();
//
//    mt9v03x_init();
//
//    tft180_show_string(0, 128, "EXP :");                    // 屏幕显示曝光标签
//    tft180_show_string(0, 144, "MEAN:");                    // 屏幕显示均值标签
//    tft180_show_string(0, 160, "TIME:");                    // 屏幕显示耗时标签
//    tft180_show_string(80, 160, "us");                      // 耗时单位

    // 此处编写用户代码 例如外设初始化代码等
    cpu_wait_event_ready();         // 等待所有核心初始化完毕
    while (TRUE)
    {
        // 此处编写需要循环执行的代码

//        if(mt9v03x_finish_flag)
//        {
//            mt9v03x_finish_flag = 0;
//
//            system_start();                                          // 开始计时 自己使用时可以删掉耗时统计
//            cur_mean = auto_exposure_adjust(mt9v03x_image, 80);      // 自动曝光并返回当前均值  目标像素均值 80
//            ae_time_us = system_getval_us();                         // 读取耗时，单位 us
//
//            printf("exp:%d mean:%d time:%ld us\r\n", camera_exp_data, cur_mean, ae_time_us); // 串口打印
//
//            tft180_displayimage03x(mt9v03x_image[0], MT9V03X_W, MT9V03X_H);
//            tft180_show_uint(40, 128, camera_exp_data, 3);           // 屏幕显示曝光值
//            tft180_show_uint(40, 144, cur_mean, 3);                  // 屏幕显示整体均值
//            tft180_show_uint(40, 160, ae_time_us, 5);                // 屏幕显示耗时(us)
//
//        }

        // 此处编写需要循环执行的代码
    }
}


//-------------------------------------------------------------------------------------------------------------------
// 函数简介     自动调节曝光度
// 参数说明     image       图像二维数组
// 参数说明     target_mean 目标灰度均值
// 返回参数     uint8       当前采样区整体像素均值
// 使用示例     cur_mean = auto_exposure_adjust(mt9v03x_image, 80);
// 备注信息     可自行阅读并且修改部分参数
//-------------------------------------------------------------------------------------------------------------------
//uint8 auto_exposure_adjust(uint8 image[MT9V03X_H][MT9V03X_W], uint8 target_mean)
//{
//    const uint8 row_step = 5;                                           // 每隔多少行统计一次
//    const uint8 sample_num = 8;                                         // 中间采样行数
//    const uint16 sample_row0 = (uint16)((((MT9V03X_H / row_step) - sample_num) / 2) * row_step); // 中间采样起始行
//    const uint16 exp_min = 3;                                           // 曝光下限
//    const uint16 exp_max = 600;                                         // 曝光上限
//    const uint8 frame_interval = 3;                                     // 每隔几帧修正一次(最低3帧 否则无法正常调节)
//    const uint8 step_base_min = 1;                                      // 修正基准下限
//    const uint8 step_base_max = 64;                                     // 修正基准上限
//    const uint8 step_base = (uint8)func_limit_ab(64, step_base_min, step_base_max); // 限幅后的修正基准
//
//    static uint8 frame_skip = 0;                                        // 帧间隔计数
//    static uint16 mean_filt = 0;                                        // 均值低通滤波值
//    static uint8 mean_inited = 0;                                       // 滤波初值是否已建立
//    uint16 row;                                                         // 当前统计行号
//    uint16 col;                                                         // 当前列号
//    uint16 index;                                                       // 采样行计数
//    uint16 step_limit;                                                  // 单次最大步进
//    uint16 new_exp;                                                     // 新曝光值
//    uint16 deadzone;                                                    // 自适应误差死区
//    uint32 sum_all;                                                     // 采样区像素累加和
//    uint8 cur_mean;                                                     // 当前像素均值
//    uint8 mean_use;                                                     // 用于调光的滤波均值
//    int16 error;                                                        // 均值误差
//    int32 adj;                                                          // 曝光调节量
//    int32 exp_temp;                                                     // 临时曝光值
//
//    index = 0;
//    sum_all = 0;
//    for(row = sample_row0; index < sample_num; row += row_step)
//    {
//        for(col = 0; col < MT9V03X_W; col ++)
//        {
//            sum_all += image[row][col];                                 // 累加采样区像素
//        }
//        index ++;
//    }
//    cur_mean = (uint8)(sum_all / (sample_num * MT9V03X_W));             // 整体像素均值
//
//    if(0 == mean_inited)                                                // 首帧直接建立滤波初值
//    {
//        mean_filt = cur_mean;
//        mean_inited = 1;
//    }
//    else
//    {
//        mean_filt = (uint16)((mean_filt * 3 + cur_mean) / 4);           // IIR 低通，削弱帧间抖动
//    }
//    mean_use = (uint8)mean_filt;
//
//    frame_skip ++;
//    if(frame_skip >= frame_interval)                                    // 满间隔帧才修正曝光
//    {
//        frame_skip = 0;
//        error = (int16)target_mean - (int16)mean_use;
//
//        // 曝光越低，灰度对曝光越敏感，死区越大
//        if(camera_exp_data < 30)
//        {
//            deadzone = 8;
//        }
//        else if(camera_exp_data < 80)
//        {
//            deadzone = 4;
//        }
//        else
//        {
//            deadzone = 2;
//        }
//
//        if(func_abs(error) > deadzone)                                      // 超出死区才调节
//        {
//            step_limit = (uint16)((uint32)camera_exp_data * step_base / 100);       // 当前曝光的 N%
//            step_limit = (uint16)func_limit_ab((int32)step_limit, 1, step_base);    // 限制在 1~N
//
//            if(camera_exp_data < 30)
//            {
//                step_limit = (uint16)func_limit_ab((int32)step_limit, 1, 2);
//            }
//            else if(camera_exp_data < 80)
//            {
//                step_limit = (uint16)func_limit_ab((int32)step_limit, 1, 4);
//            }
//
//            adj = (int32)func_limit((int32)error, (int32)step_limit);       // 上限限幅
//
//            if((adj < 0) && (camera_exp_data < 30))
//            {
//                adj = (int32)func_limit_ab(adj, -1, 0);
//            }
//
//            if(adj < 0)                                                     // 下调曝光
//            {
//                if(camera_exp_data <= exp_min)
//                {
//                    adj = 0;
//                }
//                else
//                {
//                    adj = (int32)func_limit_ab(adj, -((int32)camera_exp_data - exp_min), 0);
//                }
//            }
//            else                                                            // 上调曝光
//            {
//                if(camera_exp_data >= exp_max)
//                {
//                    adj = 0;
//                }
//                else
//                {
//                    adj = (int32)func_limit_ab(adj, 0, (int32)exp_max - camera_exp_data);
//                }
//            }
//
//            exp_temp = (int32)camera_exp_data + adj;
//            new_exp = (uint16)func_limit_ab(exp_temp, exp_min, exp_max);    // 曝光限幅
//
//            if(new_exp != camera_exp_data)
//            {
//                camera_exp_data = new_exp;
//                mt9v03x_set_exposure_time(camera_exp_data);                 // 写入摄像头曝光
//            }
//        }
//    }
//    return cur_mean;
//}

//#pragma section all restore
// **************************** 代码区域 ****************************
