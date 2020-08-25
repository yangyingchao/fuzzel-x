#include "match.h"

#include <stdlib.h>
#include <wctype.h>
#include <assert.h>

struct matches {
    const struct application_list *applications;
    struct match *matches;
    size_t match_count;
    size_t selected;
    size_t max_matches;
};

struct matches *
matches_init(const struct application_list *applications)
{
    struct matches *matches = malloc(sizeof(*matches));
    *matches = (struct matches) {
        .applications = applications,
        .matches = malloc(applications->count * sizeof(matches->matches[0])),
        .match_count = 0,
        .selected = 0,
        .max_matches = 0,
    };
    return matches;
}

void
matches_destroy(struct matches *matches)
{
    if (matches == NULL)
        return;

    free(matches->matches);
    free(matches);
}

size_t
matches_max_matches(const struct matches *matches)
{
    return matches->max_matches;
}

void
matches_max_matches_set(struct matches *matches, size_t max_matches)
{
    matches->max_matches = max_matches;
}

const struct match *
matches_get(const struct matches *matches, size_t idx)
{
    assert(idx < matches->match_count);
    return &matches->matches[idx];
}

const struct match *
matches_get_match(const struct matches *matches)
{
    return matches->match_count > 0
        ? matches_get(matches, matches->selected)
        : NULL;
}

size_t
matches_get_count(const struct matches *matches)
{
    return matches->match_count;
}

size_t
matches_get_match_index(const struct matches *matches)
{
    return matches->selected;
}

bool
matches_selected_prev(struct matches *matches, bool wrap)
{
    if (matches->selected > 0) {
        matches->selected--;
        return true;
    } else if (wrap && matches->match_count > 1) {
        matches->selected = 0;
        return true;
    }

    return false;
}

bool
matches_selected_next(struct matches *matches, bool wrap)
{
    if (matches->selected + 1 < matches->match_count) {
        matches->selected++;
        return true;
    } else if (wrap && matches->match_count > 1) {
        matches->selected = matches->match_count - 1;
        return true;
    }

    return false;
}

static int
sort_match_by_count(const void *_a, const void *_b)
{
    const struct match *a = _a;
    const struct match *b = _b;
    return b->application->count - a->application->count;
}

static wchar_t *
wcscasestr(const wchar_t *haystack, const wchar_t *needle)
{
    const size_t hay_len = wcslen(haystack);
    const size_t needle_len = wcslen(needle);

    if (needle_len > hay_len)
        return NULL;

    for (size_t i = 0; i < hay_len - needle_len + 1; i++) {
        bool matched = true;
        for (size_t j = 0; j < needle_len; j++) {
            if (towlower(haystack[i + j]) != towlower(needle[j])) {
                matched = false;
                break;
            }
        }

        if (matched)
            return (wchar_t *)&haystack[i];
    }

    return NULL;
}

void
matches_update(struct matches *matches, const struct prompt *prompt)
{
    assert(matches->max_matches > 0);

    const wchar_t *ptext = prompt_text(prompt);

    /* Nothing entered; all programs found matches */
    if (wcslen(ptext) == 0) {

        for (size_t i = 0; i < matches->applications->count; i++) {
            matches->matches[i] = (struct match){
                .application = &matches->applications->v[i],
                .start_title = -1,
                .start_comment = -1};
        }

        /* Sort */
        matches->match_count = matches->applications->count;
        qsort(matches->matches, matches->match_count, sizeof(matches->matches[0]), &sort_match_by_count);

        /* Limit count (don't render outside window) */
        if (matches->match_count > matches->max_matches)
            matches->match_count = matches->max_matches;

        if (matches->selected >= matches->match_count && matches->selected > 0)
            matches->selected = matches->match_count - 1;
        return;
    }

    matches->match_count = 0;
    for (size_t i = 0; i < matches->applications->count; i++) {
        struct application *app = &matches->applications->v[i];
        size_t start_title = -1;
        size_t start_comment = -1;

        const wchar_t *m = wcscasestr(app->title, ptext);
        if (m != NULL)
            start_title = m - app->title;

        if (app->comment != NULL) {
            m = wcscasestr(app->comment, ptext);
            if (m != NULL)
                start_comment = m - app->comment;
        }

        if (start_title == -1 && start_comment == -1)
            continue;

        matches->matches[matches->match_count++] = (struct match){
            .application = app,
            .start_title = start_title,
            .start_comment = start_comment};
    }

    /* Sort */
    qsort(matches->matches, matches->match_count, sizeof(matches->matches[0]), &sort_match_by_count);

    /* Limit count (don't render outside window) */
    if (matches->match_count > matches->max_matches)
        matches->match_count = matches->max_matches;

    if (matches->selected >= matches->match_count && matches->selected > 0)
        matches->selected = matches->match_count - 1;
}
