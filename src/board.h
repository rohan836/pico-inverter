#ifndef BOARD_H
#define BOARD_H

#include "hardware/i2c.h"

/* Pico pin assignment */
#define PIN_I2C_SDA        0
#define PIN_I2C_SCL        1
#define PIN_IR             2
#define PIN_DRV_A          6
#define PIN_DRV_B          7
#define PIN_SETUP          14
#define PIN_STATUS_LED     25

#define INV_I2C            i2c0
#define INV_I2C_BAUD       400000
#define DISPLAY_ADDR       0x3C

/*
 * DUTY is the per-channel duty cycle over the FULL period, shared symmetrically
 * between Side A and Side B (the modified-square-wave push-pull layout):
 *
 *   GP6 high          [0, on)
 *   both OFF          [on, half)
 *   GP7 high          [half, half + on)
 *   both OFF          [half + on, 2*half)
 *
 * The both-OFF interval between the pulses and before the next cycle is
 * identical (half - on), and it is clamped to at least PWM_DEAD_TIME_US.
 * Exact 50 % per channel would leave zero dead time, so the duty saturates
 * just below 50 % (about 48.8 % at 60 Hz) and the display shows LIM.
 */
#define PWM_MIN_HZ         1
#define PWM_MAX_HZ         60
#define PWM_DEFAULT_HZ     50
#define PWM_DEFAULT_DUTY   25
#define PWM_MIN_DUTY       1
/*
 * 1..100 range for the remote.  The symmetric both-gaps-equal layout needs
 * on <= half - dead, so the ON time saturates at about 49 % of the period
 * (e.g. 8133 us at 60 Hz).  Any DUTY above that still sets the same maximum
 * pulse and the display shows LIM (dead-time clamp).
 */
#define PWM_MAX_DUTY       100
/* Minimum both-OFF interval.  At DUTY < 100 % the OFF time is longer. */
#ifndef PWM_DEAD_TIME_US
#define PWM_DEAD_TIME_US   200
#endif

/*
 * Output generator selection.
 *
 * Default (0): timer generator.  Side A on for `on` microseconds, both off
 * for `half - on`, then Side B for the same `on`, both off.  The duty setting
 * is the per-channel duty cycle over the full period, so the OLED percentage
 * is what the scope shows on each channel (saturates just below 50 % with LIM).
 *
 * 1: fixed exact 50 % complementary square wave from one hardware PWM slice
 *    (no dead time, duty locked).  Bench test only.
 */
#ifndef PWM_HW_SQUARE
#define PWM_HW_SQUARE      0
#endif

/*
 * Bench-test mode: timer generator with zero dead time.
 * Enable with -DPWM_TEST_SQUARE=1.
 */
#ifndef PWM_TEST_SQUARE
#define PWM_TEST_SQUARE    0
#endif

#if PWM_TEST_SQUARE
#undef  PWM_HW_SQUARE
#define PWM_HW_SQUARE      0
#undef  PWM_DEAD_TIME_US
#define PWM_DEAD_TIME_US   0
#undef  PWM_SOFT_START
#define PWM_SOFT_START     0
#elif PWM_HW_SQUARE
#undef  PWM_DEAD_TIME_US
#define PWM_DEAD_TIME_US   0
#undef  PWM_SOFT_START
#define PWM_SOFT_START     0
#else
/*
 * Soft start ramps the ON time from zero when the output turns on.  It is OFF
 * by default so PWM ON immediately produces the full square wave (harder on a
 * transformer at start; better for bench/scope work).  Enable with
 * -DPWM_SOFT_START=1 if you want the ramp back.
 */
#ifndef PWM_SOFT_START
#define PWM_SOFT_START     0
#endif
#endif

/*
 * Transformer volt-second guard.  A normal 50/60 Hz transformer cannot accept
 * a 500 ms half-cycle at 1 Hz.  This caps the maximum ON pulse to 10 ms, which
 * is safe for a 50 Hz transformer and has no effect at 50/60 Hz.  Set to 0 to
 * disable only if your transformer is rated for the lower frequency.
 */
#define PWM_MAX_ON_US      10000u

#endif
