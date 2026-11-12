#ifndef WAVEFORM_TIMING_H
#define WAVEFORM_TIMING_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t on_us, off_us, period_us;
    uint32_t on_count, off_count;
    bool limited;
} waveform_timing_t;

/* Shared by the production PIO driver and host verification. */
static inline waveform_timing_t waveform_timing(uint16_t hz, uint8_t duty,
                                               uint32_t dead, uint32_t max_on)
{
    waveform_timing_t t;
    uint32_t half = 1000000u / (2u * hz);
    uint32_t requested = (2u * half * duty) / 100u;
    t.on_us = requested;
    if (t.on_us > half - dead) t.on_us = half - dead;
    if (max_on && t.on_us > max_on) t.on_us = max_on;
    t.off_us = half - t.on_us;
    t.period_us = 2u * half;
    t.limited = t.on_us < requested;
    /* SET + (X+1) JMP instructions is ON. OFF also contains two MOVs. */
    t.on_count = t.on_us - 2u;
    t.off_count = t.off_us - 4u;
    return t;
}
#endif
