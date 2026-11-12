#include "ir_nec.h"

#include "board.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"
#include "pico/time.h"

#define IR_EVENT_QUEUE 8

typedef enum {
    IR_WAIT_IDLE = 0,
    IR_WAIT_DATA,
} ir_state_t;

static volatile ir_event_t event_queue[IR_EVENT_QUEUE];
static volatile uint8_t event_head;
static volatile uint8_t event_tail;

static volatile uint64_t last_edge_us;
static volatile uint8_t last_level = 1;
static volatile ir_state_t state;
static volatile uint32_t code;
static volatile uint8_t bit_count;
static volatile uint8_t have_mark;
static volatile uint32_t last_code;
static volatile uint8_t have_last_code;

static bool near_us(uint32_t value, uint32_t target, uint32_t percent)
{
    uint32_t tolerance = (target * percent) / 100u;
    return value >= (target - tolerance) && value <= (target + tolerance);
}

static void push_event(uint32_t value, bool repeat)
{
    uint8_t next = (uint8_t)((event_head + 1u) % IR_EVENT_QUEUE);
    if (next == event_tail) {
        event_tail = (uint8_t)((event_tail + 1u) % IR_EVENT_QUEUE);
    }

    event_queue[event_head].code = value;
    event_queue[event_head].repeat = repeat;
    event_queue[event_head].time_us = time_us_64();
    event_head = next;
}

static void __not_in_flash_func(ir_gpio_cb)(uint gpio, uint32_t events)
{
    (void)gpio;
    gpio_acknowledge_irq(PIN_IR, events);

    uint64_t now = time_us_64();
    uint32_t duration = (uint32_t)(now - last_edge_us);
    uint8_t previous_level = last_level;
    uint8_t new_level = (uint8_t)gpio_get(PIN_IR);

    last_edge_us = now;
    last_level = new_level;

    if (previous_level == 0) {
        /* A low burst just ended (the TSOP output is active low). */
        if (near_us(duration, 9000, 25)) {
            state = IR_WAIT_DATA;
            code = 0;
            bit_count = 0;
            have_mark = 0;
        } else if (near_us(duration, 4500, 25)) {
            /* Samsung-style lead mark. */
            state = IR_WAIT_DATA;
            code = 0;
            bit_count = 0;
            have_mark = 0;
        } else if (near_us(duration, 560, 30)) {
            if (state == IR_WAIT_DATA) {
                have_mark = 1;
            }
        } else {
            state = IR_WAIT_IDLE;
            have_mark = 0;
        }
    } else {
        /* A high space just ended. */
        if (state == IR_WAIT_DATA) {
            if (bit_count == 0 && !have_mark) {
                if (near_us(duration, 4500, 25)) {
                    /* Normal NEC start space. */
                    return;
                }
                if (near_us(duration, 2250, 25)) {
                    /* NEC repeat frame. */
                    if (have_last_code) {
                        push_event(last_code, true);
                    }
                    state = IR_WAIT_IDLE;
                    return;
                }
                state = IR_WAIT_IDLE;
                return;
            }

            if (have_mark) {
                if (near_us(duration, 560, 30)) {
                    /* bit 0 */
                    have_mark = 0;
                    bit_count++;
                } else if (near_us(duration, 1690, 25)) {
                    /* bit 1 */
                    code |= (1u << bit_count);
                    have_mark = 0;
                    bit_count++;
                } else {
                    state = IR_WAIT_IDLE;
                    have_mark = 0;
                    return;
                }

                if (bit_count >= 32) {
                    last_code = code;
                    have_last_code = 1;
                    push_event(code, false);
                    state = IR_WAIT_IDLE;
                }
            } else if (duration > 20000u) {
                state = IR_WAIT_IDLE;
            }
        }
    }
}

void ir_nec_init(void)
{
    gpio_init(PIN_IR);
    gpio_set_dir(PIN_IR, GPIO_IN);
    gpio_pull_up(PIN_IR);

    last_edge_us = time_us_64();
    last_level = (uint8_t)gpio_get(PIN_IR);
    state = IR_WAIT_IDLE;
    event_head = 0;
    event_tail = 0;

    gpio_set_irq_enabled_with_callback(
        PIN_IR,
        GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE,
        true,
        ir_gpio_cb);

    /* Waveform alarm priority is higher; IR can wait a few microseconds. */
    irq_set_priority(IO_IRQ_BANK0, 0x80);
}

bool ir_nec_poll(ir_event_t *event)
{
    bool available = false;

    uint32_t ints = save_and_disable_interrupts();
    if (event_tail != event_head) {
        *event = event_queue[event_tail];
        event_tail = (uint8_t)((event_tail + 1u) % IR_EVENT_QUEUE);
        available = true;
    }
    restore_interrupts(ints);

    return available;
}
