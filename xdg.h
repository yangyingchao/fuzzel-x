#pragma once

#include "application.h"
#include "tllist.h"

typedef tll(struct application) application_list_t;

void xdg_find_programs(application_list_t *applications);
