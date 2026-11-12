#include "waveform.h"
#include "board.h"
#include "waveform_timing.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "inverter.pio.h"
#include "pico/stdlib.h"

_Static_assert(PIN_DRV_B == PIN_DRV_A + 1, "PIO requires consecutive drive pins");
static PIO pio = pio0;
static uint sm, offset;
static uint16_t frequency = PWM_DEFAULT_HZ;
static uint8_t duty = PWM_DEFAULT_DUTY;
static waveform_timing_t timing;
static bool running;

static void configure(void)
{
    timing = waveform_timing(frequency, duty, PWM_DEAD_TIME_US, PWM_MAX_ON_US);
}

static void pins_off(void)
{
    gpio_put_masked((1u << PIN_DRV_A) | (1u << PIN_DRV_B), 0);
    gpio_set_function(PIN_DRV_A, GPIO_FUNC_SIO);
    gpio_set_function(PIN_DRV_B, GPIO_FUNC_SIO);
}

void waveform_off(void)
{
    running = false;
    pio_sm_set_enabled(pio, sm, false);
    pins_off();
}

static void start(void)
{
    /* Clear the state machine, FIFO, and output latch before handing over pins. */
    pio_sm_set_enabled(pio, sm, false);
    pins_off();
    pio_sm_config c = inverter_program_get_default_config(offset);
    sm_config_set_set_pins(&c, PIN_DRV_A, 2);
    /* RP2040's normal clock is an integer MHz, yielding exact 1 us cycles. */
    uint32_t clock = clock_get_hz(clk_sys);
    hard_assert(clock % 1000000u == 0);
    sm_config_set_clkdiv(&c, (float)(clock / 1000000u));
    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_pins_with_mask(pio, sm, 0, (1u << PIN_DRV_A) | (1u << PIN_DRV_B));
    pio_sm_set_consecutive_pindirs(pio, sm, PIN_DRV_A, 2, true);
    pio_sm_put_blocking(pio, sm, timing.on_count);
    pio_sm_put_blocking(pio, sm, timing.off_count);
    sleep_us(PWM_DEAD_TIME_US);
    pio_gpio_init(pio, PIN_DRV_A);
    pio_gpio_init(pio, PIN_DRV_B);
    pio_sm_set_enabled(pio, sm, true);
    running = true;
}

void waveform_init(void)
{
    gpio_init(PIN_DRV_A); gpio_init(PIN_DRV_B);
    gpio_set_dir(PIN_DRV_A, GPIO_OUT); gpio_set_dir(PIN_DRV_B, GPIO_OUT);
    pins_off();
    gpio_set_drive_strength(PIN_DRV_A, GPIO_DRIVE_STRENGTH_8MA);
    gpio_set_drive_strength(PIN_DRV_B, GPIO_DRIVE_STRENGTH_8MA);
    sm = pio_claim_unused_sm(pio, true);
    offset = pio_add_program(pio, &inverter_program);
    configure();
}
void waveform_on(void) { if (!running) start(); }
bool waveform_is_on(void) { return running; }
void waveform_set_frequency(uint16_t hz)
{
    if (hz < PWM_MIN_HZ) hz = PWM_MIN_HZ;
    if (hz > PWM_MAX_HZ) hz = PWM_MAX_HZ;
    if (frequency == hz) return;
    waveform_off(); frequency = hz; configure();
}
void waveform_set_duty(uint8_t pct)
{
    if (pct < PWM_MIN_DUTY) pct = PWM_MIN_DUTY;
    if (pct > PWM_MAX_DUTY) pct = PWM_MAX_DUTY;
    if (duty == pct) return;
    waveform_off(); duty = pct; configure();
}
uint16_t waveform_get_frequency(void) { return frequency; }
uint8_t waveform_get_duty(void) { return duty; }
void waveform_task(void) { }
uint32_t waveform_get_period_us(void) { return timing.period_us; }
uint32_t waveform_get_on_us(void) { return timing.on_us; }
bool waveform_is_limited(void) { return timing.limited; }
