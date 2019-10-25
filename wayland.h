#pragma once

#include "fdm.h"
#include "kbd-repeater.h"

struct wayland;
struct wayland *wayl_init(
    struct fdm *fdm, struct repeat *repeat, int width, int height);
void wayl_destroy(struct wayland *wayl);
