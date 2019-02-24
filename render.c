#include "render.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define LOG_MODULE "render"
#include "log.h"
#include "font.h"

struct render {
    cairo_scaled_font_t *regular_font;
    double font_size;
};

void
render_prompt(const struct render *render, struct buffer *buf,
              const struct prompt *prompt)
{
    const size_t x = 20;
    const size_t y_base = 0;
    const size_t y_advance = render->font_size + 10; /* TODO: how much "extra" is "right"? */
    size_t y = y_base;

    const char *const text = prompt->text;

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
        glyphs[i].x += x;
        glyphs[i].y += y +
            ((double)y_advance - extents.height) / 2 - extents.y_bearing;
    }

    cairo_set_scaled_font(buf->cairo, render->regular_font);
    cairo_set_source_rgba(buf->cairo, 1.0, 1.0, 1.0, 1.0);
    cairo_set_operator(buf->cairo, CAIRO_OPERATOR_OVER);
    cairo_show_text_glyphs(
        buf->cairo, text, -1, glyphs, num_glyphs,
        clusters, num_clusters, cluster_flags);

    /* prompt->cursor is the *byte* position, but we need the
     * *character| position */
    size_t cursor = 0;
    size_t cursor_at_glyph = 0;

    while (cursor < prompt->cursor) {
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
        cursor_x = x;
    else if (cursor_at_glyph >= num_glyphs)
        cursor_x = x + extents.x_advance;
    else
        cursor_x = glyphs[cursor_at_glyph].x;

    cairo_set_line_width(buf->cairo, 1);
    cairo_move_to(buf->cairo, cursor_x - 0.5, y_base + 2);
    cairo_line_to(buf->cairo, cursor_x - 0.5, y_base + y_advance - 2);
    cairo_stroke(buf->cairo);

    cairo_glyph_free(glyphs);
    cairo_text_cluster_free(clusters);

    cairo_set_line_width(buf->cairo, 1);
    cairo_move_to(buf->cairo, 0, y_base +  y_advance + 0.5);
    cairo_line_to(buf->cairo, buf->width, y_base + y_advance + 0.5);
    cairo_stroke(buf->cairo);
}

static void
render_text(struct buffer *buf, int *x, int y, double y_advance,
            const char *text, size_t len, cairo_scaled_font_t *font, uint32_t color)
{
    cairo_glyph_t *glyphs = NULL;
    cairo_text_cluster_t *clusters = NULL;
    cairo_text_cluster_flags_t cluster_flags;
    int num_glyphs = 0;
    int num_clusters = 0;

    cairo_status_t cr_status = cairo_scaled_font_text_to_glyphs(
        font, 0, 0, text, len, &glyphs, &num_glyphs,
        &clusters, &num_clusters, &cluster_flags);

    if (cr_status != CAIRO_STATUS_SUCCESS) {
        LOG_ERR("cairo failure: TODO: error message");
        return;
    }

    cairo_text_extents_t extents;
    cairo_scaled_font_glyph_extents(font, glyphs, num_glyphs, &extents);

    for (int j = 0; j < num_glyphs; j++) {
        glyphs[j].x += *x;
        glyphs[j].y += y + (y_advance - extents.height) / 2 - extents.y_bearing;
    }

    double red, green, blue, alpha;
    red = (double)((color >> 24) & 0xff) / 255.0;
    green = (double)((color >> 16) & 0xff) / 255.0;
    blue = (double)((color >> 8) & 0xff) / 255.0;
    alpha = (double)((color >> 0) & 0xff) / 255.0;

    cairo_set_scaled_font(buf->cairo, font);
    cairo_set_source_rgba(buf->cairo, red, green, blue, alpha);
    cairo_set_operator(buf->cairo, CAIRO_OPERATOR_OVER);
    cairo_show_text_glyphs(
        buf->cairo, text, len, glyphs, num_glyphs,
        clusters, num_clusters, cluster_flags);

    cairo_glyph_free(glyphs);
    cairo_text_cluster_free(clusters);

    *x += extents.width;
}

static void
render_match_text(struct buffer *buf, int *x, int y, double y_advance,
                  const char *text, ssize_t start, size_t length,
                  cairo_scaled_font_t *font,
                  uint32_t regular_color, uint32_t match_color)
{
    if (start >= 0) {
        render_text(buf, x, y, y_advance, &text[0], start, font, regular_color);
        render_text(buf, x, y, y_advance, &text[start], length, font, match_color);
        render_text(buf, x, y, y_advance, &text[start + length],
                    strlen(text) - (start + length), font, regular_color);
    } else
        render_text(buf, x, y, y_advance, text, strlen(text), font, regular_color);
}

void
render_match_list(const struct render *render, struct buffer *buf,
                  const struct match matches[], size_t match_count,
                  size_t match_length, size_t selected)
{
    assert(match_count == 0 || selected < match_count);

    const size_t x = 20;
    const size_t y_advance = render->font_size + 10; /* TODO: how much "extra" is "right"? */
    const size_t y_base = 2 + y_advance;
    size_t y = y_base;

    for (size_t i = 0; i < match_count; i++) {
        const struct match *match = &matches[i];

        /* Hightlight selected entry */
        if (i == selected) {
            double f = 0.5;
            cairo_set_source_rgba(buf->cairo, 0.067+f, 0.067+f, 0.067+f, 0.9);
            cairo_set_operator(buf->cairo, CAIRO_OPERATOR_SOURCE);
            cairo_rectangle(buf->cairo, x - 2, y + 2, buf->width - 2 * (x - 2), y_advance - 4);
            cairo_fill(buf->cairo);
        }

        /* Slightly different background on every other item */
        else if ((y - y_base) % (2 * y_advance) == 0) {
            double f = 0.1;
            cairo_set_source_rgba(buf->cairo, 0.067+f, 0.067+f, 0.067+f, 0.9);
            cairo_set_operator(buf->cairo, CAIRO_OPERATOR_SOURCE);
            cairo_rectangle(buf->cairo, x - 2, y + 2, buf->width - 2 * (x - 2), y_advance - 4);
            cairo_fill(buf->cairo);
        }

        /* Application title */
        int cur_x = x;
        render_match_text(
            buf, &cur_x, y, y_advance,
            match->application->title, match->start_title, match_length,
            render->regular_font, 0xffffffff, 0xffff00ff);

#if 0
        /* Comment, if available */
        if (match->application->comment != NULL) {
            char comment[2 + strlen(match->application->comment) + 1 + 1];
            sprintf(comment, " (%s)", match->application->comment);

            render_match_text(
                buf, &cur_x, y, y_advance,
                comment, match->start_comment + 2,
                match_length, render->regular_font, 0xffffffff, 0xffff00ff);
        }
#endif
        y += y_advance;
    }
}

struct render *
render_init(const char *font_name)
{
    struct render *render = calloc(1, sizeof(*render));
    render->regular_font = font_from_name(font_name, &render->font_size);
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
