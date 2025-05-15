#include "column.h"

#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <uchar.h>
#include <unistd.h>

#define LOG_MODULE "column"
#define LOG_ENABLE_DBG 0
#include "log.h"
#include "xmalloc.h"

char32_t *
nth_column(const char32_t *string, unsigned int nth) {
    assert(nth > 0);

    const char32_t* column = string;

    for (unsigned int i = 0;
         i < nth - 1;
         i++)
    {
        column = c32chr(column, U'\t');

        if (column == NULL) {
            LOG_WARN("%ls: only %u column(s) available",
                     (const wchar_t *)string, i + 1);
            column = U"";
            break;
        }

        column++;
    }

    char32_t *result = NULL;
    const char32_t *next_tab = c32chr(column, U'\t');

    if (next_tab == NULL)
        result = xc32dup(column);
    else {
        const size_t len = next_tab - column;
        result = xmalloc((len + 1) * sizeof(char32_t));
        memcpy(result, column, len * sizeof(char32_t));
        result[len] = U'\0';
    }

    return result;
}
