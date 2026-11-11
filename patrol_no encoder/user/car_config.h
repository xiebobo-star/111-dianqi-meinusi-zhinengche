#ifndef CAR_CONFIG_H
#define CAR_CONFIG_H

/* TC264D / C chassis / MT9V034 / BDS300. See ../ADAPTATION.md.
 * Hardware values below are commissioning defaults, NOT measured values. */
#ifndef CAR_DRIVE_ENABLE
#define CAR_DRIVE_ENABLE        1   /* Outputs enabled; key3 starts, key4 stops. */
#endif
/* Pure vision steering + open-loop motor duty. No encoder or IMU needed.
 * Set 0 to restore the legacy encoder PI + IMU ring configuration. */
#ifndef CAR_VISION_ONLY
#define CAR_VISION_ONLY          1
#endif
#define CAR_USE_IPS200          1
#define CAR_CONTROL_MS          10
#define CAR_FRAME_TIMEOUT_MS    150
#define CAR_START_GOOD_FRAMES   3
#define CAR_LOST_STOP_FRAMES    3
#define CAR_JUNCTION_STOP_FRAMES 3

/* One loop then exit at the same junction. AUTO needs a stable visible bend;
 * -1 selects the left branch, +1 the right branch, 0 infers from approach.
 * These are image-relative directions, not compass directions. */
#ifndef CAR_RING_ENABLE
#define CAR_RING_ENABLE          (!CAR_VISION_ONLY)
#endif
#if CAR_VISION_ONLY && CAR_RING_ENABLE
#error Pure vision mode cannot use the IMU-dependent ring state machine
#endif
#define CAR_RING_ENTRY_DIR       0
#define CAR_RING_SPEED           6.0f
#define CAR_RING_CLEAR_FRAMES    4
#define CAR_RING_EXIT_MIN_DEG    300.0f
#define CAR_RING_MAX_DEG         430.0f
#define CAR_RING_TIMEOUT_MS      30000UL
#define CAR_RING_BRIDGE_MS       3000UL
#define CAR_RING_HINT_PIXELS     6
#define CAR_RING_HINT_FRAMES     3
/* IMU Z axis must be vertical. + yaw passed to control means RIGHT turn.
 * Z upwards typically gives positive CCW, hence -1; verify on the vehicle. */
#define CAR_IMU_YAW_SIGN         (-1.0f)
#define CAR_IMU_MAX_DPS          500.0f
#define CAR_IMU_TIMEOUT_MS       50
#define CAR_IMU_CAL_SAMPLES      200

/* PWM+DIR driver only. Forward-only output; zero PWM means coast. */
#define CAR_MOTOR_L_PWM         ATOM0_CH0_P21_2
#define CAR_MOTOR_R_PWM         ATOM0_CH2_P21_4
#define CAR_MOTOR_L_DIR         P21_3
#define CAR_MOTOR_R_DIR         P21_5
#define CAR_MOTOR_L_FORWARD     1
#define CAR_MOTOR_R_FORWARD     1
#define CAR_MOTOR_HZ            17000
#define CAR_MOTOR_LIMIT         2500   /* 25% of PWM_DUTY_MAX=10000 */
#define CAR_ENC_L_TIMER         TIM4_ENCODER
#define CAR_ENC_L_A             TIM4_ENCODER_CH1_P02_8
#define CAR_ENC_L_B             TIM4_ENCODER_CH2_P00_9
#define CAR_ENC_R_TIMER         TIM5_ENCODER
#define CAR_ENC_R_A             TIM5_ENCODER_CH1_P10_3
#define CAR_ENC_R_B             TIM5_ENCODER_CH2_P10_1
#define CAR_ENC_L_SIGN          1
#define CAR_ENC_R_SIGN          1
#define CAR_ENC_MAX_PER_TICK    2000
#define CAR_STALL_MS            500
#define CAR_STALL_PWM           700

/* Separate ATOM instance from motor PWM; use enum directly, not #ifdef. */
#define CAR_SERVO_PWM           ATOM1_CH1_P33_9
#define CAR_SERVO_HZ            50
#define CAR_SERVO_MID_US        1540
#define CAR_SERVO_MIN_US        1340
#define CAR_SERVO_MAX_US        1740
#define CAR_SERVO_SIGN          1   /* Positive image error must steer RIGHT. */
#define CAR_SERVO_SLEW_US       12  /* Per 10 ms */
#define CAR_STEER_KP            150.0f /* us / normalized error [-1,1] */
#define CAR_STEER_KD            6.0f   /* us*s / normalized error */
#define CAR_STEER_D_ALPHA       0.25f

/* Open-loop values are PWM duty / 10000, NOT measured speed.
 * Low-speed commissioning defaults; tune for the actual battery and motors.
 * No automatic boost when the motor fails to move. */
#define CAR_OPEN_PWM_STRAIGHT    1100
#define CAR_OPEN_PWM_BEND        800
#define CAR_OPEN_PWM_CAUTIOUS    650
#define CAR_OPEN_PWM_LIMIT       1500
#define CAR_OPEN_PWM_ACCEL       10  /* Max increase per wheel per 10 ms. */
#define CAR_OPEN_PWM_DECEL       40  /* Normal decrease; faults cut immediately. */
#define CAR_OPEN_PWM_TRIM_L      1.0f
#define CAR_OPEN_PWM_TRIM_R      1.0f
#if CAR_OPEN_PWM_CAUTIOUS <= 0 || CAR_OPEN_PWM_CAUTIOUS > CAR_OPEN_PWM_BEND || CAR_OPEN_PWM_BEND > CAR_OPEN_PWM_STRAIGHT || CAR_OPEN_PWM_STRAIGHT > CAR_OPEN_PWM_LIMIT || CAR_OPEN_PWM_LIMIT > CAR_MOTOR_LIMIT
#error Invalid open-loop PWM ordering or limit
#endif
#if CAR_OPEN_PWM_ACCEL <= 0 || CAR_OPEN_PWM_DECEL <= 0
#error Open-loop PWM ramp steps must be positive
#endif

/* Planner units depend on mode: duty in vision mode, counts/10 ms otherwise.
 * These aliases keep turn planning independent of the motor feedback mode. */
#if CAR_VISION_ONLY
#define CAR_SPEED_STRAIGHT      ((float)CAR_OPEN_PWM_STRAIGHT)
#define CAR_SPEED_BEND          ((float)CAR_OPEN_PWM_BEND)
#define CAR_SPEED_CAUTIOUS      ((float)CAR_OPEN_PWM_CAUTIOUS)
#define CAR_SPEED_ACCEL         ((float)CAR_OPEN_PWM_ACCEL)
#define CAR_SPEED_DECEL         ((float)CAR_OPEN_PWM_DECEL)
#else
/* Legacy speed unit: encoder counts per 10 ms, NOT RPM or m/s. */
#define CAR_SPEED_STRAIGHT      18.0f
#define CAR_SPEED_BEND          8.0f
#define CAR_SPEED_CAUTIOUS      6.0f
#define CAR_SPEED_ACCEL         0.20f /* counts/tick target increment per tick */
#define CAR_SPEED_DECEL         1.00f
#endif
#define CAR_SPEED_KP_L          25.0f
#define CAR_SPEED_KP_R          25.0f
#define CAR_SPEED_KI_L          40.0f /* output / (error*s) */
#define CAR_SPEED_KI_R          40.0f
#define CAR_SPEED_FILTER        0.35f
/* Outer rear wheel runs faster in a turn. Tune to wheelbase/track geometry;
 * use 0 for a single drive motor with a mechanical differential. */
#define CAR_TURN_SPEED_RATIO    0.15f

/* Camera ROI excludes distant scenery and the bonnet. All coordinates pixels. */
#define CAR_ROI_TOP             32
#define CAR_ROI_BOTTOM          111
#define CAR_ROAD_MIN_WIDTH      12
#define CAR_ROAD_MAX_WIDTH      174
#define CAR_ROAD_JUMP           28
#define CAR_SEAM_GAP            3
#define CAR_MIN_CONTRAST        24
#define CAR_WIDTH_MEMORY_FRAMES 5
#endif
