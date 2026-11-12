#include <stdio.h>

#include "board.h"
#include "config_flash.h"
#include "display.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/watchdog.h"
#include "ir_nec.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include "ui.h"
#include "waveform.h"

/* Build marker used to identify the flashed image on the USB serial console. */
#if PWM_HW_SQUARE
#define GEN_MARKER "hw-pwm-exact-50"
#elif PWM_TEST_SQUARE
#define GEN_MARKER "timer-0us-dead"
#elif PWM_SOFT_START
#define GEN_MARKER "timer-soft-start"
#else
#define GEN_MARKER "pio-200us-dead"
#endif

int main(void)
{
    waveform_init();
    waveform_off();
    stdio_init_all();

    /* Setup-force jumper: fit GP14 to GND before power-up to re-run IR setup. */
    gpio_init(PIN_SETUP);
    gpio_set_dir(PIN_SETUP, GPIO_IN);
    gpio_pull_up(PIN_SETUP);

    /* Status LED heartbeat. */
    gpio_init(PIN_STATUS_LED);
    gpio_set_dir(PIN_STATUS_LED, GPIO_OUT);
    gpio_put(PIN_STATUS_LED, 0);

    /* OLED I2C bus. */
    i2c_init(INV_I2C, INV_I2C_BAUD);
    gpio_set_function(PIN_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(PIN_I2C_SDA);
    gpio_pull_up(PIN_I2C_SCL);

    if (!display_init(INV_I2C, DISPLAY_ADDR)) {
        printf("OLED not found at 0x%02X/0x%02X\n", DISPLAY_ADDR, DISPLAY_ADDR == 0x3C ? 0x3D : 0x3C);
    }

    ir_nec_init();

    inverter_config_t config;
    bool stored = config_load(&config);
    if (!stored) {
        config_load_defaults(&config);
    }

    /* Apply this release's 50 Hz / 33% preset once, preserving learned keys.
     * Subsequent boots retain user adjustments. Output is OFF during the save. */
    if (stored && config.reserved0 != CONFIG_TIMING_PROFILE) {
        config.freq_hz = PWM_DEFAULT_HZ;
        config.duty_pct = PWM_DEFAULT_DUTY;
        config.reserved0 = CONFIG_TIMING_PROFILE;
        if (!config_save(&config)) {
            printf("Timing preset save failed; will retry on next boot\n");
        }
    }

    waveform_set_frequency(config.freq_hz);
    waveform_set_duty(config.duty_pct);

    printf("Pico inverter FW1 [%s] stored=%d configured=%d f=%uHz duty=%u%%\n",
           GEN_MARKER,
           stored,
           config.configured,
           (unsigned)config.freq_hz,
           (unsigned)config.duty_pct);

    ui_init(&config);

    watchdog_enable(1000, 1);

    uint64_t last_task_us = time_us_64();
    uint64_t last_led_us = last_task_us;
    bool led = false;

    while (true) {
        watchdog_update();

#ifdef FW_BANNER_LOOP
        /* Diagnostic: reprint the boot banner periodically (host-capture only). */
        static uint64_t banner_last_us;
        uint64_t banner_now = time_us_64();
        if (banner_now - banner_last_us > 1000000u) {
            banner_last_us = banner_now;
            printf("Pico inverter FW1 [%s] stored=%d f=%uHz\n",
                   GEN_MARKER, config.configured, (unsigned)config.freq_hz);
        }
#endif

        ir_event_t event;
        while (ir_nec_poll(&event)) {
            ui_handle_ir(&event);
        }

        uint64_t now = time_us_64();
        if ((now - last_task_us) >= 10000u) {
            last_task_us = now;
            waveform_task();
            ui_task();
        }

        if ((now - last_led_us) >= 500000u) {
            last_led_us = now;
            led = !led;
            gpio_put(PIN_STATUS_LED, led);
        }

        tight_loop_contents();
    }
}
