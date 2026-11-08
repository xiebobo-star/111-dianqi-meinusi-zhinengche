/*********************************************************************************************************************
* 文件名            car_ring.c
* 说明              圆环处理：接近入口 -> 入环 -> 环内绕行 -> 出环 -> 完成
* 图像提供岔口与弯向，IMU 提供累计转角；本模块不直接操作电机或调用停车。
********************************************************************************************************************/
#include "car_ring.h"
#include <string.h>
#pragma section all "cpu0_dsram"
static uint8 increment(uint8 x) { return x<255 ? x+1 : x; }

void car_ring_init(car_ring *r)
{
    memset(r,0,sizeof(*r));
}
void car_ring_restart(car_ring *r)
{
    r->state=CAR_RING_APPROACH; r->completed=0;
    r->clear_frames=r->hint_frames=0;
    r->direction=r->hint_dir=0; r->yaw_deg=0;
}
uint8 car_ring_active(const car_ring *r)
{
    return CAR_RING_ENABLE && r->state>=CAR_RING_ENTER && r->state<=CAR_RING_EXIT;
}
uint8 car_ring_ready(const car_ring *r,uint32 now)
{
    return !CAR_RING_ENABLE || (r->imu_ready && now-r->imu_ms<=CAR_IMU_TIMEOUT_MS);
}
int16 car_ring_route(const car_ring *r)
{
    if(!CAR_RING_ENABLE) return 0;
    if(r->state==CAR_RING_ENTER) return r->direction;
    if(r->state==CAR_RING_EXIT) return -r->direction;
    return 0;
}
uint8 car_ring_motion(car_ring *r,float yaw_dps,uint8 valid,uint8 running,uint32 now)
{
    uint32 dt=now-r->imu_ms;
    uint8 had_sample=r->imu_ready;
    if(!valid || !(yaw_dps>=-CAR_IMU_MAX_DPS && yaw_dps<=CAR_IMU_MAX_DPS)) {
        r->imu_ready=0;
        return 0;
    }
    r->imu_ready=1; r->imu_ms=now;
    if(!CAR_RING_ENABLE || !running) return 1;
    if(!had_sample || dt>CAR_IMU_TIMEOUT_MS) return 0;
    if(car_ring_active(r))
        r->yaw_deg+=yaw_dps*r->direction*(dt/1000.0f);
    return 1;
}

uint8 car_ring_vision(car_ring *r,const car_image_info_struct *v,uint8 junction_frames,uint32 time)
{
    int16 hint;
    uint8 reset_steering=0;
    if(!CAR_RING_ENABLE) return 0;
    if(r->state==CAR_RING_APPROACH) {
        if(!v->junction && v->valid) {
            hint=v->err_far-v->err_near;
            hint=hint>CAR_RING_HINT_PIXELS ? 1 : (hint< -CAR_RING_HINT_PIXELS ? -1 : 0);
            r->hint_frames=(hint && hint==r->hint_dir) ? increment(r->hint_frames) : (hint ? 1 : 0);
            r->hint_dir=hint;
        }
        if(v->junction && junction_frames>=2 && v->near_rows>=5) {
            int16 direction=CAR_RING_ENTRY_DIR;
            if(!direction && r->hint_frames>=CAR_RING_HINT_FRAMES) direction=r->hint_dir;
            if(!direction) return 0; /* 没有稳定方向提示时，不猜测对称岔口的支路。 */
            r->direction=direction; r->state=CAR_RING_ENTER;
            r->yaw_deg=0; r->clear_frames=0; r->start_ms=r->phase_ms=time;
            reset_steering=1;
        }
    } else if(r->state==CAR_RING_ENTER) {
        r->clear_frames=(v->valid && !v->junction) ? increment(r->clear_frames) : 0;
        if(r->clear_frames>=CAR_RING_CLEAR_FRAMES && r->yaw_deg>=30) {
            r->state=CAR_RING_INSIDE; r->clear_frames=0;
        }
    } else if(r->state==CAR_RING_INSIDE) {
        if(v->junction && junction_frames>=2 &&
           r->yaw_deg>=CAR_RING_EXIT_MIN_DEG && r->yaw_deg<=CAR_RING_MAX_DEG) {
            r->state=CAR_RING_EXIT; r->clear_frames=0; r->phase_ms=time;
            reset_steering=1;
        }
    } else if(r->state==CAR_RING_EXIT) {
        r->clear_frames=(v->valid && !v->junction) ? increment(r->clear_frames) : 0;
        if(r->clear_frames>=CAR_RING_CLEAR_FRAMES && time-r->phase_ms>=200) {
            r->state=CAR_RING_DONE; r->completed=1;
        }
    }
    return reset_steering;
}

car_ring_check_result car_ring_check(const car_ring *r,uint32 now)
{
    if(!car_ring_ready(r,now)) return CAR_RING_CHECK_IMU;
    if(car_ring_active(r) &&
       (now-r->start_ms>CAR_RING_TIMEOUT_MS || r->yaw_deg>CAR_RING_MAX_DEG || r->yaw_deg< -30 ||
        ((r->state==CAR_RING_ENTER || r->state==CAR_RING_EXIT) &&
         now-r->phase_ms>CAR_RING_BRIDGE_MS)))
        return CAR_RING_CHECK_LIMIT;
    return CAR_RING_CHECK_OK;
}
float car_ring_limit_speed(const car_ring *r,float speed)
{
    if(!car_ring_active(r)) return speed;
    return speed<0 ? 0 : (speed>CAR_RING_SPEED ? CAR_RING_SPEED : speed);
}
const char *car_ring_state_name(uint8 state)
{
    switch(state) {
    case CAR_RING_ENTER:return "ENTER";
    case CAR_RING_INSIDE:return "LOOP";
    case CAR_RING_EXIT:return "EXIT";
    case CAR_RING_DONE:return "DONE";
    default:return "ROAD";
    }
}
#pragma section all restore
