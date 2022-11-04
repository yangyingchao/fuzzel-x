#include "render.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <fcft/fcft.h>

#if defined(FUZZEL_ENABLE_SVG_NANOSVG)
 #include <nanosvgrast.h>
#endif

#define LOG_MODULE "render"
#define LOG_ENABLE_DBG 0
#include "log.h"
#include "char32.h"
#include "icon.h"
#include "stride.h"
#include "wayland.h"

#define min(x, y) ((x) < (y) ? (x) : (y))
#define max(x, y) ((x) > (y) ? (x) : (y))

struct render {
    const struct config *conf;
    struct fcft_font *font;
    enum fcft_subpixel subpixel;

    int scale;
    float dpi;
    bool size_font_by_dpi;

    pixman_color_t pix_background_color;
    pixman_color_t pix_border_color;
    pixman_color_t pix_text_color;
    pixman_color_t pix_match_color;
    pixman_color_t pix_selection_color;
    pixman_color_t pix_selection_text_color;
    pixman_color_t pix_selection_match_color;

    unsigned x_margin;
    unsigned y_margin;
    unsigned inner_pad;
    unsigned border_size;
    unsigned row_height;
    unsigned icon_height;

    mtx_t *icon_lock;
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
pt_or_px_as_pixels(const struct render *render, const struct pt_or_px *pt_or_px)
{
    double scale = !render->size_font_by_dpi ? render->scale : 1.;
    double dpi = render->size_font_by_dpi  ? render->dpi : 96.;

    return pt_or_px->px == 0
        ? round(pt_or_px->pt * scale * dpi / 72.)
        : pt_or_px->px;
}

void
render_background(const struct render *render, struct buffer *buf)
{
    bool use_pixman =
#if defined(FUZZEL_ENABLE_CAIRO)
        render->conf->border.radius == 0
#else
        true
#endif
        ;

    if (use_pixman) {
        unsigned bw = render->conf->border.size;

        pixman_color_t bg = rgba2pixman(render->conf->colors.background);
        pixman_image_fill_rectangles(
            PIXMAN_OP_SRC, buf->pix, &bg,
            1, &(pixman_rectangle16_t){
                bw, bw, buf->width - 2 * bw, buf->height - 2 * bw});

        pixman_color_t border_color = rgba2pixman(render->conf->colors.border);
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
        /* Erase */
        cairo_set_operator(buf->cairo, CAIRO_OPERATOR_CLEAR);
        cairo_rectangle(buf->cairo, 0, 0, buf->width, buf->height);
        cairo_fill(buf->cairo);

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
        const double radius = render->conf->border.radius;

        /* Path describing an arc:ed rectangle */
        cairo_new_path(buf->cairo);
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
        const struct rgba *bc = &render->conf->colors.border;
        cairo_set_operator(buf->cairo, CAIRO_OPERATOR_SOURCE);
        cairo_set_line_width(buf->cairo, 2 * b);
        cairo_set_source_rgba(buf->cairo, bc->r, bc->g, bc->b, bc->a);
        cairo_stroke_preserve(buf->cairo);

        /* Background */
        const struct rgba *bg = &render->conf->colors.background;
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

    const struct config *conf = render->conf;

    const char32_t *pprompt = prompt_prompt(prompt);
    size_t prompt_len = c32len(pprompt);

    const char32_t *ptext = prompt_text(prompt);
    const size_t text_len = c32len(ptext);

    const enum fcft_subpixel subpixel =
        (render->conf->colors.background.a == 1. &&
         render->conf->colors.selection.a == 1.)
        ? render->subpixel : FCFT_SUBPIXEL_NONE;

    int x = render->border_size + render->x_margin;
    int y = render->border_size + render->y_margin + font->ascent;

    if (fcft_capabilities() & FCFT_CAPABILITY_TEXT_RUN_SHAPING) {
        struct fcft_text_run *run = fcft_rasterize_text_run_utf32(
            font, prompt_len, pprompt, subpixel);

        if (run != NULL) {
            for (size_t i = 0; i < run->count; i++) {
                const struct fcft_glyph *glyph = run->glyphs[i];
                render_glyph(buf->pix, glyph, x, y, &render->pix_text_color);
                x += glyph->advance.x;
            }
            fcft_text_run_destroy(run);

            /* Cursor, if right after the prompt. In all other cases,
             * the cursor will be rendered by the loop below */
            if (prompt_cursor(prompt) == 0) {
                pixman_image_fill_rectangles(
                    PIXMAN_OP_SRC, buf->pix, &render->pix_text_color,
                    1, &(pixman_rectangle16_t){
                        x, y - font->ascent,
                        font->underline.thickness, font->ascent + font->descent});
            }

            /* Prevent loop below from rendering the prompt */;
            prompt_len = 0;
        }
    }

    char32_t prev = 0;

    for (size_t i = 0; i < prompt_len + text_len; i++) {
        char32_t wc = i < prompt_len
            ? pprompt[i]
            : (conf->password != 0
               ? conf->password
               : ptext[i - prompt_len]);

        const struct fcft_glyph *glyph = fcft_rasterize_char_utf32(
            font, wc, subpixel);

        if (glyph == NULL) {
            prev = wc;
            continue;
        }

        long x_kern;
        fcft_kerning(font, prev, wc, &x_kern, NULL);

        x += x_kern;
        render_glyph(buf->pix, glyph, x, y, &render->pix_text_color);
        x += glyph->advance.x;
        if (i >= prompt_len)
            x += pt_or_px_as_pixels(render, &render->conf->letter_spacing);

        /* Cursor */
        if (prompt_cursor(prompt) + prompt_len - 1 == i) {
            pixman_image_fill_rectangles(
                PIXMAN_OP_SRC, buf->pix, &render->pix_text_color,
                1, &(pixman_rectangle16_t){
                    x, y - font->ascent,
                    font->underline.thickness, font->ascent + font->descent});
        }

        prev = wc;
    }
}

static void
render_match_text(struct buffer *buf, double *_x, double _y, double max_x,
                  const char32_t *text, size_t match_count,
                  const struct match_substring matches[static match_count],
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
        *run = fcft_rasterize_text_run_utf32(
            font, c32len(text), text, subpixel);
    }

    if (*run != NULL) {
        glyphs = (*run)->glyphs;
        clusters = (*run)->cluster;
        count = (*run)->count;
    } else {
        count = c32len(text);
        glyphs = malloc(count * sizeof(glyphs[0]));
        clusters = malloc(count * sizeof(clusters[0]));
        kern = malloc(count * sizeof(kern[0]));

        for (size_t i = 0; i < count; i++) {
            const struct fcft_glyph *glyph = fcft_rasterize_char_utf32(
                font, text[i], subpixel);
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
        bool is_match = false;
        for (size_t j = 0; j < match_count; j++) {
            const struct match_substring *match = &matches[j];
            assert(match->start >= 0);

            if (clusters[i] >= match->start &&
                clusters[i] < match->start + match->len)
            {
                is_match = true;
                break;
            }
        }

        if (x + (kern != NULL ? kern[i] : 0) + glyphs[i]->advance.x >= max_x) {
            const struct fcft_glyph *ellipses =
                fcft_rasterize_char_utf32(font, U'…', subpixel);

            if (ellipses != NULL)
                render_glyph(buf->pix, ellipses, x, y, &regular_color);

            break;
        }

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

#if defined(FUZZEL_ENABLE_SVG_LIBRSVG)
static void
render_svg_librsvg(const struct icon *icon, int x, int y, int size, struct buffer *buf)
{
    RsvgHandle *svg = icon->svg;

    cairo_save(buf->cairo);
    cairo_set_operator(buf->cairo, CAIRO_OPERATOR_ATOP);

 #if LIBRSVG_CHECK_VERSION(2, 46, 0)
    if (cairo_status(buf->cairo) == CAIRO_STATUS_SUCCESS) {
        const RsvgRectangle viewport = {
            .x = x,
            .y = y,
            .width = size,
            .height = size,
        };

        cairo_rectangle(buf->cairo, x, y, size, size);
        cairo_clip(buf->cairo);

        rsvg_handle_render_document(svg, buf->cairo, &viewport, NULL);
    }
 #else
    RsvgDimensionData dim;
    rsvg_handle_get_dimensions(svg, &dim);

    const double scale_x = size / dim.width;
    const double scale_y = size / dim.height;
    const double scale = scale_x < scale_y ? scale_x : scale_y;

    const double height = dim.height * scale;
    const double width = dim.width * scale;

    cairo_rectangle(buf->cairo, x, y, height, width);
    cairo_clip(buf->cairo);

    /* Translate + scale. Note: order matters! */
    cairo_translate(buf->cairo, x, y);
    cairo_scale(buf->cairo, scale, scale);

    if (cairo_status(buf->cairo) == CAIRO_STATUS_SUCCESS)
        rsvg_handle_render_cairo(svg, buf->cairo);
 #endif
    cairo_restore(buf->cairo);
}
#endif /* FUZZEL_ENABLE_SVG_LIBRSVG */

#if defined(FUZZEL_ENABLE_SVG_NANOSVG)
static void
render_svg_nanosvg(struct icon *icon, int x, int y, int size, struct buffer *buf)
{
#if defined(FUZZEL_ENABLE_CAIRO)
    cairo_surface_flush(buf->cairo_surface);
#endif

    pixman_image_t *img = NULL;

    /* Look for a cached image, at the correct size */
    tll_foreach(icon->rasterized, it) {
        if (it->item.size == size) {
            img = it->item.pix;
            break;
        }
    }

    if (img == NULL) {
        NSVGimage *svg = icon->svg;
        struct NSVGrasterizer *rast = nsvgCreateRasterizer();

        if (rast == NULL)
            return;

        float scale = svg->width > svg->height ? size / svg->width : size / svg->height;

        uint8_t *data = malloc(size * size * 4);
        nsvgRasterize(rast, svg, 0, 0, scale, data, size, size, size * 4);

        img = pixman_image_create_bits_no_clear(
            PIXMAN_a8b8g8r8, size, size, (uint32_t *)data, size * 4);

        /* Nanosvg produces non-premultiplied ABGR, while pixman expects
         * premultiplied */
        for (uint32_t *abgr = (uint32_t *)data;
             abgr < (uint32_t *)(data + size * size * 4);
             abgr++)
        {
            uint8_t alpha = (*abgr >> 24) & 0xff;
            uint8_t blue = (*abgr >> 16) & 0xff;
            uint8_t green = (*abgr >> 8) & 0xff;
            uint8_t red = (*abgr >> 0) & 0xff;

            if (alpha == 0xff)
                continue;

            if (alpha == 0x00)
                blue = green = red = 0x00;
            else {
                blue = blue * alpha / 0xff;
                green = green * alpha / 0xff;
                red = red * alpha / 0xff;
            }

            *abgr = (uint32_t)alpha << 24 | blue << 16 | green << 8 | red;
        }

        nsvgDeleteRasterizer(rast);
        tll_push_back(icon->rasterized, ((struct rasterized){img, size}));
    }

    pixman_image_composite32(
        PIXMAN_OP_OVER, img, NULL, buf->pix, 0, 0, 0, 0, x, y, size, size);

#if defined(FUZZEL_ENABLE_CAIRO)
    cairo_surface_mark_dirty(buf->cairo_surface);
#endif
}
#endif /* FUZZEL_ENABLE_SVG_NANOSVG */

static void
render_svg(struct icon *icon, int x, int y, int size, struct buffer *buf)
{
    assert(icon->type == ICON_SVG);

    if (icon->svg == NULL) {
        if (!icon_from_svg(icon, icon->path))
            return;
        LOG_DBG("%s", icon->path);
    }

#if defined(FUZZEL_ENABLE_SVG_LIBRSVG)
    render_svg_librsvg(icon, x, y, size, buf);
#elif defined(FUZZEL_ENABLE_SVG_NANOSVG)
    render_svg_nanosvg(icon, x, y, size, buf);
#endif
}

#if defined(FUZZEL_ENABLE_PNG_LIBPNG)
static void
render_png_libpng(struct icon *icon, int x, int y, int size, struct buffer *buf)
{
#if defined(FUZZEL_ENABLE_CAIRO)
    cairo_surface_flush(buf->cairo_surface);
#endif

    pixman_image_t *png = icon->png;
    pixman_format_code_t fmt = pixman_image_get_format(png);
    int height = pixman_image_get_height(png);
    int width = pixman_image_get_width(png);

    if (height > size) {
        double scale = (double)size / height;

        pixman_f_transform_t _scale_transform;
        pixman_f_transform_init_scale(&_scale_transform, 1. / scale, 1. / scale);

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
        PIXMAN_OP_OVER, png, NULL, buf->pix, 0, 0, 0, 0, x, y, width, height);

#if defined(FUZZEL_ENABLE_CAIRO)
    cairo_surface_mark_dirty(buf->cairo_surface);
#endif
}
#endif /* FUZZEL_ENABLE_PNG_LIBPNG */

static void
render_png(struct icon *icon, int x, int y, int size, struct buffer *buf)
{
    assert(icon->type == ICON_PNG);

    if (icon->png == NULL) {
        if (!icon_from_png(icon, icon->path))
            return;
        LOG_DBG("%s", icon->path);
    }

#if defined(FUZZEL_ENABLE_PNG_LIBPNG)
    render_png_libpng(icon, x, y, size, buf);
#endif
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
        (render->conf->colors.background.a == 1. &&
         render->conf->colors.selection.a == 1.)
        ? render->subpixel : FCFT_SUBPIXEL_NONE;
    const struct fcft_glyph *ellipses =
        fcft_rasterize_char_utf32(font, U'…', subpixel);

    assert(match_count == 0 || selected < match_count);

    const int row_height = render->row_height;
    const int first_row = 1 * border_size + y_margin + row_height + inner_pad;
    const int sel_margin = x_margin / 3;

    int y = first_row + (row_height + font->height) / 2 - font->descent;

    bool render_icons = mtx_trylock(render->icon_lock) == thrd_success;

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

            const double ratio = render->conf->image_size_ratio;
            const double size = min(buf->height * ratio, buf->width * ratio);
            const double img_x = (buf->width - size) / 2.;
            const double img_y_bottom = max(buf->height - first_row, 0.);
            const double img_y = max(img_y_bottom - size, 0.);

            const double list_end = first_row + match_count * row_height;

            LOG_DBG("img_y=%f, list_end=%f", img_y, list_end);

            if (render_icons &&
                match->application->icon.type == ICON_SVG &&
                img_y > list_end + row_height)
            {
                render_svg(&match->application->icon, img_x, img_y, size, buf);
            }

            pixman_color_t sc = rgba2pixman(render->conf->colors.selection);
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
        double max_x = buf->width - border_size - x_margin;

#if 0 /* Render the icon+text bounding box */
        pixman_color_t sc = rgba2pixman(render->conf->match_color);
        pixman_image_fill_rectangles(
            PIXMAN_OP_SRC, buf->pix, &sc, 1,
            &(pixman_rectangle16_t){
                cur_x, first_row + i * row_height,
                max_x - cur_x, row_height}
            );
#endif

        if (render_icons) {
            struct icon *icon = &match->application->icon;
            const int size = render->icon_height;
            const int img_x = cur_x;
            const int img_y = first_row + i * row_height + (row_height - size) / 2;

            switch (icon->type) {
            case ICON_NONE:
                break;

            case ICON_PNG:
                render_png(icon, img_x, img_y, size, buf);
                break;

            case ICON_SVG:
                render_svg(icon, img_x, img_y, size, buf);
                break;
            }
        }

        const struct fcft_glyph *space = fcft_rasterize_char_utf32(
            font, U' ', subpixel);

        cur_x +=
            (render->conf->icons_enabled && matches_have_icons(matches)
             ? (row_height +
                (space != NULL ? space->advance.x : font->max_advance.x))
             : 0) +
            pt_or_px_as_pixels(render, &render->conf->letter_spacing);

        /* Application title */
        render_match_text(
            buf, &cur_x, y, max_x - (ellipses != NULL ? ellipses->width : 0),
            match->application->title, match->pos_count, match->pos,
            font, subpixel,
            pt_or_px_as_pixels(render, &render->conf->letter_spacing),
            (i == selected
             ? render->pix_selection_text_color
             : render->pix_text_color),
            (i == selected
             ? render->pix_selection_match_color
             : render->pix_match_color),
            &match->application->shaped);

        y += row_height;
    }

    if (render_icons)
        mtx_unlock(render->icon_lock);
}

struct render *
render_init(const struct config *conf, mtx_t *icon_lock)
{
    struct render *render = calloc(1, sizeof(*render));
    *render = (struct render){
        .conf = conf,
        .icon_lock = icon_lock,
    };

    render->pix_background_color = rgba2pixman(render->conf->colors.background);
    render->pix_border_color = rgba2pixman(render->conf->colors.border);
    render->pix_text_color = rgba2pixman(render->conf->colors.text);
    render->pix_match_color = rgba2pixman(render->conf->colors.match);
    render->pix_selection_color = rgba2pixman(render->conf->colors.selection);
    render->pix_selection_text_color = rgba2pixman(render->conf->colors.selection_text);
    render->pix_selection_match_color = rgba2pixman(render->conf->colors.selection_match);
    return render;
}

void
render_set_subpixel(struct render *render, enum fcft_subpixel subpixel)
{
    render->subpixel = subpixel;
}

bool
render_set_font(struct render *render, struct fcft_font *font,
                int scale, float dpi, bool size_font_by_dpi,
                int *new_width, int *new_height)
{
    if (font != NULL) {
        fcft_destroy(render->font);
        render->font = font;
    } else {
        assert(render->font != NULL);
        font = render->font;
    }

    render->scale = scale;
    render->dpi = dpi;
    render->size_font_by_dpi = size_font_by_dpi;

    const struct fcft_glyph *W = fcft_rasterize_char_utf32(
        font, U'W', render->subpixel);

    const unsigned x_margin = render->conf->pad.x * scale;
    const unsigned y_margin = render->conf->pad.y * scale;
    const unsigned inner_pad = render->conf->pad.inner * scale;

    const unsigned border_size = render->conf->border.size * scale;

    const unsigned row_height = render->conf->line_height.px >= 0
        ? pt_or_px_as_pixels(render, &render->conf->line_height)
        : font->height;

    const unsigned icon_height = max(0, row_height - font->descent);

    const unsigned height =
        border_size +                        /* Top border */
        y_margin +
        row_height +                         /* The prompt */
        inner_pad +                          /* Padding between prompt and matches */
        render->conf->lines * row_height + /* Matches */
        y_margin +
        border_size;                         /* Bottom border */

    const unsigned width =
        border_size +
        x_margin +
        (max((W->advance.x + pt_or_px_as_pixels(
                  render, &render->conf->letter_spacing)),
             0)
         * render->conf->chars) +
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

    if (new_width != NULL)
        *new_width = width;
    if (new_height != NULL)
        *new_height = height;

    return true;
}

int
render_icon_size(const struct render *render)
{
    return render->icon_height;
}

void
render_destroy(struct render *render)
{
    if (render == NULL)
        return;

    fcft_destroy(render->font);
    free(render);
}
