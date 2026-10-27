#include "servo_control.h"
#include "test_config.h"
#include "IfxGtm_reg.h"        /* MODULE_GTM: read CM0/CM1 back for the debug line */

/* Servo PWM output pins.
 * SERVO_PWM_CH       ATOM1_CH1_P33_9 = ATOM1 channel 1 -> TOUT31 -> P33_9, 50 Hz.
 *                    P33_9 is the on-board servo connector signal (Seekfree main
 *                    board: P18 on V2.5, P1 on V2.6, P2 on V3.1, through 100R).
 * SERVO_PWM_MIRROR   also drive the old pin so a servo still plugged into
 *                    P21_6 keeps working while the wiring is being sorted out.
 *                    Set SERVO_PWM_MIRROR_EN to 0 for the final build. */
#define SERVO_PWM_CH            ATOM1_CH1_P33_9
#define SERVO_PWM_MIRROR_EN     (1)
#define SERVO_PWM_MIRROR_CH     ATOM1_CH4_P21_6

#define SERVO_PULSE_INVALID     (0xffffu)

static uint16 servo_current_pulse = SERVO_PULSE_INVALID;
static uint16 servo_min_us;
static uint16 servo_center_us;
static uint16 servo_max_us;
static uint8  servo_enabled;
static uint32 servo_duty;

/* ---------------------------------------------------------------- pulse/duty */

static uint32 servo_pulse_to_duty(uint16 pulse_us)
{
    return (uint32)pulse_us * SERVO_PWM_FREQ_HZ * PWM_DUTY_MAX / 1000000u;
}

uint16 servo_pulse_to_angle_x10_cal(uint16 pulse_us, uint16 min_us, uint16 center_us, uint16 max_us)
{
    uint32 span;

    if (center_us <= min_us) { return 0u; }
    if (pulse_us <= min_us)  { return 0u; }
    if (pulse_us >= max_us)  { return (uint16)(SERVO_MAX_ANGLE_DEG * 10u); }

    if (pulse_us <= center_us)
    {
        span = (uint32)(pulse_us - min_us);
        return (uint16)(span * (SERVO_CENTER_ANGLE_DEG * 10u) / (center_us - min_us));
    }

    if (max_us <= center_us) { return (uint16)(SERVO_CENTER_ANGLE_DEG * 10u); }

    span = (uint32)(pulse_us - center_us);
    return (uint16)(SERVO_CENTER_ANGLE_DEG * 10u
        + span * ((SERVO_MAX_ANGLE_DEG - SERVO_CENTER_ANGLE_DEG) * 10u)
        / (max_us - center_us));
}

uint16 servo_pulse_to_angle_x10(uint16 pulse_us)
{
    return servo_pulse_to_angle_x10_cal(pulse_us, servo_min_us, servo_center_us, servo_max_us);
}

/* angle in 0.1 deg -> pulse.  Keeps the 0.1 deg resolution, so 90.5 deg maps to
 * 1505 us instead of collapsing to the whole-degree value. */
uint16 servo_angle_x10_to_pulse_cal(uint16 angle_x10, uint16 min_us, uint16 center_us, uint16 max_us)
{
    if (angle_x10 > (uint16)(SERVO_MAX_ANGLE_DEG * 10u)) { angle_x10 = (uint16)(SERVO_MAX_ANGLE_DEG * 10u); }
    if (center_us <= min_us) { return min_us; }

    if (angle_x10 <= (uint16)(SERVO_CENTER_ANGLE_DEG * 10u))
    {
        return (uint16)(min_us
            + (uint32)angle_x10 * (center_us - min_us) / (SERVO_CENTER_ANGLE_DEG * 10u));
    }

    if (max_us <= center_us) { return max_us; }

    return (uint16)(center_us
        + (uint32)(angle_x10 - SERVO_CENTER_ANGLE_DEG * 10u) * (max_us - center_us)
        / ((SERVO_MAX_ANGLE_DEG - SERVO_CENTER_ANGLE_DEG) * 10u));
}

static uint16 servo_angle_to_pulse(uint16 angle)
{
    if (angle > SERVO_MAX_ANGLE_DEG) { angle = SERVO_MAX_ANGLE_DEG; }
    return servo_angle_x10_to_pulse_cal((uint16)(angle * 10u), servo_min_us, servo_center_us, servo_max_us);
}

/* --------------------------------------------------------------- calibration */

/* min/center/max are the three points of the angle <-> pulse mapping.  They are
 * kept inside the legal window and strictly ordered, so every division below
 * stays safe even when the menu hands over nonsense. */
static void servo_params_set(uint16 min_us, uint16 center_us, uint16 max_us)
{
    if (min_us < SERVO_PULSE_MIN_US) { min_us = SERVO_PULSE_MIN_US; }
    if (max_us > SERVO_PULSE_MAX_US) { max_us = SERVO_PULSE_MAX_US; }
    if ((uint16)(min_us + 20u) >= max_us)
    {
        min_us = SERVO_MIN_US;
        max_us = SERVO_MAX_US;
    }
    if (center_us <= min_us) { center_us = (uint16)(min_us + (uint16)((max_us - min_us) / 2u)); }
    if (center_us >= max_us) { center_us = (uint16)(max_us - 100u); }

    servo_min_us    = min_us;
    servo_center_us = center_us;
    servo_max_us    = max_us;
}

/* ------------------------------------------------------------------ hardware */

/* --------------------------------------------------------------- test hooks */

/* Duty actually sent to the pin.  Both test switches live in test_config.h and
 * both default to 0, so a normal build returns the value unchanged. */
static uint32 servo_duty_override(uint32 duty)
{
#if (1 == SERVO_SLOW_TEST_EN)
    (void)duty;
    return (uint32)(PWM_DUTY_MAX / 2u);          /* slow 50 % square wave */
#elif (1 == SERVO_DUTY_TEST_EN)
    (void)duty;
    return (uint32)SERVO_DUTY_TEST_VAL;          /* fixed duty for a meter check */
#else
    return duty;
#endif
}

static void servo_pwm_init_ch(uint32 duty)
{
#if (1 == SERVO_SLOW_TEST_EN)
    pwm_init(SERVO_PWM_CH, SERVO_SLOW_TEST_HZ, PWM_DUTY_MAX / 2u);
#if (1 == SERVO_PWM_MIRROR_EN)
    pwm_init(SERVO_PWM_MIRROR_CH, SERVO_SLOW_TEST_HZ, PWM_DUTY_MAX / 2u);
#endif
#else
    pwm_init(SERVO_PWM_CH, SERVO_PWM_FREQ_HZ, duty);
#if (1 == SERVO_PWM_MIRROR_EN)
    pwm_init(SERVO_PWM_MIRROR_CH, SERVO_PWM_FREQ_HZ, duty);
#endif
#endif
}

static void servo_pwm_write_ch(uint32 duty)
{
    pwm_set_duty(SERVO_PWM_CH, duty);
#if (1 == SERVO_PWM_MIRROR_EN)
    pwm_set_duty(SERVO_PWM_MIRROR_CH, duty);
#endif
}

/* ---------------------------------------------------------------- public API */

void servo_control_init(uint16 pulse_us, uint16 min_us, uint16 center_us, uint16 max_us, uint8 enable)
{
    servo_params_set(min_us, center_us, max_us);
    servo_pwm_init_ch(servo_duty_override(servo_pulse_to_duty(servo_center_us)));    /* safe start at center */
    servo_current_pulse = SERVO_PULSE_INVALID;
    servo_control_set_pulse(pulse_us, min_us, center_us, max_us, enable);
}

void servo_control_set_pulse(uint16 pulse_us, uint16 min_us, uint16 center_us, uint16 max_us, uint8 enable)
{
    uint16 old_min    = servo_min_us;
    uint16 old_center = servo_center_us;
    uint16 old_max    = servo_max_us;
    uint32 duty;

    servo_params_set(min_us, center_us, max_us);

    if (pulse_us < SERVO_PULSE_MIN_US) { pulse_us = SERVO_PULSE_MIN_US; }
    if (pulse_us > SERVO_PULSE_MAX_US) { pulse_us = SERVO_PULSE_MAX_US; }
    enable = (enable != 0u) ? 1u : 0u;

    if ((pulse_us == servo_current_pulse) && (enable == servo_enabled)
        && (old_min == servo_min_us) && (old_center == servo_center_us)
        && (old_max == servo_max_us))
        return;

    duty = (0 != enable) ? servo_pulse_to_duty(pulse_us) : 0u;
    duty = servo_duty_override(duty);           /* no-op unless a test switch is on */

    servo_pwm_write_ch(duty);
    servo_current_pulse = pulse_us;
    servo_enabled       = enable;
    servo_duty          = duty;

#if SERVO_SERIAL_TELEMETRY
    /* UART0 protocol consumed by Virtual_BDS300/virtual_bds300.py.  Keep the
     * line ASCII and stable so it is also convenient to log in a terminal.
     * cm0/cm1 are read straight back from the GTM: cm1 ticks of every cm0
     * ticks is what the pin really outputs, independent of any maths above. */
    printf("BDS300 angle=%u min_us=%u center_us=%u max_us=%u enable=%u pulse_us=%u duty=%lu cm0=%lu cm1=%lu\r\n",
           (unsigned)(servo_pulse_to_angle_x10(pulse_us) / 10u),
           (unsigned)servo_min_us, (unsigned)servo_center_us, (unsigned)servo_max_us,
           (unsigned)servo_enabled,
           (unsigned)((0 != enable) ? pulse_us : 0u),
           (unsigned long)duty,
           (unsigned long)MODULE_GTM.ATOM[1].CH1.CM0.U,
           (unsigned long)MODULE_GTM.ATOM[1].CH1.CM1.U);
#endif
}

void servo_control_set(uint16 angle, uint16 min_us, uint16 center_us, uint16 max_us, uint8 enable)
{
    servo_params_set(min_us, center_us, max_us);
    servo_control_set_pulse(servo_angle_to_pulse(angle), min_us, center_us, max_us, enable);
}

uint16 servo_get_pulse(void)
{
    return (SERVO_PULSE_INVALID == servo_current_pulse) ? (0u) : (servo_current_pulse);
}

uint32 servo_get_duty(void)
{
    return servo_duty;
}

uint8 servo_get_enable(void)
{
    return servo_enabled;
}
