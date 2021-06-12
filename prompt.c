#include "prompt.h"

#include <stdlib.h>
#include <string.h>
#include <wctype.h>

#define LOG_MODULE "prompt"
#define LOG_ENABLE_DBG 0
#include "log.h"

struct prompt {
    wchar_t *prompt;
    wchar_t *text;
    size_t cursor;
};

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

bool
prompt_insert_chars(struct prompt *prompt, const char *text, size_t len)
{
    const char *b = text;
    mbstate_t ps = {0};
    size_t wlen = mbsnrtowcs(NULL, &b, len, 0, &ps);

    const size_t new_len = wcslen(prompt->text) + wlen + 1;
    wchar_t *new_text = realloc(prompt->text, new_len * sizeof(wchar_t));
    if (new_text == NULL)
        return false;

    memmove(&new_text[prompt->cursor + wlen],
            &new_text[prompt->cursor],
            (wcslen(new_text) - prompt->cursor + 1) * sizeof(wchar_t));

    b = text;
    ps = (mbstate_t){0};
    mbsnrtowcs(&new_text[prompt->cursor], &b, len, wlen + 1, &ps);

    prompt->text = new_text;
    prompt->cursor += wlen;

    LOG_DBG("prompt: \"%ls\" (cursor=%zu, length=%zu)",
            prompt->text, prompt->cursor, new_len);

    return true;
}

const wchar_t *
prompt_prompt(const struct prompt *prompt)
{
    return prompt->prompt;
}

const wchar_t *
prompt_text(const struct prompt *prompt)
{
    return prompt->text;
}

size_t
prompt_cursor(const struct prompt *prompt)
{
    return prompt->cursor;
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
prompt_cursor_home(struct prompt *prompt)
{
    if (prompt->cursor == 0)
        return false;

    prompt->cursor = 0;
    return true;
}

bool
prompt_cursor_end(struct prompt *prompt)
{
    size_t text_len = wcslen(prompt->text);
    if (prompt->cursor >= text_len)
        return false;

    prompt->cursor = text_len;
    return true;
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
