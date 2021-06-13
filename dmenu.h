#pragma once

#include "application.h"
#include "prompt.h"

void dmenu_load_entries(struct application_list *applications);
bool dmenu_execute(const struct application *app, const struct prompt *prompt);
