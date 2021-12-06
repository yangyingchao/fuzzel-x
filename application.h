#pragma once

#include <stdbool.h>
#include <pixman.h>

#if defined(FUZZEL_ENABLE_SVG_LIBRSVG)
 #include <librsvg/rsvg.h>
#endif

#if defined(FUZZEL_ENABLE_SVG_NANOSVG)
 #include <nanosvg.h>
#endif

#include <fcft/fcft.h>
#include <tllist.h>

#include "prompt.h"

enum icon_type { ICON_NONE, ICON_PNG, ICON_SVG };

struct rasterized {
    pixman_image_t *pix;
    int size;
};
typedef tll(struct rasterized) rasterized_list_t;

struct icon {
    char *name;
    enum icon_type type;
    union {
#if defined(FUZZEL_ENABLE_PNG_LIBPNG)
        pixman_image_t *png;
#endif
#if defined(FUZZEL_ENABLE_SVG_LIBRSVG)
        RsvgHandle *svg;
#elif defined(FUZZEL_ENABLE_SVG_NANOSVG)
        NSVGimage *svg;
#endif
    };

    /* List of cached rasterizations (used with SVGs) */
    rasterized_list_t rasterized;
};

struct application {
    char *path;
    char *exec;
    wchar_t *basename;
    wchar_t *title;
    wchar_t *comment;
    struct icon icon;
    unsigned count;
    struct fcft_text_run *shaped;
};

bool application_execute(
    const struct application *app, const struct prompt *prompt, const char *launch_prefix);

struct application_list {
    struct application *v;
    size_t count;
};

struct application_list *applications_init(void);
void applications_destroy(struct application_list *apps);
void applications_flush_text_run_cache(struct application_list *apps);
