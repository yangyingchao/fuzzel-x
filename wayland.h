#pragma once

#include "fdm.h"

#include "prompt.h"
#include "match.h"
#include "render.h"

struct wayland;
struct wayland *wayl_init(
    struct fdm *fdm, struct render *render, struct prompt *prompt,
    struct matches *matches, int width, int height, const char *output_name,
    bool dmenu_mode);
void wayl_destroy(struct wayland *wayl);

void wayl_flush(struct wayland *wayl);

bool wayl_exit_code(const struct wayland *wayl);
bool wayl_update_cache(const struct wayland *wayl);
