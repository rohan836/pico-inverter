#include <stdio.h>
#include "waveform_timing.h"
int main(void) {
    for (unsigned hz = 1; hz <= 60; ++hz)
        for (unsigned duty = 1; duty <= 100; ++duty) {
            waveform_timing_t t = waveform_timing(hz, duty, 200, 10000);
            printf("%u %u %u %u %u %u %u %u\n", hz, duty, t.on_us,
                   t.off_us, t.period_us, t.on_count, t.off_count, t.limited);
        }
    return 0;
}
