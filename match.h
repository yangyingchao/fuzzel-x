#pragma once

#include <stdbool.h>
#include <unistd.h>

#include "application.h"
#include "config.h"
#include "prompt.h"

enum matched_type {
    MATCHED_NONE,
    MATCHED_EXACT,
    MATCHED_FUZZY,
};

struct match_substring {
    ssize_t start;
    size_t len;
};

struct match {
    enum matched_type matched_type;
    struct application *application;
    struct match_substring *pos;
    size_t pos_count;
    size_t index;
};

struct matches;
struct matches *matches_init(enum match_fields fields, bool fuzzy,
                             size_t fuzzy_min_length,
                             size_t fuzzy_max_length_discrepancy,
                             size_t fuzzy_max_distance);
void matches_destroy(struct matches *matches);

void matches_set_applications(
    struct matches *matches, const struct application_list *applications);
bool matches_have_icons(const struct matches *matches);

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
