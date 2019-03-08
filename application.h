#pragma once

#include <cairo.h>

struct application {
    char *path;
    char *exec;
    char *title;
    char *comment;
    cairo_surface_t *icon;
};
