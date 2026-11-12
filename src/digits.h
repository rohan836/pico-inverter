#ifndef DIGITS_H
#define DIGITS_H

#include <stdbool.h>
#include <stdint.h>

/* One numeral rasterised from a bold sans typeface, 8-bit coverage per pixel. */
typedef struct {
    uint8_t width;
    uint8_t height;
    const uint8_t *coverage;   /* width * height bytes, 0 = empty .. 255 = ink */
} digit_glyph_t;

typedef struct {
    const digit_glyph_t *glyphs;   /* 10 entries, '0'..'9' */
    uint8_t tracking;              /* blank columns between numerals */
} digit_font_t;

extern const digit_font_t digit_font_hero;    /* ~24 px cap height */
extern const digit_font_t digit_font_mid;     /* ~18 px cap height */
extern const digit_font_t digit_font_small;   /* ~14 px cap height */

/* Ink width of a numeric string, tracking included, trailing gap excluded. */
int digits_width(const char *text, const digit_font_t *font);

/* Draw a numeric string; non-digit characters are skipped. */
void digits_draw(int x, int y, const char *text, const digit_font_t *font, bool on);

#endif
