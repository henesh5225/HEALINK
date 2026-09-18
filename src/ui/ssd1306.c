#define _POSIX_C_SOURCE 200809L
#include "ssd1306.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define SSD1306_WIDTH 128U
#define SSD1306_HEIGHT 64U

/* Compact 5x7 ASCII font for the jury-facing OLED UI.
 * Supported characters: space, punctuation used by HEALINK, digits,
 * uppercase letters. Unknown characters render as blank.
 */
typedef struct {
    char character;
    uint8_t glyph[5];
} font_glyph_t;

static const font_glyph_t font_ascii[] = {
    {' ', {0x00,0x00,0x00,0x00,0x00}},
    {'.', {0x00,0x00,0x00,0x60,0x60}},
    {':', {0x00,0x36,0x36,0x00,0x00}},
    {'-', {0x08,0x08,0x08,0x08,0x08}},
    {'/', {0x20,0x10,0x08,0x04,0x02}},
    {'%', {0x63,0x13,0x08,0x64,0x63}},
    {'0', {0x3E,0x51,0x49,0x45,0x3E}},
    {'1', {0x00,0x42,0x7F,0x40,0x00}},
    {'2', {0x42,0x61,0x51,0x49,0x46}},
    {'3', {0x21,0x41,0x45,0x4B,0x31}},
    {'4', {0x18,0x14,0x12,0x7F,0x10}},
    {'5', {0x27,0x45,0x45,0x45,0x39}},
    {'6', {0x3C,0x4A,0x49,0x49,0x30}},
    {'7', {0x01,0x71,0x09,0x05,0x03}},
    {'8', {0x36,0x49,0x49,0x49,0x36}},
    {'9', {0x06,0x49,0x49,0x29,0x1E}},
    {'A', {0x7E,0x11,0x11,0x11,0x7E}},
    {'B', {0x7F,0x49,0x49,0x49,0x36}},
    {'C', {0x3E,0x41,0x41,0x41,0x22}},
    {'D', {0x7F,0x41,0x41,0x22,0x1C}},
    {'E', {0x7F,0x49,0x49,0x49,0x41}},
    {'F', {0x7F,0x09,0x09,0x09,0x01}},
    {'G', {0x3E,0x41,0x49,0x49,0x7A}},
    {'H', {0x7F,0x08,0x08,0x08,0x7F}},
    {'I', {0x00,0x41,0x7F,0x41,0x00}},
    {'J', {0x20,0x40,0x41,0x3F,0x01}},
    {'K', {0x7F,0x08,0x14,0x22,0x41}},
    {'L', {0x7F,0x40,0x40,0x40,0x40}},
    {'M', {0x7F,0x02,0x0C,0x02,0x7F}},
    {'N', {0x7F,0x04,0x08,0x10,0x7F}},
    {'O', {0x3E,0x41,0x41,0x41,0x3E}},
    {'P', {0x7F,0x09,0x09,0x09,0x06}},
    {'Q', {0x3E,0x41,0x51,0x21,0x5E}},
    {'R', {0x7F,0x09,0x19,0x29,0x46}},
    {'S', {0x46,0x49,0x49,0x49,0x31}},
    {'T', {0x01,0x01,0x7F,0x01,0x01}},
    {'U', {0x3F,0x40,0x40,0x40,0x3F}},
    {'V', {0x1F,0x20,0x40,0x20,0x1F}},
    {'W', {0x7F,0x20,0x18,0x20,0x7F}},
    {'X', {0x63,0x14,0x08,0x14,0x63}},
    {'Y', {0x07,0x08,0x70,0x08,0x07}},
    {'Z', {0x61,0x51,0x49,0x45,0x43}}
};

static const uint8_t *lookup_glyph(char character)
{
    size_t i;

    for (i = 0U; i < sizeof(font_ascii) / sizeof(font_ascii[0]); ++i) {
        if (font_ascii[i].character == character) {
            return font_ascii[i].glyph;
        }
    }

    return font_ascii[0].glyph;
}

static int set_page_col(ssd1306_t *display, uint8_t page, uint8_t text_column)
{
    const uint8_t c0 = (uint8_t)(text_column & 0x0Fu);
    const uint8_t c1 = (uint8_t)(0x10u | ((text_column >> 4U) & 0x0Fu));
    const uint8_t cmds[] = {
        0x00u,
        (uint8_t)(0xB0u | (page & 0x07u)),
        c0,
        c1
    };

    return healink_i2c_write(display->i2c, display->address, cmds, sizeof(cmds));
}

int ssd1306_init(ssd1306_t *display,
                 healink_i2c_t *i2c,
                 uint8_t address)
{
    static const uint8_t init1[] = {
        0x00u, 0xAEu, 0xD5u, 0x80u, 0xA8u, 0x3Fu, 0xD3u, 0x00u,
        0x40u, 0x8Du, 0x14u, 0x20u, 0x00u, 0xA1u, 0xC8u, 0xDAu,
        0x12u, 0x81u, 0x7Fu, 0xD9u, 0xF1u, 0xDBu, 0x40u, 0xA4u,
        0xA6u, 0xAFu
    };

    if (display == NULL || i2c == NULL || i2c->file_descriptor < 0) {
        errno = EINVAL;
        return -1;
    }

    memset(display, 0, sizeof(*display));
    display->i2c = i2c;
    display->address = address;

    if (healink_i2c_write(i2c, address, init1, sizeof(init1)) != 0) {
        return -1;
    }

    display->ready = true;
    ssd1306_clear(display);
    return ssd1306_flush(display);
}

void ssd1306_clear(ssd1306_t *display)
{
    if (display != NULL) {
        memset(display->framebuffer, 0, sizeof(display->framebuffer));
    }
}

void ssd1306_display_off(ssd1306_t *display)
{
    const uint8_t cmd[] = {0x00u, 0xAEu};

    if (display != NULL && display->i2c != NULL && display->i2c->file_descriptor >= 0) {
        (void)healink_i2c_write(display->i2c, display->address, cmd, sizeof(cmd));
    }
}

static void draw_char(ssd1306_t *display, int text_row, int text_column, char character)
{
    size_t offset;
    const uint8_t *glyph;

    if (display == NULL || text_row < 0 || text_row >= 8 || text_column < 0 || text_column >= 21) {
        return;
    }

    offset = (size_t)text_row * SSD1306_WIDTH + (size_t)text_column * 6U;
    if (offset + 5U >= sizeof(display->framebuffer)) {
        return;
    }

    glyph = lookup_glyph(character);
    memcpy(&display->framebuffer[offset], glyph, 5U);
    display->framebuffer[offset + 5U] = 0U;
}

void ssd1306_draw_text(ssd1306_t *display, int text_row, const char *text)
{
    int text_column = 0;

    if (display == NULL || text == NULL) {
        return;
    }

    while (*text != '\0' && text_column < 21) {
        draw_char(display, text_row, text_column, *text);
        ++text_column;
        ++text;
    }
}


/* FRAMEBUFFER DRAWING PRIMITIVES */

static void framebuffer_pixel(ssd1306_t *display, int x, int y, bool on)
{
    size_t index;
    uint8_t mask;

    if (display == NULL ||
        x < 0 || x >= (int)SSD1306_WIDTH ||
        y < 0 || y >= (int)SSD1306_HEIGHT) {
        return;
    }

    index = (size_t)(y / 8) * SSD1306_WIDTH + (size_t)x;
    mask = (uint8_t)(1u << (unsigned)(y & 7));

    if (on) {
        display->framebuffer[index] |= mask;
    } else {
        display->framebuffer[index] &= (uint8_t)~mask;
    }
}

void ssd1306_draw_pixel(ssd1306_t *display, int x, int y, bool on)
{
    framebuffer_pixel(display, x, y, on);
}

void ssd1306_draw_line(ssd1306_t *display,
                       int x0, int y0,
                       int x1, int y1,
                       bool on)
{
    int dx;
    int sx;
    int dy;
    int sy;
    int err;
    int e2;

    if (display == NULL) {
        return;
    }

    dx = abs(x1 - x0);
    sx = (x0 < x1) ? 1 : -1;
    dy = -abs(y1 - y0);
    sy = (y0 < y1) ? 1 : -1;
    err = dx + dy;

    for (;;) {
        framebuffer_pixel(display, x0, y0, on);

        if (x0 == x1 && y0 == y1) {
            break;
        }

        e2 = 2 * err;

        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }

        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void ssd1306_draw_rect(ssd1306_t *display,
                       int x, int y,
                       int width, int height,
                       bool on)
{
    if (display == NULL || width <= 0 || height <= 0) {
        return;
    }

    ssd1306_draw_line(display, x, y, x + width - 1, y, on);
    ssd1306_draw_line(display, x, y + height - 1, x + width - 1, y + height - 1, on);
    ssd1306_draw_line(display, x, y, x, y + height - 1, on);
    ssd1306_draw_line(display, x + width - 1, y, x + width - 1, y + height - 1, on);
}

void ssd1306_fill_rect(ssd1306_t *display,
                       int x, int y,
                       int width, int height,
                       bool on)
{
    int yy;
    int xx;

    if (display == NULL || width <= 0 || height <= 0) {
        return;
    }

    for (yy = y; yy < y + height; ++yy) {
        for (xx = x; xx < x + width; ++xx) {
            framebuffer_pixel(display, xx, yy, on);
        }
    }
}

void ssd1306_draw_text_scaled(ssd1306_t *display,
                              int x,
                              int y,
                              const char *text,
                              unsigned text_scale,
                              bool on)
{
    int cursor_x;

    if (display == NULL || text == NULL || text_scale == 0U) {
        return;
    }

    cursor_x = x;

    while (*text != '\0') {
        const uint8_t *glyph = lookup_glyph(*text);
        unsigned text_column;
        unsigned text_row;

        for (text_column = 0U; text_column < 5U; ++text_column) {
            for (text_row = 0U; text_row < 7U; ++text_row) {
                bool set = ((glyph[text_column] >> text_row) & 1U) != 0U;

                if (set) {
                    unsigned sy;
                    unsigned sx;

                    for (sy = 0U; sy < text_scale; ++sy) {
                        for (sx = 0U; sx < text_scale; ++sx) {
                            framebuffer_pixel(
                                display,
                                cursor_x + (int)(text_column * text_scale + sx),
                                y + (int)(text_row * text_scale + sy),
                                on);
                        }
                    }
                }
            }
        }

        cursor_x += (int)(6U * text_scale);
        ++text;
    }
}

void ssd1306_draw_text_centered_scaled(ssd1306_t *display,
                                       int y,
                                       const char *text,
                                       unsigned text_scale,
                                       bool on)
{
    size_t len;
    int width;
    int x;

    if (display == NULL || text == NULL || text_scale == 0U) {
        return;
    }

    len = strlen(text);
    if (len == 0U) {
        return;
    }

    width = (int)(len * 6U * text_scale - text_scale);
    x = ((int)SSD1306_WIDTH - width) / 2;

    ssd1306_draw_text_scaled(display, x, y, text, text_scale, on);
}


int ssd1306_flush(ssd1306_t *display)
{
    unsigned page;

    if (display == NULL || !display->ready || display->i2c == NULL || display->i2c->file_descriptor < 0) {
        errno = EINVAL;
        return -1;
    }

    for (page = 0U; page < SSD1306_HEIGHT / 8U; ++page) {
        uint8_t tx[129];

        if (set_page_col(display, (uint8_t)page, 0U) != 0) {
            return -1;
        }

        tx[0] = 0x40u;
        memcpy(&tx[1],
               &display->framebuffer[page * SSD1306_WIDTH],
               SSD1306_WIDTH);

        if (healink_i2c_write(display->i2c,
                              display->address,
                              tx,
                              sizeof(tx)) != 0) {
            return -1;
        }
    }

    return 0;
}
