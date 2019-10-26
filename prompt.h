#pragma once

#include <stddef.h>
#include <wchar.h>

struct prompt {
    wchar_t *prompt;
    wchar_t *text;
    size_t cursor;
};

struct prompt *prompt_init(const wchar_t *prompt_text);
void prompt_destroy(struct prompt *prompt);
