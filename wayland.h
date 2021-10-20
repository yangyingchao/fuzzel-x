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

typedef void (*font_reloaded_t)(
    struct wayland *wayl, struct fcft_font *font, void *data);

enum dpi_aware {DPI_AWARE_AUTO, DPI_AWARE_YES, DPI_AWARE_NO};

struct wayland *wayl_init(
    struct fdm *fdm,
    struct render *render, struct prompt *prompt, struct matches *matches,
    const struct render_options *render_options, bool dmenu_mode,
    const char *output_name, const char *font_spec, enum dpi_aware dpi_aware,
    font_reloaded_t font_reloaded_cb, void *data);

void wayl_destroy(struct wayland *wayl);

void wayl_refresh(struct wayland *wayl);
void wayl_flush(struct wayland *wayl);

bool wayl_exit_code(const struct wayland *wayl);
bool wayl_update_cache(const struct wayland *wayl);

bool wayl_size_font_by_dpi(const struct wayland *wayl);
