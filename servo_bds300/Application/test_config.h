#ifndef TEST_CONFIG_H
#define TEST_CONFIG_H

/* BDS300 electrical and PWM operating points. */
#define SERVO_PWM_FREQ_HZ       50u
#define SERVO_MIN_ANGLE_DEG     0u
#define SERVO_CENTER_ANGLE_DEG  90u
#define SERVO_MAX_ANGLE_DEG     180u
/* 三点标定（0° / 90° / 180°）。舵机装到机构上对不齐时，软件补偿就靠这三个数：
 * 整体平移 = 改中位、比例不变 = min/center/max 三点的间距保持不变。
 * 现在：90° = 1540 us（原始 1500 us 往右挪了 40 us，约 7.2°），
 *       0° = 1040 us，180° = 2040 us，每 90° 仍然是 500 us，斜率不变。 */
#define SERVO_MIN_US             1040u
#define SERVO_CENTER_US          1540u
#define SERVO_MAX_US             2040u

/* Legal window for a commanded pulse (us).  The menu can command a pulse
 * outside the nominal 1040..2040 mapping, which helps to find the real
 * endpoints of a servo, but it is hard-clamped to this window. */
#define SERVO_PULSE_MIN_US       500u
#define SERVO_PULSE_MAX_US       2500u

/* 0.1 s/60 deg gives about 300 ms for a 0-180 deg move. */
#define SERVO_AUTO_CENTER_MS     300u

/* Power-on servo self test: sweep 0 -> 180 -> 0 once (about 4 s) before the
 * menu starts, using the compile-time pulse limits and ignoring both the menu
 * angle and the stored "Pulse enable" flag.  It is the quickest way to prove
 * servo power / wiring / pin mapping without touching the screen.
 * Set to 0 for normal use. */
#define SERVO_BOOT_SWEEP         (1)

/* Emit one machine-readable line on UART0 whenever a servo command changes.
 * Set to 0 when the debug UART must remain completely silent. */
#define SERVO_SERIAL_TELEMETRY   (1)

/* ---- Multimeter / pin sanity test ----------------------------------------
 * 1 = ignore the pulse width completely and force this duty cycle, so the
 *     meter sees a signal with a KNOWN duty ratio.  Unit is PWM_DUTY_MAX
 *     (10000 = 100 %).  Use it to separate "the code is wrong" from "the
 *     meter / the pin is wrong":
 *        10000 -> pin must read a solid 3.30 V
 *         5000 -> pin must read 1.65 V  (0..100 % duty of a 3.3 V pin)
 *         2500 -> pin must read 0.825 V
 *     Set SERVO_DUTY_TEST_EN back to 0 after the test. */
#define SERVO_DUTY_TEST_EN       (0)
#define SERVO_DUTY_TEST_VAL      (10000u)

/* ---- Slow square wave test (easiest thing for a multimeter to read) ------
 * 1 = the pin outputs a SLOW 50 % square wave instead of the 50 Hz servo
 *     signal.  A DC meter must then sit on ~1.65 V (a needle meter visibly
 *     swings), which proves both "the pin really drives" and "the meter
 *     averages correctly".  Anything else (0 V, 1.x V frozen, 3.3 V) points
 *     straight at the pin / the wiring.
 *     2 Hz is used on purpose: the GTM ATOM period register CM0 is 24 bit, so
 *     1 Hz (20 000 000 ticks at 20 MHz) would overflow.  2 Hz = 10 000 000.
 *     Set back to 0 after the test. */
#define SERVO_SLOW_TEST_EN       (0)
#define SERVO_SLOW_TEST_HZ       (2u)

#if SERVO_PWM_FREQ_HZ != 50u || SERVO_MIN_US < 500u || SERVO_MAX_US > 2500u \
    || SERVO_MIN_US >= SERVO_CENTER_US || SERVO_CENTER_US >= SERVO_MAX_US \
    || SERVO_MIN_ANGLE_DEG != 0u || SERVO_CENTER_ANGLE_DEG != 90u \
    || SERVO_MAX_ANGLE_DEG != 180u
#error "Check servo pulse limits and center"
#endif

#endif
