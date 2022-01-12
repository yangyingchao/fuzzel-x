#pragma once

#include <threads.h>
#include <pixman.h>

#include <fcft/fcft.h>

#include "shm.h"
#include "match.h"
#include "tllist.h"

struct rgba {double r; double g; double b; double a;};
struct pt_or_px {int px; float pt;};

struct render_options {
    unsigned lines;
    unsigned chars;
    unsigned border_size;
    unsigned border_radius;
    struct {
        unsigned x;
        unsigned y;
        unsigned inner;
    } pad;

    struct rgba background_color;
    struct rgba border_color;
    struct rgba text_color;
    struct rgba match_color;
    struct rgba selection_color;
    struct rgba selection_text_color;

    struct pt_or_px line_height;
    struct pt_or_px letter_spacing;

    /* Filled in by render, for now */
    pixman_color_t pix_background_color;
    pixman_color_t pix_border_color;
    pixman_color_t pix_text_color;
    pixman_color_t pix_match_color;
    pixman_color_t pix_selection_color;
    pixman_color_t pix_selection_text_color;
};

struct render;
struct render *render_init(
    const struct render_options *options, mtx_t *icon_lock);
void render_destroy(struct render *render);

void render_set_subpixel(struct render *render, enum fcft_subpixel subpixel);
bool render_set_font(struct render *render, struct fcft_font *font,
                     int scale, float dpi, bool size_font_by_dpi,
                     int *new_width, int *new_height);

void render_background(const struct render *render, struct buffer *buf);

void render_prompt(
    const struct render *render, struct buffer *buf,
    const struct prompt *prompt);

void render_match_list(
    const struct render *render, struct buffer *buf,
    const struct prompt *prompt, const struct matches *matches);
