#pragma once

#include <cairo.h>
#include <librsvg/rsvg.h>

struct icon {
    cairo_surface_t *surface;
    RsvgHandle *svg;
};

struct application {
    char *path;
    char *exec;
    char *title;
    char *comment;
    //cairo_surface_t *icon;
    struct icon icon;
};
