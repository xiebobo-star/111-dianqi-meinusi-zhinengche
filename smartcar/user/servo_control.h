#ifndef SERVO_CONTROL_H
#define SERVO_CONTROL_H

#include "zf_common_headfile.h"

/* The pulse width in microseconds is the single source of truth; the angle is
 * only a derived value for display.  servo_control_set() stays available as an
 * angle-based wrapper for tests (boot sweep).
 *   servo_control_init()      : init PWM, then apply the given pulse
 *   servo_control_set_pulse() : menu path, command a pulse directly
 *   servo_control_set()       : command an angle, converted with min/center/max */
void    servo_control_init      (uint16 pulse_us, uint16 min_us, uint16 center_us, uint16 max_us, uint8 enable);
void    servo_control_set_pulse (uint16 pulse_us, uint16 min_us, uint16 center_us, uint16 max_us, uint8 enable);
void    servo_control_set       (uint16 angle,    uint16 min_us, uint16 center_us, uint16 max_us, uint8 enable);

/* pulse -> equivalent angle, in 0.1 deg units (900 = 90.0 deg), for the menu.
 * The _cal variants take the min/center/max calibration points explicitly, so
 * the menu can convert with the values currently in the parameter table. */
uint16  servo_pulse_to_angle_x10     (uint16 pulse_us);
uint16  servo_pulse_to_angle_x10_cal (uint16 pulse_us, uint16 min_us, uint16 center_us, uint16 max_us);
uint16  servo_angle_x10_to_pulse_cal (uint16 angle_x10, uint16 min_us, uint16 center_us, uint16 max_us);

/* current output state, for the menu pages */
uint16  servo_get_pulse         (void);
uint32  servo_get_duty          (void);
uint8   servo_get_enable        (void);

#endif
