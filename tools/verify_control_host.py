"""Compile the production PIO driver against a host SDK shim; test control state.
The shim does not model electrical outputs. Timing is tested separately.
"""
import os
from pathlib import Path
import subprocess
root = Path(__file__).resolve().parent.parent
stub = root / 'build/control_stubs'
stub.mkdir(parents=True, exist_ok=True)
header = '''#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
typedef unsigned uint;
typedef void *PIO;
#define pio0 ((PIO)1)
#define GPIO_FUNC_SIO 0
#define GPIO_OUT 1
#define GPIO_DRIVE_STRENGTH_8MA 8
#define clk_sys 0
#define hard_assert assert
typedef struct {int unused;} pio_sm_config;
static int inverter_program;
static bool enabled;
static unsigned delay_us, words;
static uint32_t fifo[2];
static void gpio_put_masked(uint32_t a, uint32_t b) {(void)a; assert(b==0);}
static void gpio_set_function(uint a, uint b) {(void)a;(void)b;}
static void gpio_init(uint a) {(void)a;}
static void gpio_set_dir(uint a, uint b) {(void)a;(void)b;}
static void gpio_set_drive_strength(uint a, uint b) {(void)a;(void)b;}
static void pio_sm_set_enabled(PIO p,uint s,bool e) {(void)p;(void)s;enabled=e;}
static pio_sm_config inverter_program_get_default_config(uint o) {(void)o;return (pio_sm_config){0};}
static void sm_config_set_set_pins(pio_sm_config*c,uint a,uint b){(void)c;(void)a;assert(b==2);}
static uint32_t clock_get_hz(uint c){(void)c;return 125000000;}
static void sm_config_set_clkdiv(pio_sm_config*c,float d){(void)c;assert(d==125);}
static void pio_sm_init(PIO p,uint s,uint o,const pio_sm_config*c){(void)p;(void)s;(void)o;(void)c;assert(!enabled);words=0;}
static void pio_sm_set_pins_with_mask(PIO p,uint s,uint v,uint m){(void)p;(void)s;(void)m;assert(v==0);}
static void pio_sm_set_consecutive_pindirs(PIO p,uint s,uint a,uint b,bool d){(void)p;(void)s;(void)a;assert(b==2&&d);}
static void pio_sm_put_blocking(PIO p,uint s,uint v){(void)p;(void)s;assert(words<2);fifo[words++]=v;}
static void sleep_us(uint d){delay_us=d;}
static void pio_gpio_init(PIO p,uint pin){(void)p;(void)pin;assert(!enabled);}
static uint pio_claim_unused_sm(PIO p,bool b){(void)p;(void)b;return 0;}
static uint pio_add_program(PIO p,const void*c){(void)p;(void)c;return 0;}
'''
(stub/'sdk.h').write_text(header)
for name in ['hardware/i2c.h','hardware/clocks.h','hardware/gpio.h','hardware/pio.h','pico/stdlib.h','inverter.pio.h']:
    p=stub/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('#include "sdk.h"\n')
harness = stub/'control.c'
harness.write_text('#include "'+(root/'src/waveform_pio.c').as_posix()+'"\n'+'''
int main(void) {
    waveform_init();
    assert(!enabled && !waveform_is_on());
    for (unsigned hz=1; hz<=60; ++hz) {
        waveform_off(); waveform_set_frequency(hz);
        for (unsigned d=1; d<=100; ++d) {
            waveform_set_duty(d);
            assert(!enabled && !waveform_is_on());
            waveform_on();
            assert(enabled && waveform_is_on() && words==2 && delay_us>=200);
            assert(fifo[0]+2==waveform_get_on_us());
            assert(2*(fifo[0]+2+fifo[1]+4)==waveform_get_period_us());
            waveform_set_duty(d); assert(enabled);
            waveform_off(); assert(!enabled);
        }
    }
    waveform_on(); waveform_set_frequency(50); assert(!enabled);
    waveform_on(); waveform_set_duty(25); assert(!enabled);
    return 0;
}
''')
exe=stub/'control.exe'
subprocess.run([os.environ.get('HOST_CC','gcc'),'-std=c11','-Wall','-Wextra','-Werror','-I'+str(stub),str(harness),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
print('PASS: production PIO control, startup, shutdown, and adjustment behavior')
