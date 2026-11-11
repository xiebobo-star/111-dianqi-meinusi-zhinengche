/*********************************************************************************************************************
* 文件名            car_control.c
* 说明              总调度：连接图像、弯道、圆环、PID 模块，并统一执行启停和故障保护
* 普通弯道见 car_turn.c；圆环流程见 car_ring.c；本文件保留定时输出和停车条件。
********************************************************************************************************************/
#include "car_control.h"
#include "car_turn.h"
#include <string.h>
#pragma section all "cpu0_dsram"
static float clamp(float x,float lo,float hi) { return x<lo?lo:(x>hi?hi:x); }
#if !CAR_VISION_ONLY
static float magnitude(float x) { return x<0 ? -x : x; }
#else
/* Applied independently after differential and trim; neither can bypass limits. */
static uint16 open_pwm_step(uint16 previous,float target)
{
    float limit=CAR_OPEN_PWM_LIMIT<CAR_MOTOR_LIMIT ? CAR_OPEN_PWM_LIMIT : CAR_MOTOR_LIMIT;
    target=clamp(target,0,limit);
    return (uint16)clamp(previous+clamp(target-previous,
                         -CAR_OPEN_PWM_DECEL,CAR_OPEN_PWM_ACCEL),0,limit);
}
#endif
static uint8 increment(uint8 x) { return x<255 ? x+1 : x; }
void car_control_stop(car_control *c, uint8 reason)
{
    c->state=reason; c->pwm_l=c->pwm_r=0;
    c->speed_plan=0;
    c->good_frames=0;
    car_pid_pi_reset(&c->speed_pi_l); car_pid_pi_reset(&c->speed_pi_r);
    c->speed_target=0; c->target_l=c->target_r=0;
    c->stall_l_ms=c->stall_r_ms=0; car_pid_pd_reset(&c->steering);
    /* Hold steering on a fault; centering a moving car can leave the road. */
    c->steer_target=c->servo_us;
}
void car_control_init(car_control *c)
{
    memset(c,0,sizeof(*c));
    car_ring_init(&c->ring);
    c->servo_us=CAR_SERVO_MID_US; c->steer_target=CAR_SERVO_MID_US;
}
int16 car_control_route(const car_control *c)
{
    return c->state==CAR_RUNNING ? car_ring_route(&c->ring) : 0;
}
void car_control_motion(car_control *c,float yaw_dps,uint8 valid,uint32 now)
{
#if CAR_VISION_ONLY
    (void)c; (void)yaw_dps; (void)valid; (void)now;
#else
    uint8 ok=car_ring_motion(&c->ring,yaw_dps,valid,c->state==CAR_RUNNING,now);
    if(!ok && CAR_RING_ENABLE && c->state==CAR_RUNNING)
        car_control_stop(c,CAR_FAULT_IMU);
#endif
}
void car_control_vision(car_control *c,const car_image_info_struct *v,uint32 time)
{
    uint32 elapsed=time-c->frame_ms;
    car_turn_output turn;
    if(c->seen_frame && elapsed==0) return; /* No repeated-frame derivative. */
    if(elapsed>CAR_FRAME_TIMEOUT_MS) c->good_frames=0;
    c->seen_frame=1; c->frame_ms=time;
    c->junction_frames=v->junction ? increment(c->junction_frames) : 0;
    c->bad_frames=v->valid ? 0 : increment(c->bad_frames);
    c->good_frames=(v->valid && !v->junction) ? increment(c->good_frames) : 0;
    if(c->state!=CAR_RUNNING) return;
    if(car_ring_vision(&c->ring,v,c->junction_frames,time))
        car_pid_pd_reset(&c->steering);
    /* Only a path selected from visible edges in ENTER/EXIT can bypass the
     * junction stop. Plain valid=1 never suppresses an unresolved junction. */
    if(v->junction && v->route_used && car_control_route(c)) c->junction_frames=0;
    if(c->junction_frames>=CAR_JUNCTION_STOP_FRAMES) {
        car_control_stop(c,CAR_FAULT_JUNCTION); return;
    }
    if(c->bad_frames>=CAR_LOST_STOP_FRAMES) {
        car_control_stop(c,CAR_FAULT_IMAGE); return;
    }
    if(!v->valid || (v->junction && !(v->route_used && car_control_route(c)))) {
        /* Never accelerate or turn toward an ambiguous/inferred path. */
        c->speed_plan=0; car_pid_pd_reset(&c->steering); return;
    }
    turn=car_turn_plan(&c->steering,v,elapsed);
    c->steer_target=turn.steer_us;
    c->speed_plan=car_ring_limit_speed(&c->ring,turn.speed);
}
uint8 car_control_start(car_control *c,uint32 now)
{
    if(!c->seen_frame || now-c->frame_ms>CAR_FRAME_TIMEOUT_MS ||
       c->good_frames<CAR_START_GOOD_FRAMES) return 0;
    if(!car_ring_ready(&c->ring,now)) return 0;
    car_control_stop(c,CAR_STOPPED);
    c->state=CAR_RUNNING; c->speed_plan=0; /* Wait for next valid frame. */
    c->speed_l=c->speed_r=0;
    car_ring_restart(&c->ring);
    c->bad_frames=c->junction_frames=0;
    return 1;
}
void car_control_tick(car_control *c,int32 left,int32 right,uint32 now)
{
    float step;
    car_ring_check_result ring_check;
    if(c->state!=CAR_RUNNING) { c->pwm_l=c->pwm_r=0; return; }
    ring_check=car_ring_check(&c->ring,now);
    if(ring_check!=CAR_RING_CHECK_OK) {
        car_control_stop(c,ring_check==CAR_RING_CHECK_IMU ? CAR_FAULT_IMU : CAR_FAULT_RING);
        return;
    }
    if(now-c->frame_ms>CAR_FRAME_TIMEOUT_MS) {
        car_control_stop(c,CAR_FAULT_IMAGE); return;
    }
#if !CAR_VISION_ONLY
    if(left< -5 || right< -5 || left>CAR_ENC_MAX_PER_TICK || right>CAR_ENC_MAX_PER_TICK) {
        car_control_stop(c,CAR_FAULT_ENCODER); return;
    }
    c->speed_l+=CAR_SPEED_FILTER*(left-c->speed_l);
    c->speed_r+=CAR_SPEED_FILTER*(right-c->speed_r);
#else
    (void)left; (void)right; /* Damaged/disconnected encoder signals are ignored. */
    c->speed_l=c->speed_r=0; /* Unavailable, not an estimate; telemetry says NA. */
#endif
    step=clamp(c->speed_plan-c->speed_target,-CAR_SPEED_DECEL,CAR_SPEED_ACCEL);
    c->speed_target+=step;
    step=clamp(c->steer_target-c->servo_us,-CAR_SERVO_SLEW_US,CAR_SERVO_SLEW_US);
    c->servo_us=(uint16)clamp(c->servo_us+step,CAR_SERVO_MIN_US,CAR_SERVO_MAX_US);
    if(c->speed_plan<=0) {
        c->pwm_l=c->pwm_r=0;
        car_pid_pi_reset(&c->speed_pi_l); car_pid_pi_reset(&c->speed_pi_r);
        c->speed_target=0; /* Restart the ramp after a missing/ambiguous frame. */
        c->target_l=c->target_r=0;
        c->stall_l_ms=c->stall_r_ms=0; return;
    }
    car_turn_wheel_targets(c->servo_us,c->speed_target,&c->target_l,&c->target_r);
#if CAR_VISION_ONLY
    c->pwm_l=open_pwm_step(c->pwm_l,c->target_l*CAR_OPEN_PWM_TRIM_L);
    c->pwm_r=open_pwm_step(c->pwm_r,c->target_r*CAR_OPEN_PWM_TRIM_R);
#else
    c->pwm_l=car_pid_speed_step(&c->speed_pi_l,c->target_l,c->speed_l,CAR_SPEED_KP_L,CAR_SPEED_KI_L);
    c->pwm_r=car_pid_speed_step(&c->speed_pi_r,c->target_r,c->speed_r,CAR_SPEED_KP_R,CAR_SPEED_KI_R);
    c->stall_l_ms=(c->pwm_l>=CAR_STALL_PWM && magnitude(c->speed_l)<1) ? c->stall_l_ms+CAR_CONTROL_MS : 0;
    c->stall_r_ms=(c->pwm_r>=CAR_STALL_PWM && magnitude(c->speed_r)<1) ? c->stall_r_ms+CAR_CONTROL_MS : 0;
    if(c->stall_l_ms>=CAR_STALL_MS || c->stall_r_ms>=CAR_STALL_MS)
        car_control_stop(c,CAR_FAULT_STALL);
#endif
}
const char *car_control_state_name(uint8 state)
{
    switch(state) {
    case CAR_STOPPED:return "STOP";
    case CAR_RUNNING:return "RUN";
    case CAR_FAULT_IMAGE:return "IMG STOP";
    case CAR_FAULT_JUNCTION:return "FORK STOP";
    case CAR_FAULT_ENCODER:return "ENC STOP";
    case CAR_FAULT_STALL:return "STALL";
    case CAR_FAULT_IMU:return "IMU STOP";
    case CAR_FAULT_RING:return "RING STOP";
    default:return "KEY STOP";
    }
}
#pragma section all restore
