#pragma once

#include <time.h>

void time_init(void);
void time_enable(void);

void time_since_boot(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

struct timespec *time_begin(void);
void time_end(struct timespec *start, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
