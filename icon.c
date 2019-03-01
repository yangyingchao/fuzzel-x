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
            } else
                free(section);

            free(context);
            free(type);

            size = -1;
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

        free(line);
    }

    if (section != NULL && context != NULL && type != NULL &&
        strcasecmp(context, "applications") == 0 &&
        strcasecmp(type, "scalable") != 0)
    {
        //LOG_INFO("%s: size=%d, scale=%d, context=%s, type=%s", section, size, scale, context, type);
        struct icon_dir dir = {.path = section, .size = size, .scale = scale};
        tll_push_back(theme->dirs, dir);
    } else
        free(section);

    free(context);
    free(type);
}

static struct icon_theme *
load_theme_in(const char *name, int data_dir_fd)
{
    int theme_dir_fd = -1;
    int index_fd = -1;
    FILE *index = NULL;
    struct icon_theme *theme = NULL;

#if 0
    data_dir_fd = open("/usr/share/icons", O_RDONLY);
    if (data_dir_fd == -1) {
        LOG_ERRNO("%s: failed to open", "/usr/share/icons");
        goto out;
    }
#endif

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
#if 0
    if (data_dir_fd != -1)
        close(data_dir_fd);
#endif
    return theme;
}

struct icon_theme *
icon_load_theme(const char *name)
{
    struct icon_theme *theme = NULL;

    /*
     * First, look in the user's private XDG_DATA_HOME (falling back
     * to .local/share if XDG_DATA_HOME hasn't been set)
     */

    const char *xdg_data_home = getenv("XDG_DATA_HOME");
    if (xdg_data_home != NULL) {
        int data_fd = open(xdg_data_home, O_RDONLY);
        int fd = data_fd != -1 ? openat(data_fd, "icons", O_RDONLY) : -1;

        if (fd != -1) {
            theme = load_theme_in(name, fd);
            close(fd);
        }

        if (data_fd != -1)
            close(data_fd);
    } else {
        const char *home = getenv("HOME");
        int home_fd = open(home, O_RDONLY);
        int fd = home_fd != -1
            ? openat(home_fd, ".local/share/icons", O_RDONLY)
            : -1;

        if (fd != -1) {
            theme = load_theme_in(name, fd);
            close(fd);
        }

        if (home_fd != -1)
            close(home_fd);
    }

    if (theme != NULL)
        return theme;

    /*
     * Second, look through the directories specified in XDG_DATA_DIRS
     * (falling back to /usr/local/share:/usr/share if XDG_DATA_DIRS
     * hasn't been set).
     */

    const char *_xdg_data_dirs = getenv("XDG_DATA_DIRS");
    if (_xdg_data_dirs != NULL) {

        char *xdg_data_dirs = strdup(_xdg_data_dirs);

        for (const char *data_dir = strtok(xdg_data_dirs, ":");
             data_dir != NULL;
             data_dir = strtok(NULL, ":"))
        {
            int fd_base = open(data_dir, O_RDONLY);
            int fd = fd_base != -1 ? openat(fd_base, "icons", O_RDONLY) : -1;

            if (fd != -1) {
                theme = load_theme_in(name, fd);
                close(fd);
            }

            if (fd_base != -1)
                close(fd_base);

            if (theme != NULL)
                break;
        }
        free(xdg_data_dirs);

    } else {
        static const char *const default_paths[] = {
            "/usr/local/share/icons",
            "/usr/share/icons",
        };
        static const size_t cnt = sizeof(default_paths) / sizeof(default_paths[0]);

        for (size_t i = 0; i < cnt; i++) {
            int fd = open(default_paths[i], O_RDONLY);
            if (fd != -1) {
                theme = load_theme_in(name, fd);
                close(fd);
            }

            if (theme != NULL)
                break;
        }
    }

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
