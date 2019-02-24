#pragma once

#include <cairo.h>

#include "shm.h"
#include "match.h"
#include "tllist.h"

struct prompt {
    char *text;
    size_t cursor;
};

struct render;
struct render *render_init(const char *font_name);
void render_destroy(struct render *render);

void render_prompt(
    const struct render *render, struct buffer *buf,
    const struct prompt *prompt);

void render_match_list(const struct render *render, struct buffer *buf,
                       const struct match matches[], size_t match_count,
                       size_t selected);
