#ifndef CAR_CONTROL_H
#define CAR_CONTROL_H
#include "car_image.h"
#include "car_pid.h"
#include "car_ring.h"
typedef enum {
    CAR_STOPPED, CAR_RUNNING, CAR_FAULT_IMAGE, CAR_FAULT_JUNCTION,
    CAR_FAULT_ENCODER, CAR_FAULT_STALL, CAR_FAULT_ESTOP,
    CAR_FAULT_IMU, CAR_FAULT_RING
} car_control_state;
typedef struct {
    uint8 state, good_frames, bad_frames, junction_frames, seen_frame;
    uint32 frame_ms;
    car_pid_pd steering;
    car_pid_pi speed_pi_l, speed_pi_r;
    float steer_target;
    float speed_plan, speed_target, target_l, target_r, speed_l, speed_r;
    uint16 stall_l_ms, stall_r_ms, servo_us, pwm_l, pwm_r;
    car_ring ring;                          // 圆环模块独立状态，包含 IMU 时效及累计转角
} car_control;
void car_control_init(car_control *c);
void car_control_vision(car_control *c, const car_image_info_struct *v, uint32 capture_ms);
uint8 car_control_start(car_control *c, uint32 now);
void car_control_stop(car_control *c, uint8 reason);
void car_control_tick(car_control *c, int32 left, int32 right, uint32 now);
/* Calibrated signed deg/s, sampled by the 10 ms board interrupt. */
void car_control_motion(car_control *c, float yaw_dps, uint8 valid, uint32 now);
int16 car_control_route(const car_control *c);
const char *car_control_state_name(uint8 state);
#endif
