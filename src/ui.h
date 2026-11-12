#ifndef UI_H
#define UI_H

#include "config_flash.h"
#include "ir_nec.h"

void ui_init(inverter_config_t *cfg);
void ui_handle_ir(const ir_event_t *event);
void ui_task(void);

#endif
