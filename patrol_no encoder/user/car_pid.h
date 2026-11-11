#ifndef CAR_PID_H
#define CAR_PID_H
#include "zf_common_headfile.h"

/* PID state belongs to each controller instance. No image or board dependency. */
typedef struct {
    float error, derivative;
    uint8 ready;
} car_pid_pd;
typedef struct {
    float integral;
} car_pid_pi;

void car_pid_pd_reset(car_pid_pd *pid);
void car_pid_pi_reset(car_pid_pi *pid);
/* Input: image error [-1000,1000], elapsed milliseconds. Output: servo us. */
float car_pid_steering_step(car_pid_pd *pid, int16 error_norm, uint32 elapsed_ms);
/* Speed units: encoder counts / 10 ms. Output: bounded forward PWM duty. */
uint16 car_pid_speed_step(car_pid_pi *pid, float target, float actual, float kp, float ki);
#endif
