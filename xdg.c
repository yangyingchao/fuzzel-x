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

#define LOG_MODULE "xdg"
#define LOG_ENABLE_DBG 0
#include "log.h"

static void
parse_desktop_file(int fd, application_list_t *applications)
{
    FILE *f = fdopen(fd, "r");
    if (f == NULL)
        return;

    bool is_desktop_entry = false;
    char *name = NULL;
    char *exec = NULL;
    char *generic_name = NULL;

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

        else if (strcasecmp(key, "exec") == 0) {
            const char *first = strtok(value, " ");
            exec = strdup(first);
        }

        else if (strcasecmp(key, "genericname") == 0)
            generic_name = strdup(value);

        free(line);
    }

    if (is_desktop_entry && name != NULL && exec != NULL) {
        tll_push_back(
            *applications,
            ((struct application){
                .path = exec, .title = name, .comment = generic_name}));
    } else {
        free(name);
        free(exec);
        free(generic_name);
    }

    fclose(f);
}

static void
scan_dir(int base_fd, application_list_t *applications)
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

            scan_dir(dir_fd, applications);
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
                parse_desktop_file(fd, applications);
                close(fd);
            }
        }
    }

    closedir(d);
}

void
xdg_find_programs(application_list_t *applications)
{
    const char *xdg_data_home = getenv("XDG_DATA_HOME");
    if (xdg_data_home != NULL) {
        int fd = open(xdg_data_home, O_RDONLY);
        if (fd == -1)
            LOG_WARN("%s: failed to open", xdg_data_home);
        else {
            scan_dir(fd, applications);
            close(fd);
        }
    } else {
        const char *home = getenv("HOME");
        char buf[strlen(home) + 1 + strlen(".local/share") + 1];
        sprintf(buf, "%s/%s", home, ".local/share");
        int fd = open(buf, O_RDONLY);
        if (fd == -1)
            LOG_WARN("%s: failed to open", buf);
        else {
            scan_dir(fd, applications);
            close(fd);
        }
    }

    const char *_xdg_data_dirs = getenv("XDG_DATA_DIRS");
    if (_xdg_data_dirs != NULL) {
        char *xdg_data_dirs = strdup(_xdg_data_dirs);
        for (const char *data_dir = strtok(xdg_data_dirs, ":");
             data_dir != NULL;
             data_dir = strtok(NULL, ":"))
        {
            int fd = open(data_dir, O_RDONLY);
            if (fd == -1)
                LOG_WARN("%s: failed to open", data_dir);
            else {
                scan_dir(fd, applications);
                close(fd);
            }
        }

        free(xdg_data_dirs);
    } else {
        static const char *const default_paths[] = {
            "/usr/local/share",
            "/usr/share",
        };

        for (size_t i = 0; i < sizeof(default_paths) / sizeof(default_paths[0]); i++) {
            int fd = open(default_paths[i], O_RDONLY);
            if (fd == -1)
                LOG_WARN("%s: failed to open", default_paths[i]);
            else {
                scan_dir(fd, applications);
                close(fd);
            }
        }
    }
}
