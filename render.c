#include "render.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <fcft/fcft.h>

#define LOG_MODULE "render"
#define LOG_ENABLE_DBG 0
#include "log.h"
#include "stride.h"
#include "wayland.h"

#define min(x, y) ((x) < (y) ? (x) : (y))
#define max(x, y) ((x) > (y) ? (x) : (y))

struct render {
    struct render_options options;
    struct fcft_font *font;
    enum fcft_subpixel subpixel;
    float dpi;

    unsigned x_margin;
    unsigned y_margin;
    unsigned inner_pad;
    unsigned border_size;
    unsigned row_height;
    unsigned icon_height;
};

static pixman_color_t
rgba2pixman(struct rgba rgba)
{
    uint16_t r = rgba.r * 65535.0;
    uint16_t g = rgba.g * 65535.0;
    uint16_t b = rgba.b * 65535.0;
    uint16_t a = rgba.a * 65535.0;

    return (pixman_color_t){
        .red = (uint32_t)r * a / 0xffff,
        .green = (uint32_t)g * a / 0xffff,
        .blue = (uint32_t)b * a / 0xffff,
        .alpha = a,
    };
}

static int
pt_or_px_as_pixels(const struct pt_or_px *pt_or_px, float dpi)
{
    return pt_or_px->px == 0
        ? pt_or_px->pt * dpi / 72.
        : pt_or_px->px;
}

void
render_background(const struct render *render, struct buffer *buf)
{
    bool use_pixman =
#if defined(FUZZEL_ENABLE_CAIRO)
        render->options.border_radius == 0
#else
        true
#endif
        ;

    if (use_pixman) {
        unsigned bw = render->options.border_size;

        pixman_color_t bg = rgba2pixman(render->options.background_color);
        pixman_image_fill_rectangles(
            PIXMAN_OP_SRC, buf->pix, &bg,
            1, &(pixman_rectangle16_t){
                bw, bw, buf->width - 2 * bw, buf->height - 2 * bw});

        pixman_color_t border_color = rgba2pixman(render->options.border_color);
        pixman_image_fill_rectangles(
            PIXMAN_OP_SRC, buf->pix, &border_color,
            4, (pixman_rectangle16_t[]){
                {0, 0, buf->width, bw},                          /* top */
                {0, bw, bw, buf->height - 2 * bw},               /* left */
                {buf->width - bw, bw, bw, buf->height - 2 * bw}, /* right */
                {0, buf->height - bw, buf->width, bw}            /* bottom */
            });
    } else {
#if defined(FUZZEL_ENABLE_CAIRO)
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
#else
        assert(false);
#endif
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
        if (i >= prompt_len)
            x += pt_or_px_as_pixels(&render->options.letter_spacing, render->dpi);

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
                  int letter_spacing,
                  pixman_color_t regular_color, pixman_color_t match_color,
                  struct fcft_text_run **run)
{
    int x = *_x;
    int y = _y;

    const struct fcft_glyph **glyphs = NULL;
    int *clusters = NULL;
    long *kern = NULL;
    size_t count = 0;

    if (*run == NULL &&
        (fcft_capabilities() & FCFT_CAPABILITY_TEXT_RUN_SHAPING))
    {
        *run = fcft_text_run_rasterize(font, wcslen(text), text, subpixel);
    }

    if (*run != NULL) {
        glyphs = (*run)->glyphs;
        clusters = (*run)->cluster;
        count = (*run)->count;
    } else {
        count = wcslen(text);
        glyphs = malloc(count * sizeof(glyphs[0]));
        clusters = malloc(count * sizeof(clusters[0]));
        kern = malloc(count * sizeof(kern[0]));

        for (size_t i = 0; i < count; i++) {
            const struct fcft_glyph *glyph = fcft_glyph_rasterize(font, text[i], subpixel);
            if (glyph == NULL) {
                glyphs[i] = NULL;
                continue;
            }

            if (i > 0)
                fcft_kerning(font, text[i - 1], text[i], &kern[i], NULL);
            else
                kern[i] = 0;

            glyphs[i] = glyph;
            clusters[i] = i;
        }
    }

    for (size_t i = 0; i < count; i++) {
        bool is_match = start >= 0 && clusters[i] >= start && clusters[i] < start + length;
        x += kern != NULL ? kern[i] : 0;
        render_glyph(buf->pix, glyphs[i], x, y, is_match ? &match_color : &regular_color);
        x += glyphs[i]->advance.x + letter_spacing;
        y += glyphs[i]->advance.y;
    }

    if (*run == NULL) {
        free(kern);
        free(clusters);
        free(glyphs);
    }

    *_x = x;
}

void
render_match_list(const struct render *render, struct buffer *buf,
                  const struct prompt *prompt, const struct matches *matches)
{
    struct fcft_font *font = render->font;
    assert(font != NULL);

    const int x_margin = render->x_margin;
    const int y_margin = render->y_margin;
    const int inner_pad = render->inner_pad;
    const int border_size = render->border_size;
    const size_t match_count = matches_get_count(matches);
    const size_t selected = matches_get_match_index(matches);
    const enum fcft_subpixel subpixel =
        (render->options.background_color.a == 1. &&
         render->options.selection_color.a == 1.)
        ? render->subpixel : FCFT_SUBPIXEL_NONE;

    assert(match_count == 0 || selected < match_count);

    const int row_height = render->row_height;
    const int first_row = 1 * border_size + y_margin + row_height + inner_pad;
    const int sel_margin = x_margin / 3;

    int y = first_row + (row_height + font->height) / 2 - font->descent;

    for (size_t i = 0; i < match_count; i++) {
        if (y + font->descent > buf->height - y_margin - border_size) {
            /* Window too small - happens if the compositor doesn't
             * respect our requested size */
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

                    cairo_rectangle(buf->cairo, img_x, img_y, height, width);
                    cairo_clip(buf->cairo);

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
 #if defined(FUZZEL_ENABLE_CAIRO)
            cairo_surface_flush(buf->cairo_surface);
 #endif
            pixman_image_t *png = icon->png;
            pixman_format_code_t fmt = pixman_image_get_format(png);
            int height = pixman_image_get_height(png);
            int width = pixman_image_get_width(png);

            if (height > render->icon_height) {
                double scale = (double)render->icon_height / height;

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

                width *= scale;
                height *= scale;

                int stride = stride_for_format_and_width(fmt, width);
                uint8_t *data = malloc(height * stride);
                pixman_image_t *scaled_png = pixman_image_create_bits_no_clear(
                    fmt, width, height, (uint32_t *)data, stride);
                pixman_image_composite32(
                    PIXMAN_OP_SRC, png, NULL, scaled_png, 0, 0, 0, 0, 0, 0, width, height);

                free(pixman_image_get_data(png));
                pixman_image_unref(png);

                png = scaled_png;
                icon->png = png;
            }

            pixman_image_composite32(
                PIXMAN_OP_OVER, png, NULL, buf->pix,
                0, 0, 0, 0,
                cur_x, first_row + i * row_height + (row_height - height) / 2,
                width, height);

 #if defined(FUZZEL_ENABLE_CAIRO)
            cairo_surface_mark_dirty(buf->cairo_surface);
 #endif
#endif /* FUZZEL_ENABLE_PNG */
            break;
        }

        case ICON_SVG: {
#if defined(FUZZEL_ENABLE_SVG)
            RsvgDimensionData dim;
            rsvg_handle_get_dimensions(icon->svg, &dim);

            double height = render->icon_height;
            double scale = height / dim.height;

            double img_x = cur_x;
            double img_y = first_row + i * row_height + (row_height - height) / 2;
            double img_width = dim.width * scale;
            double img_height = dim.height * scale;

            cairo_save(buf->cairo);
            cairo_set_operator(buf->cairo, CAIRO_OPERATOR_OVER);

            cairo_rectangle(buf->cairo, img_x, img_y, img_width, img_height);
            cairo_clip(buf->cairo);

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

        cur_x += row_height + font->space_advance.x + pt_or_px_as_pixels(
            &render->options.letter_spacing, render->dpi);

        /* Application title */
        render_match_text(
            buf, &cur_x, y,
            match->application->title, match->start_title, wcslen(prompt_text(prompt)),
            font, subpixel,
            pt_or_px_as_pixels(&render->options.letter_spacing, render->dpi),
            render->options.pix_text_color, render->options.pix_match_color,
            &match->application->shaped);

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

    /* TODO: the one providing the opti3Dons should calculate these */
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
render_set_font(struct render *render, struct fcft_font *font,
                int scale, float dpi,
                int *new_width, int *new_height)
{
    if (font != NULL) {
        fcft_destroy(render->font);
        render->font = font;
    } else {
        assert(render->font != NULL);
        font = render->font;
    }

    const struct fcft_glyph *W = fcft_glyph_rasterize(
        font, L'W', render->subpixel);

    const unsigned x_margin = render->options.pad.x * scale;
    const unsigned y_margin = render->options.pad.y * scale;
    const unsigned inner_pad = render->options.pad.inner * scale;

    const unsigned border_size = render->options.border_size * scale;

    const unsigned row_height = render->options.line_height.px >= 0
        ? pt_or_px_as_pixels(&render->options.line_height, dpi)
        : font->height;

    const unsigned icon_height = max(0, row_height - font->descent);

    const unsigned height =
        border_size +                        /* Top border */
        y_margin +
        row_height +                         /* The prompt */
        inner_pad +                          /* Padding between prompt and matches */
        render->options.lines * row_height + /* Matches */
        y_margin +
        border_size;                         /* Bottom border */

    const unsigned width =
        border_size +
        x_margin +
        (max((W->advance.x + pt_or_px_as_pixels(
                 &render->options.letter_spacing, dpi)), 0)
         * render->options.chars) +
        x_margin +
        border_size;

    LOG_DBG("x-margin: %d, y-margin: %d, border: %d, row-height: %d, "
            "icon-height: %d, height: %d, width: %d, scale: %d",
            x_margin, y_margin, border_size, row_height, icon_height,
            height, width, scale);

    render->y_margin = y_margin;
    render->x_margin = x_margin;
    render->inner_pad = inner_pad;
    render->border_size = border_size;
    render->row_height = row_height;
    render->icon_height = icon_height;
    render->dpi = dpi;

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
