#include "kbd-repeater.h"

#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

#include <sys/timerfd.h>
#include <sys/epoll.h>

#define LOG_MODULE "kbd-repeater"
#define LOG_ENABLE_DBG 0
#include "log.h"
#include "fdm.h"

struct repeat {
    struct fdm *fdm;
    int fd;

    void (*cb)(uint32_t key, void *data);
    void *data;

    int32_t delay;
    int32_t rate;

    bool dont_re_repeat;
    uint32_t key;
};

static bool
fdm_handler(struct fdm *fdm, int fd, int events, void *data)
{
    struct repeat *repeat = data;

    uint64_t expiration_count;
    ssize_t ret = read(
        repeat->fd, &expiration_count, sizeof(expiration_count));

    if (ret < 0 && errno != EAGAIN) {
        LOG_ERRNO("failed to read key repeat count from timer fd");
        return false;
    }

    repeat->dont_re_repeat = true;
    for (size_t i = 0; i < expiration_count; i++)
        repeat->cb(repeat->key, repeat->data);
    repeat->dont_re_repeat = false;

    if (events & EPOLLHUP) {
        LOG_ERR("keyboard repeater timer FD closed unexpectedly");
        return false;
    }

    return true;
}

struct repeat *
repeat_init(struct fdm *fdm, void (*cb)(uint32_t key, void *data), void *data)
{
    int fd = timerfd_create(CLOCK_BOOTTIME, TFD_CLOEXEC | TFD_NONBLOCK);
    if (fd == -1) {
        LOG_ERRNO("failed to create keyboard repeat timer FD");
        return NULL;
    }

    struct repeat *repeat = malloc(sizeof(*repeat));
    *repeat = (struct repeat) {
        .fdm = fdm,
        .fd = fd,
        .cb = cb,
        .data = data,
    };

    if (!fdm_add(fdm, fd, EPOLLIN, &fdm_handler, repeat)) {
        LOG_ERR("failed to register keyboard repeater with FDM");
        free(repeat);
        close(fd);
        return NULL;
    }

    return repeat;
}

void
repeat_destroy(struct repeat *repeat)
{
    if (repeat == NULL)
        return;

    fdm_del(repeat->fdm, repeat->fd);
    close(repeat->fd);
}

void
repeat_configure(struct repeat *repeat, uint32_t delay, uint32_t rate)
{
    repeat->delay = delay;
    repeat->rate = rate;
}

bool
repeat_start(struct repeat *repeat, uint32_t key)
{
    if (repeat->dont_re_repeat)
        return true;

    struct itimerspec t = {
        .it_value = {.tv_sec = 0, .tv_nsec = repeat->delay * 1000000},
        .it_interval = {.tv_sec = 0, .tv_nsec = 1000000000 / repeat->rate},
    };

    if (t.it_value.tv_nsec >= 1000000000) {
        t.it_value.tv_sec += t.it_value.tv_nsec / 1000000000;
        t.it_value.tv_nsec %= 1000000000;
    }
    if (t.it_interval.tv_nsec >= 1000000000) {
        t.it_interval.tv_sec += t.it_interval.tv_nsec / 1000000000;
        t.it_interval.tv_nsec %= 1000000000;
    }
    if (timerfd_settime(repeat->fd, 0, &t, NULL) < 0) {
        LOG_ERRNO("failed to arm keyboard repeat timer");
        return false;
    }

    repeat->key = key;
    return true;
}

bool
repeat_stop(struct repeat *repeat, uint32_t key)
{
    if (key != (uint32_t)-1 && key != repeat->key)
        return true;

    if (timerfd_settime(repeat->fd, 0, &(struct itimerspec){{0}}, NULL) < 0) {
        LOG_ERRNO("failed to disarm keyboard repeat timer");
        return false;
    }

    return true;
}
