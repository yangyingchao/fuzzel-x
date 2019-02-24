#include "render.h"

#include <stdlib.h>
#include <string.h>

void
render_prompt(struct buffer *buf,
              cairo_scaled_font_t *scaled_font, double font_size,
              const struct prompt *prompt)
{
    const size_t x = 20;
    const size_t y_base = 0;
    const size_t y_advance = font_size + 10; /* TODO: how much "extra" is "right"? */
    size_t y = y_base;

    const char *const text = prompt->text;

    cairo_glyph_t *glyphs = NULL;
    cairo_text_cluster_t *clusters = NULL;
    cairo_text_cluster_flags_t cluster_flags;
    int num_glyphs = 0;
    int num_clusters = 0;

    cairo_status_t cr_status = cairo_scaled_font_text_to_glyphs(
        scaled_font, 0, 0, text, -1, &glyphs, &num_glyphs,
        &clusters, &num_clusters, &cluster_flags);
    assert(cr_status == CAIRO_STATUS_SUCCESS);

    cairo_text_extents_t extents;
    cairo_scaled_font_glyph_extents(
        scaled_font, glyphs, num_glyphs, &extents);

    for (int i = 0; i < num_glyphs; i++) {
        glyphs[i].x += x;
        glyphs[i].y += y +
            ((double)y_advance - extents.height) / 2 - extents.y_bearing;
    }

    cairo_set_scaled_font(buf->cairo, scaled_font);
    cairo_set_source_rgba(buf->cairo, 1.0, 1.0, 1.0, 1.0);
    cairo_set_operator(buf->cairo, CAIRO_OPERATOR_OVER);
    cairo_show_text_glyphs(
        buf->cairo, text, -1, glyphs, num_glyphs,
        clusters, num_clusters, cluster_flags);

    int cursor_x;
    if (prompt->cursor == 0)
        cursor_x = x;
    else if (prompt->cursor >= (size_t)num_glyphs)
        cursor_x = x + extents.x_advance;
    else
        cursor_x = glyphs[prompt->cursor].x;

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

void
render_match_list(struct buffer *buf,
                  cairo_scaled_font_t *scaled_font, double font_size,
                  const struct match matches[], size_t match_count,
                  size_t selected)
{
    assert(match_count == 0 || selected < match_count);

    const size_t x = 20;
    const size_t y_advance = font_size + 10; /* TODO: how much "extra" is "right"? */
    const size_t y_base = 2 + y_advance;
    size_t y = y_base;

    for (size_t i = 0; i < match_count; i++) {
        const struct match *match = &matches[i];
        const char *const text = match->application->title;

        cairo_glyph_t *glyphs = NULL;
        cairo_text_cluster_t *clusters = NULL;
        cairo_text_cluster_flags_t cluster_flags;
        int num_glyphs = 0;
        int num_clusters = 0;

        cairo_status_t cr_status = cairo_scaled_font_text_to_glyphs(
            scaled_font, 0, 0, text, -1, &glyphs, &num_glyphs,
            &clusters, &num_clusters, &cluster_flags);
        assert(cr_status == CAIRO_STATUS_SUCCESS);

        cairo_text_extents_t extents;
        cairo_scaled_font_glyph_extents(
            scaled_font, glyphs, num_glyphs, &extents);

        for (int j = 0; j < num_glyphs; j++) {
            glyphs[j].x += x;
            glyphs[j].y += y +
                ((double)y_advance - extents.height) / 2 - extents.y_bearing;
        }

        if (i == selected) {
            double f = 0.5;
            cairo_set_source_rgba(buf->cairo, 0.067+f, 0.067+f, 0.067+f, 0.9);
            cairo_set_operator(buf->cairo, CAIRO_OPERATOR_SOURCE);
            cairo_rectangle(buf->cairo, x - 2, y + 2, buf->width - 2 * (x - 2), y_advance - 4);
            cairo_fill(buf->cairo);
        } else if ((y - y_base) % (2 * y_advance) == 0) {
            double f = 0.1;
            cairo_set_source_rgba(buf->cairo, 0.067+f, 0.067+f, 0.067+f, 0.9);
            cairo_set_operator(buf->cairo, CAIRO_OPERATOR_SOURCE);
            cairo_rectangle(buf->cairo, x - 2, y + 2, buf->width - 2 * (x - 2), y_advance - 4);
            cairo_fill(buf->cairo);
        }

        cairo_set_scaled_font(buf->cairo, scaled_font);
        cairo_set_source_rgba(buf->cairo, 1.0, 1.0, 1.0, 1.0);
        cairo_set_operator(buf->cairo, CAIRO_OPERATOR_OVER);
        cairo_show_text_glyphs(
            buf->cairo, text, -1, glyphs, num_glyphs,
            clusters, num_clusters, cluster_flags);

        cairo_glyph_free(glyphs);
        cairo_text_cluster_free(clusters);

        y += y_advance;
    }
}
