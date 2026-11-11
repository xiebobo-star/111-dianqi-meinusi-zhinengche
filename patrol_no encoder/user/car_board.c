#include "car_board.h"
#pragma section all "cpu0_dsram"
static car_control controller;
volatile uint32 car_capture_ms;
#if CAR_DRIVE_ENABLE && CAR_RING_ENABLE
static float yaw_bias;
static uint8 imu_ok;
static uint8 imu_sample(float *yaw)
{
    float x,y,z,norm;
    imu660rx_get_gyro(); imu660rx_get_acc();
    *yaw=imu660rx_gyro_transition(imu660rx_gyro_z);
    x=imu660rx_acc_transition(imu660rx_acc_x);
    y=imu660rx_acc_transition(imu660rx_acc_y);
    z=imu660rx_acc_transition(imu660rx_acc_z); norm=x*x+y*y+z*z;
    /* Reject unplugged/all-zero/all-ones samples and gross shock/tilt.
     * Library read API has no transport status; see commissioning notes. */
    return norm>0.25f && norm<4.0f && z*z>norm*0.5f &&
           *yaw>=-CAR_IMU_MAX_DPS && *yaw<=CAR_IMU_MAX_DPS;
}
#endif
uint8 car_board_imu_init(void)
{
#if CAR_DRIVE_ENABLE && CAR_RING_ENABLE
    int n;
    float yaw,sum=0,squared=0;
    imu_ok=0;
    if(imu660rx_init()) return 0;
    for(n=0;n<CAR_IMU_CAL_SAMPLES;n++) {
        if(!imu_sample(&yaw)) return 0;
        sum+=yaw; squared+=yaw*yaw;
        system_delay_ms(CAR_CONTROL_MS);
    }
    yaw_bias=sum/CAR_IMU_CAL_SAMPLES;
    if(yaw_bias< -5 || yaw_bias>5 || squared/CAR_IMU_CAL_SAMPLES-yaw_bias*yaw_bias>1.0f) return 0;
    imu_ok=1;
    car_control_motion(&controller,0,1,system_getval_ms());
#endif
    return 1;
}
#if CAR_DRIVE_ENABLE
static uint32 servo_duty(uint16 us)
{ return (uint32)us*CAR_SERVO_HZ*PWM_DUTY_MAX/1000000UL; }
static void outputs(void)
{
    pwm_set_duty(CAR_MOTOR_L_PWM,controller.pwm_l);
    pwm_set_duty(CAR_MOTOR_R_PWM,controller.pwm_r);
    pwm_set_duty(CAR_SERVO_PWM,servo_duty(controller.servo_us));
}
#endif
void car_board_init(void)
{
    car_control_init(&controller);
#if CAR_DRIVE_ENABLE
    gpio_init(CAR_MOTOR_L_DIR,GPO,CAR_MOTOR_L_FORWARD,GPO_PUSH_PULL);
    gpio_init(CAR_MOTOR_R_DIR,GPO,CAR_MOTOR_R_FORWARD,GPO_PUSH_PULL);
    pwm_init(CAR_MOTOR_L_PWM,CAR_MOTOR_HZ,0);
    pwm_init(CAR_MOTOR_R_PWM,CAR_MOTOR_HZ,0);
    pwm_init(CAR_SERVO_PWM,CAR_SERVO_HZ,servo_duty(CAR_SERVO_MID_US));
#if !CAR_VISION_ONLY
    encoder_dir_init(CAR_ENC_L_TIMER,CAR_ENC_L_A,CAR_ENC_L_B);
    encoder_dir_init(CAR_ENC_R_TIMER,CAR_ENC_R_A,CAR_ENC_R_B);
    encoder_clear_count(CAR_ENC_L_TIMER); encoder_clear_count(CAR_ENC_R_TIMER);
#endif
#endif
}
/* Only PIT calls tick. Main accesses controller with interrupt state restored. */
void car_board_tick(void)
{
#if CAR_DRIVE_ENABLE
#if CAR_VISION_ONLY
    int32 l=0,r=0;
#else
    int32 l=(int32)encoder_get_count(CAR_ENC_L_TIMER)*CAR_ENC_L_SIGN;
    int32 r=(int32)encoder_get_count(CAR_ENC_R_TIMER)*CAR_ENC_R_SIGN;
    encoder_clear_count(CAR_ENC_L_TIMER); encoder_clear_count(CAR_ENC_R_TIMER);
#endif
    /* Physical key4 stop has bounded latency even while the display blocks. */
    if(!gpio_get_level(P11_3)) car_control_stop(&controller,CAR_FAULT_ESTOP);
#if CAR_RING_ENABLE
    {
        float yaw=0;
        uint8 valid=imu_ok && imu_sample(&yaw);
        car_control_motion(&controller,(yaw-yaw_bias)*CAR_IMU_YAW_SIGN,valid,system_getval_ms());
    }
#endif
    car_control_tick(&controller,l,r,system_getval_ms()); outputs();
#endif
}
void car_board_vision(const car_image_info_struct *v,uint32 capture_ms)
{
    uint32 irq=interrupt_global_disable();
    car_control_vision(&controller,v,capture_ms);
    interrupt_global_enable(irq);
}
uint8 car_board_start(void)
{
    uint8 ok=0;
#if CAR_DRIVE_ENABLE
    uint32 irq=interrupt_global_disable();
    if(gpio_get_level(P11_3)) ok=car_control_start(&controller,system_getval_ms());
    interrupt_global_enable(irq);
#endif
    return ok;
}
void car_board_stop(void)
{
    uint32 irq=interrupt_global_disable();
    car_control_stop(&controller,CAR_STOPPED);
#if CAR_DRIVE_ENABLE
    outputs();
#endif
    interrupt_global_enable(irq);
}
void car_board_status(car_control *out)
{
    uint32 irq=interrupt_global_disable();
    *out=controller;
    interrupt_global_enable(irq);
}
#pragma section all restore
