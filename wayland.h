#pragma once

#include <fcft/fcft.h>

#include "fdm.h"

#include "prompt.h"
#include "match.h"
#include "render.h"

/* TODO? */
#include "application.h"
#include "icon.h"

struct wayland;
struct wayland *wayl_init(
    struct fdm *fdm,
    struct render *render, struct prompt *prompt, struct matches *matches,
    const struct render_options *render_options, bool dmenu_mode,
    const char *output_name, const char *font_name,

    /* TODO? */
    const icon_theme_list_t *themes, struct application_list *apps
    );
void wayl_destroy(struct wayland *wayl);

void wayl_refresh(struct wayland *wayl);
void wayl_flush(struct wayland *wayl);

enum fcft_subpixel wayl_subpixel(const struct wayland *wayl);
bool wayl_exit_code(const struct wayland *wayl);
bool wayl_update_cache(const struct wayland *wayl);
