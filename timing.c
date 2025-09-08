#include "timing.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#define LOG_MODULE "timing"
#define LOG_ENABLE_DBG 0
#include "log.h"
#include "xsnprintf.h"

static struct timespec bootup = {0};
static bool enabled = false;

void
time_init(void)
{
    clock_gettime(CLOCK_MONOTONIC, &bootup);
}

void
time_enable(void)
{
    enabled = true;
}

static void
timespec_sub(const struct timespec *a, const struct timespec *b,
             struct timespec *res)
{
    const long one_sec_in_ns = 1000000000;

    res->tv_sec = a->tv_sec - b->tv_sec;
    res->tv_nsec = a->tv_nsec - b->tv_nsec;

    /* tv_nsec may be negative */
    if (res->tv_nsec < 0) {
        res->tv_sec--;
        res->tv_nsec += one_sec_in_ns;
    }
}

void
time_since_boot(const char *fmt, ...)
{
    if (!enabled)
        return;

    struct timespec stop;
    clock_gettime(CLOCK_MONOTONIC, &stop);

    struct timespec diff;
    timespec_sub(&stop, &bootup, &diff);

    va_list va1, va2;
    va_start(va1, fmt);
    va_copy(va2, va1);

    int len = vsnprintf(NULL, 0, fmt, va1);
    va_end(va1);

    char msg[len + 1];
    xvsnprintf(msg, len + 1, fmt, va2);
    va_end(va2);


    LOG_WARN("%s: %lds %ldµs since bootup", msg, diff.tv_sec, diff.tv_nsec / 1000);
}

struct timespec *
time_begin(void)
{
    if (!enabled)
        return NULL;

    struct timespec *ret = malloc(sizeof(*ret));
    if (ret == NULL)
        return NULL;

    clock_gettime(CLOCK_MONOTONIC, ret);
    return ret;
}

void
time_end(struct timespec *start, const char *fmt, ...)
{
    if (start == NULL)
        return;

    if (!enabled) {
        free(start);
        return;
    }

    struct timespec stop;
    clock_gettime(CLOCK_MONOTONIC, &stop);

    struct timespec diff;
    timespec_sub(&stop, start, &diff);

    va_list va1, va2;
    va_start(va1, fmt);
    va_copy(va2, va1);

    int len = vsnprintf(NULL, 0, fmt, va1);
    va_end(va1);

    char msg[len + 1];
    xvsnprintf(msg, len + 1, fmt, va2);
    va_end(va2);

    LOG_WARN("%s in %lds %ldµs", msg, diff.tv_sec, diff.tv_nsec / 1000);

    free(start);
}
