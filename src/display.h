#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

#include "hardware/i2c.h"

#define DISPLAY_WIDTH  128
#define DISPLAY_HEIGHT 64

bool display_init(i2c_inst_t *i2c, uint8_t preferred_addr);
void display_clear(void);
void display_pixel(int x, int y, bool on);
void display_fill_rect(int x, int y, int w, int h, bool on);
void display_rect(int x, int y, int w, int h, bool on);
void display_text(int x, int y, const char *text, uint8_t scale);
void display_text_ex(int x, int y, const char *text, uint8_t scale, bool on);
void display_flush(void);

#endif
