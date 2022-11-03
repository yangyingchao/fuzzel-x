#include "icon.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <unistd.h>
#include <limits.h>

#include <sys/stat.h>
#include <fcntl.h>

#if defined(FUZZEL_ENABLE_PNG_LIBPNG)
 #include "png-fuzzel.h"
#endif

#if defined(FUZZEL_ENABLE_SVG_LIBRSVG)
 #include <librsvg/rsvg.h>
#endif

#if defined(FUZZEL_ENABLE_SVG_NANOSVG)
 #include <nanosvg.h>
#endif

#include <tllist.h>

#define LOG_MODULE "icon"
#define LOG_ENABLE_DBG 0
#include "log.h"
#include "xdg.h"

typedef tll(char *) theme_names_t;

static void
parse_theme(FILE *index, struct icon_theme *theme, theme_names_t *themes_to_load)
{
    char *section = NULL;
    int size = -1;
    int min_size = -1;
    int max_size = -1;
    int scale = 1;
    int threshold = 2;
    char *context = NULL;
    enum icon_dir_type type = ICON_DIR_THRESHOLD;

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

            tll_foreach(theme->dirs, it) {
                struct icon_dir *d = &it->item;

                if (section == NULL || strcmp(d->path, section) != 0)
                    continue;

                d->size = size;
                d->min_size = min_size >= 0 ? min_size : size;
                d->max_size = max_size >= 0 ? max_size : size;
                d->scale = scale;
                d->threshold = threshold;
                d->type = type;
            }

            free(section);
            free(context);

            size = min_size = max_size = -1;
            scale = 1;
            section = NULL;
            context = NULL;
            type = ICON_DIR_THRESHOLD;
            threshold = 2;

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
                tll_push_back(*themes_to_load, strdup(theme_name));
            }
        }

        if (strcasecmp(key, "directories") == 0) {
            char *save = NULL;
            for (const char *d = strtok_r(value, ",", &save);
                 d != NULL;
                 d = strtok_r(NULL, ",", &save))
            {
                struct icon_dir dir = {.path = strdup(d)};
                tll_push_back(theme->dirs, dir);
            }
        }

        else if (strcasecmp(key, "size") == 0)
            sscanf(value, "%d", &size);

        else if (strcasecmp(key, "minsize") == 0)
            sscanf(value, "%d", &min_size);

        else if (strcasecmp(key, "maxsize") == 0)
            sscanf(value, "%d", &max_size);

        else if (strcasecmp(key, "scale") == 0)
            sscanf(value, "%d", &scale);

        else if (strcasecmp(key, "context") == 0)
            context = strdup(value);

        else if (strcasecmp(key, "threshold") == 0)
            sscanf(value, "%d", &threshold);

        else if (strcasecmp(key, "type") == 0) {
            if (strcasecmp(value, "fixed") == 0)
                type = ICON_DIR_FIXED;
            else if (strcasecmp(value, "scalable") == 0)
                type = ICON_DIR_SCALABLE;
            else if (strcasecmp(value, "threshold") == 0)
                type = ICON_DIR_THRESHOLD;
            else {
                LOG_WARN(
                    "ignoring unrecognized icon theme directory type: %s",
                    value);
            }
        }

        free(line);
    }

    tll_foreach(theme->dirs, it) {
        struct icon_dir *d = &it->item;

        if (section == NULL || strcmp(d->path, section) != 0)
            continue;

        d->size = size;
        d->min_size = min_size >= 0 ? min_size : size;
        d->max_size = max_size >= 0 ? max_size : size;
        d->scale = scale;
        d->threshold = threshold;
        d->type = type;
    }

    tll_foreach(theme->dirs, it) {
        if (it->item.size == 0) {
            free(it->item.path);
            tll_remove(theme->dirs, it);
        }
    }

    free(section);
    free(context);
}

static bool
load_theme_in(const char *dir, struct icon_theme *theme,
              theme_names_t *themes_to_load)
{
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/index.theme", dir);

    FILE *index = fopen(path, "r");
    if (index == NULL)
        return false;

    parse_theme(index, theme, themes_to_load);
    fclose(index);
    return true;
}

icon_theme_list_t
icon_load_theme(const char *name)
{
    /* List of themes; first item is the primary theme, subsequent
     * items are inherited items (i.e. fallback themes) */
    icon_theme_list_t themes = tll_init();

    /* List of themes to try to load. This list will be appended to as
     * we go, and find 'Inherits' values in the theme index files. */
    theme_names_t themes_to_load = tll_init();
    tll_push_back(themes_to_load, strdup(name));

    xdg_data_dirs_t dirs = xdg_data_dirs();

    while (tll_length(themes_to_load) > 0) {
        char *theme_name = tll_pop_front(themes_to_load);

        /*
         * Check if we've already loaded this theme. Example:
         * "Arc" inherits "Moka,Faba,elementary,Adwaita,ghome,hicolor
         * "Moka" inherits "Faba"
         * "Faba" inherits "elementary,gnome,hicolor"
         */
        bool theme_already_loaded = false;
        tll_foreach(themes, it) {
            if (strcasecmp(it->item.name, theme_name) == 0) {
                theme_already_loaded = true;
                break;
            }
        }

        if (theme_already_loaded) {
            free(theme_name);
            continue;
        }

        tll_foreach(dirs, dir_it) {
            char path[strlen(dir_it->item.path) + 1 +
                      strlen("icons") + 1 +
                      strlen(theme_name) + 1];
            sprintf(path, "%s/icons/%s", dir_it->item.path, theme_name);

            struct icon_theme theme = {0};
            if (load_theme_in(path, &theme, &themes_to_load)) {
                theme.name = strdup(theme_name);
                tll_push_back(themes, theme);
            }
        }

        free(theme_name);
    }

    xdg_data_dirs_destroy(dirs);
    return themes;
}

static void
theme_destroy(struct icon_theme theme)
{
    free(theme.name);

    tll_foreach(theme.dirs, it) {
        free(it->item.path);
        tll_remove(theme.dirs, it);
    }
}

void
icon_themes_destroy(icon_theme_list_t themes)
{
    tll_foreach(themes, it) {
        theme_destroy(it->item);
        tll_remove(themes, it);
    }
}

#if defined(FUZZEL_ENABLE_PNG_LIBPNG)
static bool
icon_from_png_libpng(struct icon *icon, const char *file_name)
{
    pixman_image_t *png = png_load(file_name);
    if (png == NULL)
        return false;

    icon->type = ICON_PNG;
    icon->png = png;
    return true;
}
#endif

bool
icon_from_png(struct icon *icon, const char *name)
{
#if defined(FUZZEL_ENABLE_PNG_LIBPNG)
    return icon_from_png_libpng(icon, name);
#else
    return false;
#endif
}

#if defined(FUZZEL_ENABLE_SVG_LIBRSVG)
static bool
icon_from_svg_librsvg(struct icon *icon, const char *file_name)
{
    RsvgHandle *svg = rsvg_handle_new_from_file(file_name, NULL);
    if (svg == NULL)
        return false;

    icon->type = ICON_SVG;
    icon->svg = svg;
    return true;
}
#endif

#if defined(FUZZEL_ENABLE_SVG_NANOSVG)
static bool
icon_from_svg_nanosvg(struct icon *icon, const char *file_name)
{
    /* TODO: DPI */
    NSVGimage *svg = nsvgParseFromFile(file_name, "px", 96);
    if (svg == NULL)
        return false;

    if (svg->width == 0 || svg->height == 0)  {
        nsvgDelete(svg);
        return false;
    }

    icon->type = ICON_SVG;
    icon->svg = svg;
    return true;
}
#endif

bool
icon_from_svg(struct icon *icon, const char *name)
{
#if defined(FUZZEL_ENABLE_SVG_LIBRSVG)
    return icon_from_svg_librsvg(icon, name);
#elif defined(FUZZEL_ENABLE_SVG_NANOSVG)
    return icon_from_svg_nanosvg(icon, name);
#else
    return false;
#endif
}

static bool
svg(struct icon *icon, const char *path)
{
    icon->path = strdup(path);
    icon->type = ICON_SVG;
    icon->svg = NULL;
    return true;
}

static bool
png(struct icon *icon, const char *path)
{
    icon->path = strdup(path);
    icon->type = ICON_PNG;
    icon->png = NULL;
    return true;
}

static void
icon_reset(struct icon *icon)
{
    free(icon->path);
    icon->path = NULL;

    switch (icon->type) {
    case ICON_NONE:
        break;

    case ICON_PNG:
        if (icon->png != NULL) {
#if defined(FUZZEL_ENABLE_PNG_LIBPNG)
            free(pixman_image_get_data(icon->png));
            pixman_image_unref(icon->png);
            icon->png = NULL;
#endif
        }
        break;

    case ICON_SVG:
        if (icon->svg != NULL) {
#if defined(FUZZEL_ENABLE_SVG_LIBRSVG)
            g_object_unref(icon->svg);
#elif defined(FUZZEL_ENABLE_SVG_NANOSVG)
            nsvgDelete(icon->svg);
#endif
            icon->svg = NULL;
        }
        break;
    }

    tll_foreach(icon->rasterized, it) {
        struct rasterized *rast = &it->item;
        free(pixman_image_get_data(rast->pix));
        pixman_image_unref(rast->pix);
        tll_remove(icon->rasterized, it);
    }

    icon->type = ICON_NONE;
}

static bool
lookup_icons(const icon_theme_list_t *themes, int icon_size,
             struct application_list *applications,
             const xdg_data_dirs_t *xdg_dirs)
{
    struct icon_data {
        const char *name;
        struct application *app;

        char *file_name;
        size_t file_name_len;

        struct {
            int diff;
            const struct xdg_data_dir *xdg_dir;
            const struct icon_theme *theme;
            const struct icon_dir *icon_dir;
            enum icon_type type;
        } min_diff;
    };

    tll(struct icon_data) icons = tll_init();

    for (size_t i = 0; i < applications->count; i++) {
        struct application *app = &applications->v[i];
        icon_reset(&app->icon);

        if (app->icon.name == NULL)
            continue;

        if (app->icon.name[0] == '/') {
            if (svg(&app->icon, app->icon.name))
                LOG_DBG("%s: absolute path SVG", app->icon.name);
            else if (png(&app->icon, app->icon.name))
                LOG_DBG("%s: abslute path PNG", app->icon.name);
        } else {
            size_t file_name_len = strlen(app->icon.name) + 4;
            char *file_name = malloc(file_name_len + 1);
            strcpy(file_name, app->icon.name);
            strcat(file_name, ".xxx");

            struct icon_data data = {
                .name = app->icon.name,
                .app = app,
                .file_name = file_name,
                .file_name_len = file_name_len,
                .min_diff = {.diff = INT_MAX},
            };
            tll_push_back(icons, data);
        }
    }

    /* For details, see
     * https://specifications.freedesktop.org/icon-theme-spec/icon-theme-spec-latest.html#icon_lookup */

    tll_foreach(*themes, theme_it) {
        const struct icon_theme *theme = &theme_it->item;

        /* Fallback icon to use if there aren’t any exact matches */
        /* Assume sorted */
        tll_foreach(theme->dirs, icon_dir_it) {
            const struct icon_dir *icon_dir = &icon_dir_it->item;

            char theme_relative_path[
                5 + 1 + /* “icons” */
                strlen(theme->name) + 1 +
                strlen(icon_dir->path) + 1];

            sprintf(theme_relative_path, "icons/%s/%s",
                    theme->name, icon_dir->path);

            tll_foreach(*xdg_dirs, xdg_dir_it) {
                const struct xdg_data_dir *xdg_dir = &xdg_dir_it->item;

                const int scale = icon_dir->scale;
                const int size = icon_dir->size * scale;
                const int min_size = icon_dir->min_size * scale;
                const int max_size = icon_dir->max_size * scale;
                const int threshold = icon_dir->threshold * scale;
                const enum icon_dir_type type = icon_dir->type;

                bool is_exact_match = false;;
                int diff = INT_MAX;

                /* See if this directory is usable for the requested icon size */
                switch (type) {
                case ICON_DIR_FIXED:
                    is_exact_match = size == icon_size;
                    diff = abs(size - icon_size);
                    LOG_DBG(
                        "%s/%s (fixed): "
                        "icon-size=%d, size=%d, exact=%d, diff=%d",
                        xdg_dir->path, theme_relative_path, icon_size, size,
                        is_exact_match, diff);
                    break;

                case ICON_DIR_THRESHOLD:
                    is_exact_match =
                        (size - threshold) <= icon_size &&
                        (size + threshold) >= icon_size;
                    diff = icon_size < (size - threshold)
                        ? min_size - icon_size
                        : (icon_size > (size + threshold)
                           ? icon_size - max_size
                           : 0);
                    LOG_DBG(
                        "%s/%s (threshold): "
                        "icon-size=%d, threshold=%d, exact=%d, diff=%d",
                        xdg_dir->path, theme_relative_path, icon_size, threshold,
                        is_exact_match, diff);
                    break;

                case ICON_DIR_SCALABLE:
                    is_exact_match =
                        min_size <= icon_size &&
                        max_size >= icon_size;
                    diff = icon_size < min_size
                        ? min_size - icon_size
                        : (icon_size > max_size
                           ? icon_size - max_size
                           : 0);
                    LOG_DBG("%s/%s (scalable): "
                            "icon-size=%d, min=%d, max=%d, exact=%d, diff=%d",
                            xdg_dir->path, theme_relative_path, icon_size,
                            min_size, max_size, is_exact_match, diff);
                    break;
                }

                int dir_fd = openat(
                    xdg_dir->fd, theme_relative_path, O_RDONLY | O_DIRECTORY);
                if (dir_fd < 0)
                    continue;

                tll_foreach(icons, icon_it) {
                    struct icon_data *icon = &icon_it->item;

                    if (!is_exact_match && icon->min_diff.diff <= diff)
                        continue;

                    size_t len = icon->file_name_len;
                    char *path = icon->file_name;
                    path[len - 4] = '.';
                    path[len - 3] = 'p';
                    path[len - 2] = 'n';
                    path[len - 1] = 'g';

                    if (faccessat(dir_fd, path, R_OK, 0) < 0) {
                        path[len - 3] = 's';
                        path[len - 2] = 'v';
                        path[len - 1] = 'g';
                        if (faccessat(dir_fd, path, R_OK, 0) < 0)
                            continue;
                    }

                    if (!is_exact_match) {
                        assert(diff < icon->min_diff.diff);
                        icon->min_diff.diff = diff;
                        icon->min_diff.xdg_dir = xdg_dir;
                        icon->min_diff.theme = theme;
                        icon->min_diff.icon_dir = icon_dir;
                        icon->min_diff.type = path[len - 3] == 's'
                            ? ICON_SVG : ICON_PNG;
                        continue;
                    }

                    char *full_path = malloc(
                        strlen(xdg_dir->path) + 1 +
                        5 + 1 + /* “icons” */
                        strlen(theme->name) + 1 +
                        strlen(icon_dir->path) + 1 +
                        len + 1);

                    sprintf(full_path, "%s/icons/%s/%s/%s",
                            xdg_dir->path, theme->name, icon_dir->path, path);

                    if ((path[len - 3] == 's' &&
                         svg(&icon->app->icon, full_path)) ||
                        (path[len - 3] == 'p' &&
                         png(&icon->app->icon, full_path)))
                    {
                        LOG_DBG("%s: %s", icon->name, full_path);
                        free(icon->file_name);
                        tll_remove(icons, icon_it);
                    }

                    free(full_path);
                }

                close(dir_fd);
            }
        }

        /* Try loading fallbacks for those icons we didn’t find an
         * exact match */
        tll_foreach(icons, icon_it) {
            struct icon_data *icon = &icon_it->item;

            if (icon->min_diff.type == ICON_NONE) {
                assert(icon->min_diff.xdg_dir == NULL);
                continue;
            }

            size_t path_len =
                strlen(icon->min_diff.xdg_dir->path) + 1 +
                5 + 1 + /* “icons” */
                strlen(icon->min_diff.theme->name) + 1 +
                strlen(icon->min_diff.icon_dir->path) + 1 +
                strlen(icon->name) + 4;

            char full_path[path_len + 1];
            sprintf(full_path, "%s/icons/%s/%s/%s.%s",
                    icon->min_diff.xdg_dir->path,
                    icon->min_diff.theme->name,
                    icon->min_diff.icon_dir->path,
                    icon->name,
                    icon->min_diff.type == ICON_SVG ? "svg" : "png");

            if ((icon->min_diff.type == ICON_SVG &&
                 svg(&icon->app->icon, full_path)) ||
                (icon->min_diff.type == ICON_PNG &&
                 png(&icon->app->icon, full_path)))
            {
                LOG_DBG("%s: %s (fallback)", icon->name, full_path);
                free(icon->file_name);
                tll_remove(icons, icon_it);
            } else {
                /* Reset diff data, before checking the parent theme(s) */
                icon->min_diff.diff = INT_MAX;
                icon->min_diff.xdg_dir = NULL;
                icon->min_diff.theme = NULL;
                icon->min_diff.icon_dir = NULL;
                icon->min_diff.type = ICON_NONE;
            }
        }
    }

    /* Finally, look in XDG_DATA_DIRS/pixmaps */
    tll_foreach(icons, icon_it) {
        const struct icon_data *icon = &icon_it->item;

        tll_foreach(*xdg_dirs, it) {
            int pixmaps_fd = openat(it->item.fd, "pixmaps", O_RDONLY);
            if (pixmaps_fd < 0)
                continue;

            size_t len = icon->file_name_len;
            char *path = icon->file_name;
            path[len - 3] = 's';
            path[len - 2] = 'v';
            path[len - 1] = 'g';

            if (faccessat(pixmaps_fd, path, R_OK, 0) < 0) {
                path[len - 3] = 'p';
                path[len - 2] = 'n';
                path[len - 1] = 'g';

                if (faccessat(pixmaps_fd, path, R_OK, 0) < 0) {
                    close(pixmaps_fd);
                    continue;
                }
            }

            close(pixmaps_fd);

            char full_path[strlen(it->item.path) + 1 +
                           strlen("pixmaps") + 1 +
                           len + 1];

            /* Try SVG variant first */
            sprintf(full_path, "%s/pixmaps/%s", it->item.path, path);
            if (path[len - 3] == 's' && svg(&icon->app->icon, full_path)) {
                LOG_DBG("%s: %s (pixmaps)", icon->name, full_path);
                break;
            }

            /* No SVG, look for PNG instead */
            if (path[len - 3] == 'p' && png(&icon->app->icon, full_path)) {
                LOG_DBG("%s: %s (pixmaps)", icon->name, full_path);
                break;
            }

        }

        free(icon->file_name);
        tll_remove(icons, icon_it);
    }

    return true;
}

bool
icon_lookup_application_icons(icon_theme_list_t themes, int icon_size,
                              struct application_list *applications)
{
    xdg_data_dirs_t xdg_dirs = xdg_data_dirs();
    lookup_icons(&themes, icon_size, applications, &xdg_dirs);
    xdg_data_dirs_destroy(xdg_dirs);

    return true;
}
