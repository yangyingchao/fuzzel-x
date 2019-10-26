#include "prompt.h"

#include <stdlib.h>
#include <string.h>
#include <wctype.h>

struct prompt *
prompt_init(const wchar_t *prompt_text)
{
    struct prompt *prompt = malloc(sizeof(*prompt));
    *prompt = (struct prompt) {
        .prompt = wcsdup(prompt_text),
        .text = calloc(1, sizeof(wchar_t)),
        .cursor = 0,
    };

    return prompt;
}

void
prompt_destroy(struct prompt *prompt)
{
    if (prompt == NULL)
        return;

    free(prompt->prompt);
    free(prompt->text);
    free(prompt);
}

static size_t
idx_next_char(struct prompt *prompt)
{
    if (prompt->text[prompt->cursor] == L'\0')
        return prompt->cursor;
    return prompt->cursor + 1;
}

static size_t
idx_prev_char(const struct prompt *prompt)
{
    if (prompt->cursor == 0)
        return 0;

    return prompt->cursor - 1;
}

static size_t
idx_prev_word(const struct prompt *prompt)
{
    size_t prev_char = idx_prev_char(prompt);
    const wchar_t *space = &prompt->text[prev_char];

    /* Ignore initial spaces */
    while (space >= prompt->text && iswspace(*space))
        space--;

    /* Skip non-spaces */
    while (space >= prompt->text && !iswspace(*space))
        space--;

    return space - prompt->text + 1;
}

static size_t
idx_next_word(const struct prompt *prompt)
{
    const wchar_t *end = prompt->text + wcslen(prompt->text);
    const wchar_t *space = &prompt->text[prompt->cursor];

    /* Ignore initial non-spaces */
    while (space < end && !iswspace(*space))
        space++;

    /* Skip spaces */
    while (space < end && iswspace(*space))
        space++;

    return space - prompt->text;
}

bool
prompt_cursor_next_char(struct prompt *prompt)
{
    size_t idx = idx_next_char(prompt);
    if (idx == prompt->cursor)
        return false;

    prompt->cursor = idx;
    return true;
}

bool
prompt_cursor_prev_char(struct prompt *prompt)
{
    size_t idx = idx_prev_char(prompt);
    if (idx == prompt->cursor)
        return false;

    prompt->cursor = idx;
    return true;
}

bool
prompt_cursor_prev_word(struct prompt *prompt)
{
    size_t idx = idx_prev_word(prompt);
    if (idx == prompt->cursor)
        return false;

    prompt->cursor = idx;
    return true;
}

bool
prompt_cursor_next_word(struct prompt *prompt)
{
    size_t idx = idx_next_word(prompt);
    if (idx == prompt->cursor)
        return false;

    prompt->cursor = idx;
    return true;
}

bool
prompt_erase_next_char(struct prompt *prompt)
{
    if (prompt->cursor >= wcslen(prompt->text))
        return false;

    size_t next_char = idx_next_char(prompt);
    memmove(&prompt->text[prompt->cursor],
            &prompt->text[next_char],
            (wcslen(prompt->text) - next_char + 1) * sizeof(wchar_t));
    return true;
}

bool
prompt_erase_prev_char(struct prompt *prompt)
{

    if (prompt->cursor == 0)
        return false;

    size_t prev_char = idx_prev_char(prompt);
    prompt->text[prev_char] = L'\0';
    prompt->cursor = prev_char;
    return true;
}

bool
prompt_erase_next_word(struct prompt *prompt)
{
    size_t next_word = idx_next_word(prompt);
    if (next_word == prompt->cursor)
        return false;

    memmove(&prompt->text[prompt->cursor],
            &prompt->text[next_word],
            (wcslen(prompt->text) - next_word + 1) * sizeof(wchar_t));
    return true;
}

bool
prompt_erase_prev_word(struct prompt *prompt)
{
    size_t new_cursor = idx_prev_word(prompt);
    if (new_cursor == prompt->cursor)
        return false;

    memmove(&prompt->text[new_cursor],
            &prompt->text[prompt->cursor],
            (wcslen(prompt->text) - prompt->cursor + 1) * sizeof(wchar_t));
    prompt->cursor = new_cursor;
    return true;
}

bool
prompt_erase_after_cursor(struct prompt *prompt)
{
    prompt->text[prompt->cursor] = L'\0';
    return true;
}
