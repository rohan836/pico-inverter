#include "dashboard.h"

#include <stdio.h>

#include "digits.h"
#include "display.h"

/* Static 128 x 64 layout: frame, two readings, and an explicit status row. */
void dashboard_render(uint32_t freq_hz, uint32_t duty_pct, bool output_on,
                      bool limited, uint16_t phase)
{
    char value[12];
    (void)phase;
    display_clear();
    display_rect(0, 0, 128, 64, true);
    display_fill_rect(64, 3, 1, 41, true);
    display_fill_rect(1, 46, 126, 1, true);

    display_text(6, 5, "FREQ HZ", 1);
    display_text(70, 5, "DUTY %", 1);

    snprintf(value, sizeof(value), "%lu", (unsigned long)freq_hz);
    digits_draw(4 + (57 - digits_width(value, &digit_font_hero)) / 2,
                17, value, &digit_font_hero, true);

    snprintf(value, sizeof(value), "%lu", (unsigned long)duty_pct);
    digits_draw(68 + (56 - digits_width(value, &digit_font_mid)) / 2,
                20, value, &digit_font_mid, true);

    display_text(6, 51, "PWM", 1);
    display_text(32, 51, output_on ? "ON" : "OFF", 1);
    if (limited) {
        display_text(103, 51, "LIM", 1);
    }
    display_flush();
}
