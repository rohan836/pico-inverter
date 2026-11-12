#include "digits.h"

#include <stddef.h>

#include "display.h"

/* 4x4 ordered Bayer matrix for the anti-aliased edge pixels. */
static const uint8_t bayer4[4][4] = {
    { 0,  8,  2, 10},
    {12,  4, 14,  6},
    { 3, 11,  1,  9},
    {15,  7, 13,  5},
};

static const digit_glyph_t *digit_for(char c, const digit_font_t *font)
{
    if (c < '0' || c > '9') {
        return NULL;
    }
    return &font->glyphs[c - '0'];
}

int digits_width(const char *text, const digit_font_t *font)
{
    int width = 0;
    int count = 0;

    for (const char *p = text; *p != '\0'; p++) {
        const digit_glyph_t *glyph = digit_for(*p, font);
        if (glyph == NULL) {
            continue;
        }
        width += glyph->width;
        count++;
    }
    if (count > 1) {
        width += (int)font->tracking * (count - 1);
    }
    return width;
}

void digits_draw(int x, int y, const char *text, const digit_font_t *font, bool on)
{
    for (const char *p = text; *p != '\0'; p++) {
        const digit_glyph_t *glyph = digit_for(*p, font);
        if (glyph == NULL) {
            continue;
        }

        for (int py = 0; py < (int)glyph->height; py++) {
            const uint8_t *row = &glyph->coverage[py * glyph->width];
            int sy = y + py;
            for (int px = 0; px < (int)glyph->width; px++) {
                uint8_t cov = row[px];
                if (cov == 0) {
                    continue;
                }
                int sx = x + px;
                if (cov >= 224 || bayer4[sy & 3][sx & 3] < (int)(cov >> 4)) {
                    display_pixel(sx, sy, on);
                }
            }
        }

        x += (int)glyph->width + (int)font->tracking;
    }
}
