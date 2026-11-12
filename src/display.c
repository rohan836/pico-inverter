#include "display.h"

#include <string.h>

#include "font.h"
#include "pico/stdlib.h"

static i2c_inst_t *display_i2c;
static uint8_t display_addr;
static bool display_available;
static uint8_t framebuffer[DISPLAY_WIDTH * DISPLAY_HEIGHT / 8];

static bool write_bytes(const uint8_t *data, size_t len)
{
    if (!display_available) {
        return false;
    }
    return i2c_write_timeout_us(display_i2c, display_addr, data, len, false, 20000) == (int)len;
}

static bool write_commands(const uint8_t *commands, size_t count)
{
    uint8_t buffer[32];
    if (count + 1u > sizeof(buffer)) {
        return false;
    }
    buffer[0] = 0x00;
    memcpy(&buffer[1], commands, count);
    return write_bytes(buffer, count + 1u);
}

static bool init_controller(void)
{
#ifdef DISPLAY_SSD1306
    static const uint8_t init_sequence[] = {
        0xAE,
        0xD5, 0x80,
        0xA8, 0x3F,
        0xD3, 0x00,
        0x40,
        0x8D, 0x14, /* charge pump on */
        0x20, 0x02, /* page addressing mode */
        0xA1,
        0xC8,
        0xDA, 0x12,
        0x81, 0xCF,
        0xD9, 0xF1,
        0xDB, 0x40,
        0xA4,
        0xA6,
        0xAF,
    };
#else
    static const uint8_t init_sequence[] = {
        0xAE,       /* display off */
        0xD5, 0x80, /* clock divide */
        0xA8, 0x3F, /* multiplex 64 */
        0xD3, 0x00, /* display offset */
        0x40,       /* start line 0 */
        0xAD, 0x8B, /* charge pump on */
        0xA1,       /* segment remap */
        0xC8,       /* COM scan direction */
        0xDA, 0x12, /* COM pins */
        0x81, 0xCF, /* contrast */
        0xD9, 0xF1, /* pre-charge */
        0xDB, 0x40, /* VCOM */
        0xA4,       /* display follows RAM */
        0xA6,       /* normal (not inverted) */
        0xAF,       /* display on */
    };
#endif

    return write_commands(init_sequence, sizeof(init_sequence));
}

bool display_init(i2c_inst_t *i2c, uint8_t preferred_addr)
{
    display_i2c = i2c;
    display_addr = preferred_addr;
    display_available = true;
    display_clear();
    sleep_ms(50);

    if (init_controller()) {
        return true;
    }

    /* Common alternate address for 128x64 modules. */
    display_addr = (preferred_addr == 0x3C) ? 0x3D : 0x3C;
    if (init_controller()) {
        return true;
    }

    display_available = false;
    return false;
}

void display_clear(void)
{
    memset(framebuffer, 0, sizeof(framebuffer));
}

void display_pixel(int x, int y, bool on)
{
    if (x < 0 || x >= DISPLAY_WIDTH || y < 0 || y >= DISPLAY_HEIGHT) {
        return;
    }

    uint32_t index = (uint32_t)x + ((uint32_t)y / 8u) * DISPLAY_WIDTH;
    uint8_t mask = (uint8_t)(1u << (y & 7));
    if (on) {
        framebuffer[index] |= mask;
    } else {
        framebuffer[index] &= (uint8_t)~mask;
    }
}

void display_fill_rect(int x, int y, int w, int h, bool on)
{
    for (int yy = y; yy < y + h; yy++) {
        for (int xx = x; xx < x + w; xx++) {
            display_pixel(xx, yy, on);
        }
    }
}

void display_rect(int x, int y, int w, int h, bool on)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    display_fill_rect(x, y, w, 1, on);
    display_fill_rect(x, y + h - 1, w, 1, on);
    display_fill_rect(x, y, 1, h, on);
    display_fill_rect(x + w - 1, y, 1, h, on);
}

void display_text(int x, int y, const char *text, uint8_t scale)
{
    display_text_ex(x, y, text, scale, true);
}

void display_text_ex(int x, int y, const char *text, uint8_t scale, bool on)
{
    if (scale < 1) {
        scale = 1;
    }

    while (*text) {
        unsigned char c = (unsigned char)*text++;
        if (c >= 'a' && c <= 'z') {
            c = (unsigned char)(c - 'a' + 'A');
        }
        if (c < 32 || c > 127) {
            c = '?';
        }

        const uint8_t *glyph = font5x7[c - 32];
        for (int col = 0; col < FONT_WIDTH; col++) {
            uint8_t bits = glyph[col];
            for (int row = 0; row < FONT_HEIGHT; row++) {
                if (bits & (1u << row)) {
                    if (scale == 1) {
                        display_pixel(x + col, y + row, on);
                    } else {
                        display_fill_rect(x + col * scale, y + row * scale, scale, scale, on);
                    }
                }
            }
        }
        x += (FONT_WIDTH + 1) * scale;
    }
}

void display_flush(void)
{
    if (!display_available) {
        return;
    }

#ifdef DISPLAY_SSD1306
    const uint8_t column_offset = 0;
#else
    const uint8_t column_offset = 2;
#endif

    for (uint8_t page = 0; page < 8; page++) {
        uint8_t command[4] = {
            0x00,
            (uint8_t)(0xB0u | page),
            (uint8_t)(column_offset & 0x0Fu),
            (uint8_t)(0x10u | ((column_offset >> 4) & 0x0Fu)),
        };
        write_bytes(command, sizeof(command));

        uint8_t data[1 + DISPLAY_WIDTH];
        data[0] = 0x40;
        memcpy(&data[1], &framebuffer[page * DISPLAY_WIDTH], DISPLAY_WIDTH);
        write_bytes(data, sizeof(data));
    }
}
