#pragma once

#include <stdbool.h>
#include <unistd.h>
#include "application.h"

struct match {
    struct application *application;
    ssize_t start_title;
    ssize_t start_comment;
};

struct matches;
struct matches *matches_init(
    const struct application_list *applications, size_t max_matches);
void matches_destroy(struct matches *matches);

void matches_update(struct matches *matches, const wchar_t *match_text);

const struct match *matches_get(const struct matches *matches, size_t idx);
const struct match *matches_get_match(const struct matches *matches);
size_t matches_get_count(const struct matches *matches);
size_t matches_get_match_index(const struct matches *matches);

bool matches_selected_prev(struct matches *matches, bool wrap);
bool matches_selected_next(struct matches *matches, bool wrap);
