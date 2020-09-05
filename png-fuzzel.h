#pragma once

#if defined(FUZZEL_ENABLE_PNG)

#include <pixman.h>

pixman_image_t *png_load(const char *path);

#endif /* FUZZEL_ENABLE_PNG */
