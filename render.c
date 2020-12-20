#include "render.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <fcft/fcft.h>

#define LOG_MODULE "render"
#include "log.h"
#include "wayland.h"

#define max(x, y) ((x) > (y) ? (x) : (y))

struct render {
    struct render_options options;
    struct fcft_font *font;
    enum fcft_subpixel subpixel;

    unsigned x_margin;
    unsigned y_margin;
    unsigned border_size;
    unsigned row_height;
};

static pixman_color_t
rgba2pixman(struct rgba rgba)
{
    uint16_t r = rgba.r * 65535.0;
    uint16_t g = rgba.g * 65535.0;
    uint16_t b = rgba.b * 65535.0;
    uint16_t a = rgba.a * 65535.0;

    return (pixman_color_t){
        .red = r * a / 0xffff,
        .green = g * a / 0xffff,
        .blue = b * a / 0xffff,
        .alpha = a,
    };
}

void
render_background(const struct render *render, struct buffer *buf)
{
    if (render->options.border_radius == 0) {
        pixman_color_t bg = rgba2pixman(render->options.background_color);
        pixman_image_fill_rectangles(
            PIXMAN_OP_SRC, buf->pix, &bg, 1, &(pixman_rectangle16_t){0, 0, buf->width, buf->height}
            );
    } else {
        /*
         * Lines in cairo are *between* pixels.
         *
         * To get a sharp 1px line, we need to draw it with
         * line-width=2.
         *
         * Thus, we need to draw the path offset:ed with half that
         * (=actual border width).
         */
        const double b = render->border_size;
        const double w = max(buf->width - 2 * b, 0.);
        const double h = max(buf->height - 2 * b, 0.);

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
    struct fcft_font *font = render->font;
    assert(font != NULL);

    const wchar_t *pprompt = prompt_prompt(prompt);
    const size_t prompt_len = wcslen(pprompt);

    const wchar_t *ptext = prompt_text(prompt);
    const size_t text_len = wcslen(ptext);

    const enum fcft_subpixel subpixel =
        (render->options.background_color.a == 1. &&
         render->options.selection_color.a == 1.)
        ? render->subpixel : FCFT_SUBPIXEL_NONE;

    int x = render->border_size + render->x_margin;
    int y = render->border_size + render->y_margin + font->ascent;

    wchar_t prev = 0;

    for (size_t i = 0; i < prompt_len + text_len; i++) {
        wchar_t wc = i < prompt_len ? pprompt[i] : ptext[i - prompt_len];
        const struct fcft_glyph *glyph = fcft_glyph_rasterize(font, wc, subpixel);
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
                  struct fcft_font *font, enum fcft_subpixel subpixel,
                  pixman_color_t regular_color, pixman_color_t match_color)
{
    int x = *_x;
    int y = _y;

    for (size_t i = 0; i < wcslen(text); i++) {
        const struct fcft_glyph *glyph = fcft_glyph_rasterize(font, text[i], subpixel);
        if (glyph == NULL)
            continue;

        long x_kern = 0, y_kern = 0;
        if (i > 0)
            fcft_kerning(font, text[i - 1], text[i], &x_kern, &y_kern);

        bool is_match = start >= 0 && i >= start && i < start + length;
        x += x_kern;
        render_glyph(buf->pix, glyph, x, y + y_kern, is_match ? &match_color : &regular_color);
        x += glyph->advance.x;
    }

    *_x = x;
}

void
render_match_list(const struct render *render, struct buffer *buf,
                  const struct prompt *prompt, const struct matches *matches)
{
    struct fcft_font *font = render->font;
    assert(font != NULL);

    const double x_margin = render->x_margin;
    const double y_margin = render->y_margin;
    const double border_size = render->border_size;
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
        if ((int)(y + font->descent + row_height / 2) >
            (int)(buf->height - y_margin - border_size))
        {
            /* Window too small - happens if the compositor doesn't
             * repsect our requested size */
            break;
        }

        const struct match *match = matches_get(matches, i);

        if (i == selected) {
            /* If currently selected item has a scalable icon, and if
             * there's "enough" free space, render a large
             * representation of the icon */

            if (match->application->icon.type == ICON_SVG) {
#if defined(FUZZEL_ENABLE_SVG)
                RsvgHandle *svg = match->application->icon.svg;
                RsvgDimensionData dim;
                rsvg_handle_get_dimensions(svg, &dim);

                const double max_height = buf->height * 0.618;
                const double max_width = buf->width * 0.618;

                const double scale_x = max_width / dim.width;
                const double scale_y = max_height / dim.height;
                const double scale = scale_x < scale_y ? scale_x : scale_y;

                const double height = dim.height * scale;
                const double width = dim.width * scale;

                const double img_x = (buf->width - width) / 2.;
                const double img_y = first_row + (buf->height - height) / 2.;

                double list_end = first_row + match_count * row_height;

                if (img_y > list_end) {

                    cairo_save(buf->cairo);
                    cairo_set_operator(buf->cairo, CAIRO_OPERATOR_ATOP);

                    /* Translate + scale. Note: order matters! */
                    cairo_translate(buf->cairo, img_x, img_y);
                    cairo_scale(buf->cairo, scale, scale);

                    if (cairo_status(buf->cairo) == CAIRO_STATUS_SUCCESS)
                        rsvg_handle_render_cairo(svg, buf->cairo);
                    cairo_restore(buf->cairo);
                }
#endif /* FUZZEL_ENABLE_SVG */
            }

            pixman_color_t sc = rgba2pixman(render->options.selection_color);
            pixman_image_fill_rectangles(
                PIXMAN_OP_SRC, buf->pix, &sc, 1,
                &(pixman_rectangle16_t){
                    x_margin - sel_margin,
                    first_row + i * row_height,
                    buf->width - 2 * (x_margin - sel_margin),
                    row_height}
                );
        }

        double cur_x = border_size + x_margin;
        struct icon *icon = &match->application->icon;

        switch (icon->type) {
        case ICON_NONE:
            break;

        case ICON_PNG: {
#if defined(FUZZEL_ENABLE_PNG)
            cairo_surface_flush(buf->cairo_surface);

            pixman_image_t *png = icon->png.pix;
            int height = pixman_image_get_height(png);
            int width = pixman_image_get_width(png);

            if (height > row_height) {
                double scale = (double)font->height / height;

                if (!icon->png.has_scale_transform) {
                    pixman_f_transform_t _scale_transform;
                    pixman_f_transform_init_scale(
                        &_scale_transform, 1. / scale, 1. / scale);

                    pixman_transform_t scale_transform;
                    pixman_transform_from_pixman_f_transform(
                        &scale_transform, &_scale_transform);
                    pixman_image_set_transform(png, &scale_transform);

                    int param_count = 0;
                    pixman_kernel_t kernel = PIXMAN_KERNEL_LANCZOS3;
                    pixman_fixed_t *params = pixman_filter_create_separable_convolution(
                        &param_count,
                        pixman_double_to_fixed(1. / scale),
                        pixman_double_to_fixed(1. / scale),
                        kernel, kernel,
                        kernel, kernel,
                        pixman_int_to_fixed(1),
                        pixman_int_to_fixed(1));

                    if (params != NULL || param_count == 0) {
                        pixman_image_set_filter(
                            png, PIXMAN_FILTER_SEPARABLE_CONVOLUTION,
                            params, param_count);
                    }

                    free(params);
                    icon->png.has_scale_transform = true;
                }

                assert(icon->png.has_scale_transform);
                width *= scale;
                height *= scale;
            }

            pixman_image_composite32(
                PIXMAN_OP_OVER, png, NULL, buf->pix,
                0, 0, 0, 0,
                cur_x, first_row + i * row_height + (row_height - height) / 2,
                width, height);

            cairo_surface_mark_dirty(buf->cairo_surface);
#endif /* FUZZEL_ENABLE_PNG */
            break;
        }

        case ICON_SVG: {
#if defined(FUZZEL_ENABLE_SVG)
            RsvgDimensionData dim;
            rsvg_handle_get_dimensions(icon->svg, &dim);

            double height = font->height;
            double scale = height / dim.height;

            cairo_save(buf->cairo);
            cairo_set_operator(buf->cairo, CAIRO_OPERATOR_OVER);

            /* Translate + scale. Note: order matters! */
            cairo_translate(
                buf->cairo, cur_x,
                first_row + i * row_height + (row_height - height) / 2);
            cairo_scale(buf->cairo, scale, scale);

            if (cairo_status(buf->cairo) == CAIRO_STATUS_SUCCESS)
                rsvg_handle_render_cairo(icon->svg, buf->cairo);
            cairo_restore(buf->cairo);
#endif /* FUZZEL_ENABLE_SVG */
            break;
        }
        }

        cur_x += row_height;

        /* Application title */
        render_match_text(
            buf, &cur_x, y,
            match->application->title, match->start_title, wcslen(prompt_text(prompt)),
            font, subpixel,
            render->options.pix_text_color, render->options.pix_match_color);

        y += row_height;
    }
}

struct render *
render_init(const struct render_options *options)
{
    struct render *render = calloc(1, sizeof(*render));
    *render = (struct render){
        .options = *options,
    };

    /* TODO: the one providing the options should calculate these */
    render->options.pix_background_color = rgba2pixman(render->options.background_color);
    render->options.pix_border_color = rgba2pixman(render->options.border_color);
    render->options.pix_text_color = rgba2pixman(render->options.text_color);
    render->options.pix_match_color = rgba2pixman(render->options.match_color);
    render->options.pix_selection_color = rgba2pixman(render->options.selection_color);
    return render;
}

void
render_set_subpixel(struct render *render, enum fcft_subpixel subpixel)
{
    render->subpixel = subpixel;
}

bool
render_set_font(struct render *render, struct fcft_font *font, int scale,
                int *new_width, int *new_height)
{
    if (font != NULL) {
        fcft_destroy(render->font);
        render->font = font;
    } else {
        assert(render->font != NULL);
        font = render->font;
    }

#define max(x, y) ((x) > (y) ? (x) : (y))

    const unsigned y_margin = max(1, (double)font->height / 10.);
    const unsigned x_margin = font->height * 2;

#undef max

    const unsigned border_size = render->options.border_size * scale;
    const unsigned row_height = 2 * y_margin + font->height;

    const unsigned height =
        border_size +                        /* Top border */
        row_height +                         /* The prompt */
        render->options.lines * row_height + /* Matches */
        + row_height / 2 +                   /* Spacing at the bottom */
        border_size;                         /* Bottom border */

    const struct fcft_glyph *M = fcft_glyph_rasterize(
        font, L'W', render->subpixel);

    const unsigned width =
        border_size + x_margin +
        M->advance.x * render->options.chars +
        x_margin + border_size;

    LOG_DBG("x-margin: %d, y-margin: %d, border: %d, row-height: %d, "
            "height: %d, width: %d, scale: %d",
            x_margin, y_margin, border_size, row_height, height, width, scale);

    render->y_margin = y_margin;
    render->x_margin = x_margin;
    render->border_size = border_size;
    render->row_height = row_height;
    if (new_width != NULL)
        *new_width = width;
    if (new_height != NULL)
        *new_height = height;

    return true;
}

void
render_destroy(struct render *render)
{
    if (render == NULL)
        return;

    fcft_destroy(render->font);
    free(render);
}
