#pragma once

#include "tllist.h"

struct icon_dir {
    char *path;  /* Relative to theme's base path */
    int size;
    int scale;
};

struct icon_theme {
    char *path;
    tll(struct icon_dir) dirs;
    tll(struct icon_theme *) inherits;
};

struct icon_theme *icon_load_theme(const char *name);
void icon_theme_destroy(struct icon_theme *theme);
