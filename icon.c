#include "icon.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <unistd.h>

#include <sys/stat.h>
#include <fcntl.h>

#define LOG_MODULE "icon"
#include "log.h"
#include "tllist.h"
#include "xdg.h"

static bool
dir_is_usable(const char *path, const char *context, const char *type)
{
    return (path != NULL &&
            context != NULL && strcasecmp(context, "applications") == 0 &&
            type != NULL && strcasecmp(type, "scalable") != 0
        );
}

static void
parse_theme(FILE *index, struct icon_theme *theme)
{
    char *section = NULL;
    int size = -1;
    int min_size = -1;
    int max_size = -1;
    int scale = 1;
    char *context = NULL;
    char *type = NULL;

    while (true) {
        char *line = NULL;
        size_t sz = 0;
        ssize_t len = getline(&line, &sz, index);

        if (len == -1) {
            free(line);
            break;
        }

        if (len == 0) {
            free(line);
            continue;
        }

        if (line[len - 1] == '\n') {
            line[len - 1] = '\0';
            len--;
        }

        if (len == 0) {
            free(line);
            continue;
        }

        if (line[0] == '[' && line[len - 1] == ']') {

            if (dir_is_usable(section, context, type)) {
                struct icon_dir dir = {
                    .path = section,
                    .size = size,
                    .min_size = min_size >= 0 ? min_size : size,
                    .max_size = max_size >= 0 ? max_size : size,
                    .scale = scale
                };
                tll_push_back(theme->dirs, dir);
            } else
                free(section);

            free(context);
            free(type);

            size = min_size = max_size = -1;
            scale = 1;
            section = NULL;
            context = NULL;
            type = NULL;

            section = malloc(len - 2 + 1);
            memcpy(section, &line[1], len - 2);
            section[len - 2] = '\0';
            free(line);
            continue;
        }

        char *tok_ctx = NULL;

        const char *key = strtok_r(line, "=", &tok_ctx);
        char *value = strtok_r(NULL, "=", &tok_ctx);

        if (strcasecmp(key, "inherits") == 0) {
            char *ctx = NULL;
            for (const char *theme_name = strtok_r(value, ",", &ctx);
                 theme_name != NULL; theme_name = strtok_r(NULL, ",", &ctx))
            {
                struct icon_theme *sub_theme = icon_load_theme(theme_name);
                if (sub_theme != NULL) {
                    LOG_DBG("%s inherits %s", theme->path, sub_theme->path);
                    tll_push_back(theme->inherits, sub_theme);
                }
            }
        }

        if (strcasecmp(key, "size") == 0)
            sscanf(value, "%d", &size);

        else if (strcasecmp(key, "minsize") == 0)
            sscanf(value, "%d", &min_size);

        else if (strcasecmp(key, "maxsize") == 0)
            sscanf(value, "%d", &max_size);

        else if (strcasecmp(key, "scale") == 0)
            sscanf(value, "%d", &scale);

        else if (strcasecmp(key, "context") == 0)
            context = strdup(value);

        else if (strcasecmp(key, "type") == 0)
            type = strdup(value);

        free(line);
    }

    if (dir_is_usable(section, context, type)) {
        struct icon_dir dir = {
            .path = section,
            .size = size,
            .min_size = min_size >= 0 ? min_size : size,
            .max_size = max_size >= 0 ? max_size : size,
            .scale = scale
        };
        tll_push_back(theme->dirs, dir);
    } else
        free(section);

    free(context);
    free(type);
}

static struct icon_theme *
load_theme_in(const char *dir)
{
    int theme_dir_fd = -1;
    int index_fd = -1;
    FILE *index = NULL;
    struct icon_theme *theme = NULL;

    theme_dir_fd = open(dir, O_RDONLY);
    if (theme_dir_fd == -1) {
        //LOG_WARN("%s: failed to open", dir);
        goto out;
    }

    index_fd = openat(theme_dir_fd, "index.theme", O_RDONLY);
    if (index_fd == -1) {
        LOG_ERRNO("%s/index.theme: failed to open", dir);
        goto out;
    }

    index = fdopen(index_fd, "r");
    assert(index != NULL);

    theme = calloc(1, sizeof(*theme));
    theme->path = strdup(dir);
    parse_theme(index, theme);

 out:
    if (index != NULL)
        fclose(index);
    if (index_fd != -1)
        close(index_fd);
    if (theme_dir_fd != -1)
        close(theme_dir_fd);
    return theme;
}

struct icon_theme *
icon_load_theme(const char *name)
{
    struct icon_theme *theme = NULL;

    xdg_data_dirs_t dirs = xdg_data_dirs();
    tll_foreach(dirs, it) {
        char path[strlen(it->item) + 1 + strlen("icons") + 1 + strlen(name) + 1];
        sprintf(path, "%s/icons/%s", it->item, name);

        theme = load_theme_in(path);
        if (theme != NULL)
            break;
    }

    xdg_data_dirs_destroy(dirs);
    return theme;
}

void
icon_theme_destroy(struct icon_theme *theme)
{
    if (theme == NULL)
        return;

    free(theme->path);

    tll_foreach(theme->dirs, it) {
        free(it->item.path);
        tll_remove(theme->dirs, it);
    }

    tll_foreach(theme->inherits, it) {
        icon_theme_destroy(it->item);
        tll_remove(theme->inherits, it);
    }

    free(theme);
}
