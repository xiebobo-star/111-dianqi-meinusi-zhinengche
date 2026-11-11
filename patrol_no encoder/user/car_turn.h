/*********************************************************************************************************************
* 文件名            car_turn.h
* 说明              普通直道和左右弯道的转向、减速及轮速分配接口
********************************************************************************************************************/
#ifndef CAR_TURN_H
#define CAR_TURN_H
#include "car_image.h"
#include "car_pid.h"

typedef struct {
    float steer_us;                         // 目标舵机脉宽，单位 us
    float speed;                            // 纯视觉模式为 PWM 占空比数值；传统模式为编码器计数/10 ms
} car_turn_output;

// 调度层确认图像有效且岔路已处理后调用；PID 历史由调用方持有。
car_turn_output car_turn_plan(car_pid_pd *steering,const car_image_info_struct *v,uint32 elapsed_ms);
// 用已限速变化的实际舵机脉宽分配左右轮目标，确保外轮快、内轮慢。
void car_turn_wheel_targets(uint16 servo_us,float speed,float *left,float *right);
#endif
