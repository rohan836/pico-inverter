#include "config_flash.h"

#include <stddef.h>
#include <string.h>

#include "board.h"
#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

#define CONFIG_MAGIC       0x31564E49u /* "INV1" */
/* Bump whenever the layout or the meaning of stored fields changes.  A version
 * mismatch falls back to defaults, which also triggers the IR setup wizard. */
#define CONFIG_VERSION     2u

#define CONFIG_SLOT_SIZE   FLASH_SECTOR_SIZE
#define CONFIG_SLOT0       (PICO_FLASH_SIZE_BYTES - (2u * CONFIG_SLOT_SIZE))
#define CONFIG_SLOT1       (PICO_FLASH_SIZE_BYTES - CONFIG_SLOT_SIZE)

static uint32_t crc32_calc(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;

    while (len--) {
        crc ^= *p++;
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
        }
    }
    return ~crc;
}

static bool config_valid(const inverter_config_t *cfg)
{
    if (cfg->magic != CONFIG_MAGIC) {
        return false;
    }
    if (cfg->version != CONFIG_VERSION) {
        return false;
    }
    if (cfg->size != sizeof(inverter_config_t)) {
        return false;
    }
    return cfg->crc == crc32_calc(cfg, offsetof(inverter_config_t, crc));
}

static const inverter_config_t *slot_ptr(uint32_t offset)
{
    return (const inverter_config_t *)(XIP_BASE + offset);
}

void config_load_defaults(inverter_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->magic = CONFIG_MAGIC;
    cfg->version = CONFIG_VERSION;
    cfg->size = sizeof(inverter_config_t);
    cfg->seq = 0;
    cfg->freq_hz = PWM_DEFAULT_HZ;
    cfg->duty_pct = PWM_DEFAULT_DUTY;
    cfg->dead_us = PWM_DEAD_TIME_US;
    cfg->configured = 0;
}

bool config_load(inverter_config_t *cfg)
{
    const inverter_config_t *a = slot_ptr(CONFIG_SLOT0);
    const inverter_config_t *b = slot_ptr(CONFIG_SLOT1);
    bool va = config_valid(a);
    bool vb = config_valid(b);

    if (!va && !vb) {
        config_load_defaults(cfg);
        return false;
    }

    if (vb && (!va || b->seq > a->seq)) {
        *cfg = *b;
    } else {
        *cfg = *a;
    }
    return true;
}

bool config_save(inverter_config_t *cfg)
{
    inverter_config_t next = *cfg;
    const inverter_config_t *a = slot_ptr(CONFIG_SLOT0);
    const inverter_config_t *b = slot_ptr(CONFIG_SLOT1);
    bool valid_a = config_valid(a);
    bool valid_b = config_valid(b);
    uint32_t max_seq = cfg->seq;
    uint32_t target = CONFIG_SLOT0;

    if (valid_a && valid_b) {
        if (b->seq > a->seq) {
            max_seq = (b->seq > max_seq) ? b->seq : max_seq;
            target = CONFIG_SLOT0;
        } else {
            max_seq = (a->seq > max_seq) ? a->seq : max_seq;
            target = CONFIG_SLOT1;
        }
    } else if (valid_a) {
        max_seq = (a->seq > max_seq) ? a->seq : max_seq;
        target = CONFIG_SLOT1;
    } else if (valid_b) {
        max_seq = (b->seq > max_seq) ? b->seq : max_seq;
        target = CONFIG_SLOT0;
    }

    next.magic = CONFIG_MAGIC;
    next.version = CONFIG_VERSION;
    next.size = sizeof(inverter_config_t);
    next.seq = max_seq + 1u;
    next.crc = 0;
    next.crc = crc32_calc(&next, offsetof(inverter_config_t, crc));

    uint32_t offset = target;
    uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xFF, sizeof(page));
    memcpy(page, &next, sizeof(next));

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(offset, CONFIG_SLOT_SIZE);
    flash_range_program(offset, page, sizeof(page));
    restore_interrupts(ints);

    if (!config_valid(slot_ptr(offset))) {
        return false;
    }

    *cfg = next;
    return true;
}

const char *config_ir_func_name(ir_func_t func)
{
    switch (func) {
    case IR_FUNC_FREQ_UP:   return "FRQ+";
    case IR_FUNC_FREQ_DOWN: return "FRQ-";
    case IR_FUNC_DUTY_UP:   return "DUTY+";
    case IR_FUNC_DUTY_DOWN: return "DUTY-";
    case IR_FUNC_PWM_ON:    return "PWM ON";
    case IR_FUNC_PWM_OFF:   return "PWM OFF";
    case IR_FUNC_RESET:     return "RESET ALL";
    default:                return "?";
    }
}
