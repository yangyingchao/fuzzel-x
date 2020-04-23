#include "render.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <fcft/fcft.h>

#define LOG_MODULE "render"
#include "log.h"
#include "wayland.h"

struct render {
    struct render_options options;
    struct fcft_font *regular_font;
    enum fcft_subpixel subpixel;
};

void
render_background(const struct render *render, struct buffer *buf)
{
    /*
     * Lines in cairo are *between* pixels.
     *
     * To get a sharp 1px line, we need to draw it with
     * line-width=2.
     *
     * Thus, we need to draw the path offset:ed with half that
     * (=actual border width).
     */
    const double b = render->options.border_size;
    const double w = render->options.width - 2 * b;
    const double h = render->options.height - 2 * b;

    if (render->options.border_radius == 0) {
        cairo_rectangle(buf->cairo, b, b, w, h);
    } else {
        const double from_degree = M_PI / 180;
        const double radius = render->options.border_radius;


        /* Path describing an arc:ed rectangle */
        cairo_arc(buf->cairo, b + w - radius, b + h - radius, radius,
                  0.0 * from_degree, 90.0 * from_degree);
        cairo_arc(buf->cairo, b + radius, b + h - radius, radius,
                  90.0 * from_degree, 180.0 * from_degree);
        cairo_arc(buf->cairo, b + radius, b + radius, radius,
                  180.0 * from_degree, 270.0 * from_degree);
        cairo_arc(buf->cairo, b + w - radius, b + radius, radius,
                  270.0 * from_degree, 360.0 * from_degree);
        cairo_close_path(buf->cairo);
    }

    /* Border */
    const struct rgba *bc = &render->options.border_color;
    cairo_set_operator(buf->cairo, CAIRO_OPERATOR_SOURCE);
    cairo_set_line_width(buf->cairo, 2 * b);
    cairo_set_source_rgba(buf->cairo, bc->r, bc->g, bc->b, bc->a);
    cairo_stroke_preserve(buf->cairo);

    /* Background */
    const struct rgba *bg = &render->options.background_color;
    cairo_set_source_rgba(buf->cairo, bg->r, bg->g, bg->b, bg->a);
    cairo_fill(buf->cairo);
}

static pixman_color_t
rgba2pixman(struct rgba rgba)
{
    if (rgba.a == 0.)
        return (pixman_color_t){0, 0, 0, 0};

    uint16_t r = rgba.r * 65535.0;
    uint16_t g = rgba.g * 65535.0;
    uint16_t b = rgba.b * 65535.0;
    uint16_t a = rgba.a * 65535.0;

    uint16_t a_div = 0xffff / a;

    return (pixman_color_t){
        .red = r / a_div,
        .green = g / a_div,
        .blue = b / a_div,
        .alpha = a,
    };
}

static void
render_glyph(pixman_image_t *pix, const struct fcft_glyph *glyph, int x, int y, const pixman_color_t *color)
{
    if (pixman_image_get_format(glyph->pix) == PIXMAN_a8r8g8b8) {
        /* Glyph surface is a pre-rendered image (typically a color emoji...) */
        pixman_image_composite32(
            PIXMAN_OP_OVER, glyph->pix, NULL, pix, 0, 0, 0, 0,
            x + glyph->x, y - glyph->y,
            glyph->width, glyph->height);
    } else {
        /* Glyph surface is an alpha mask */
        pixman_image_t *src = pixman_image_create_solid_fill(color);
        pixman_image_composite32(
            PIXMAN_OP_OVER, src, glyph->pix, pix, 0, 0, 0, 0,
            x + glyph->x, y - glyph->y,
            glyph->width, glyph->height);
        pixman_image_unref(src);
    }
}

void
render_prompt(const struct render *render, struct buffer *buf,
              const struct prompt *prompt)
{
    struct fcft_font *font = render->regular_font;

    const wchar_t *pprompt = prompt_prompt(prompt);
    const size_t prompt_len = wcslen(pprompt);

    const wchar_t *ptext = prompt_text(prompt);
    const size_t text_len = wcslen(ptext);

    const bool subpixel_antialias =
        render->options.background_color.a == 1. &&
        render->options.selection_color.a == 1.;

    int x = render->options.border_size + render->options.x_margin;
    int y = render->options.border_size + render->options.y_margin + font->ascent;

    wchar_t prev = 0;

    for (size_t i = 0; i < prompt_len + text_len; i++) {
        wchar_t wc = i < prompt_len ? pprompt[i] : ptext[i - prompt_len];
        const struct fcft_glyph *glyph = fcft_glyph_for_wc(font, wc, subpixel_antialias);
        if (glyph == NULL) {
            prev = wc;
            continue;
        }

        long x_kern;
        fcft_kerning(font, prev, wc, &x_kern, NULL);

        x += x_kern;
        render_glyph(buf->pix, glyph, x, y, &render->options.pix_text_color);
        x += glyph->advance.x;

        /* Cursor */
        if (prompt_cursor(prompt) + prompt_len - 1 == i) {
            pixman_image_fill_rectangles(
                PIXMAN_OP_SRC, buf->pix, &render->options.pix_text_color,
                1, &(pixman_rectangle16_t){
                    x, y - font->ascent,
                    font->underline.thickness, font->ascent + font->descent});
        }

        prev = wc;
    }
}

static void
render_match_text(struct buffer *buf, double *_x, double _y,
                  const wchar_t *text, ssize_t start, size_t length,
                  struct fcft_font *font, bool subpixel_antialias,
                  pixman_color_t regular_color, pixman_color_t match_color)
{
    int x = *_x;
    int y = _y;

    for (size_t i = 0; i < wcslen(text); i++) {
        const struct fcft_glyph *glyph = fcft_glyph_for_wc(font, text[i], subpixel_antialias);
        if (glyph == NULL)
            continue;

        long x_kern = 0;
        if (i > 0)
            fcft_kerning(font, text[i - 1], text[i], &x_kern, NULL);

        bool is_match = start >= 0 && i >= start && i < start + length;
        x += x_kern;
        render_glyph(buf->pix, glyph, x, y, is_match ? &match_color : &regular_color);
        x += glyph->advance.x;
    }

    *_x = x;
}

void
render_match_list(const struct render *render, struct buffer *buf,
                  const struct prompt *prompt, const struct matches *matches)
{
    struct fcft_font *font = render->regular_font;
    const double x_margin = render->options.x_margin;
    const double y_margin = render->options.y_margin;
    const double border_size = render->options.border_size;
    const size_t match_count = matches_get_count(matches);
    const size_t selected = matches_get_match_index(matches);
    const enum fcft_subpixel subpixel =
        (render->options.background_color.a == 1. &&
         render->options.selection_color.a == 1.)
        ? render->subpixel : FCFT_SUBPIXEL_NONE;

    assert(match_count == 0 || selected < match_count);

    const double row_height = 2 * y_margin + font->height;
    const double first_row = 1 * border_size + row_height;
    const double sel_margin = x_margin / 3;

    double y = first_row + (row_height + font->height) / 2 - font->descent;

    for (size_t i = 0; i < match_count; i++) {
        const struct match *match = matches_get(matches, i);//&matches[i];

        /* Hightlight selected entry */
        if (i == selected) {
            const struct rgba *sc = &render->options.selection_color;
            cairo_set_source_rgba(buf->cairo, sc->r, sc->g, sc->b, sc->a);
            cairo_set_operator(buf->cairo, CAIRO_OPERATOR_SOURCE);
            cairo_rectangle(buf->cairo,
                            x_margin - sel_margin,
                            first_row + i * row_height,
                            buf->width - 2 * (x_margin - sel_margin),
                            row_height);
            cairo_fill(buf->cairo);
        }

        double cur_x = border_size + x_margin;

        switch (match->application->icon.type) {
        case ICON_NONE:
            break;

        case ICON_SURFACE: {
            cairo_surface_t *surf = match->application->icon.surface;
            double width = cairo_image_surface_get_width(surf);
            double height = cairo_image_surface_get_height(surf);
            double scale = 1.0;

            if (height > row_height) {
                scale = font->height / height;
                LOG_DBG("%s: scaling: %f (row-height: %f, size=%fx%f)",
                        match->application->title, scale, row_height, width, height);

                width *= scale;
                height *= scale;
            }

            cairo_save(buf->cairo);
            cairo_set_operator(buf->cairo, CAIRO_OPERATOR_OVER);

            /* Translate + scale. Note: order matters! */
            cairo_translate(
                buf->cairo, cur_x,
                first_row + i * row_height + (row_height - height) / 2);
            cairo_scale(buf->cairo, scale, scale);

            cairo_set_source_surface(buf->cairo, surf, 0, 0);
            cairo_paint(buf->cairo);
            cairo_restore(buf->cairo);
            break;
        }

        case ICON_SVG: {
            RsvgHandle *svg = match->application->icon.svg;

            RsvgDimensionData dim;
            rsvg_handle_get_dimensions(svg, &dim);

            double height = font->height;
            double scale = height / dim.height;

            cairo_save(buf->cairo);
            cairo_set_operator(buf->cairo, CAIRO_OPERATOR_OVER);

            /* Translate + scale. Note: order matters! */
            cairo_translate(
                buf->cairo, cur_x,
                first_row + i * row_height + (row_height - height) / 2);
            cairo_scale(buf->cairo, scale, scale);

            rsvg_handle_render_cairo(svg, buf->cairo);
            cairo_restore(buf->cairo);
            break;
        }
        }

        cur_x += row_height;

        /* Application title */
        render_match_text(
            buf, &cur_x, y,
            match->application->title, match->start_title, wcslen(prompt_text(prompt)),
            render->regular_font, subpixel,
            render->options.pix_text_color, render->options.pix_match_color);

        y += row_height;
    }
}

struct render *
render_init(struct fcft_font *font, const struct render_options *options,
            enum fcft_subpixel subpixel)
{
    struct render *render = calloc(1, sizeof(*render));
    render->options = *options;
    render->regular_font = font;
    render->subpixel = subpixel;

    /* TODO: the one providing the options should calculate these */
    render->options.pix_background_color = rgba2pixman(render->options.background_color);
    render->options.pix_border_color = rgba2pixman(render->options.border_color);
    render->options.pix_text_color = rgba2pixman(render->options.text_color);
    render->options.pix_match_color = rgba2pixman(render->options.match_color);
    render->options.pix_selection_color = rgba2pixman(render->options.selection_color);
    return render;
}

void
render_destroy(struct render *render)
{
    if (render == NULL)
        return;

    fcft_destroy(render->regular_font);
    free(render);
}
