#if defined(FUZZEL_ENABLE_PNG_LIBPNG)

#include "png-fuzzel.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include <png.h>
#include <pixman.h>

#define LOG_MODULE "png"
#define LOG_ENABLE_DBG 0
#include "log.h"
#include "stride.h"
#include "xmalloc.h"

static void
png_warning_cb(png_structp png_ptr, png_const_charp warning_msg)
{
    LOG_WARN("libpng: %s", warning_msg);
}

pixman_image_t *
png_load(const char *path)
{
    pixman_image_t *pix = NULL;

    FILE *fp = NULL;
    png_structp png_ptr = NULL;
    png_infop info_ptr = NULL;
    png_bytepp row_pointers = NULL;
    uint8_t *image_data = NULL;

    /* open file and test for it being a png */
    if ((fp = fopen(path, "rbe")) == NULL) {
        //LOG_ERRNO("%s: failed to open", path);
        goto err;
    }

    /* Verify PNG header */
    uint8_t header[8] = {0};
    if (fread(header, 1, 8, fp) != 8 || png_sig_cmp(header, 0, 8)) {
        // LOG_ERR("%s: not a PNG", path);
        goto err;
    }

    /* Prepare for reading the PNG */
    if ((png_ptr = png_create_read_struct(
             PNG_LIBPNG_VER_STRING, NULL, NULL, NULL)) == NULL ||
        (info_ptr = png_create_info_struct(png_ptr)) == NULL)
    {
        LOG_ERR("%s: failed to initialize libpng", path);
        goto err;
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        LOG_ERR("%s: libpng error", path);
        goto err;
    }

    /* Set custom “warning” function */
    png_set_error_fn(
        png_ptr, png_get_error_ptr(png_ptr), NULL, &png_warning_cb);

    png_init_io(png_ptr, fp);
    png_set_sig_bytes(png_ptr, 8);

    /* Get meta data */
    png_read_info(png_ptr, info_ptr);
    int width = png_get_image_width(png_ptr, info_ptr);
    int height = png_get_image_height(png_ptr, info_ptr);
    png_byte color_type = png_get_color_type(png_ptr, info_ptr);
    png_byte bit_depth UNUSED = png_get_bit_depth(png_ptr, info_ptr);
    int channels UNUSED = png_get_channels(png_ptr, info_ptr);

    LOG_DBG("%s: %dx%d@%hhubpp, %d channels", path, width, height, bit_depth, channels);

    png_set_packing(png_ptr);
    png_set_interlace_handling(png_ptr);
    png_set_strip_16(png_ptr);  /* "pack" 16-bit colors to 8-bit */
    png_set_bgr(png_ptr);

    /* pixman expects pre-multiplied alpha */

    /* Tell libpng to expand to RGB(A) when necessary, and tell pixman
     * whether we have alpha or not */
    pixman_format_code_t format;
    switch (color_type) {
    case PNG_COLOR_TYPE_GRAY:
    case PNG_COLOR_TYPE_GRAY_ALPHA:
        LOG_DBG("%d-bit gray%s", bit_depth,
                color_type == PNG_COLOR_TYPE_GRAY_ALPHA ? "+alpha" : "");

        if (bit_depth < 8)
            png_set_expand_gray_1_2_4_to_8(png_ptr);

        png_set_gray_to_rgb(png_ptr);
        format = color_type == PNG_COLOR_TYPE_GRAY ? PIXMAN_r8g8b8 : PIXMAN_a8r8g8b8;
        break;

    case PNG_COLOR_TYPE_PALETTE:
        LOG_DBG("%d-bit colormap%s", bit_depth,
                png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS) ? "+tRNS" : "");

        png_set_palette_to_rgb(png_ptr);
        if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) {
            png_set_tRNS_to_alpha(png_ptr);
            format = PIXMAN_a8r8g8b8;
        } else
            format = PIXMAN_r8g8b8;
        break;

    case PNG_COLOR_TYPE_RGB:
        LOG_DBG("RGB");
        format = PIXMAN_r8g8b8;
        break;

    case PNG_COLOR_TYPE_RGBA:
        LOG_DBG("RGBA");
        format = PIXMAN_a8r8g8b8;
        break;

    default:
        LOG_ERR("unhandled PNG color type: %d", color_type);
        goto err;
    }

    png_read_update_info(png_ptr, info_ptr);

    size_t row_bytes UNUSED = png_get_rowbytes(png_ptr, info_ptr);
    int stride = stride_for_format_and_width(format, width);
    image_data = xmalloc(height * stride);

    LOG_DBG("stride=%d, row-bytes=%zu", stride, row_bytes);
    assert(stride >= row_bytes);

    row_pointers = xmalloc(height * sizeof(png_bytep));
    for (int i = 0; i < height; i++)
        row_pointers[i] = &image_data[i * stride];

    png_read_image(png_ptr, row_pointers);

    /* pixman expects pre-multiplied alpha */
    if (format == PIXMAN_a8r8g8b8) {
        for (int i = 0; i < height; i++) {
            uint32_t *p = (uint32_t *)row_pointers[i];
            for (int j = 0; j < width; j++, p++) {
                uint8_t a = (*p >> 24) & 0xff;
                uint8_t r = (*p >> 16) & 0xff;
                uint8_t g = (*p >> 8) & 0xff;
                uint8_t b = (*p >> 0) & 0xff;

                if (a == 0xff)
                    continue;

                if (a == 0) {
                    r = g = b = 0;
                } else {
                    r = r * a / 0xff;
                    g = g * a / 0xff;
                    b = b * a / 0xff;
                }

                *p = (uint32_t)a << 24 | r << 16 | g << 8 | b;
            }
        }
    }

    pix = pixman_image_create_bits_no_clear(
        format, width, height, (uint32_t *)image_data, stride);

err:
    if (pix == NULL)
        free(image_data);
    free(row_pointers);
    if (png_ptr != NULL)
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    if (fp != NULL)
        fclose(fp);

    return pix;
}

#endif /* FUZZEL_ENABLE_PNG_LIBPNG */
