#include "xdg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <limits.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <pwd.h>

#define LOG_MODULE "xdg"
#define LOG_ENABLE_DBG 0
#include "log.h"
#include "char32.h"
#include "icon.h"

typedef tll(struct application) application_llist_t;

static void
parse_desktop_file(int fd, char *id, const char32_t *file_basename,
                   const char *terminal, bool include_actions,
                   application_llist_t *applications)
{
    FILE *f = fdopen(fd, "r");
    if (f == NULL)
        return;

    bool is_desktop_entry = false;

    tll(char *) action_names = tll_init();

    struct action {
        char32_t *name;
        char32_t *generic_name;
        char32_t *comment;
        char32_list_t keywords;
        char32_list_t categories;

        char *icon;
        char *exec;
        char32_t *wexec;

        char *path;
        bool visible;
        bool use_terminal;
    };

    tll(struct action) actions = tll_init();
    tll_push_back(actions, ((struct action){.visible = true}));
    struct action *action = &tll_back(actions);
    struct action *default_action = action;

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
            }

            else if (include_actions &&
                     strncasecmp(&line[1], "desktop action ", 15) == 0)
            {
                const char *action_name = &line[16];
                const size_t name_len = len - 1 - 16;

                bool action_is_valid = false;
                tll_foreach(action_names, it) {
                    if (strlen(it->item) == name_len &&
                        strncmp(it->item, action_name, name_len) == 0)
                    {
                        tll_push_back(actions, ((struct action){0}));
                        action = &tll_back(actions);

                        action->generic_name =
                            default_action->generic_name != NULL
                            ? c32dup(default_action->generic_name) : NULL;
                        action->comment = default_action->comment != NULL
                            ? c32dup(default_action->comment) : NULL;
                        action->path = default_action->path != NULL
                            ? strdup(default_action->path) : NULL;
                        action->icon = default_action->icon != NULL
                            ? strdup(default_action->icon) : NULL;
                        action->visible = default_action->visible;
                        action->use_terminal = default_action->use_terminal;

                        tll_foreach(default_action->keywords, it)
                            tll_push_back(action->keywords, c32dup(it->item));
                        tll_foreach(default_action->categories, it)
                            tll_push_back(action->categories, c32dup(it->item));

                        action_is_valid = true;
                        break;
                    }
                }

                free(line);
                if (action_is_valid)
                    continue;
                else
                    break;
            }

            else {
                free(line);
                break;
            }
        }

        const char *key = strtok(line, "=");
        char *value = strtok(NULL, "\n");

        if (key != NULL && value != NULL) {
            if (strcmp(key, "Name") == 0) {
                free(action->name);
                action->name = ambstoc32(value);
            }

            else if (strcmp(key, "Exec") == 0) {
                free(action->exec);
                free(action->wexec);
                action->exec = strdup(value);
                action->wexec = ambstoc32(value);
            }

            else if (strcmp(key, "Path") == 0) {
                free(action->path);
                action->path = strdup(value);
            }

            else if (strcmp(key, "GenericName") == 0) {
                free(action->generic_name);
                action->generic_name = ambstoc32(value);
            }

            else if (strcmp(key, "Comment") == 0) {
                free(action->comment);
                action->comment = ambstoc32(value);
            }

            else if (strcmp(key, "Keywords") == 0) {
                for (const char *kw = strtok(value, ";");
                     kw != NULL;
                     kw = strtok(NULL, ";"))
                {
                    char32_t *wide_kw = ambstoc32(kw);
                    if (wide_kw != NULL)
                        tll_push_back(action->keywords, wide_kw);
                }
            }

            else if (strcmp(key, "Categories") == 0) {
                for (const char *category = strtok(value, ";");
                     category != NULL;
                     category = strtok(NULL, ";"))
                {
                    char32_t *wide_category = ambstoc32(category);
                    if (wide_category != NULL)
                        tll_push_back(action->categories, wide_category);
                }
            }

            else if (strcmp(key, "Actions") == 0) {
                for (const char *action = strtok(value, ";");
                     action != NULL;
                     action = strtok(NULL, ";"))
                {
                    tll_push_back(action_names, strdup(action));
                }
            }

            else if (strcmp(key, "Icon") == 0) {
                free(action->icon);
                action->icon = strdup(value);
            }

            else if (strcmp(key, "Hidden") == 0 ||
                     strcmp(key, "NoDisplay") == 0)
            {
                if (strcmp(value, "true") == 0)
                    action->visible = false;
            }

            else if (strcmp(key, "Terminal") == 0) {
                if (strcmp(value, "true") == 0)
                    action->use_terminal = true;
            }
        }

        free(line);
    }

    fclose(f);

    tll_foreach(actions, it) {
        struct action *a = &it->item;

        if (!(is_desktop_entry &&
              a->name != NULL &&
              a->exec != NULL))
        {
            free(a->name);
            free(a->generic_name);
            free(a->comment);
            free(a->icon);
            free(a->exec);
            free(a->wexec);
            free(a->path);

            tll_free_and_free(a->keywords, free);
            tll_free_and_free(a->categories, free);

            continue;
        }

        if (a->use_terminal && terminal != NULL) {
            char *exec_with_terminal = malloc(
                strlen(terminal) + 1 + strlen(a->exec) + 1);
            strcpy(exec_with_terminal, terminal);
            strcat(exec_with_terminal, " ");
            strcat(exec_with_terminal, a->exec);
            free(a->exec);
            a->exec = exec_with_terminal;
        }

        char32_t *title = a->name;
        if (a != default_action) {
            size_t title_len = c32len(default_action->name) +
                3 +  /* “ - “ */
                c32len(a->name) +
                1;
            title = malloc(title_len * sizeof(char32_t));

            c32cpy(title, default_action->name);
            c32cat(title, U" - ");
            c32cat(title, a->name);
            free(a->name);
        }

        tll_push_back(
            *applications,
            ((struct application){
                .id = strdup(id),
                .path = a->path,
                .exec = a->exec,
                .basename = c32dup(file_basename),
                .wexec = a->wexec,
                .title = title,
                .generic_name = a->generic_name,
                .comment = a->comment,
                .keywords = a->keywords,
                .categories = a->categories,
                .icon = {.name = a->icon},
                .visible = a->visible,
                .count = 0}));
    }

    free(id);
    tll_free(actions);
    tll_free_and_free(action_names, free);
}

static char *
new_id(const char *base_id, const char *new_part)
{
    if (base_id == NULL)
        return strdup(new_part);

    size_t len = strlen(base_id) + 1 + strlen(new_part) + 1;
    char *id = malloc(len);

    strcpy(id, base_id);
    strcat(id, "-");
    strcat(id, new_part);

    return id;
}

static void
scan_dir(int base_fd, const char *terminal, bool include_actions,
         application_llist_t *applications, const char *base_id)
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

            char *nested_base_id = new_id(base_id, e->d_name);
            scan_dir(dir_fd, terminal, include_actions,
                     applications, nested_base_id);
            free(nested_base_id);
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
                const char *file_basename = strrchr(e->d_name, '/');
                if (file_basename == NULL)
                    file_basename = e->d_name;
                else
                    file_basename++;

                const char *extension = strchr(file_basename, '.');
                if (extension == NULL)
                    extension = file_basename + strlen(file_basename);

                int chars = mbsntoc32(NULL, file_basename, extension - file_basename, 0);
                assert(chars >= 0);

                char32_t wfile_basename[chars + 1];
                mbsntoc32(wfile_basename, file_basename, extension - file_basename, chars);
                wfile_basename[chars] = U'\0';

                char *id = new_id(base_id, e->d_name);

                bool already_added = false;
                tll_foreach(*applications, it) {
                    if (strcmp(it->item.id, id) == 0) {
                        already_added = true;
                        break;
                    }
                }

                if (!already_added) {
                    parse_desktop_file(
                        fd, id, wfile_basename, terminal, include_actions,
                        applications);
                } else
                    free(id);
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
    return c32cmp(a->title, b->title);
}

void
xdg_find_programs(const char *terminal, bool include_actions,
                  struct application_list *applications)
{
    application_llist_t apps = tll_init();

    xdg_data_dirs_t dirs = xdg_data_dirs();
    tll_foreach(dirs, it) {
        char path[strlen(it->item) + 1 + strlen("applications") + 1];
        sprintf(path, "%s/applications", it->item);

        int fd = open(path, O_RDONLY);
        if (fd != -1) {
            scan_dir(fd, terminal, include_actions, &apps, NULL);
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

#if defined(_DEBUG) && LOG_ENABLE_DBG && 0
    for (size_t i = 0; i < applications->count; i++) {
        const struct application *app = &applications->v[i];

        char32_t keywords[1024];
        char32_t categories[1024];

        int idx = 0;
        tll_foreach(app->keywords, it) {
            idx += swprintf(&keywords[idx],
                            (sizeof(keywords) / sizeof(keywords[0])) - idx,
                            L"%ls, ", (const wchar_t *)it->item);
        }

        if (idx > 0)
            keywords[idx - 2] = U'\0';

        idx = 0;
        tll_foreach(app->categories, it) {
            idx += swprintf(&categories[idx],
                            (sizeof(categories) / sizeof(categories[0])) - idx,
                            L"%ls, ", (const wchar_t *)it->item);
        }

        if (idx > 0)
            categories[idx - 2] = U'\0';

        LOG_DBG("%s:\n"
                "  name/title:   %ls\n"
                "  path:         %s\n"
                "  exec:         %s\n"
                "  basename:     %ls\n"
                "  generic-name: %ls\n"
                "  comment:      %ls\n"
                "  keywords:     %ls\n"
                "  categories:   %ls\n"
                "  icon:\n"
                "    name: %s\n"
                "    type: %s",
                app->id, (const wchar_t *)app->title, app->path, app->exec,
                (const wchar_t *)app->basename,
                (const wchar_t *)app->generic_name,
                (const wchar_t *)app->comment,
                (const wchar_t *)keywords,
                (const wchar_t *)categories,
                app->icon.name,
                app->icon.type == ICON_PNG ? "PNG" :
                app->icon.type == ICON_SVG ? "SVG" : "<none>");


    }
#endif
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
