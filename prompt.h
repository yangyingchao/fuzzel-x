#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <wchar.h>

struct prompt {
    wchar_t *prompt;
    wchar_t *text;
    size_t cursor;
};

struct prompt *prompt_init(const wchar_t *prompt_text);
void prompt_destroy(struct prompt *prompt);

bool prompt_cursor_next_char(struct prompt *prompt);
bool prompt_cursor_prev_char(struct prompt *prompt);
bool prompt_cursor_prev_word(struct prompt *prompt);
bool prompt_cursor_next_word(struct prompt *prompt);

bool prompt_erase_next_char(struct prompt *prompt);
bool prompt_erase_prev_char(struct prompt *prompt);
bool prompt_erase_next_word(struct prompt *prompt);
bool prompt_erase_prev_word(struct prompt *prompt);
bool prompt_erase_after_cursor(struct prompt *prompt);
