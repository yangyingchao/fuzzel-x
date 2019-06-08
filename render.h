#pragma once

#include <cairo.h>

#include "shm.h"
#include "match.h"
#include "tllist.h"

struct rgba {double r; double g; double b; double a;};

struct options {
    int width;
    int height;
    int x_margin;
    int y_margin;
    int border_size;
    int border_radius;

    struct rgba background_color;
    struct rgba border_color;
    struct rgba text_color;
    struct rgba match_color;
    struct rgba selection_color;
};

struct render;
struct render *render_init(cairo_scaled_font_t *font, struct options options);
void render_destroy(struct render *render);

void render_background(const struct render *render, struct buffer *buf);

void render_prompt(
    const struct render *render, struct buffer *buf,
    const struct prompt *prompt);

void render_match_list(const struct render *render, struct buffer *buf,
                       const struct match matches[], size_t match_count,
                       size_t match_length, size_t selected);
