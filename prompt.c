#include "prompt.h"

#include <stdlib.h>

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
