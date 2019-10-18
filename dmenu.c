#include "dmenu.h"

#include <stdbool.h>
#include <errno.h>

#define LOG_MODULE "dmenu"
#define LOG_ENABLE_DBG 0
#include "log.h"
#include "tllist.h"

void
dmenu_load_entries(struct application_list *applications)
{
    tll(wchar_t *) entries = tll_init();

    errno = 0;
    while (true) {
        char *line = NULL;
        size_t alloc_size = 0;
        ssize_t len = getline(&line, &alloc_size, stdin);

        if (len == -1) {
            free(line);
            if (errno != 0) {
                LOG_ERRNO("failed to read from stdin");
            }
            break;
        }

        while (len > 0 && line[len - 1] == '\n')
            line[--len] = '\0';

        LOG_DBG("%s", line);

        size_t wlen = mbstowcs(NULL, line, 0);
        wchar_t *wline = malloc((wlen + 1) * sizeof(wchar_t));
        mbstowcs(wline, line, wlen + 1);
        tll_push_back(entries, wline);

        free(line);
    }

    applications->v = malloc(tll_length(entries) * sizeof(applications->v[0]));
    applications->count = tll_length(entries);

    size_t i = 0;
    tll_foreach(entries, it) {
        struct application *app = &applications->v[i++];
        app->path = NULL;
        app->exec = NULL;
        app->title = it->item;
        app->comment = NULL;
        app->icon.type = ICON_NONE;
        app->count = 0;

        tll_remove(entries, it);
    }

    tll_free(entries);
}

bool
dmenu_execute(const struct application *app)
{
    printf("%S\n", app->title);
    return true;
}
