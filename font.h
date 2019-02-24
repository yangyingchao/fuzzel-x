#pragma once

#include <cairo/cairo.h>

cairo_scaled_font_t *font_from_name(const char *name, double *size);
