/* BDS300 steering PD and encoder speed PI. Gains/limits: car_config.h.
 * This module computes outputs only; scheduling and faults: car_control.c. */
#include "car_pid.h"
#include "car_config.h"
#pragma section all "cpu0_dsram"
static float clamp(float x,float lo,float hi) { return x<lo?lo:(x>hi?hi:x); }

void car_pid_pd_reset(car_pid_pd *pid)
{
    pid->error=0; pid->derivative=0; pid->ready=0;
}
void car_pid_pi_reset(car_pid_pi *pid) { pid->integral=0; }

float car_pid_steering_step(car_pid_pd *pid,int16 error_norm,uint32 elapsed_ms)
{
    float e=clamp(error_norm/1000.0f,-1,1);
    float dt=clamp(elapsed_ms/1000.0f,0.005f,0.15f);
    float d=pid->ready ? (e-pid->error)/dt : 0;
    pid->derivative+=CAR_STEER_D_ALPHA*(d-pid->derivative);
    pid->error=e; pid->ready=1;
    return clamp(CAR_SERVO_MID_US+CAR_SERVO_SIGN*
        (CAR_STEER_KP*e+CAR_STEER_KD*pid->derivative),CAR_SERVO_MIN_US,CAR_SERVO_MAX_US);
}

uint16 car_pid_speed_step(car_pid_pi *pid,float target,float actual,float kp,float ki)
{
    float error=target-actual;
    float candidate=pid->integral+ki*(CAR_CONTROL_MS/1000.0f)*error;
    float output=kp*error+candidate;
    /* Conditional integration permits unwinding at either saturation limit. */
    if((output>=0 && output<=CAR_MOTOR_LIMIT) ||
       (output>CAR_MOTOR_LIMIT && error<0) || (output<0 && error>0))
        pid->integral=clamp(candidate,-CAR_MOTOR_LIMIT,CAR_MOTOR_LIMIT);
    return (uint16)clamp(kp*error+pid->integral,0,CAR_MOTOR_LIMIT);
}
#pragma section all restore
