/*********************************************************************************************************************
* 文件名            car_ring.h
* 说明              圆环模块：独立保存入环、绕环、出环及 IMU 状态
********************************************************************************************************************/
#ifndef CAR_RING_H
#define CAR_RING_H
#include "car_image.h"

typedef enum {
    CAR_RING_APPROACH, CAR_RING_ENTER, CAR_RING_INSIDE,
    CAR_RING_EXIT, CAR_RING_DONE
} car_ring_state;

typedef enum {
    CAR_RING_CHECK_OK, CAR_RING_CHECK_IMU, CAR_RING_CHECK_LIMIT
} car_ring_check_result;

typedef struct {
    uint8 imu_ready;                         // 是否已有有效 IMU 采样
    uint8 state, clear_frames, hint_frames, completed;
    int16 direction, hint_dir;               // -1 左，+1 右，0 未确定
    uint32 imu_ms, start_ms, phase_ms;        // 采样、入环及阶段起始时间，单位 ms
    float yaw_deg;                          // 沿入环方向累计的转角，单位度
} car_ring;

void car_ring_init(car_ring *r);             // 上电初始化全部圆环状态
void car_ring_restart(car_ring *r);          // 重新开始一圈，保留当前 IMU 有效性和时间
uint8 car_ring_active(const car_ring *r);     // 是否处于入环、环内或出环阶段
uint8 car_ring_ready(const car_ring *r,uint32 now); // IMU 是否满足启动/运行时效要求
int16 car_ring_route(const car_ring *r);      // 当前选支方向，出环时与入环方向相反

// 输入已扣零偏且正值代表右转的角速度（度/秒）；返回 0 表示采样或连续性异常。
uint8 car_ring_motion(car_ring *r,float yaw_dps,uint8 valid,uint8 running,uint32 now);
// 每个新图像调用；返回 1 表示进入新选支阶段，需要调度层复位转向微分。
uint8 car_ring_vision(car_ring *r,const car_image_info_struct *v,uint8 junction_frames,uint32 time);
car_ring_check_result car_ring_check(const car_ring *r,uint32 now);
float car_ring_limit_speed(const car_ring *r,float speed);
const char *car_ring_state_name(uint8 state);
#endif
