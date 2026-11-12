#ifndef CONFIG_FLASH_H
#define CONFIG_FLASH_H

#include <stdbool.h>
#include <stdint.h>

#define IR_FUNC_COUNT 7

typedef enum {
    IR_FUNC_FREQ_UP = 0,
    IR_FUNC_FREQ_DOWN,
    IR_FUNC_DUTY_UP,
    IR_FUNC_DUTY_DOWN,
    IR_FUNC_PWM_ON,
    IR_FUNC_PWM_OFF,
    /* Re-runs the whole learning wizard, like a factory reset of the remote. */
    IR_FUNC_RESET,
} ir_func_t;

/*
 * Stored in the last two flash sectors. Two slots are used so that a power
 * loss during a save cannot destroy the only valid configuration.
 */
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t seq;
    uint16_t freq_hz;
    uint16_t duty_pct;
    uint16_t dead_us;
    uint8_t configured;
    uint8_t reserved0;
    uint32_t ir_codes[IR_FUNC_COUNT];
    uint8_t ir_protocol[IR_FUNC_COUNT];
    uint8_t reserved[18];
    uint32_t crc;
} inverter_config_t;

void config_load_defaults(inverter_config_t *cfg);

/* Returns true only if a valid stored configuration was found. */
bool config_load(inverter_config_t *cfg);

/* Must only be called while the inverter output is OFF. */
bool config_save(inverter_config_t *cfg);

const char *config_ir_func_name(ir_func_t func);

#endif
