#include "icon.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <unistd.h>
#include <limits.h>
#include <time.h>

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
timespec_sub(const struct timespec *a, const struct timespec *b,
             struct timespec *res)
{
    res->tv_sec = a->tv_sec - b->tv_sec;
    res->tv_nsec = a->tv_nsec - b->tv_nsec;
    /* tv_nsec may be negative */
    if (res->tv_nsec < 0) {
        res->tv_sec--;
        res->tv_nsec += 1000 * 1000 * 1000;
    }
}

static bool
dir_is_usable(const char *path, const char *context)
{
    if (path == NULL || context == NULL) {
        return false;
    }

    /*
     * Valid names for application context: some icon themes use different
     * names other than applications, for example, Faenza uses "apps".
     */
    static const char *const app_contex[] = { "applications", "apps" };

    for (size_t i = 0; i < sizeof(app_contex)/sizeof(char*); i++) {
        if (strcasecmp(context, app_contex[i]) == 0) {
            return true;
        }
    }

    return false;
}

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

            if (dir_is_usable(section, context)) {
                struct icon_dir dir = {
                    .path = section,
                    .size = size,
                    .min_size = min_size >= 0 ? min_size : size,
                    .max_size = max_size >= 0 ? max_size : size,
                    .scale = scale,
                    .threshold = threshold,
                    .type = type,
                };
                tll_push_back(theme->dirs, dir);
            } else
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

    if (dir_is_usable(section, context)) {
        struct icon_dir dir = {
            .path = section,
            .size = size,
            .min_size = min_size >= 0 ? min_size : size,
            .max_size = max_size >= 0 ? max_size : size,
            .scale = scale,
            .threshold = threshold,
            .type = type,
        };
        tll_push_back(theme->dirs, dir);
    } else
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

static bool
icon_null(struct icon *icon)
{
    icon->type = ICON_NONE;
    return true;
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

static bool
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

static bool
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

static void
icon_reset(struct icon *icon)
{
    switch (icon->type) {
    case ICON_NONE:
        break;

    case ICON_PNG:
#if defined(FUZZEL_ENABLE_PNG_LIBPNG)
        free(pixman_image_get_data(icon->png));
        pixman_image_unref(icon->png);
        icon->png = NULL;
#endif
        break;

    case ICON_SVG:
#if defined(FUZZEL_ENABLE_SVG_LIBRSVG)
        g_object_unref(icon->svg);
        icon->svg = NULL;
#elif defined(FUZZEL_ENABLE_SVG_NANOSVG)
        nsvgDelete(icon->svg);
        icon->svg = NULL;
#endif
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
reload_icon(struct icon *icon, int icon_size, const icon_theme_list_t *themes,
            const xdg_data_dirs_t *xdg_dirs)
{
    const char *name = icon->name;
    icon_reset(icon);

    if (name == NULL)
        return icon_null(icon);

    if (name[0] == '/') {
        if (icon_from_svg(icon, name)) {
            LOG_DBG("%s: absolute path SVG", name);
            return true;
        }

        if (icon_from_png(icon, name)) {
            LOG_DBG("%s: absolute path PNG", name);
            return true;
        }

        return icon_null(icon);
    }

    LOG_DBG("looking for %s (wanted size: %d)", name, icon_size);

    /* For details, see
     * https://specifications.freedesktop.org/icon-theme-spec/icon-theme-spec-latest.html#icon_lookup */

    tll_foreach(*themes, theme_it) {
        const struct icon_theme *theme = &theme_it->item;

        /* Fallback icon to use if there aren’t any exact matches */
        int min_diff = INT_MAX;
        char path_of_min_diff[PATH_MAX]; path_of_min_diff[0] = '\0';

        char theme_relative_path[5 + 1 + strlen(theme->name) + 1];
        strcpy(theme_relative_path, "icons/");
        strcat(theme_relative_path, theme->name);

        /* Assume sorted */
        tll_foreach(theme->dirs, icon_dir_it) {
            const struct icon_dir *icon_dir = &icon_dir_it->item;

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
                    break;

                case ICON_DIR_THRESHOLD:
                    is_exact_match =
                        (size - threshold) <= icon_size &&
                        (size + threshold) >= icon_size;
                    diff = icon_size < (size - threshold)
                        ? (size - threshold) - icon_size
                        : icon_size - (size + threshold);
                    break;

                case ICON_DIR_SCALABLE:
                    is_exact_match =
                        min_size <= icon_size &&
                        max_size >= icon_size;
                    diff = icon_size < min_size
                        ? min_size - icon_size
                        : icon_size - max_size;
                    break;
                }

                if (!is_exact_match && diff >= min_diff) {
                    /* Not a match, and the size diff is worse than
                     * previously found - no need to check if the file
                     * actually exists */
                    continue;
                }

                if (faccessat(xdg_dir->fd, theme_relative_path, R_OK, 0) < 0)
                    continue;

                const size_t len = strlen(xdg_dir->path) + 1 +
                    strlen("icons") + 1 +
                    strlen(theme->name) + 1 +
                    strlen(icon_dir->path) + 1 +
                    strlen(name) + strlen(".png");

                char full_path[len + 1];

                /* Check if a png/svg file exists at all */
                sprintf(full_path, "%s/icons/%s/%s/%s.xxx",
                        xdg_dir->path, theme->name, icon_dir->path, name);

                /* Look for SVGs first, if directory is ‘Scalable’ */
                if (type == ICON_DIR_SCALABLE) {
                    full_path[len - 3] = 's';
                    full_path[len - 2] = 'v';
                    full_path[len - 1] = 'g';
                } else {
                    full_path[len - 3] = 'p';
                    full_path[len - 2] = 'n';
                    full_path[len - 1] = 'g';
                }

                if (access(full_path, R_OK) < 0) {
                    if (full_path[len - 3] == 'p') {
                        full_path[len - 3] = 's';
                        full_path[len - 2] = 'v';
                        full_path[len - 1] = 'g';
                    } else {
                        full_path[len - 3] = 'p';
                        full_path[len - 2] = 'n';
                        full_path[len - 1] = 'g';
                    }

                    if (access(full_path, R_OK) < 0)
                        continue;
                }

                if (!is_exact_match) {
                    /*
                     * Not an exact match, but since file exists,
                     * remember the path and the size diff - we may
                     * use this icon as a fallback if we don’t find an
                     * exact match.
                     */

                    assert(diff < min_diff);

                    strcpy(path_of_min_diff, full_path);
                    min_diff = diff;
                    continue;
                }

                if (full_path[len - 3] == 's' &&
                    icon_from_svg(icon, full_path))
                {
                    LOG_DBG("%s: %s", name, full_path);
                    return true;
                }

                if (full_path[len - 3] == 'p' &&
                    icon_from_png(icon, full_path))
                {
                    LOG_DBG("%s: %s", name, full_path);
                    return true;
                }
            }
        }

        /* No exact matches found - try loading the fallback */
        if (path_of_min_diff[0] != '\0') {
            const size_t len = strlen(path_of_min_diff);
            if (path_of_min_diff[len - 3] == 's' &&
                icon_from_svg(icon, path_of_min_diff))
            {
                return true;
            }

            else if (path_of_min_diff[len - 3] == 'p' &&
                       icon_from_png(icon, path_of_min_diff))
            {
                return true;
            }
        }
    }

    /* Finally, look in XDG_DATA_DIRS/pixmaps */
    tll_foreach(*xdg_dirs, it) {
        char path[strlen(it->item.path) + 1 +
                  strlen("pixmaps") + 1 +
                  strlen(name) + strlen(".svg") + 1];

        /* Try SVG variant first */
        sprintf(path, "%s/pixmaps/%s.svg", it->item.path, name);
        if (icon_from_svg(icon, path))
            return true;

        /* No SVG, look for PNG instead */
        sprintf(path, "%s/pixmaps/%s.png", it->item.path, name);
        if (icon_from_png(icon, path))
            return true;
    }

    icon_null(icon);
    return true;
}

bool
icon_reload_application_icons(icon_theme_list_t themes, int icon_size,
                              struct application_list *applications)
{
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);

    xdg_data_dirs_t xdg_dirs = xdg_data_dirs();

    for (size_t i = 0; i < applications->count; i++) {
        if (!reload_icon(
                &applications->v[i].icon, icon_size, &themes, &xdg_dirs))
        {
            xdg_data_dirs_destroy(xdg_dirs);
            return false;
        }
    }

    xdg_data_dirs_destroy(xdg_dirs);

    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &end);

    struct timespec diff;
    timespec_sub(&end, &start, &diff);
    LOG_WARN("reloaded icons in %lds %09ldns", diff.tv_sec, diff.tv_nsec);
    return true;
}
