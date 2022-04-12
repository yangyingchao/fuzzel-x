#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <char32.h>

#include <tllist.h>

struct rgba {double r; double g; double b; double a;};
struct pt_or_px {int px; float pt;};

enum dpi_aware {
    DPI_AWARE_AUTO,
    DPI_AWARE_YES,
    DPI_AWARE_NO,
};

enum dmenu_mode {
    DMENU_MODE_NONE,
    DMENU_MODE_TEXT,
    DMENU_MODE_INDEX,
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

struct config {
    char *output;
    char32_t *prompt;
    char32_t password;
    enum match_fields match_fields;

    char *terminal;
    char *launch_prefix;

    char *font;
    enum dpi_aware dpi_aware;

    bool icons_enabled;
    char *icon_theme;

    bool actions_enabled;

    struct {
        size_t min_length;
        size_t max_length_discrepancy;
        size_t max_distance;
        bool enabled;
    } fuzzy;

    struct {
        enum dmenu_mode mode;
        bool exit_immediately_if_empty;
    } dmenu;

    unsigned lines;
    unsigned chars;

    struct {
        unsigned x;
        unsigned y;
        unsigned inner;
    } pad;

    struct {
        struct rgba background;
        struct rgba border;
        struct rgba text;
        struct rgba match;
        struct rgba selection;
        struct rgba selection_text;
    } colors;

    struct {
        unsigned size;
        unsigned radius;
    } border;

    struct pt_or_px line_height;
    struct pt_or_px letter_spacing;
};

typedef tll(char *) config_override_t;

bool config_load(
    struct config *conf, const char *path, const config_override_t *overrides,
    bool errors_are_fatal);
void config_free(struct config *conf);

struct rgba conf_hex_to_rgba(uint32_t color);
