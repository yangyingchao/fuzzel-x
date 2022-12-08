#include "application.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#define LOG_MODULE "application"
#define LOG_ENABLE_DBG 0
#include "log.h"
#include "char32.h"

static bool
push_argv(char ***argv, size_t *size, char *arg, size_t *argc)
{
    if (arg != NULL && arg[0] == '%')
        return true;

    if (*argc >= *size) {
        size_t new_size = *size > 0 ? 2 * *size : 10;
        char **new_argv = realloc(*argv, new_size * sizeof(new_argv[0]));

        if (new_argv == NULL)
            return false;

        *argv = new_argv;
        *size = new_size;
    }

    (*argv)[(*argc)++] = arg;
    return true;
}

static bool
tokenize_cmdline(char *cmdline, char ***argv)
{
    *argv = NULL;
    size_t argv_size = 0;

    char *p = cmdline;
    char *search_start = p; /* points to start of current arg */
    char open_quote = 0; /* the current opening quote character, 0 means none open */

    size_t idx = 0;
    while (*p != '\0') {
        if (*p == '\\') {
            /* lookahead to handle escaped chars
             * only escape if not within single quotes */
            if (open_quote != '\'') {
                /* within double quotes, only \$, \`, \", \\ should be
                 * escaped, others should be literal */
                if ((open_quote != '"' && (*(p + 1) == '\'' || *(p + 1) == ' ')) ||
                    *(p + 1) == '$' ||
                    *(p + 1) == '"' ||
                    *(p + 1) == '`' ||
                    *(p + 1) == '\\')
                {
                    /* essentially delete the (first) backslash */
                    memmove(p, p + 1, strlen(p + 1) + 1);
                }
            }
            /* ignore the other cases */
        } else {
            if (open_quote == 0 && (*p == '\'' || *p == '"')) {
                /* open a quote, delete the opening character but
                 * remember that we have one open */
                open_quote = *p;
                memmove(p, p + 1, strlen(p + 1) + 1);
                continue; /* don't increment p */
            } else if (*p == open_quote) {
                /* close the quote, delete the closing character */
                open_quote = 0;
                memmove(p, p + 1, strlen(p + 1) + 1);
                continue; /* don't increment p */
            } else if (*p == ' ' && open_quote == 0) {
                /* we must not be in an argument rn
                 * check if we can close the arg at p (exclusive)
                 * note: passing empty quotes doesn't count as an argument */
                if (p > search_start) {
                    *p = '\0';
                    if (!push_argv(argv, &argv_size, search_start, &idx))
                        goto err;
                }
                search_start = p + 1;
            }
        }
        p++;
    }

    if (open_quote != 0) {
        LOG_ERR("unterminated %s quote\n", open_quote == '"' ? "double" : "single");
        goto err;
    }

    /* edge case: argument terminated by \0 */
    if (p > search_start) {
        if (!push_argv(argv, &argv_size, search_start, &idx))
            goto err;
    }

    if (!push_argv(argv, &argv_size, NULL, &idx))
        goto err;

    return true;

err:
    free(*argv);
    return false;
}

bool
application_execute(const struct application *app, const struct prompt *prompt,
                    const char *launch_prefix, const char *xdg_activation_token)
{
    const char32_t *ptext = prompt_text(prompt);
    const size_t c32len = c32tombs(NULL, ptext, 0);
    char cprompt[c32len + 1];
    c32tombs(cprompt, ptext, c32len + 1);

    const char *execute = app != NULL ? app->exec : cprompt;
    const char *path = app != NULL ? app->path : NULL;

    LOG_DBG("exec(%s)", execute);

    /* Tokenize the command */
    char *unescaped;
    char *execute_dest;
    size_t execute_len = strlen(execute);
    if (launch_prefix != NULL) {
      size_t launch_len = strlen(launch_prefix);
      unescaped = malloc(launch_len + execute_len + 2 /* whitespace + null terminator */);
      sprintf(unescaped, "%s ", launch_prefix);
      execute_dest = unescaped + launch_len + 1;
    } else {
      unescaped = malloc(execute_len + 1);
      execute_dest = unescaped;
    }

    /* Substitute escape sequences for their literal character values */
    for (size_t i = 0; i <= execute_len /* so null terminator is copied */; i++, execute_dest++) {
      if (execute[i] != '\\') {
        *execute_dest = execute[i];
      } else {
        i++;
        switch(execute[i]) {
          case 's':
            *execute_dest = ' ';
            break;
          case 'n':
            *execute_dest = '\n';
            break;
          case 't':
            *execute_dest = '\t';
            break;
          case 'r':
            *execute_dest = '\r';
            break;
          case ';':
            *execute_dest = ';';
            break;
          case '\\':
            *execute_dest = '\\';
            break;
          default:
            free(unescaped);
            LOG_ERR("invalid escaped exec argument character: %c", execute[i]);
            return false;
        }
      }
    }

    char **argv;
    if (!tokenize_cmdline(unescaped, &argv)) {
        free(unescaped);
        return false;
    }

    LOG_DBG("argv:");
    for (size_t i = 0; argv[i] != NULL; i++)
        LOG_DBG("  %zu: \"%s\"", i, argv[i]);

    int pipe_fds[2];
    if (pipe2(pipe_fds, O_CLOEXEC) == -1) {
        LOG_ERRNO("failed to create pipe");
        free(unescaped);
        free(argv);
        return false;
    }

    pid_t pid = fork();
    if (pid == -1) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);

        free(unescaped);
        free(argv);

        LOG_ERRNO("failed to fork");
        return false;
    }

    if (pid == 0) {
        /* Child */

        /* Close read end */
        close(pipe_fds[0]);

        if (path != NULL) {
            if (chdir(path) == -1)
                LOG_ERRNO("failed to chdir to %s", path);
        }

        if (xdg_activation_token != NULL) {
            /* Prepare client for xdg activation startup */
            setenv("XDG_ACTIVATION_TOKEN", xdg_activation_token, 1);
            /* And X11 startup notifications */
            setenv("DESKTOP_STARTUP_ID", xdg_activation_token, 1);
        }

        /* Redirect stdin/stdout/stderr -> /dev/null */
        int devnull_r = open("/dev/null", O_RDONLY | O_CLOEXEC);
        int devnull_w = open("/dev/null", O_WRONLY | O_CLOEXEC);

        if (devnull_r == -1 || devnull_w == -1)
            goto child_err;

        if (dup2(devnull_r, STDIN_FILENO) == -1 ||
            dup2(devnull_w, STDOUT_FILENO) == -1 ||
            dup2(devnull_w, STDERR_FILENO) == -1)
        {
            goto child_err;
        }

        execvp(argv[0], argv);

    child_err:
        /* Signal error back to parent process */
        (void)!write(pipe_fds[1], &errno, sizeof(errno));
        _exit(1);
    } else {
        /* Parent */

        free(unescaped);
        free(argv);

        /* Close write end */
        close(pipe_fds[1]);

        int _errno;
        static_assert(sizeof(_errno) == sizeof(errno), "errno size mismatch");

        ssize_t ret = read(pipe_fds[0], &_errno, sizeof(_errno));
        if (ret == -1)
            LOG_ERRNO("failed to read from pipe");
        else if (ret == sizeof(_errno))
            LOG_ERRNO_P("%s: failed to execute", _errno, execute);
        else {
            LOG_DBG("%s: fork+exec succeeded", execute);
        }

        close(pipe_fds[0]);
        return ret == 0;
    }
}

struct application_list *
applications_init(void)
{
    return calloc(1, sizeof(struct application_list));
}

void
applications_destroy(struct application_list *apps)
{
    if (apps == NULL)
        return;

    for (size_t i = 0; i < apps->count; i++) {
        struct application *app = &apps->v[i];

        free(app->id);
        free(app->path);
        free(app->exec);
        free(app->basename);
        free(app->wexec);
        free(app->app_id);
        free(app->title);
        if (app->render_title != app->title)
            free(app->render_title);
        free(app->generic_name);
        free(app->comment);
        free(app->icon.name);

        tll_free_and_free(app->keywords, free);
        tll_free_and_free(app->categories, free);

        switch (app->icon.type) {
        case ICON_NONE:
            break;

        case ICON_PNG:
            if (app->icon.png != NULL) {
#if defined(FUZZEL_ENABLE_PNG_LIBPNG)
                free(pixman_image_get_data(app->icon.png));
                pixman_image_unref(app->icon.png);
#endif
            }
            break;

        case ICON_SVG:
            if (app->icon.svg != NULL) {
#if defined(FUZZEL_ENABLE_SVG_LIBRSVG)
                g_object_unref(app->icon.svg);
#elif defined(FUZZEL_ENABLE_SVG_NANOSVG)
                nsvgDelete(app->icon.svg);
#endif
            }
            break;
        }

        tll_foreach(app->icon.rasterized, it) {
            struct rasterized *rast = &it->item;
            free(pixman_image_get_data(rast->pix));
            pixman_image_unref(rast->pix);
            tll_remove(app->icon.rasterized, it);
        }
        free(app->icon.path);

        fcft_text_run_destroy(app->shaped);
    }

    free(apps->v);
    free(apps);
}

void
applications_flush_text_run_cache(struct application_list *apps)
{
    for (size_t i = 0; i < apps->count; i++) {
        fcft_text_run_destroy(apps->v[i].shaped);
        apps->v[i].shaped = NULL;
    }
}
