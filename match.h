#pragma once

#include <stdbool.h>
#include <unistd.h>

#include "application.h"
#include "prompt.h"

enum matched_type {
    MATCHED_NONE,
    MATCHED_EXACT,
    MATCHED_FUZZY,
};

struct match {
    enum matched_type matched_type;
    struct application *application;
    ssize_t start_title;
};

enum match_fields {
    MATCH_FILENAME =   0x01,
    MATCH_NAME =       0x02,
    MATCH_GENERIC =    0x04,
    MATCH_EXEC =       0x08,
    MATCH_CATEGORIES = 0x10,
    MATCH_KEYWORDS =   0x20,
    MATCH_COMMENT =    0x40,
};

struct matches;
struct matches *matches_init(const struct application_list *applications,
                             enum match_fields fields, bool fuzzy,
                             size_t fuzzy_min_length,
                             size_t fuzzy_max_length_discrepancy,
                             size_t fuzzy_max_distance);
void matches_destroy(struct matches *matches);

size_t matches_max_matches_per_page(const struct matches *matches);
void matches_max_matches_per_page_set(
    struct matches *matches, size_t max_matches);

void matches_update(struct matches *matches, const struct prompt *prompt);

size_t matches_get_page_count(const struct matches *matches);
size_t matches_get_page(const struct matches *matches);

const struct match *matches_get(const struct matches *matches, size_t idx);
const struct match *matches_get_match(const struct matches *matches);
size_t matches_get_count(const struct matches *matches);
size_t matches_get_match_index(const struct matches *matches);

bool matches_selected_prev(struct matches *matches, bool wrap);
bool matches_selected_next(struct matches *matches, bool wrap);

bool matches_selected_prev_page(struct matches *matches);
bool matches_selected_next_page(struct matches *matches);
