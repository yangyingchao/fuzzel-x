#pragma once

#include <threads.h>
#include <pixman.h>

#include <fcft/fcft.h>

#include "config.h"
#include "match.h"
#include "shm.h"
#include "tllist.h"

struct render;
struct render *render_init(const struct config *conf, mtx_t *icon_lock);
void render_destroy(struct render *render);

void render_set_subpixel(struct render *render, enum fcft_subpixel subpixel);
bool render_set_font_and_update_sizes(
    struct render *render, struct fcft_font *font,
    float scale, float dpi, bool size_font_by_dpi,
    int *new_width, int *new_height);

void render_background(const struct render *render, struct buffer *buf);

void render_prompt(
    const struct render *render, struct buffer *buf,
    const struct prompt *prompt, const struct matches *matches);

void render_match_list(
    struct render *render, struct buffer *buf,
    const struct prompt *prompt, const struct matches *matches);

int render_icon_size(const struct render *render);

size_t render_get_row_num(
        const struct render *render, int y,
        const struct matches *matches);
