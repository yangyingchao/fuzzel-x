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

#include <librsvg/rsvg.h>

#define LOG_MODULE "xdg"
#define LOG_ENABLE_DBG 0
#include "log.h"
#include "icon.h"

typedef tll(struct application) application_llist_t;

static struct icon
icon_null(void)
{
    return (struct icon){.type = ICON_NONE};
}

static struct icon
icon_from_surface(cairo_surface_t *surf)
{
    return (struct icon){.type = ICON_SURFACE, .surface = surf};
}

static struct icon
icon_from_svg(RsvgHandle *svg)
{
    return (struct icon){.type = ICON_SVG, .svg = svg};
}

static struct icon
load_icon(const char *name, int icon_size, icon_theme_list_t themes)
{
    if (name == NULL)
        return icon_null();

    if (name[0] == '/') {
        cairo_surface_t *surf = cairo_image_surface_create_from_png(name);
        if (cairo_surface_status(surf) == CAIRO_STATUS_SUCCESS)
            return icon_from_surface(surf);

        return icon_null();
    }

    LOG_DBG("looking for %s (wanted size: %d)", name, icon_size);

    tll_foreach(themes, theme_it) {
        const struct icon_theme *theme = &theme_it->item;
        int min_diff = 10000;

        /* Assume sorted */
        for (size_t i = 0; i < 4; i++) {
            tll_foreach(theme->dirs, it) {
                const int size = it->item.size * it->item.scale;
                const int min_size = it->item.min_size * it->item.scale;
                const int max_size = it->item.max_size * it->item.scale;
                const bool scalable = it->item.scalable;

                const size_t len = strlen(theme->path) + 1 +
                    strlen(it->item.path) + 1 +
                    strlen(name) + strlen(".png") + 1;

                /* Check if a png/svg file exists at all */
                char *full_path = malloc(len);
                sprintf(full_path, "%s/%s/%s.png", theme->path, it->item.path, name);
                if (access(full_path, O_RDONLY) == -1) {
                    /* Also check for svg variant */
                    full_path[len - 4] = 's';
                    full_path[len - 3] = 'v';
                    full_path[len - 2] = 'g';
                    if (access(full_path, O_RDONLY) == -1) {
                        free(full_path);
                        continue;
                    }
                }

                const int diff = scalable ? 0 : abs(size - icon_size);
                if (i == 0 && diff != 0) {
                    /* Looking for *exactly* our wanted size */
                    if (diff < min_diff)
                        min_diff = diff;
                    free(full_path);
                    continue;
                } else if (i == 1 && diff != min_diff) {
                    /* Try the one which matches most closely */
                    free(full_path);
                    continue;
                } else if (i == 2 && (icon_size < min_size ||
                                      icon_size > max_size))
                {
                    /* Find one whose scalable range we're in */
                    free(full_path);
                    continue;
                } else {
                    /* Use anyone available */
                }

                RsvgHandle *svg = rsvg_handle_new_from_file(full_path, NULL);
                if (svg != NULL) {
                    LOG_DBG("%s: %s scalable", name, full_path);
                    free(full_path);
                    return icon_from_svg(svg);
                }

                cairo_surface_t *surf = cairo_image_surface_create_from_png(full_path);
                if (cairo_surface_status(surf) == CAIRO_STATUS_SUCCESS) {
                    if (scalable)
                        LOG_DBG("%s: %s: scalable", name, full_path);
                    else if (i == 0)
                        LOG_DBG("%s: %s: exact match", name, full_path);
                    else if (i == 1)
                        LOG_DBG("%s: %s: diff = %d", name, full_path, diff);
                    else if (i == 2)
                        LOG_DBG("%s: %s: range %d-%d",
                                name, full_path, min_size, max_size);
                    else
                        LOG_DBG("%s: %s: nothing else matched", name, full_path);

                    free(full_path);
                    return icon_from_surface(surf);
                }

                free(full_path);
                cairo_surface_destroy(surf);
            }
        }
    }

    xdg_data_dirs_t dirs = xdg_data_dirs();
    tll_foreach(dirs, it) {
        char path[strlen(it->item) + 1 +
                  strlen("pixmaps") + 1 +
                  strlen(name) + strlen(".svg") + 1];

        /* Try SVG variant first */
        sprintf(path, "%s/pixmaps/%s.svg", it->item, name);
        RsvgHandle *svg = rsvg_handle_new_from_file(path, NULL);
        if (svg != NULL) {
            xdg_data_dirs_destroy(dirs);
            return icon_from_svg(svg);
        }

        /* No SVG, look for PNG instead */
        sprintf(path, "%s/pixmaps/%s.png", it->item, name);
        cairo_surface_t *surf = cairo_image_surface_create_from_png(path);
        if (cairo_surface_status(surf) == CAIRO_STATUS_SUCCESS) {
            xdg_data_dirs_destroy(dirs);
            return icon_from_surface(surf);
        }
        cairo_surface_destroy(surf);
    }
    xdg_data_dirs_destroy(dirs);

    return icon_null();
}

static void
parse_desktop_file(int fd, icon_theme_list_t themes, int icon_size,
                   application_llist_t *applications)
{
    FILE *f = fdopen(fd, "r");
    if (f == NULL)
        return;

    bool is_desktop_entry = false;
    char *name = NULL;
    char *exec = NULL;
    char *path = NULL;
    char *generic_name = NULL;
    char *icon = NULL;
    bool visible = true;

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
        char *value = strtok(NULL, "\n");

        if (key != NULL && value != NULL) {
            if (strcmp(key, "Name") == 0)
                name = strdup(value);

            else if (strcmp(key, "Exec") == 0)
                exec = strdup(value);

            else if (strcmp(key, "Path") == 0)
                path = strdup(value);

            else if (strcmp(key, "GenericName") == 0)
                generic_name = strdup(value);

            else if (strcmp(key, "Icon") == 0)
                icon = strdup(value);

            else if (strcmp(key, "Hidden") == 0 ||
                     strcmp(key, "NoDisplay") == 0)
            {
                if (strcmp(value, "true") == 0)
                    visible = false;
            }
        }

        free(line);
    }

    fclose(f);

    if (is_desktop_entry && visible && name != NULL && exec != NULL) {
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
                    .path = path, .exec = exec, .title = name,
                    .comment = generic_name,
                    .icon = load_icon(icon, icon_size, themes),
                    .count = 0}));
            free(icon);
            return;
        }
    }

    free(path);
    free(name);
    free(exec);
    free(generic_name);
    free(icon);
}

static void
scan_dir(int base_fd, icon_theme_list_t themes, int icon_size,
         application_llist_t *applications)
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

            scan_dir(dir_fd, themes, icon_size, applications);
            close(dir_fd);
        } else if (S_ISREG(st.st_mode)) {
            /* Skip files not ending with ".desktop" */
            const size_t desktop_len = strlen(".desktop");
            const size_t name_len = strlen(e->d_name);
            if (name_len < desktop_len)
                continue;

            if (strcmp(&e->d_name[name_len - desktop_len], ".desktop") != 0)
                continue;

            //LOG_DBG("%s", e->d_name);
            int fd = openat(base_fd, e->d_name, O_RDONLY);
            if (fd == -1)
                LOG_WARN("%s: failed to open", e->d_name);
            else {
                parse_desktop_file(fd, themes, icon_size, applications);
                close(fd);
            }
        }

    }

    closedir(d);
}

static int
sort_application_by_title(const void *_a, const void *_b)
{
    const struct application *a = _a;
    const struct application *b = _b;
    return strcmp(a->title, b->title);
}

void
xdg_find_programs(int icon_size, struct application_list *applications)
{
    icon_theme_list_t themes = icon_load_theme("Arc");
    if (tll_length(themes) > 0)
        LOG_INFO("theme: %s", tll_front(themes).path);
    else
        LOG_WARN("%s: icon theme not found", "Arc");

    application_llist_t apps = tll_init();

    xdg_data_dirs_t dirs = xdg_data_dirs();
    tll_foreach(dirs, it) {
        char path[strlen(it->item) + 1 + strlen("applications") + 1];
        sprintf(path, "%s/applications", it->item);

        int fd = open(path, O_RDONLY);
        if (fd != -1) {
            scan_dir(fd, themes, icon_size, &apps);
            close(fd);
        }
    }

    applications->count = tll_length(apps);
    applications->v = malloc(tll_length(apps) * sizeof(applications->v[0]));

    size_t i = 0;
    tll_foreach(apps, it) {
        applications->v[i++] = it->item;
        tll_remove(apps, it);
    }
    tll_free(apps);

    qsort(applications->v, applications->count, sizeof(applications->v[0]),
          &sort_application_by_title);

    xdg_data_dirs_destroy(dirs);
    icon_themes_destroy(themes);
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

const char *
xdg_cache_dir(void)
{
    const char *xdg_cache_home = getenv("XDG_CACHE_HOME");
    if (xdg_cache_home != NULL)
        return xdg_cache_home;

    static char path[PATH_MAX];
    const struct passwd *pw = getpwuid(getuid());
    snprintf(path, sizeof(path), "%s/.cache", pw->pw_dir);
    return path;
}
