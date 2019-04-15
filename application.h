#pragma once

#include <cairo.h>
#include <librsvg/rsvg.h>

enum icon_type { ICON_NONE, ICON_SURFACE, ICON_SVG };

struct icon {
    enum icon_type type;
    union {
        cairo_surface_t *surface;
        RsvgHandle *svg;
    };
};

struct application {
    char *path;
    char *exec;
    char *title;
    char *comment;
    struct icon icon;
    unsigned count;
};
