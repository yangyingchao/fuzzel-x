#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <wchar.h>


#if 0
struct prompt {
    wchar_t *prompt;
    wchar_t *text;
    size_t cursor;
};
#endif

struct prompt;
struct prompt *prompt_init(const wchar_t *prompt_text);
void prompt_destroy(struct prompt *prompt);

const wchar_t *prompt_prompt(const struct prompt *prompt);
const wchar_t *prompt_text(const struct prompt *prompt);
size_t prompt_cursor(const struct prompt *prompt);

bool prompt_cursor_home(struct prompt *prompt);
bool prompt_cursor_end(struct prompt *prompt);
bool prompt_cursor_next_char(struct prompt *prompt);
bool prompt_cursor_prev_char(struct prompt *prompt);
bool prompt_cursor_prev_word(struct prompt *prompt);
bool prompt_cursor_next_word(struct prompt *prompt);

bool prompt_erase_next_char(struct prompt *prompt);
bool prompt_erase_prev_char(struct prompt *prompt);
bool prompt_erase_next_word(struct prompt *prompt);
bool prompt_erase_prev_word(struct prompt *prompt);
bool prompt_erase_after_cursor(struct prompt *prompt);
