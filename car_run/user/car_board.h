#ifndef CAR_BOARD_H
#define CAR_BOARD_H
#include "car_control.h"
void car_board_motor_safe(void);   /* ★ 上电第一时间调用：EN 拉低 + PWM 0%，防上电飞车 */
void car_board_init(void);
uint8 car_board_imu_init(void);
void car_board_tick(void);
void car_board_vision(const car_image_info_struct *v,uint32 capture_ms);
uint8 car_board_start(void);
void car_board_stop(void);
void car_board_status(car_control *out);
extern volatile uint32 car_capture_ms;
#endif
