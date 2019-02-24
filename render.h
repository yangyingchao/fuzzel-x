#pragma once

#include <cairo/cairo.h>

#include "shm.h"
#include "match.h"
#include "tllist.h"

struct prompt {
    char *text;
    size_t cursor;
};

void render_prompt(struct buffer *buf,
                   cairo_scaled_font_t *scaled_font, double font_size,
                   const struct prompt *prompt);

void render_match_list(struct buffer *buf,
                       cairo_scaled_font_t *scaled_font, double font_size,
                       const struct match matches[], size_t match_count,
                       size_t selected);
