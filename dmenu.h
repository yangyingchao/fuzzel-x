#pragma once

#include <sys/types.h>

#include "application.h"
#include "prompt.h"

enum dmenu_mode { DMENU_MODE_NONE, DMENU_MODE_TEXT, DMENU_MODE_INDEX };

void dmenu_load_entries(struct application_list *applications, int abort_fd);
bool dmenu_execute(const struct application *app, ssize_t index,
                   const struct prompt *prompt, enum dmenu_mode format);
