#ifndef IR_NEC_H
#define IR_NEC_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t code;
    bool repeat;
    uint64_t time_us;
} ir_event_t;

void ir_nec_init(void);
bool ir_nec_poll(ir_event_t *event);

#endif
