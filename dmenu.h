#pragma once

#include <sys/types.h>

#include "application.h"
#include "config.h"
#include "prompt.h"

void dmenu_load_entries(struct application_list *applications, char delim, int abort_fd);
bool dmenu_execute(const struct application *app, ssize_t index,
                   const struct prompt *prompt, enum dmenu_mode format);
