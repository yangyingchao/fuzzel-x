#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "fdm.h"

struct repeat;
struct repeat *repeat_init(
    struct fdm *fdm,
    void (*cb)(uint32_t key, void *data), void *data);
void repeat_destroy(struct repeat *repeat);

void repeat_configure(struct repeat *repeat, uint32_t delay, uint32_t rate);

bool repeat_start(struct repeat *repeat, uint32_t key);
bool repeat_stop(struct repeat *repeat, uint32_t key);
