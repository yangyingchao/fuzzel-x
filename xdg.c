#include "xdg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <pwd.h>

#define LOG_MODULE "xdg"
#define LOG_ENABLE_DBG 0
#include "log.h"
#include "icon.h"

static struct cairo_icon
load_icon(const char *name, const struct icon_theme *theme)
{
    if (name == NULL || theme == NULL)
        return (struct cairo_icon){0};

    LOG_INFO("looking for %s in %s", name, theme->path);

    int min_diff = 10000;

    /* Assume sorted */
    for (size_t i = 0; i < 3; i++) {
        tll_rforeach(theme->dirs, it) {
            if (it->item.scale != 1)
                continue;

            const int diff = abs(it->item.size - 16);
            if (i == 0 && diff != 0){
                /* Looking for *exactly* our wanted size */
                if (diff < min_diff)
                    min_diff = diff;
                continue;
            } else if (i == 1 && diff != min_diff) {
                /* Try the one which matches most closely */
                continue;
            } else {
                /* Use anyone available */
            }
            
            const size_t len = strlen(theme->path) + 1 +
                strlen(it->item.path) + 1 +
                strlen(name) + strlen(".png") + 1;

            char *full_path = malloc(len);
            sprintf(full_path, "%s/%s/%s.png", theme->path, it->item.path, name);

            cairo_surface_t *surf = cairo_image_surface_create_from_png(full_path);

            if (cairo_surface_status(surf) == CAIRO_STATUS_SUCCESS) {
                LOG_INFO("%s", full_path);
                free(full_path);
                return (struct cairo_icon){.size = it->item.size, .surface = surf};
            }

            free(full_path);
            cairo_surface_destroy(surf);
        }
    }

    tll_foreach(theme->inherits, it) {
        struct cairo_icon icon = load_icon(name, it->item);
        if (icon.size > 0)
            return icon;
    }

    return (struct cairo_icon){0};
}

static void
parse_desktop_file(int fd, const struct icon_theme *theme, application_list_t *applications)
{
    FILE *f = fdopen(fd, "r");
    if (f == NULL)
        return;

    bool is_desktop_entry = false;
    char *name = NULL;
    char *exec = NULL;
    char *generic_name = NULL;
    char *icon = NULL;

    while (true) {
        char *line = NULL;
        size_t sz = 0;
        ssize_t len = getline(&line, &sz, f);

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
            if (strncasecmp(&line[1], "desktop entry", len - 2) == 0) {
                is_desktop_entry = true;
                free(line);
                continue;
            } else {
                free(line);
                break;
            }
        }

        const char *key = strtok(line, "=");
        char *value = strtok(NULL, "=");

        if (strcasecmp(key, "name") == 0)
            name = strdup(value);

        else if (strcasecmp(key, "exec") == 0)
            exec = strdup(value);

        else if (strcasecmp(key, "genericname") == 0)
            generic_name = strdup(value);

        else if (strcasecmp(key, "icon") == 0)
            icon = strdup(value);

        free(line);
    }

    fclose(f);

    if (is_desktop_entry && name != NULL && exec != NULL) {
        bool already_added = false;
        tll_foreach(*applications, it) {
            if (strcmp(it->item.title, name) == 0) {
                already_added = true;
                break;
            }
        }

        if (!already_added) {
            tll_push_back(
                *applications,
                ((struct application){
                    .exec = exec, .title = name, .comment = generic_name,
                    .icon = load_icon(icon, theme)}));
            free(icon);
            return;
        }
    }

    free(name);
    free(exec);
    free(generic_name);
    free(icon);
}

static void
scan_dir(int base_fd, const struct icon_theme *theme, application_list_t *applications)
{
    DIR *d = fdopendir(base_fd);
    if (d == NULL) {
        LOG_ERRNO("failed to open directory");
        return;
    }

    for (const struct dirent *e = readdir(d); e != NULL; e = readdir(d)) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;

        struct stat st;
        if (fstatat(base_fd, e->d_name, &st, 0) == -1) {
            LOG_WARN("%s: failed to stat", e->d_name);
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            int dir_fd = openat(base_fd, e->d_name, O_RDONLY);
            if (dir_fd == -1) {
                LOG_ERRNO("%s: failed to open", e->d_name);
                continue;
            }

            scan_dir(dir_fd, theme, applications);
            close(dir_fd);
        } else if (S_ISREG(st.st_mode)) {
            /* Skip files not ending with ".desktop" */
            const size_t desktop_len = strlen(".desktop");
            const size_t name_len = strlen(e->d_name);
            if (name_len < desktop_len)
                continue;

            if (strcmp(&e->d_name[name_len - desktop_len], ".desktop") != 0)
                continue;

            LOG_DBG("%s", e->d_name);
            int fd = openat(base_fd, e->d_name, O_RDONLY);
            if (fd == -1)
                LOG_WARN("%s: failed to open", e->d_name);
            else {
                parse_desktop_file(fd, theme, applications);
                close(fd);
            }
        }

    }

    closedir(d);
}

void
xdg_find_programs(application_list_t *applications)
{
    struct icon_theme *theme = icon_load_theme("Arc");
    if (theme == NULL)
        LOG_WARN("icon theme not found");
    else
        LOG_INFO("theme: %s", theme->path);

    xdg_data_dirs_t dirs = xdg_data_dirs();
    tll_foreach(dirs, it) {
        char path[strlen(it->item) + 1 + strlen("applications") + 1];
        sprintf(path, "%s/applications", it->item);

        int fd = open(path, O_RDONLY);
        if (fd != -1) {
            scan_dir(fd, theme, applications);
            close(fd);
        }
    }

    xdg_data_dirs_destroy(dirs);
    icon_theme_destroy(theme);
}

xdg_data_dirs_t
xdg_data_dirs(void)
{
    xdg_data_dirs_t ret = tll_init();

    const char *xdg_data_home = getenv("XDG_DATA_HOME");
    if (xdg_data_home != NULL)
        tll_push_back(ret, strdup(xdg_data_home));
    else {
        static const char *const local = ".local/share";
        const struct passwd *pw = getpwuid(getuid());

        char *path = malloc(strlen(pw->pw_dir) + 1 + strlen(local) + 1);
        sprintf(path, "%s/%s", pw->pw_dir, local);
        tll_push_back(ret, path);
    }

    const char *_xdg_data_dirs = getenv("XDG_DATA_DIRS");

    if (_xdg_data_dirs != NULL) {

        char *ctx = NULL;
        char *copy = strdup(_xdg_data_dirs);

        for (const char *tok = strtok_r(copy, ":", &ctx);
             tok != NULL;
             tok = strtok_r(NULL, ":", &ctx))
        {
            tll_push_back(ret, strdup(tok));
        }

        free(copy);
    } else {
        tll_push_back(ret, strdup("/usr/local/share"));
        tll_push_back(ret, strdup("/usr/share"));
    }

    return ret;
}

void
xdg_data_dirs_destroy(xdg_data_dirs_t dirs)
{
    tll_foreach(dirs, it) {
        free(it->item);
        tll_remove(dirs, it);
    }
}
