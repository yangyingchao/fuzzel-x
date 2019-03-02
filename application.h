#pragma once

#include <cairo.h>

struct cairo_icon {
    int size;
    cairo_surface_t *surface;
};

struct application {
    char *exec;
    char *title;
    char *comment;
    struct cairo_icon icon;
};
