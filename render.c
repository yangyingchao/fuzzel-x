#include "render.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define LOG_MODULE "render"
#include "log.h"
#include "font.h"

struct render {
    struct options options;
    cairo_scaled_font_t *regular_font;
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

void
render_prompt(const struct render *render, struct buffer *buf,
              const struct prompt *prompt)
{
    const double x_margin = render->options.x_margin;
    const double y_margin = render->options.y_margin;
    const double border_size = render->options.border_size;

    cairo_set_scaled_font(buf->cairo, render->regular_font);

    cairo_font_extents_t fextents;
    cairo_font_extents(buf->cairo, &fextents);

    /* Text to render is the prompt followed by the entered text */
    char text[strlen(prompt->prompt) + strlen(prompt->text) + 1];
    strcpy(text, prompt->prompt);
    strcat(text, prompt->text);

    cairo_glyph_t *glyphs = NULL;
    cairo_text_cluster_t *clusters = NULL;
    cairo_text_cluster_flags_t cluster_flags;
    int num_glyphs = 0;
    int num_clusters = 0;

    cairo_status_t cr_status = cairo_scaled_font_text_to_glyphs(
        render->regular_font, 0, 0, text, -1, &glyphs, &num_glyphs,
        &clusters, &num_clusters, &cluster_flags);
    if (cr_status != CAIRO_STATUS_SUCCESS) {
        LOG_ERR("cairo failure: TODO: error message");
        return;
    }

    cairo_text_extents_t extents;
    cairo_scaled_font_glyph_extents(
        render->regular_font, glyphs, num_glyphs, &extents);

    for (int i = 0; i < num_glyphs; i++) {
        glyphs[i].x += border_size + x_margin;
        glyphs[i].y += border_size + y_margin + fextents.height - fextents.descent;
    }

    const struct rgba *tc = &render->options.text_color;

    cairo_set_source_rgba(buf->cairo, tc->r, tc->g, tc->b, tc->a);
    cairo_set_operator(buf->cairo, CAIRO_OPERATOR_OVER);
    cairo_show_text_glyphs(
        buf->cairo, text, -1, glyphs, num_glyphs,
        clusters, num_clusters, cluster_flags);

    /* prompt->cursor is the *byte* position, but we need the
     * *character| position */
    size_t cursor = 0;
    size_t cursor_at_glyph = 0;

    const size_t prompt_len = strlen(prompt->prompt);
    const size_t text_len = strlen(prompt->text);

    /* Count glyphs that make up the prompt */
    for (size_t i = 0; i < prompt_len;) {
        int clen = mblen(&prompt->prompt[i], MB_CUR_MAX);
        if (clen < 0) {
            LOG_ERRNO("prompt: %s", prompt->prompt);
            break;
        }

        i += clen;
        cursor_at_glyph++;
    }

    /* Count glyphs *before* the cursor */
    while (cursor < prompt->cursor && cursor < text_len) {
        int clen = mblen(&prompt->text[cursor], MB_CUR_MAX);
        if (clen < 0) {
            LOG_ERRNO("prompt: %s", prompt->text);
            break;
        }

        cursor += clen;
        cursor_at_glyph++;
    }

    int cursor_x;
    if (cursor_at_glyph == 0)
        cursor_x = border_size + x_margin;
    else if (cursor_at_glyph >= num_glyphs)
        cursor_x = border_size + x_margin + extents.x_advance;
    else
        cursor_x = glyphs[cursor_at_glyph].x;

    cairo_set_line_width(buf->cairo, 1);
    cairo_move_to(buf->cairo, cursor_x - 0.5, border_size + y_margin);
    cairo_line_to(buf->cairo, cursor_x - 0.5, border_size + y_margin + fextents.height);
    cairo_stroke(buf->cairo);

    cairo_glyph_free(glyphs);
    cairo_text_cluster_free(clusters);

#if 0
    const struct rgba *bc = &render->options.border_color;

    cairo_set_source_rgba(buf->cairo, bc->r, bc->g, bc->b, bc->a);
    cairo_set_line_width(buf->cairo, border_size);
    cairo_move_to(buf->cairo, 0, border_size + 2 * y_margin + fextents.height + border_size / 2);
    cairo_line_to(buf->cairo, buf->width, border_size + 2 * y_margin + fextents.height + border_size / 2);
    cairo_stroke(buf->cairo);
#endif
}

static void
render_glyphs(struct buffer *buf, struct rgba color,
              const cairo_glyph_t *glyphs, int num_glyphs)
{
    cairo_set_source_rgba(buf->cairo, color.r, color.g, color.b, color.a);
    cairo_set_operator(buf->cairo, CAIRO_OPERATOR_OVER);
    cairo_show_glyphs(buf->cairo, glyphs, num_glyphs);
}

static void
render_match_text(struct buffer *buf, double *x, double y,
                  const char *text, ssize_t start, size_t length,
                  cairo_scaled_font_t *font, struct rgba regular_color,
                  struct rgba match_color)
{
    cairo_set_scaled_font(buf->cairo, font);

    cairo_glyph_t *glyphs = NULL;
    cairo_text_cluster_t *clusters = NULL;
    cairo_text_cluster_flags_t cluster_flags;
    int num_glyphs = 0;
    int num_clusters = 0;

    cairo_status_t cr_status = cairo_scaled_font_text_to_glyphs(
        font, 0, 0, text, -1, &glyphs, &num_glyphs,
        &clusters, &num_clusters, &cluster_flags);

    if (cr_status != CAIRO_STATUS_SUCCESS) {
        LOG_ERR("cairo failure: TODO: error message");
        return;
    }

    cairo_text_extents_t extents;
    cairo_scaled_font_glyph_extents(font, glyphs, num_glyphs, &extents);

    for (int j = 0; j < num_glyphs; j++) {
        glyphs[j].x += *x;
        glyphs[j].y += y;
    }

    if (start >= 0) {

        /*
         * start/length are in bytes, but we need to know which
         * *glyph* the match starts at, and how many *glyphs* the match
         * is. For regular ascii, these will always be the same, but
         * for multibyte UTF-8 characters not so.
         */

        /* Calculate at which *glyph* the matching part starts */
        size_t idx = 0;
        size_t glyph_start = 0;
        while (idx < start) {
            int clen = mblen(&text[idx], MB_CUR_MAX);
            if (clen < 0) {
                LOG_ERRNO("match text: %s", text);
                break;
            }

            glyph_start++;
            idx += clen;
        }

        /* Find out how many *glyphs* that match */
        size_t glyph_match_count = 0;
        while (idx < start + length) {
            int clen = mblen(&text[idx], MB_CUR_MAX);
            if (clen < 0) {
                LOG_ERRNO("match text: %s", text);
                break;
            }

            glyph_match_count++;
            idx += clen;
        }

        render_glyphs(buf, regular_color, &glyphs[0], glyph_start);
        render_glyphs(buf, match_color, &glyphs[start], glyph_match_count);
        render_glyphs(buf, regular_color, &glyphs[start + length],
                      num_glyphs - glyph_start - glyph_match_count);
    } else
        render_glyphs(buf, regular_color, glyphs, num_glyphs);

    cairo_glyph_free(glyphs);
    cairo_text_cluster_free(clusters);

    *x += extents.width;
}

void
render_match_list(const struct render *render, struct buffer *buf,
                  const struct match matches[], size_t match_count,
                  size_t match_length, size_t selected)
{
    const double x_margin = render->options.x_margin;
    const double y_margin = render->options.y_margin;
    const double border_size = render->options.border_size;

    assert(match_count == 0 || selected < match_count);

    cairo_set_scaled_font(buf->cairo, render->regular_font);

    cairo_font_extents_t fextents;
    cairo_font_extents(buf->cairo, &fextents);

    const double row_height = 2 * y_margin + fextents.height;
    const double first_row = 1 * border_size + row_height;
    const double sel_margin = x_margin / 3;

    /*
     * LOG_DBG("height=%f, ascent=%f, descent=%f", fextents.height, fextents.ascent,
     *         fextents.descent);
     */
    double y = first_row + (row_height + fextents.height) / 2 - fextents.descent;

    for (size_t i = 0; i < match_count; i++) {
        const struct match *match = &matches[i];

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
                scale = (row_height - 2 * y_margin) / height;
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

            double height = row_height - 4;
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
            match->application->title, match->start_title, match_length,
            render->regular_font,
            render->options.text_color, render->options.match_color);

        y += row_height;
    }
}

struct render *
render_init(cairo_scaled_font_t *font, struct options options)
{
    struct render *render = calloc(1, sizeof(*render));
    render->options = options;
    render->regular_font = font;
    return render;
}

void
render_destroy(struct render *render)
{
    if (render == NULL)
        return;

    cairo_scaled_font_destroy(render->regular_font);
    free(render);
}
