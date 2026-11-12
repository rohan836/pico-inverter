#include "ui.h"

#include <stdio.h>

#include "board.h"
#include "dashboard.h"
#include "display.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include "waveform.h"

typedef enum {
    UI_MODE_SPLASH = 0,
    UI_MODE_SETUP,
    UI_MODE_MAIN,
} ui_mode_t;

static inverter_config_t *config;
static ui_mode_t mode;
static uint64_t mode_until_us;
static bool force_setup;
static bool dirty;

static uint8_t setup_index;
static bool setup_show_ok;
static uint64_t setup_ok_until_us;
static bool setup_error;
static uint64_t setup_error_until_us;

static bool save_pending;
static uint64_t save_due_us;
static uint64_t next_render_us;

static bool pwm_off_held;
static uint64_t pwm_off_press_us;
static uint64_t pwm_off_last_us;

/* Free-running frame counter for the dashboard's PWM meter animation. */
static uint16_t dashboard_phase;

static int find_function(uint32_t code)
{
    if (code == 0) {
        return -1;
    }
    for (int i = 0; i < IR_FUNC_COUNT; i++) {
        if (config->ir_codes[i] == code && config->ir_protocol[i] == 0) {
            return i;
        }
    }
    return -1;
}

static void request_save(uint64_t delay_us)
{
    save_pending = true;
    save_due_us = time_us_64() + delay_us;
}

/*
 * Enter the learning wizard.  With clear_codes the stored remote codes are
 * erased first, so every function can be assigned from scratch.
 */
static void enter_setup(bool clear_codes)
{
    waveform_off();

    if (clear_codes) {
        for (int i = 0; i < IR_FUNC_COUNT; i++) {
            config->ir_codes[i] = 0;
            config->ir_protocol[i] = 0;
        }
        config->configured = 0;
        /* Persist the empty state so a power cut mid-wizard re-runs setup. */
        config_save(config);
    }

    mode = UI_MODE_SETUP;
    setup_index = 0;
    setup_show_ok = false;
    setup_error = false;
    save_pending = false;
    pwm_off_held = false;
    dirty = true;
    next_render_us = 0;
}

static void render_splash(void)
{
    display_clear();
    display_text(0, 6, "PICO", 2);
    display_text(0, 24, "INVERTER", 2);
    display_text(0, 48, "MODIFIED SQUARE", 1);
    display_flush();
}

static void render_setup(void)
{
    char buffer[24];

    display_clear();
    display_text(0, 0, "IR SETUP", 1);

    if (setup_error) {
        display_text(0, 20, "DUPLICATE", 2);
        display_text(0, 42, "TRY ANOTHER", 1);
        display_text(0, 54, "BUTTON", 1);
        display_flush();
        return;
    }

    if (setup_show_ok) {
        display_text(0, 20, "OK", 2);
        if (setup_index >= IR_FUNC_COUNT) {
            display_text(0, 42, "SAVING", 1);
        } else {
            snprintf(buffer, sizeof(buffer), "NEXT %s", config_ir_func_name((ir_func_t)setup_index));
            display_text(0, 42, buffer, 1);
        }
        display_flush();
        return;
    }

    display_text(0, 20, "PRESS", 1);
    display_text(0, 34, config_ir_func_name((ir_func_t)setup_index), 2);
    snprintf(buffer, sizeof(buffer), "%u/%u", (unsigned)(setup_index + 1u), (unsigned)IR_FUNC_COUNT);
    display_text(0, 54, buffer, 1);
    display_flush();
}

static void render_main(void)
{
    dashboard_render((uint32_t)waveform_get_frequency(),
                     (uint32_t)waveform_get_duty(),
                     waveform_is_on(),
                     waveform_is_limited(),
                     dashboard_phase++);
}

static void render(void)
{
    switch (mode) {
    case UI_MODE_SPLASH:
        render_splash();
        break;
    case UI_MODE_SETUP:
        render_setup();
        break;
    case UI_MODE_MAIN:
    default:
        render_main();
        break;
    }
}

void ui_init(inverter_config_t *cfg)
{
    config = cfg;
    mode = UI_MODE_SPLASH;
    mode_until_us = time_us_64() + 800000u;
    force_setup = (!config->configured) || (gpio_get(PIN_SETUP) == 0);
    dirty = true;
    next_render_us = 0;
    setup_index = 0;
    setup_show_ok = false;
    setup_error = false;
    save_pending = false;
    pwm_off_held = false;
}

void ui_handle_ir(const ir_event_t *event)
{
    uint64_t now = time_us_64();

    if (mode == UI_MODE_SETUP) {
        if (setup_show_ok || setup_error || event->repeat || event->code == 0) {
            return;
        }

        for (int i = 0; i < IR_FUNC_COUNT; i++) {
            if (i != setup_index && config->ir_codes[i] == event->code) {
                printf("IR duplicate for %s: 0x%08lX\n",
                       config_ir_func_name((ir_func_t)setup_index),
                       (unsigned long)event->code);
                setup_error = true;
                setup_error_until_us = now + 800000u;
                dirty = true;
                return;
            }
        }

        printf("IR learn %s: 0x%08lX\n",
               config_ir_func_name((ir_func_t)setup_index),
               (unsigned long)event->code);
        config->ir_codes[setup_index] = event->code;
        config->ir_protocol[setup_index] = 0;
        setup_index++;
        setup_show_ok = true;
        setup_ok_until_us = now + 400000u;
        dirty = true;
        return;
    }

    if (mode != UI_MODE_MAIN) {
        return;
    }

    int function = find_function(event->code);
    if (function < 0) {
        pwm_off_held = false;
        return;
    }

    if (function == IR_FUNC_RESET) {
        pwm_off_held = false;
        if (!event->repeat) {
            enter_setup(true);
        }
        return;
    }

    if (function == IR_FUNC_PWM_ON) {
        pwm_off_held = false;
        if (!event->repeat) {
            waveform_on();
            dirty = true;
        }
        return;
    }

    if (function == IR_FUNC_PWM_OFF) {
        if (!pwm_off_held || event->time_us - pwm_off_last_us > 300000u) {
            pwm_off_held = true;
            pwm_off_press_us = event->time_us;
        }
        pwm_off_last_us = event->time_us;
        waveform_off();
        dirty = true;
        if (save_pending) {
            request_save(20000u);
        }

        if (event->repeat && (event->time_us - pwm_off_press_us) > 3000000u) {
            enter_setup(true);
        }
        return;
    }

    pwm_off_held = false;

    switch (function) {
    case IR_FUNC_FREQ_UP:
        if (config->freq_hz < PWM_MAX_HZ) {
            waveform_off();
            config->freq_hz++;
            waveform_set_frequency(config->freq_hz);
            dirty = true;
            request_save(500000u);
        }
        break;

    case IR_FUNC_FREQ_DOWN:
        if (config->freq_hz > PWM_MIN_HZ) {
            waveform_off();
            config->freq_hz--;
            waveform_set_frequency(config->freq_hz);
            dirty = true;
            request_save(500000u);
        }
        break;

#if !PWM_HW_SQUARE
    case IR_FUNC_DUTY_UP:
        if (config->duty_pct < PWM_MAX_DUTY) {
            waveform_off();
            config->duty_pct++;
            waveform_set_duty(config->duty_pct);
            dirty = true;
            request_save(500000u);
        }
        break;

    case IR_FUNC_DUTY_DOWN:
        if (config->duty_pct > PWM_MIN_DUTY) {
            waveform_off();
            config->duty_pct--;
            waveform_set_duty(config->duty_pct);
            dirty = true;
            request_save(500000u);
        }
        break;
#endif

    default:
        break;
    }
}

void ui_task(void)
{
    uint64_t now = time_us_64();

    if (mode == UI_MODE_SPLASH && now >= mode_until_us) {
        if (force_setup) {
            enter_setup(true);
        } else {
            mode = UI_MODE_MAIN;
        }
        dirty = true;
        next_render_us = 0;
    }

    if (mode == UI_MODE_SETUP) {
        if (setup_error && now >= setup_error_until_us) {
            setup_error = false;
            dirty = true;
        }
        if (setup_show_ok && now >= setup_ok_until_us) {
            setup_show_ok = false;
            if (setup_index >= IR_FUNC_COUNT) {
                config->configured = 1;
                config_save(config);
                mode = UI_MODE_MAIN;
            }
            dirty = true;
            next_render_us = 0;
        }
    }

    if (save_pending && !waveform_is_on() && now >= save_due_us) {
        if (config_save(config)) {
            save_pending = false;
        } else {
            save_due_us = now + 1000000u;
        }
    }

    if (dirty || now >= next_render_us) {
        render();
        dirty = false;
        next_render_us = now + 500000u;
    }
}
