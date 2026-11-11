/*********************************************************************************************************************
* 文件名            car_turn.c
* 说明              普通弯道处理：沿中线转向，按偏差和弯曲程度减速，按舵角分配差速
* 左右弯统一使用带符号的图像偏差；当前模块不新增直角弯或掉头识别。
********************************************************************************************************************/
#include "car_turn.h"
#pragma section all "cpu0_dsram"
static float clamp(float x,float lo,float hi) { return x<lo?lo:(x>hi?hi:x); }
static float magnitude(float x) { return x<0 ? -x : x; }

car_turn_output car_turn_plan(car_pid_pd *steering,const car_image_info_struct *v,uint32 elapsed_ms)
{
    car_turn_output out;
    float e=clamp(v->err_norm/1000.0f,-1,1);
    float severity;
    out.steer_us=car_pid_steering_step(steering,v->err_norm,elapsed_ms);
    severity=clamp(magnitude(e)*1.3f+v->bend_norm/650.0f,0,1);
    out.speed=CAR_SPEED_STRAIGHT-(CAR_SPEED_STRAIGHT-CAR_SPEED_BEND)*severity;
    if(v->confidence<65 || v->far_rows<3)
        out.speed=clamp(out.speed,0,CAR_SPEED_CAUTIOUS);
    return out;
}
void car_turn_wheel_targets(uint16 servo_us,float speed,float *left,float *right)
{
    float turn=CAR_SERVO_SIGN*((float)servo_us-CAR_SERVO_MID_US);
    turn=clamp(turn/(turn<0 ? CAR_SERVO_MID_US-CAR_SERVO_MIN_US :
                    CAR_SERVO_MAX_US-CAR_SERVO_MID_US),-1,1)*CAR_TURN_SPEED_RATIO;
    *left=speed*(1+turn);
    *right=speed*(1-turn);
}
#pragma section all restore
