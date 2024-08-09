#include "match.h"

#include <stdlib.h>
#include <string.h>
#include <wctype.h>
#include <assert.h>

#define LOG_MODULE "match"
#define LOG_ENABLE_DBG 0
#include "log.h"
#include "char32.h"

#define min(x, y) ((x < y) ? (x) : (y))
#define max(x, y) ((x > y) ? (x) : (y))

struct matches {
    const struct application_list *applications;
    enum match_fields fields;
    struct match *matches;
    size_t match_count;
    enum match_mode mode;
    size_t page_count;
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

static char32_t *
c32casestr(const char32_t *haystack, const char32_t *needle)
{
    const size_t hay_len = c32len(haystack);
    const size_t needle_len = c32len(needle);

    if (needle_len > hay_len)
        return NULL;

    for (size_t i = 0; i < hay_len - needle_len + 1; i++) {
        bool matched = true;
        for (size_t j = 0; j < needle_len; j++) {
            if (toc32lower(haystack[i + j]) != toc32lower(needle[j])) {
                matched = false;
                break;
            }
        }

        if (matched)
            return (char32_t *)&haystack[i];
    }

    return NULL;
}

static void
match_fzf(const char32_t *haystack, const char32_t *needle,
          struct match_substring **pos, size_t *pos_count,
          enum matched_type *match_type)
{
    const size_t haystack_len = c32len(haystack);
    const size_t needle_len = c32len(needle);
    const char32_t *const needle_end = &needle[needle_len];
    const char32_t *const haystack_end = &haystack[haystack_len];

    const char32_t *haystack_search_start = haystack;

    while (needle < needle_end) {
        if (haystack_search_start >= haystack_end) {
            if (pos != NULL) {
                free(*pos);
                *pos = NULL;
                *pos_count = 0;
            }

            if (match_type != NULL)
                *match_type = MATCHED_NONE;

            return;
        }

        size_t longest_match_len = 0;
        size_t longest_match_ofs = 0;

        for (const char32_t *start = haystack_search_start;
             start < haystack_end;
             start++)
        {
            const char32_t *n = needle;
            const char32_t *h = start;

            size_t match_len = 0;
            while (n < needle_end &&
                   h < haystack_end &&
                   toc32lower(*n) == toc32lower(*h))
            {
                match_len++;
                n++;
                h++;
            }

            if (match_len > longest_match_len) {
                longest_match_len = match_len;
                longest_match_ofs = start - haystack;
            }
        }

        if (longest_match_len == 0) {
            if (match_type != NULL)
                *match_type = MATCHED_NONE;
            return;
        }

        if (pos != NULL) {
            assert(pos_count != NULL);
            (*pos_count)++;
            *pos = realloc(*pos, (*pos_count) * sizeof((*pos)[0]));
            (*pos)[*pos_count - 1].start = longest_match_ofs;
            (*pos)[*pos_count - 1].len = longest_match_len;
        }

        if (match_type != NULL)
            *match_type = longest_match_len == haystack_len
                ? MATCHED_EXACT
                : MATCHED_FUZZY;

        needle += longest_match_len;
        haystack_search_start = haystack + longest_match_ofs + longest_match_len;
    }
}

static void
levenshtein_distance(const char32_t *a, size_t alen,
                     const char32_t *b, size_t blen,
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

static const char32_t *
match_levenshtein(struct matches *matches,
                  const char32_t *src, const char32_t *pat, size_t *_match_len)
{
    if (matches->mode != MATCH_MODE_FUZZY)
        return NULL;

    const size_t src_len = c32len(src);
    const size_t pat_len = c32len(pat);

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
    const size_t match_distance = m[pat_len][end].distance;
    const size_t match_len = end - c;

    LOG_DBG("%ls vs. %ls: sub-string: %.*ls, (distance=%zu, row=%zu, col=%zu)",
            (const wchar_t *)src, (const wchar_t *)pat,
            (int)match_len, (const wchar_t *)&src[match_ofs], match_distance, r, c);

    for (size_t i = 0; i < pat_len + 1; i++)
        free(m[i]);
    free(m);

    const size_t len_diff = match_len > pat_len
        ? match_len - pat_len
        : pat_len - match_len;

    if (len_diff <= matches->fuzzy_max_length_discrepancy &&
        match_distance <= matches->fuzzy_max_distance)
    {
        if (_match_len != NULL)
            *_match_len = match_len;
        return &src[match_ofs];
    }

    return NULL;
}

struct matches *
matches_init(enum match_fields fields, enum match_mode mode,
             size_t fuzzy_min_length, size_t fuzzy_max_length_discrepancy,
             size_t fuzzy_max_distance)
{
    struct matches *matches = malloc(sizeof(*matches));
    *matches = (struct matches) {
        .applications = NULL,
        .fields = fields,
        .matches = NULL,
        .mode = mode,
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

    if (matches->applications != NULL) {
        for (size_t i = 0; i < matches->applications->count; i++)
            free(matches->matches[i].pos);
    }

    free(matches->matches);
    free(matches);
}

void
matches_set_applications(struct matches *matches,
                         const struct application_list *applications)
{
    assert(matches->applications == NULL);
    assert(matches->matches == NULL);

    matches->applications = applications;
    matches->matches = calloc(
        applications->count, sizeof(matches->matches[0]));
}

bool
matches_have_icons(const struct matches *matches)
{
    if (matches->applications == NULL)
        return false;

    for (size_t i = 0; i < matches->applications->count; i++) {
        if (matches->applications->v[i].icon.name != NULL)
            return true;
    }

    return false;
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
    return matches->max_matches_per_page != 0
        ? matches->match_count / matches->max_matches_per_page
        : matches->match_count;
}

size_t
matches_get_page(const struct matches *matches)
{
    return matches->max_matches_per_page != 0
        ? matches->selected / matches->max_matches_per_page
        : matches->selected;
}

size_t
match_get_idx(const struct matches *matches, size_t idx)
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
    return idx;
}

const struct match *
matches_get(const struct matches *matches, size_t idx)
{
    return &matches->matches[match_get_idx(matches, idx)];
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

    if (matches->max_matches_per_page == 0)
        return 0;

    if (total == 0)
        return 0;
    else if (page_no + 1 >= matches->page_count) {
        size_t remainder = total % matches->max_matches_per_page;
        return remainder == 0 ? matches->max_matches_per_page : remainder;
    } else
        return matches->max_matches_per_page;
}

size_t
matches_get_total_count(const struct matches *matches)
{
    return matches->match_count;
}

size_t
matches_get_match_index(const struct matches *matches)
{
    return matches->max_matches_per_page != 0
        ? matches->selected % matches->max_matches_per_page
        : 0;
}

bool
matches_selected_select(struct matches *matches, const char *_string)
{
    if (_string == NULL)
        return false;

    char32_t *string = ambstoc32(_string);
    if (string == NULL)
        return false;

    for (size_t i = 0; i < matches->match_count; i++) {
        if (c32casestr(matches->matches[i].application->title, string) != NULL) {
            matches->selected = i;
            free(string);
            return true;
        }
    }

    free(string);
    return false;
}

bool
matches_idx_select(struct matches *matches, size_t idx)
{
    if (idx == -1)
        return false;

    matches->selected = match_get_idx(matches, idx);
    return true;
}

bool
matches_selected_first(struct matches *matches)
{
    if (matches->match_count <= 0 || matches->selected <= 0)
        return false;

    matches->selected = 0;
    return true;
}

bool
matches_selected_last(struct matches *matches)
{
    if (matches->match_count <= 0 ||
        matches->selected >= matches->match_count - 1)
    {
        return false;
    }

    matches->selected = matches->match_count - 1;
    return true;
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
matches_selected_prev_page(struct matches *matches, bool scrolling)
{
    const size_t page_no = matches_get_page(matches);
    if (page_no > 0) {
        assert(matches->selected >= matches->max_matches_per_page);
        matches->selected -= matches->max_matches_per_page;
        return true;
    } else if (!scrolling && matches->selected > 0) {
        matches->selected = 0;
        return true;
    }

    return false;
}

bool
matches_selected_next_page(struct matches *matches, bool scrolling)
{
    const size_t page_no = matches_get_page(matches);
    if (page_no + 1 < matches->page_count) {
        matches->selected = min(
            matches->selected + matches->max_matches_per_page,
            matches->match_count - 1);
        return true;
    } else if (!scrolling && matches->selected < matches->match_count - 1) {
        matches->selected = matches->match_count - 1;
        return true;
    }

    return false;
}

static int
match_compar(const void *_a, const void *_b)
{
    const struct match *a = _a;
    const struct match *b = _b;

    /*
     * Exact matches (of the application title) is always preferred.
     *
     * If neither match is an exact match, non-fuzzy matches are
     * preferred over fuzzy matches. I.e. a non-fuzzy match is
     * considered to be “less” than a fuzzy match.
     *
     * If the two matches have the same match type, prefer the one
     * with the highest launch count. That is, a *high* launch count
     * is considered to be “less” than a low launch count.
     */

    const bool a_is_exact_match =
        a->application->title != NULL &&
        a->matched_type == MATCHED_EXACT &&
        a->pos_count == 1 &&
        a->pos[0].start == 0 &&
        a->pos[0].len == c32len(a->application->title);

    const bool b_is_exact_match =
        b->application->title != NULL &&
        b->matched_type == MATCHED_EXACT &&
        b->pos_count == 1 &&
        b->pos[0].start == 0 &&
        b->pos[0].len == c32len(b->application->title);

    if (a_is_exact_match && !b_is_exact_match)
        return -1;
    else if (!a_is_exact_match && b_is_exact_match)
        return 1;
    else if (a->matched_type == MATCHED_FUZZY && b->matched_type == MATCHED_EXACT)
        return 1;
    else if (a->matched_type == MATCHED_EXACT && b->matched_type == MATCHED_FUZZY)
        return -1;
    else if (a->score > b->score)
        return -1;
    else if (a->score < b->score)
        return 1;
    else if (a->pos_count < b->pos_count)
        return -1;
    else if (a->pos_count > b->pos_count)
        return 1;
    else if (a->application->count > b->application->count)
        return -1;
    else if (a->application->count < b->application->count)
        return 1;
    else
        return 0;
}

void
matches_update(struct matches *matches, const struct prompt *prompt)
{
    if (matches->applications == NULL)
        return;

    const char32_t *ptext = prompt_text(prompt);

    /* Nothing entered; all programs found matches */
    if (c32len(ptext) == 0) {

        matches->match_count = 0;
        for (size_t i = 0; i < matches->applications->count; i++) {
            if (!matches->applications->v[i].visible)
                continue;

            free(matches->matches[matches->match_count].pos);

            matches->matches[matches->match_count++] = (struct match){
                .matched_type = MATCHED_NONE,
                .application = &matches->applications->v[i],
                .pos = NULL,
                .pos_count = 0,
                .index = i,
            };
        }

        /* Sort */
        qsort(matches->matches, matches->match_count, sizeof(matches->matches[0]), &match_compar);

        if (matches->selected >= matches->match_count && matches->selected > 0)
            matches->selected = matches->match_count - 1;

        matches->page_count = matches->max_matches_per_page > 0
            ? ((matches->match_count + (matches->max_matches_per_page - 1)) /
               matches->max_matches_per_page)
            : 1;
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

    LOG_DBG("match update begin");

    for (size_t i = 0; i < matches->applications->count; i++) {
        struct application *app = &matches->applications->v[i];

        if (!app->visible)
            continue;

        size_t pos_count = 0;
        struct match *match = &matches->matches[matches->match_count];
        struct match_substring *pos = match->pos;

        enum matched_type match_type_name = MATCHED_NONE;
        enum matched_type match_type_filename = MATCHED_NONE;
        enum matched_type match_type_generic = MATCHED_NONE;
        enum matched_type match_type_exec = MATCHED_NONE;
        enum matched_type match_type_comment = MATCHED_NONE;
        enum matched_type match_type_keywords[tll_length(app->keywords)];
        enum matched_type match_type_categories[tll_length(app->categories)];

        for (size_t k = 0; k < tll_length(app->keywords); k++)
            match_type_keywords[k] = MATCHED_NONE;
        for (size_t k = 0; k < tll_length(app->categories); k++)
            match_type_categories[k] = MATCHED_NONE;

        if (match_name) {
            const char32_t *m = NULL;
            size_t match_len = 0;

            switch (matches->mode) {
            case MATCH_MODE_EXACT:
                m = c32casestr(app->title, ptext);
                if (m != NULL) {
                    match_type_name = MATCHED_EXACT;
                    match_len = c32len(ptext);
                }
                break;

            case MATCH_MODE_FZF:
                match_fzf(app->title, ptext, &pos, &pos_count, &match_type_name);
                match->pos = pos;
                break;

            case MATCH_MODE_FUZZY:
                m = c32casestr(app->title, ptext);
                if (m != NULL) {
                    match_type_name = MATCHED_EXACT;
                    match_len = c32len(ptext);
                } else {
                    m = match_levenshtein(matches, app->title, ptext, &match_len);
                    if (m != NULL)
                        match_type_name = MATCHED_FUZZY;
                }
                break;
            }

            if (match_len > 0) {
                assert(matches->mode != MATCH_MODE_FZF);

                if (pos_count > 0 && m == &app->title[pos[pos_count - 1].start +
                                                      pos[pos_count - 1].len]) {
                    /* Extend last match position */
                    pos[pos_count - 1].len += match_len;
                } else {
                    size_t new_pos_count = pos_count + 1;
                    struct match_substring *new_pos =
                        realloc(pos, new_pos_count * sizeof(new_pos[0]));

                    if (new_pos != NULL) {
                        pos = new_pos;
                        pos_count = new_pos_count;

                        pos[pos_count - 1].start = m - app->title;
                        pos[pos_count - 1].len = match_len;

                        match->pos = pos;
                    }
                }
            }
        }

        if (match_filename && app->basename != NULL) {
            const char32_t *m = NULL;
            size_t match_len = 0;

            switch (matches->mode) {
            case MATCH_MODE_EXACT:
                m = c32casestr(app->basename, ptext);
                if (m != NULL) {
                    match_type_filename = MATCHED_EXACT;
                    match_len = c32len(ptext);
                }
                break;

            case MATCH_MODE_FZF:
                match_fzf(app->basename, ptext, NULL, NULL, &match_type_filename);
                break;

            case MATCH_MODE_FUZZY:
                m = c32casestr(app->basename, ptext);
                if (m != NULL) {
                    match_type_filename = MATCHED_EXACT;
                    match_len = c32len(ptext);
                } else {
                    m = match_levenshtein(matches, app->basename, ptext, &match_len);
                    if (m != NULL)
                        match_type_filename = MATCHED_FUZZY;
                }
                break;
            }
        }

        if (match_generic && app->generic_name != NULL) {
            const char32_t *m = NULL;
            size_t match_len = 0;

            switch (matches->mode) {
            case MATCH_MODE_EXACT:
                m = c32casestr(app->generic_name, ptext);
                if (m != NULL) {
                    match_type_generic = MATCHED_EXACT;
                    match_len = c32len(ptext);
                }
                break;

            case MATCH_MODE_FZF:
                match_fzf(app->generic_name, ptext, NULL, NULL, &match_type_generic);
                break;

            case MATCH_MODE_FUZZY:
                m = c32casestr(app->generic_name, ptext);
                if (m != NULL) {
                    match_type_generic = MATCHED_EXACT;
                    match_len = c32len(ptext);
                } else {
                    m = match_levenshtein(matches, app->generic_name, ptext, &match_len);
                    if (m != NULL)
                        match_type_generic = MATCHED_FUZZY;
                }
                break;
            }
        }

        if (match_exec && app->wexec != NULL) {
            const char32_t *m = NULL;
            size_t match_len = 0;

            switch (matches->mode) {
            case MATCH_MODE_EXACT:
                m = c32casestr(app->wexec, ptext);
                if (m != NULL) {
                    match_type_exec = MATCHED_EXACT;
                    match_len = c32len(ptext);
                }
                break;

            case MATCH_MODE_FZF:
                match_fzf(app->wexec, ptext, NULL, NULL, &match_type_exec);
                break;

            case MATCH_MODE_FUZZY:
                m = c32casestr(app->wexec, ptext);
                if (m != NULL) {
                    match_type_exec = MATCHED_EXACT;
                    match_len = c32len(ptext);
                } else {
                    m = match_levenshtein(matches, app->wexec, ptext, &match_len);
                    if (m != NULL)
                        match_type_exec = MATCHED_FUZZY;
                }
                break;
            }
        }

        if (match_comment && app->comment != NULL) {
            const char32_t *m = NULL;
            size_t match_len = 0;

            switch (matches->mode) {
            case MATCH_MODE_EXACT:
                m = c32casestr(app->comment, ptext);
                if (m != NULL) {
                    match_type_comment = MATCHED_EXACT;
                    match_len = c32len(ptext);
                }
                break;

            case MATCH_MODE_FZF:
                match_fzf(app->comment, ptext, NULL, NULL, &match_type_comment);
                break;

            case MATCH_MODE_FUZZY:
                m = c32casestr(app->comment, ptext);
                if (m != NULL) {
                    match_type_comment = MATCHED_EXACT;
                    match_len = c32len(ptext);
                } else {
                    m = match_levenshtein(matches, app->comment, ptext, &match_len);
                    if (m != NULL)
                        match_type_comment = MATCHED_FUZZY;
                }
                break;
            }
        }

        if (match_keywords) {
            size_t k = 0;
            tll_foreach(app->keywords, it) {
                const char32_t *m = NULL;
                size_t match_len = 0;

                switch (matches->mode) {
                case MATCH_MODE_EXACT:
                    m = c32casestr(it->item, ptext);
                    if (m != NULL) {
                        match_type_keywords[k] = MATCHED_EXACT;
                        match_len = c32len(ptext);
                    }
                    break;

                case MATCH_MODE_FZF:
                    match_fzf(it->item, ptext, NULL, NULL, &match_type_keywords[i]);
                    break;

                case MATCH_MODE_FUZZY:
                    m = c32casestr(it->item, ptext);
                    if (m != NULL) {
                        match_type_keywords[k] = MATCHED_EXACT;
                        match_len = c32len(ptext);
                    } else {
                        m = match_levenshtein(matches, it->item, ptext, &match_len);
                        if (m != NULL)
                            match_type_keywords[k] = MATCHED_FUZZY;
                    }
                    break;
                }

                k++;
            }
        }

        if (match_categories) {
            size_t k = 0;
            tll_foreach(app->categories, it) {
                const char32_t *m = NULL;
                size_t match_len = 0;

                switch (matches->mode) {
                case MATCH_MODE_EXACT:
                    m = c32casestr(it->item, ptext);
                    if (m != NULL) {
                        match_type_categories[k] = MATCHED_EXACT;
                        match_len = c32len(ptext);
                    }
                    break;

                case MATCH_MODE_FZF:
                    match_fzf(it->item, ptext, NULL, NULL, &match_type_categories[i]);
                    break;

                case MATCH_MODE_FUZZY:
                    m = c32casestr(it->item, ptext);
                    if (m != NULL) {
                        match_type_categories[k] = MATCHED_EXACT;
                        match_len = c32len(ptext);
                    } else {
                        m = match_levenshtein(matches, it->item, ptext, &match_len);
                        if (m != NULL)
                            match_type_categories[k] = MATCHED_FUZZY;
                    }
                    break;
                }

                k++;
            }
        }

        enum matched_type match_type_keywords_final = MATCHED_NONE;

        if (match_keywords) {
            for (size_t k = 0; k < tll_length(app->keywords); k++) {
                if (match_type_keywords[k] != MATCHED_NONE) {
                    /* match_type_keywords_final represents the
                       combined result of all keywords; if a single
                       keyword matched exactly, treat the entire
                       keywords field as an exact match */
                    if (match_type_keywords_final != MATCHED_EXACT)
                        match_type_keywords_final = match_type_keywords[k];
                }
            }
        }

        enum matched_type match_type_categories_final = MATCHED_NONE;

        if (match_categories) {
            for (size_t k = 0; k < tll_length(app->categories); k++) {
                if (match_type_categories[k] != MATCHED_NONE) {
                    /* match_type_categories_final represents the
                       combined result of all keywords; if a single
                       keyword matched exactly, treat the entire
                       keywords field as an exact match */
                    if (match_type_categories_final != MATCHED_EXACT)
                        match_type_categories_final = match_type_categories[k];
                }
            }
        }

        enum matched_type app_match_type = MATCHED_NONE;

        /*
         * For now, only track score for the application title. The
         * longer consecutive match, the higher the score.
         *
         * This means searching for 'oo' will sort 'foot' before
         * 'firefox browser', for example.
         */
        size_t score = 0;
        if (match_name) {
            size_t longest_match = 0;
            for (size_t k = 0; k < pos_count; k++) {
                if (pos[k].len > longest_match)
                    longest_match = pos[k].len;
            }


            score = longest_match;
        }

        if (match_type_name != MATCHED_NONE)
            app_match_type = match_type_name;
        else if (match_type_filename != MATCHED_NONE)
            app_match_type = match_type_filename;
        else if (match_type_generic != MATCHED_NONE)
            app_match_type = match_type_generic;
        else if (match_type_exec != MATCHED_NONE)
            app_match_type = match_type_exec;
        else if (match_type_comment != MATCHED_NONE)
            app_match_type = match_type_comment;
        else if (match_type_keywords_final != MATCHED_NONE)
            app_match_type = match_type_keywords_final;
        else if (match_type_categories_final != MATCHED_NONE)
            app_match_type = match_type_categories_final;

        if (app_match_type == MATCHED_NONE)
            continue;

        *match = (struct match){
            .matched_type = app_match_type,
            .application = app,
            .pos = pos,
            .pos_count = pos_count,
            .index = i,
            .score = score,
        };

        matches->match_count++;
    }

    LOG_DBG("match update done");

    /* Sort */
    qsort(matches->matches, matches->match_count, sizeof(matches->matches[0]), &match_compar);

    matches->page_count = matches->max_matches_per_page
        ? ((matches->match_count + (matches->max_matches_per_page - 1)) /
           matches->max_matches_per_page)
        : 1;

    if (matches->selected >= matches->match_count && matches->selected > 0)
        matches->selected = matches->match_count - 1;
}
