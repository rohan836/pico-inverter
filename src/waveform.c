#include "waveform.h"

#include "board.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/timer.h"
#include "pico/stdlib.h"
#include "pico/time.h"

/* Keep the first alarm target safely in the future. */
#define START_DELAY_US      100u
/* Re-arm delay, used only if a deadline has already passed. */
#define RECOVER_DELAY_US      5u

enum {
    STATE_A_ON = 0,
    STATE_A_OFF,
    STATE_B_ON,
    STATE_B_OFF,
};

static volatile bool running;
static volatile uint8_t next_state;
static volatile uint64_t next_edge_us;
static volatile uint16_t freq_hz = PWM_DEFAULT_HZ;
static volatile uint16_t duty_target_permille = PWM_DEFAULT_DUTY * 10u;
static volatile uint16_t duty_eff_permille = PWM_DEFAULT_DUTY * 10u;
static volatile uint32_t on_us;
static volatile uint32_t off_us;
static volatile bool limited;
static int alarm_num = -1;

static inline void set_outputs(bool a, bool b)
{
    uint32_t values = 0;

    if (a) {
        values |= (1u << PIN_DRV_A);
    }
    if (b) {
        values |= (1u << PIN_DRV_B);
    }

    /* One register write: both pins change together, with no intermediate state. */
    gpio_put_masked((1u << PIN_DRV_A) | (1u << PIN_DRV_B), values);
}

static uint32_t clamp_frequency(void)
{
    uint32_t f = freq_hz;
    if (f < PWM_MIN_HZ) {
        f = PWM_MIN_HZ;
    }
    if (f > PWM_MAX_HZ) {
        f = PWM_MAX_HZ;
    }
    return f;
}

/*
 * Called from thread context only.  Using a fixed integer half period makes
 * every cycle identical: at 60 Hz, half = 8333 us and the period is exactly
 * 16666 us.  There is no alternating 16666/16667 pattern, so the scope sees a
 * rock-steady period (60.0024 Hz, 40 ppm high, well inside crystal tolerance).
 *
 * DUTY is the per-channel duty cycle over the full period: `on = period * duty`.
 * Side A is on for `on` from the start of the half-cycle, both sides are off
 * for the rest of the half-cycle, then Side B does the same.  Both gaps are
 * `half - on` and are clamped to at least PWM_DEAD_TIME_US.
 */
static void compute_timing(void)
{
    uint32_t f = clamp_frequency();
    uint32_t half = 1000000u / (2u * f);

#if PWM_TEST_SQUARE
    if (half < 2u) {
        half = 2u;
    }
    /* Zero dead time: DUTY 100 gives a pure complementary 50 % square wave. */
    uint32_t on = (half * (uint32_t)duty_eff_permille) / 1000u;
    if (on > half) {
        on = half;
    }
    on_us = on;
    off_us = half - on;
    limited = false;
#else
    uint32_t dead = PWM_DEAD_TIME_US;
    if (half <= dead + 20u) {
        dead = 0;
    }

    /*
     * duty_eff_permille is permille of the FULL period.  Max `on` is
     * half - dead so that both both-OFF gaps are >= dead.
     */
    uint32_t max_on = half - dead;
    uint32_t on = (2u * half * (uint32_t)duty_eff_permille) / 1000u;
    limited = false;

    if (on > max_on) {
        on = max_on;
        limited = true;
    }
#if PWM_MAX_ON_US > 0
    if (on > PWM_MAX_ON_US) {
        on = PWM_MAX_ON_US;
        limited = true;
    }
#endif

    on_us = on;
    off_us = half - on;
#endif
}

/* Recomputed outside the ISR so the edge handler only sets pins and re-arms. */
static void __not_in_flash_func(alarm_cb)(unsigned int alarm)
{
    if (!running) {
        return;
    }

    switch (next_state) {
    case STATE_A_ON:
        if (on_us == 0) {
            set_outputs(false, false);
            next_edge_us += off_us;
            next_state = STATE_B_ON;
        } else {
            set_outputs(true, false);
            next_edge_us += on_us;
            next_state = (off_us == 0) ? STATE_B_ON : STATE_A_OFF;
        }
        break;

    case STATE_A_OFF:
        set_outputs(false, false);
        next_edge_us += off_us;
        next_state = STATE_B_ON;
        break;

    case STATE_B_ON:
        if (on_us == 0) {
            set_outputs(false, false);
            next_edge_us += off_us;
            next_state = STATE_A_ON;
        } else {
            set_outputs(false, true);
            next_edge_us += on_us;
            next_state = (off_us == 0) ? STATE_A_ON : STATE_B_OFF;
        }
        break;

    case STATE_B_OFF:
    default:
        set_outputs(false, false);
        next_edge_us += off_us;
        next_state = STATE_A_ON;
        break;
    }

    /*
     * hardware_alarm_set_target() will not arm an alarm whose target is
     * already in the past; it just reports "missed" and leaves it disarmed.
     * Re-arm a few microseconds ahead instead of forcing an output pulse
     * (which would appear as a step/glitch on the scope).
     */
    if (hardware_alarm_set_target(alarm, from_us_since_boot(next_edge_us))) {
        next_edge_us = time_us_64() + RECOVER_DELAY_US;
        hardware_alarm_set_target(alarm, from_us_since_boot(next_edge_us));
    }
}

void waveform_init(void)
{
    gpio_init(PIN_DRV_A);
    gpio_set_dir(PIN_DRV_A, GPIO_OUT);
    /* ~10 mA opto LED current: use the 8 mA pad setting so VOH stays high. */
    gpio_set_drive_strength(PIN_DRV_A, GPIO_DRIVE_STRENGTH_8MA);
    gpio_put(PIN_DRV_A, 0);

    gpio_init(PIN_DRV_B);
    gpio_set_dir(PIN_DRV_B, GPIO_OUT);
    gpio_set_drive_strength(PIN_DRV_B, GPIO_DRIVE_STRENGTH_8MA);
    gpio_put(PIN_DRV_B, 0);

    alarm_num = hardware_alarm_claim_unused(true);
    if (alarm_num < 0) {
        panic("No hardware alarm available");
    }
    hardware_alarm_set_callback((uint)alarm_num, alarm_cb);

    /* The waveform ISR must not be delayed by the IR receiver ISR. */
    irq_set_priority(TIMER_IRQ_0, 0x00);
}

void waveform_on(void)
{
    if (running || alarm_num < 0) {
        return;
    }

    uint32_t ints = save_and_disable_interrupts();
#if PWM_SOFT_START
    duty_eff_permille = 0;
#else
    duty_eff_permille = duty_target_permille;
#endif
    compute_timing();
    next_state = STATE_A_ON;
    set_outputs(false, false);
    next_edge_us = time_us_64() + START_DELAY_US;
    running = true;
    hardware_alarm_set_target((uint)alarm_num, from_us_since_boot(next_edge_us));
    restore_interrupts(ints);
}

void waveform_off(void)
{
    uint32_t ints = save_and_disable_interrupts();
    running = false;
    if (alarm_num >= 0) {
        hardware_alarm_cancel((uint)alarm_num);
    }
    set_outputs(false, false);
    duty_eff_permille = 0;
    restore_interrupts(ints);
}

bool waveform_is_on(void)
{
    return running;
}

void waveform_set_frequency(uint16_t hz)
{
    if (hz < PWM_MIN_HZ) {
        hz = PWM_MIN_HZ;
    }
    if (hz > PWM_MAX_HZ) {
        hz = PWM_MAX_HZ;
    }

    uint32_t ints = save_and_disable_interrupts();
    freq_hz = hz;
    compute_timing();
    restore_interrupts(ints);
}

void waveform_set_duty(uint8_t pct)
{
    if (pct < PWM_MIN_DUTY) {
        pct = PWM_MIN_DUTY;
    }
    if (pct > PWM_MAX_DUTY) {
        pct = PWM_MAX_DUTY;
    }

    uint32_t ints = save_and_disable_interrupts();
    duty_target_permille = (uint16_t)pct * 10u;
#if !PWM_SOFT_START
    duty_eff_permille = duty_target_permille;
    compute_timing();
#endif
    restore_interrupts(ints);
}

uint16_t waveform_get_frequency(void)
{
    return freq_hz;
}

uint8_t waveform_get_duty(void)
{
    return (uint8_t)(duty_target_permille / 10u);
}

void waveform_task(void)
{
#if PWM_SOFT_START
    if (!running) {
        return;
    }

    /*
     * 1 % per 10 ms = full 0..100 % soft start in about one second.
     * Recompute in thread context; the ISR uses the prepared values.
     */
    uint16_t target = duty_target_permille;
    uint16_t current = duty_eff_permille;

    if (current < target) {
        current = (uint16_t)((current + 10u > target) ? target : current + 10u);
    } else if (current > target) {
        current = (uint16_t)((current < target + 10u) ? target : current - 10u);
    } else {
        return;
    }

    uint32_t ints = save_and_disable_interrupts();
    duty_eff_permille = current;
    compute_timing();
    restore_interrupts(ints);
#endif
}

uint32_t waveform_get_period_us(void)
{
    uint32_t f = freq_hz;
    if (f < PWM_MIN_HZ) {
        f = PWM_MIN_HZ;
    }
    return 1000000u / f;
}

uint32_t waveform_get_on_us(void)
{
    return on_us;
}

bool waveform_is_limited(void)
{
    return limited;
}
