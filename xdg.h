#pragma once

#include "application.h"
#include "tllist.h"

typedef tll(char *) xdg_data_dirs_t;
typedef tll(struct application) application_list_t;

xdg_data_dirs_t xdg_data_dirs(void);
void xdg_data_dirs_destroy(xdg_data_dirs_t dirs);

void xdg_find_programs(application_list_t *applications);
