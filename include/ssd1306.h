#ifndef SSD1306_H
#define SSD1306_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qnx_i2c.h"

typedef struct {
    healink_i2c_t *i2c;
    uint8_t address;
    uint8_t framebuffer[1024];
    bool ready;
} ssd1306_t;

int ssd1306_init(ssd1306_t *display,
                 healink_i2c_t *i2c,
                 uint8_t address);
void ssd1306_clear(ssd1306_t *display);
void ssd1306_display_off(ssd1306_t *display);
int ssd1306_flush(ssd1306_t *display);
void ssd1306_draw_text(ssd1306_t *display, int text_row, const char *text);
void ssd1306_draw_pixel(ssd1306_t *display, int x, int y, bool on);
void ssd1306_draw_line(ssd1306_t *display, int x_start, int y_start, int x_end, int y_end, bool on);
void ssd1306_draw_rect(ssd1306_t *display, int x, int y, int width, int height, bool on);
void ssd1306_fill_rect(ssd1306_t *display, int x, int y, int width, int height, bool on);
void ssd1306_draw_text_scaled(ssd1306_t *display,
                              int x,
                              int y,
                              const char *text,
                              unsigned text_scale,
                              bool on);
void ssd1306_draw_text_centered_scaled(ssd1306_t *display,
                                       int y,
                                       const char *text,
                                       unsigned text_scale,
                                       bool on);

#endif
