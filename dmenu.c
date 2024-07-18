#include "dmenu.h"

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <tllist.h>

#define LOG_MODULE "dmenu"
#define LOG_ENABLE_DBG 0
#include "log.h"
#include "char32.h"

void
dmenu_load_entries(struct application_list *applications, char delim,
                   int abort_fd)
{
    tll(struct application) entries = tll_init();

    size_t size = 0;
    size_t alloc_size = 256;
    char *buffer = malloc(alloc_size);

    if (buffer == NULL) {
        LOG_ERRNO("failed to allocate input buffer");
        return;
    }

    int flags;
    if ((flags = fcntl(STDIN_FILENO, F_GETFL)) < 0 ||
        fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK) < 0)
    {
        LOG_ERRNO("failed to set O_NONBLOCK on stdin");
        goto out;
    }

    errno = 0;
    while (true) {
        struct pollfd fds[] = {
            {.fd = STDIN_FILENO, .events = POLLIN},

            /* Must be last */
            {.fd = abort_fd, .events = POLLIN},
        };

        size_t count = sizeof(fds) / sizeof(fds[0]);
        if (abort_fd < 0)
            count--;

        int ret = poll(fds, count, -1);
        if (ret < 0) {
            LOG_ERRNO("failed to poll stdin");
            break;
        }

        if (fds[1].revents & (POLLIN | POLLHUP))
            break;

        /* Increase size of input buffer, if necessary */
        if (size >= alloc_size - 2) {
            LOG_DBG("increasing input buffer size %zu -> %zu",
                    alloc_size, alloc_size * 2);

            alloc_size *= 2;

            char *new_buf = realloc(buffer, alloc_size);
            if (new_buf == NULL) {
                LOG_ERRNO("failed to reallocate input buffer");
                break;
            }

            buffer = new_buf;
        }

        ssize_t bytes_read = read(
            STDIN_FILENO, &buffer[size], alloc_size - size - 1);

        if (bytes_read < 0) {
            if (errno == EINTR)
                continue;
            LOG_ERRNO("failed to read from stdin");
            break;
        } else if (bytes_read == 0 && size == 0) {
            /* No more data on stdin, and all buffered data consumed */
            break;
        }

        size += bytes_read;

        /*
         * Consume as much as possible from the input buffer
         *
         * Input buffer may contain many lines, resulting in multiple
         * applications being added.
         *
         * Input buffer may not even contain a single line, in which
         * case we can't consume anything at all.
         *
         * Input buffer may contain one or more full lines, followed
         * by a final partial line. If stdin has been closed
         * (i.e. there's no more input to read), then treat the last,
         * partial line as a complete line (i.e. pretend it's followed
         * by a delimiter). But, if stdin is still open, don't consume
         * the last, partial line. Instead, wait for more input.
         */
        while (size > 0) {
            char *delim_at = memchr(buffer, delim, size);
            if (delim_at == NULL) {
                if (bytes_read > 0) {
                    /* No delimiter yet, wait for more data */
                    LOG_DBG("no delimiter found, waiting for more data...");
                    break;
                } else {
                    /* No delimiter, but stdin has been closed. Treat
                       the last chunk of data as a complete line. */
                    LOG_DBG("last line: pointing delim_at to the end of the buffer");
                    delim_at = &buffer[size];
                    size++;
                }
            }

            const size_t entry_len = delim_at - buffer;
            *delim_at = '\0';

            /*
             * Support Rofi’s extended dmenu protocol. One can specify
             * an icon by appending ‘\0icon\x1f<icon-name>’ to the
             * entry:
             *
             *  “hello world\0icon\x1ffirefox”
             */
            char *icon_name = NULL;
            const char *extra = memchr(buffer, '\0', entry_len);

            if (extra != NULL) {
                const size_t extra_len = delim_at - extra;

                /*
                 * 'extra' is "\0icon\x1f" - 6 characters. Require
                 * *more* than 6, since icon *name* cannot be empty
                 */
                if (extra_len > 6 &&  memcmp(extra, "\0icon\x1f", 6) == 0)
                    icon_name = strndup(extra + 6, delim_at - (extra + 6));
            }

            LOG_DBG("%s (icon=%s)", buffer, icon_name);

            char32_t *wline = ambstoc32(buffer);

            /* Consume entry from input buffer */
            const size_t consume = delim_at + 1 - buffer;
            assert(consume <= size);

            memmove(buffer, delim_at + 1, size - consume);
            size -= consume;

            if (wline == NULL) {
                free(icon_name);
                continue;
            }

            struct application app = {
                .title = wline,
                .icon = {.name = icon_name},
                .visible = true,
            };

            tll_push_back(entries, app);
        }
    }

    free(buffer);

out:

    applications->v = calloc(tll_length(entries), sizeof(applications->v[0]));
    applications->count = tll_length(entries);

    /* Convert linked-list to regular array */
    size_t i = 0;
    tll_foreach(entries, it) {
        applications->v[i++] = it->item;
        tll_remove(entries, it);
    }

    tll_free(entries);
}

bool
dmenu_execute(const struct application *app, ssize_t index,
              const struct prompt *prompt, enum dmenu_mode format)
{
    switch (format) {
    case DMENU_MODE_TEXT: {
        char *text = ac32tombs(app != NULL ? app->title : prompt_text(prompt));
        if (text != NULL)
            printf("%s\n", text);
        free(text);
        break;
    }

    case DMENU_MODE_INDEX:
        printf("%zd\n", index);
        break;
    }

    return true;
}
