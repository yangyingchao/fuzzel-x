#pragma once

#include <pixman.h>
#include <cairo.h>

#include <fcft/fcft.h>

#include "shm.h"
#include "match.h"
#include "tllist.h"

struct rgba {double r; double g; double b; double a;};

struct render_options {
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

    /* Filled in by render, for now */
    pixman_color_t pix_background_color;
    pixman_color_t pix_border_color;
    pixman_color_t pix_text_color;
    pixman_color_t pix_match_color;
    pixman_color_t pix_selection_color;
};

struct render;
struct render *render_init(struct fcft_font *font, const struct render_options *options,
                           enum fcft_subpixel subpixel);
void render_destroy(struct render *render);

void render_background(const struct render *render, struct buffer *buf);

void render_prompt(
    const struct render *render, struct buffer *buf,
    const struct prompt *prompt);

void render_match_list(
    const struct render *render, struct buffer *buf,
    const struct prompt *prompt, const struct matches *matches);
