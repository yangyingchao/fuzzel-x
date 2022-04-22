#pragma once

#include <fcft/fcft.h>

#include "config.h"
#include "dmenu.h"
#include "fdm.h"
#include "match.h"
#include "prompt.h"
#include "render.h"

struct wayland;

typedef void (*font_reloaded_t)(
    struct wayland *wayl, struct fcft_font *font, void *data);

struct wayland *wayl_init(
    const struct config *conf, struct fdm *fdm, struct render *render,
    struct prompt *prompt, struct matches *matches,
    //enum dmenu_mode dmenu_mode, const char *launch_prefix,
    //const char *output_name, const char *font_spec,
    //enum dpi_aware dpi_aware,
    font_reloaded_t font_reloaded_cb, void *data);

void wayl_destroy(struct wayland *wayl);

void wayl_refresh(struct wayland *wayl);
void wayl_flush(struct wayland *wayl);

bool wayl_exit_code(const struct wayland *wayl);
bool wayl_update_cache(const struct wayland *wayl);

bool wayl_size_font_by_dpi(const struct wayland *wayl);
