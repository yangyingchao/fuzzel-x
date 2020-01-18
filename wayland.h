#pragma once

#include "fdm.h"

#include "prompt.h"
#include "match.h"
#include "render.h"

struct wayland;
struct wayland *wayl_init(
    struct fdm *fdm, int width, int height, const char *output_name);
void wayl_destroy(struct wayland *wayl);

void wayl_configure(
    struct wayland *wayl, struct render *render, struct prompt *prompt,
    struct matches *matches, bool dmenu_mode);

void wayl_refresh(struct wayland *wayl);
void wayl_flush(struct wayland *wayl);

int wayl_ppi(const struct wayland *wayl);
bool wayl_exit_code(const struct wayland *wayl);
bool wayl_update_cache(const struct wayland *wayl);
