#include "match.h"

#include <stdlib.h>
#include <wctype.h>
#include <assert.h>

#define LOG_MODULE "match"
#define LOG_ENABLE_DBG 0
#include "log.h"

#define min(x, y) ((x < y) ? (x) : (y))
#define max(x, y) ((x > y) ? (x) : (y))

struct matches {
    const struct application_list *applications;
    enum match_fields fields;
    struct match *matches;
    bool fuzzy;
    size_t page_count;
    size_t match_count;
    size_t selected;
    size_t max_matches_per_page;
    size_t fuzzy_min_length;
    size_t fuzzy_max_length_discrepancy;
    size_t fuzzy_max_distance;
};


struct levenshtein_matrix {
    size_t distance;
    enum choice { UNSET, FIRST, SECOND, THIRD } choice;
};

static void
levenshtein_distance(const wchar_t *a, size_t alen,
                     const wchar_t *b, size_t blen,
                     struct levenshtein_matrix **m)
{
    for (size_t i = 1; i <= blen; i++) {
        m[i][0].distance = i;
        m[i][0].choice = FIRST;
    }

    for (size_t i = 1; i <= blen; i++) {
        for (size_t j = 1; j <= alen; j++) {
            const size_t cost = towlower(a[j - 1]) == towlower(b[i - 1]) ? 0 : 1;
            const size_t first = m[i - 1][j].distance + 1;
            const size_t second = m[i][j - 1].distance + 1;
            const size_t third = m[i - 1][j - 1].distance + cost;

            const size_t shortest = min(min(first, second), third);
            m[i][j].distance = shortest;

            if (shortest == first)
                m[i][j].choice = FIRST;
            else if (shortest == second)
                m[i][j].choice = SECOND;
            else
                m[i][j].choice = THIRD;
        }
    }
}

static const wchar_t *
match_levenshtein(struct matches *matches,
                  const wchar_t *src, const wchar_t *pat)
{
    if (!matches->fuzzy)
        return NULL;

    const size_t src_len = wcslen(src);
    const size_t pat_len = wcslen(pat);

    if (pat_len < matches->fuzzy_min_length)
        return NULL;

    if (src_len < pat_len)
        return NULL;

    struct levenshtein_matrix **m = calloc(pat_len + 1, sizeof(m[0]));
    for (size_t i = 0; i < pat_len + 1; i++)
        m[i] = calloc(src_len + 1, sizeof(m[0][0]));

    levenshtein_distance(src, src_len, pat, pat_len, m);

#if 0
    /* Dump levenshtein table */
    printf("     ");
    for (size_t j = 0; j < src_len; j++)
        printf("%lc  ", src[j]);
    printf("\n");
    for (size_t i = 0; i < pat_len + 1; i++) {
        if (i > 0)
            printf("%lc ", pat[i - 1]);
        else
            printf("  ");

        for (size_t j = 0; j < src_len + 1; j++)
            printf("%zu%c ", m[i][j].distance,
                   m[i][j].choice == FIRST ? 'f' :
                   m[i][j].choice == SECOND ? 's' :
                   m[i][j].choice == THIRD ? 't' : 'u');
        printf("\n");
    }
#endif

    size_t c = 0;
    size_t best = m[pat_len][0].distance;

    for (ssize_t j = src_len; j >= 0; j--) {
        if (m[pat_len][j].distance < best) {
            best = m[pat_len][j].distance;
            c = j;
        }
    }

    const size_t end = c;
    size_t r = pat_len;
    // printf("starting at r=%zu, c=%zu\n", r, c);

    while (r > 0) {
        switch (m[r][c].choice) {
        case UNSET: assert(false); break;
        case FIRST: r--; break;
        case SECOND: c--; break;
        case THIRD: r--; c--; break;
        }
    }

#if 0
    *match = (struct levenshtein_match){
        .start = c,
        .len = end - c,
        .distance = m[pat_len][end].distance,
    };
#endif
    const size_t match_ofs = c;
    const size_t match_len = end - c;
    const size_t match_distance = m[pat_len][end].distance;

    LOG_DBG("%s vs. %s: sub-string: %.*ls, (distance=%zu, row=%zu, col=%zu)",
            src, pat, (int)match_len, &src[match_ofs], match_distance,
            r, c);

    for (size_t i = 0; i < pat_len + 1; i++)
        free(m[i]);
    free(m);

    const size_t len_diff = match_len > pat_len
        ? match_len - pat_len
        : pat_len - match_len;

    if (len_diff <= matches->fuzzy_max_length_discrepancy &&
        match_distance <= matches->fuzzy_max_distance)
    {
        return &src[match_ofs];
    }

    return NULL;
}

struct matches *
matches_init(const struct application_list *applications,
             enum match_fields fields, bool fuzzy, size_t fuzzy_min_length,
             size_t fuzzy_max_length_discrepancy, size_t fuzzy_max_distance)
{
    struct matches *matches = malloc(sizeof(*matches));
    *matches = (struct matches) {
        .applications = applications,
        .fields = fields,
        .matches = malloc(applications->count * sizeof(matches->matches[0])),
        .fuzzy = fuzzy,
        .page_count = 0,
        .match_count = 0,
        .selected = 0,
        .max_matches_per_page = 0,
        .fuzzy_min_length = fuzzy_min_length,
        .fuzzy_max_length_discrepancy = fuzzy_max_length_discrepancy,
        .fuzzy_max_distance = fuzzy_max_distance,
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
matches_max_matches_per_page(const struct matches *matches)
{
    return matches->max_matches_per_page;
}

void
matches_max_matches_per_page_set(struct matches *matches, size_t max_matches)
{
    matches->max_matches_per_page = max_matches;
}

size_t
matches_get_page_count(const struct matches *matches)
{
    return matches->match_count / matches->max_matches_per_page;
}

size_t
matches_get_page(const struct matches *matches)
{
    return matches->selected / matches->max_matches_per_page;
}

const struct match *
matches_get(const struct matches *matches, size_t idx)
{
    const size_t page_no = matches_get_page(matches);
    const size_t items_on_page __attribute__((unused)) = matches_get_count(matches);

    LOG_DBG(
        "page-count: %zu, page-no: %zu, items-on-page: %zu, idx: %zu, max: %zu, "
        "match-count: %zu",
        matches->page_count, page_no, items_on_page, idx,
        matches->max_matches_per_page, matches->match_count);

    assert(idx < items_on_page);
    idx += page_no * matches->max_matches_per_page;

    assert(idx < matches->match_count);
    return &matches->matches[idx];
}

const struct match *
matches_get_match(const struct matches *matches)
{
    return matches->match_count > 0
        ? &matches->matches[matches->selected]
        : NULL;
}

size_t
matches_get_count(const struct matches *matches)
{
    const size_t total = matches->match_count;
    const size_t page_no = matches_get_page(matches);

    if (total == 0)
        return 0;
    else if (page_no + 1 >= matches->page_count) {
        size_t remainder = total % matches->max_matches_per_page;
        return remainder == 0 ? matches->max_matches_per_page : remainder;
    } else
        return matches->max_matches_per_page;
}

size_t
matches_get_match_index(const struct matches *matches)
{
    return matches->selected % matches->max_matches_per_page;
}

bool
matches_selected_prev(struct matches *matches, bool wrap)
{
    if (matches->selected > 0) {
        matches->selected--;
        return true;
    } else if (wrap && matches->match_count > 1) {
        matches->selected = matches->match_count - 1;
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
        matches->selected = 0;
        return true;
    }

    return false;
}

bool
matches_selected_prev_page(struct matches *matches)
{
    const size_t page_no = matches_get_page(matches);
    if (page_no > 0) {
        assert(matches->selected >= matches->max_matches_per_page);
        matches->selected -= matches->max_matches_per_page;
        return true;
    } else if (matches->selected > 0) {
        matches->selected = 0;
        return true;
    }

    return false;
}

bool
matches_selected_next_page(struct matches *matches)
{
    const size_t page_no = matches_get_page(matches);
    if (page_no + 1 < matches->page_count) {
        matches->selected = min(
            matches->selected + matches->max_matches_per_page,
            matches->match_count - 1);
        return true;
    } else if (matches->selected < matches->match_count - 1) {
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
    assert(matches->max_matches_per_page > 0);

    const wchar_t *ptext = prompt_text(prompt);

    /* Nothing entered; all programs found matches */
    if (wcslen(ptext) == 0) {

        for (size_t i = 0; i < matches->applications->count; i++) {
            matches->matches[i] = (struct match){
                .application = &matches->applications->v[i],
                .start_title = -1,
            };
        }

        /* Sort */
        matches->match_count = matches->applications->count;
        qsort(matches->matches, matches->match_count, sizeof(matches->matches[0]), &sort_match_by_count);

        if (matches->selected >= matches->match_count && matches->selected > 0)
            matches->selected = matches->match_count - 1;

        matches->page_count = (
            matches->match_count + (matches->max_matches_per_page - 1)) /
            matches->max_matches_per_page;

        return;
    }

    const enum match_fields fields = matches->fields;
    const bool match_filename = fields & MATCH_FILENAME;
    const bool match_name = fields & MATCH_NAME;
    const bool match_generic = fields & MATCH_GENERIC;
    const bool match_exec = fields & MATCH_EXEC;
    const bool match_categories = fields & MATCH_CATEGORIES;
    const bool match_keywords = fields & MATCH_KEYWORDS;
    const bool match_comment = fields & MATCH_COMMENT;

    LOG_DBG(
        "matching: filename=%s, name=%s, generic=%s, exec=%s, categories=%s, "
        "keywords=%s, comment=%s",
        match_filename ? "yes" : "no",
        match_name ? "yes" : "no",
        match_generic ? "yes" : "no",
        match_exec ? "yes" : "no",
        match_categories ? "yes" : "no",
        match_keywords ? "yes" : "no",
        match_comment ? "yes" : "no");

    matches->match_count = 0;

    for (size_t i = 0; i < matches->applications->count; i++) {
        struct application *app = &matches->applications->v[i];
        bool is_match = false;
        ssize_t start_title = -1;

        if (!is_match && match_name) {
            const wchar_t *m = wcscasestr(app->title, ptext);
            if (m == NULL)
                m = match_levenshtein(matches, app->title, ptext);
            if (m != NULL) {
                start_title = m - app->title;
                is_match = true;
            }
        }

        if (!is_match && match_filename && app->basename != NULL) {
            if (wcscasestr(app->basename, ptext) != NULL ||
                match_levenshtein(matches, app->basename, ptext) != NULL)
            {
                is_match = true;
            }
        }

        if (!is_match && match_generic && app->generic_name != NULL) {
            if (wcscasestr(app->generic_name, ptext) != NULL ||
                match_levenshtein(matches, app->generic_name, ptext) != NULL)
            {
                is_match = true;
            }
        }

        if (!is_match && match_exec && app->wexec != NULL) {
            if (wcscasestr(app->wexec, ptext) != NULL ||
                match_levenshtein(matches, app->wexec, ptext) != NULL)
            {
                is_match = true;
            }
        }

        if (!is_match && match_comment && app->comment != NULL) {
            if (wcscasestr(app->comment, ptext) != NULL ||
                match_levenshtein(matches, app->comment, ptext) != NULL)
            {
                is_match = true;
            }
        }

        if (!is_match && match_keywords) {
            tll_foreach(app->keywords, it) {
                if (wcscasestr(it->item, ptext) != NULL ||
                    match_levenshtein(matches, it->item, ptext))
                {
                    is_match = true;
                    break;
                }
            }
        }

        if (!is_match && match_categories) {
            tll_foreach(app->categories, it) {
                if (wcscasestr(it->item, ptext) != NULL ||
                    match_levenshtein(matches, it->item, ptext) != NULL)
                {
                    is_match = true;
                    break;
                }
            }
        }

        if (!is_match)
            continue;

        matches->matches[matches->match_count++] = (struct match){
            .application = app,
            .start_title = start_title,
        };
    }

    /* Sort */
    qsort(matches->matches, matches->match_count, sizeof(matches->matches[0]), &sort_match_by_count);

    matches->page_count = (
        matches->match_count + (matches->max_matches_per_page - 1)) /
        matches->max_matches_per_page;

    if (matches->selected >= matches->match_count && matches->selected > 0)
        matches->selected = matches->match_count - 1;
}
