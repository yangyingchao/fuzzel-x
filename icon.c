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

static void
parse_theme(FILE *index, struct icon_theme *theme)
{
    char *section = NULL;
    int size = -1;
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

            if (section != NULL && context != NULL && type != NULL &&
                strcasecmp(context, "applications") == 0 &&
                strcasecmp(type, "scalable") != 0)
            {
                //LOG_INFO("%s: size=%d, scale=%d, context=%s, type=%s", section, size, scale, context, type);
                struct icon_dir dir = {.path = section, .size = size, .scale = scale};
                tll_push_back(theme->dirs, dir);
            } else {
                free(section);
                free(context);
                free(type);
            }

            size = -1;
            scale = 1;
            section = NULL;
            context = NULL;
            type = NULL;

            section = malloc(len - 2 + 1);
            memcpy(section, &line[1], len - 2);
            section[len - 2] = '\0';
            continue;
        }

        char *tok_ctx = NULL;
        const char *key = strtok_r(line, "=", &tok_ctx);
        char *value = strtok_r(NULL, "=", &tok_ctx);

        if (strcasecmp(key, "inherits") == 0) {
            LOG_INFO("inherits: %s", value);

            char *ctx = NULL;
            for (const char *theme_name = strtok_r(value, ",", &ctx);
                 theme_name != NULL; theme_name = strtok_r(NULL, ",", &ctx))
            {
                LOG_INFO("trying to load: %s", theme_name);
                struct icon_theme *sub_theme = icon_load_theme(theme_name);
                if (sub_theme != NULL) {
                    LOG_INFO("%s inherits %s", theme->path, sub_theme->path);
                    tll_push_back(theme->inherits, sub_theme);
                }
            }
        }

        if (strcasecmp(key, "size") == 0)
            sscanf(value, "%d", &size);

        else if (strcasecmp(key, "scale") == 0)
            sscanf(value, "%d", &scale);

        else if (strcasecmp(key, "context") == 0)
            context = strdup(value);

        else if (strcasecmp(key, "type") == 0)
            type = strdup(value);
    }

    if (section != NULL && context != NULL && type != NULL &&
        strcasecmp(context, "applications") == 0 &&
        strcasecmp(type, "scalable") != 0)
    {
        //LOG_INFO("%s: size=%d, scale=%d, context=%s, type=%s", section, size, scale, context, type);
        struct icon_dir dir = {.path = section, .size = size, .scale = scale};
        tll_push_back(theme->dirs, dir);
    } else { 
        free(section);
        free(context);
        free(type);
    }
}

struct icon_theme *
icon_load_theme(const char *name)
{
    /* TODO: XDG_DATA_HOME, .local/share, XDG_DATA_DIRS */

    int data_dir_fd = -1;
    int theme_dir_fd = -1;
    int index_fd = -1;
    FILE *index = NULL;
    struct icon_theme *theme = NULL;

    data_dir_fd = open("/usr/share/icons", O_RDONLY);
    if (data_dir_fd == -1) {
        LOG_ERRNO("%s: failed to open", "/usr/share/icons");
        goto out;
    }

    theme_dir_fd = openat(data_dir_fd, name, O_RDONLY);
    if (theme_dir_fd == -1) {
        //LOG_WARN("%s/%s: failed to open", "/usr/share/icons", name);
        goto out;
    }

    index_fd = openat(theme_dir_fd, "index.theme", O_RDONLY);
    if (index_fd == -1) {
        LOG_ERRNO("%s/%s/index.theme: failed to open", "/usr/share/icons", name);
        goto out;
    }

    index = fdopen(index_fd, "r");
    assert(index != NULL);

    theme = calloc(1, sizeof(*theme));

    theme->path = malloc(strlen("/usr/share/icons") + 1 + strlen(name) + 1);
    sprintf(theme->path, "/usr/share/icons/%s", name);

    parse_theme(index, theme);

 out:
    if (index != NULL)
        fclose(index);
    if (index_fd != -1)
        close(index_fd);
    if (theme_dir_fd != -1)
        close(theme_dir_fd);
    if (data_dir_fd != -1)
        close(data_dir_fd);
    return theme;
}
