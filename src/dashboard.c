#include "dashboard.h"

#include <stdio.h>
#include <string.h>

#include "digits.h"
#include "display.h"

/*
 * Modern Landscape Inverter Interface (128x64 OLED)
 *
 * Architecture - Three completely non-overlapping, strictly bounded zones:
 *
 *   ZONE 1: FREQUENCY (Primary Metric)
 *     X:  0 .. 61  (Width  62 px)
 *     Y:  0 .. 47  (Height 48 px)
 *     - Header: "FREQ" [Hz] with hairline separator rule at Y=13.
 *     - Hero Readout: 24 px bold dithered numerals, centered with precise padding.
 *
 *   ZONE 2: DUTY CYCLE (Secondary Metric)
 *     X: 66 .. 127 (Width  62 px)
 *     Y:  0 .. 47  (Height 48 px)
 *     - Header: "DUTY" [%] with hairline separator rule at Y=13.
 *     - Secondary Readout: 18 px bold dithered numerals, establishing clean hierarchy.
 *
 *   INTER-ZONE SEPARATORS:
 *     - Vertical spine: dotted canal at X=63 (Y: 4..44), keeping X=62, 64, 65 clear.
 *     - Horizontal gutter: empty negative space at Y=48..49.
 *
 *   ZONE 3: PWM STATE & DYNAMIC VISUAL PROGRESS BAR
 *     X:  0 .. 127 (Width 128 px)
 *     Y: 50 .. 63  (Height 14 px)
 *     - Replaces all textual status indicators with dynamic visual telemetry.
 *     - Standby: dormant technical dot-dash resting track (PWM OFF).
 *     - Active: proportional energized duty fill with kinetic wavefront sweep (PWM ON).
 *     - Calibrated scale ticks at 25%, 50%, 75% on outer precision rails.
 *     - Limit clamp: visual hazard striae in the terminal headroom (LIMITED).
 */

/* Zone 1 Coordinates: Frequency */
#define Z1_X_MIN        0
#define Z1_X_MAX        61
#define Z1_Y_MIN        0
#define Z1_Y_MAX        47
#define Z1_INNER_X      5
#define Z1_INNER_W      52
#define Z1_HEADER_Y     4
#define Z1_RULE_Y       13
#define Z1_NUMERAL_Y    18

/* Zone 2 Coordinates: Duty Cycle */
#define Z2_X_MIN        66
#define Z2_X_MAX        127
#define Z2_Y_MIN        0
#define Z2_Y_MAX        47
#define Z2_INNER_X      71
#define Z2_INNER_W      52
#define Z2_HEADER_Y     4
#define Z2_RULE_Y       13
#define Z2_NUMERAL_Y    21

/* Zone 3 Coordinates: Dynamic Visual Progress Bar */
#define Z3_FRAME_X      2
#define Z3_FRAME_Y      51
#define Z3_FRAME_W      124
#define Z3_FRAME_H      12

#define Z3_TRACK_X      4
#define Z3_TRACK_Y      53
#define Z3_TRACK_W      120
#define Z3_TRACK_H      8

#define PULSE_WIDTH     8

/* Render technical corner datum brackets for instrument pod enclosure */
static void draw_datum_brackets(int left, int top, int right, int bottom)
{
    /* Top-left */
    display_pixel(left, top, true);
    display_pixel(left + 1, top, true);
    display_pixel(left, top + 1, true);

    /* Top-right */
    display_pixel(right, top, true);
    display_pixel(right - 1, top, true);
    display_pixel(right, top + 1, true);

    /* Bottom-left */
    display_pixel(left, bottom, true);
    display_pixel(left + 1, bottom, true);
    display_pixel(left, bottom - 1, true);

    /* Bottom-right */
    display_pixel(right, bottom, true);
    display_pixel(right - 1, bottom, true);
    display_pixel(right, bottom - 1, true);
}

/* Triangle wave generator for smooth back-and-forth travelling pulse */
static int ping_pong(uint32_t t, int span)
{
    if (span <= 0) {
        return 0;
    }
    uint32_t period = (uint32_t)(2 * span);
    uint32_t m = t % period;
    return (m < (uint32_t)span) ? (int)m : (span - (int)(m - (uint32_t)span));
}

/*
 * Dynamic Visual Progress Bar.
 * Communicates PWM state (Running vs Idle), duty percentage, and limit status
 * entirely through dynamic visual elements without any text status strings.
 */
static void draw_dynamic_progress_bar(uint32_t duty_pct, bool on, bool limited, uint16_t phase)
{
    /* 1. Precision outer frame */
    display_rect(Z3_FRAME_X, Z3_FRAME_Y, Z3_FRAME_W, Z3_FRAME_H, true);

    /* 2. Scale calibration ticks on upper and lower frame rails (25%, 50%, 75%) */
    for (int q = 1; q <= 3; q++) {
        int tx = Z3_TRACK_X + (Z3_TRACK_W * q) / 4;
        display_pixel(tx, Z3_FRAME_Y - 1, true); /* Y = 50 */
        display_pixel(tx, Z3_FRAME_Y + Z3_FRAME_H, true); /* Y = 63 */
    }

    /* 3. Inactive (PWM OFF) State: technical dot-dash resting track */
    if (!on) {
        for (int dx = 6; dx <= 120; dx += 6) {
            display_fill_rect(Z3_TRACK_X + dx - 4, Z3_TRACK_Y + 3, 3, 2, true);
        }
        return;
    }

    /* 4. Active (PWM ON) State: energized proportional fill */
    int fill_w = (int)((duty_pct * (uint32_t)Z3_TRACK_W) / 100u);
    if (fill_w < 1) {
        fill_w = 1;
    }
    if (fill_w > Z3_TRACK_W) {
        fill_w = Z3_TRACK_W;
    }

    display_fill_rect(Z3_TRACK_X, Z3_TRACK_Y, fill_w, Z3_TRACK_H, true);

    /* Scale division notches cut cleanly through the active fill */
    for (int q = 1; q <= 3; q++) {
        int tx = Z3_TRACK_X + (Z3_TRACK_W * q) / 4;
        if (tx < Z3_TRACK_X + fill_w - 1) {
            display_fill_rect(tx, Z3_TRACK_Y, 1, Z3_TRACK_H, false);
        }
    }

    /* Crisp demarcation on leading edge */
    if (fill_w >= 4) {
        display_fill_rect(Z3_TRACK_X + fill_w - 2, Z3_TRACK_Y, 2, Z3_TRACK_H, false);
    }

    /* Dynamic kinetic wavefront sweep (motion along energized vector) */
    if (fill_w > 12) {
        int span = fill_w - (PULSE_WIDTH + 2);
        int pos = ping_pong(phase, span);
        display_fill_rect(Z3_TRACK_X + pos, Z3_TRACK_Y + 1, PULSE_WIDTH, Z3_TRACK_H - 2, false);
    }

    /* Visual limit indicator: warning hazard striae at the end-cap headroom */
    if (limited) {
        for (int hx = 116; hx <= 122; hx += 2) {
            display_fill_rect(hx, Z3_TRACK_Y, 1, Z3_TRACK_H, true);
        }
    }
}

void dashboard_render(uint32_t freq_hz, uint32_t duty_pct, bool output_on,
                      bool limited, uint16_t phase)
{
    char text[8];

    display_clear();

    /* =====================================================================
     * ZONE 1: FREQUENCY (Primary Metric Instrument Pod)
     * ===================================================================== */
    draw_datum_brackets(2, 2, 59, 45);

    display_text(Z1_INNER_X, Z1_HEADER_Y, "FREQ", 1);
    display_text(Z1_INNER_X + Z1_INNER_W - 11, Z1_HEADER_Y, "HZ", 1);
    display_fill_rect(Z1_INNER_X, Z1_RULE_Y, Z1_INNER_W, 1, true);

    /* 24 px bold dithered hero numerals, centered with precise padding */
    snprintf(text, sizeof(text), "%lu", (unsigned long)freq_hz);
    int freq_w = digits_width(text, &digit_font_hero);
    int freq_x = Z1_INNER_X + (Z1_INNER_W - freq_w) / 2;
    digits_draw(freq_x, Z1_NUMERAL_Y, text, &digit_font_hero, true);

    /* =====================================================================
     * ZONE 2: DUTY CYCLE (Secondary Metric Instrument Pod)
     * ===================================================================== */
    draw_datum_brackets(68, 2, 125, 45);

    display_text(Z2_INNER_X, Z2_HEADER_Y, "DUTY", 1);
    display_text(Z2_INNER_X + Z2_INNER_W - 5, Z2_HEADER_Y, "%", 1);
    display_fill_rect(Z2_INNER_X, Z2_RULE_Y, Z2_INNER_W, 1, true);

    /* 18 px bold dithered numerals, establishing clean visual hierarchy */
    snprintf(text, sizeof(text), "%lu", (unsigned long)duty_pct);
    int duty_w = digits_width(text, &digit_font_mid);
    int duty_x = Z2_INNER_X + (Z2_INNER_W - duty_w) / 2;
    digits_draw(duty_x, Z2_NUMERAL_Y, text, &digit_font_mid, true);

    /* =====================================================================
     * INTER-ZONE DIVIDER (Vertical canal at X: 63, Y: 4..44)
     * ===================================================================== */
    for (int dy = 4; dy <= 44; dy += 2) {
        display_pixel(63, dy, true);
    }

    /* =====================================================================
     * ZONE 3: PWM STATE & DYNAMIC VISUAL PROGRESS BAR (Y: 50..63)
     * ===================================================================== */
    draw_dynamic_progress_bar(duty_pct, output_on, limited, phase);

    display_flush();
}
