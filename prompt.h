#pragma once

#include <stddef.h>

struct prompt {
    char *prompt;
    char *text;
    size_t cursor;
};
