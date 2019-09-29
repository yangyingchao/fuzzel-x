#pragma once

#include <stddef.h>
#include <wchar.h>

struct prompt {
    wchar_t *prompt;
    wchar_t *text;
    size_t cursor;
};
