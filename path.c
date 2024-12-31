#include "path.h"
#include "application.h"
#include "tllist.h"

#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdlib.h>
#include <stdio.h>

#define LOG_MODULE "path"
#define LOG_ENABLE_DBG 0
#include "log.h"
#include "xdg.h"
#include "char32.h"

void
path_find_programs(struct application_list *applications)
{
    const char *_path = getenv("PATH");

    if (_path == NULL)
        return ;

    char *copy = strdup(_path);
    char *ctx = NULL;
    tll(struct application *) entries = tll_init();

    for (const char *tok = strtok_r(copy, ":", &ctx);
         tok != NULL;
         tok = strtok_r(NULL, ":", &ctx))
    {
        DIR *d = opendir(tok);
        if (d == NULL) {
            LOG_WARN("failed to open %s from PATH", tok);
            continue;
        }
        int fd = dirfd(d);
        if (fd == -1) {
            LOG_WARN("failed to open %s's fd from PATH", tok);
            closedir(d);
            continue;
        }
        const size_t path_size = strlen(tok);
        for (const struct dirent *e = readdir(d); e != NULL; e = readdir(d)) {
            if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
                continue;

            struct stat st;
            if (fstatat(fd, e->d_name, &st, 0) == -1) {
                LOG_WARN("%s: failed to stat", e->d_name);
                continue;
            }
            if (S_ISREG(st.st_mode) && st.st_mode & S_IXUSR) {
                char32_t *wtitle = ambstoc32(e->d_name);
                if (wtitle == NULL)
                    continue;
                bool already_exist = false;
                tll_foreach(entries, it) {
                    if (c32cmp(wtitle, it->item->title) == 0) {
                        already_exist = true;
                        break;
                    }
                };
                if (already_exist) {
                    free(wtitle);
                    continue;
                }
                size_t exec_size = path_size + 1 + strlen(e->d_name) + 1;
                char *exec = malloc(exec_size);
                snprintf(exec, exec_size, "%s/%s", tok, e->d_name);

                char32_t *lowercase = c32dup(wtitle);
                for (size_t i = 0; i < c32len(lowercase); i++)
                    lowercase[i] = toc32lower(lowercase[i]);

                struct application *app = malloc(sizeof(*app));
                *app = (struct application){
                    .index = 0,  /* Not used in application mode */
                    .title = wtitle,
                    .title_lowercase = lowercase,
                    .title_len = c32len(lowercase),
                    .exec = exec,
                    .visible = true,
                };

                tll_push_back(entries, app);
            }
        }
        closedir(d);
    }
    free(copy);

    applications->v = reallocarray(
        applications->v, applications->count + tll_length(entries), sizeof(applications->v[0]));

    tll_foreach(entries, it) {
        applications->v[applications->count++] = it->item;
        applications->visible_count++;
        tll_remove(entries, it);
    }
}
