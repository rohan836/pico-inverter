#ifndef WAVEFORM_H
#define WAVEFORM_H

#include <stdbool.h>
#include <stdint.h>

void waveform_init(void);
void waveform_on(void);
void waveform_off(void);
bool waveform_is_on(void);

void waveform_set_frequency(uint16_t hz);
void waveform_set_duty(uint8_t pct);
uint16_t waveform_get_frequency(void);
uint8_t waveform_get_duty(void);

/* Call every 10 ms from the main loop for the soft-start ramp and safety. */
void waveform_task(void);

/* Diagnostics for the UI/scope. Values are in microseconds. */
uint32_t waveform_get_period_us(void);
uint32_t waveform_get_on_us(void);
bool waveform_is_limited(void);

#endif
