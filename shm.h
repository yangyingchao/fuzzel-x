#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <pixman.h>
#if defined(FUZZEL_ENABLE_CAIRO)
 #include <cairo.h>
#endif
#include <wayland-client.h>

struct buffer {
    int width;
    int height;
    int stride;

    bool busy;
    size_t size;
    void *mmapped;

    struct wl_buffer *wl_buf;

    pixman_image_t *pix;

#if defined(FUZZEL_ENABLE_CAIRO)
    cairo_surface_t *cairo_surface;
    cairo_t *cairo;
#endif
};

struct buffer *shm_get_buffer(struct wl_shm *shm, int width, int height);
void shm_fini(void);
