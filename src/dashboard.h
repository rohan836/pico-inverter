#ifndef DASHBOARD_H
#define DASHBOARD_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Main operator dashboard.  Owns the whole 128x64 frame: it clears, draws and
 * flushes the panel itself.  `phase` is a free-running frame counter used only
 * to animate the PWM meter while the output is running.
 */
void dashboard_render(uint32_t freq_hz, uint32_t duty_pct, bool output_on,
                      bool limited, uint16_t phase);

#endif
